#!/usr/bin/env python3
"""Tests for legacy comparison, including failures and output safety."""
import contextlib
import hashlib
import io
import json
import pathlib
import struct
import sys
import tempfile
import unittest
import zlib
from types import SimpleNamespace
from unittest import mock

ROOT = pathlib.Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "tools"))
import Psycho_golden as png
import Psycho_reference_diff as diff


def rgb_png(width, height, rows):
    def chunk(kind, data):
        return struct.pack(">I", len(data)) + kind + data + struct.pack(">I", zlib.crc32(kind + data))
    return (b"\x89PNG\r\n\x1a\n" + chunk(b"IHDR", struct.pack(">IIBBBBB", width, height, 8, 2, 0, 0, 0)) +
            chunk(b"IDAT", zlib.compress(b"".join(rows))) + chunk(b"IEND", b""))


class ReferenceDiffTests(unittest.TestCase):
    def setUp(self):
        self.tmp = tempfile.TemporaryDirectory()
        self.addCleanup(self.tmp.cleanup)
        self.root = pathlib.Path(self.tmp.name)
        self.refdir = self.root / "reference"
        self.refdir.mkdir()
        self.ref = self.refdir / "ref.png"
        # Two RGB rows; second uses PNG's Sub filter with 3-byte pixels.
        self.ref.write_bytes(rgb_png(2, 2, [b"\x00\x0a\x14\x1e\x28\x32\x3c", b"\x01\x01\x02\x03\x03\x03\x03"]))
        self.pixels = bytes([10, 20, 30, 255, 40, 50, 60, 255, 1, 2, 3, 255, 4, 5, 6, 255])
        self.inventory = self.root / "inventory.json"
        self.inventory.write_text(json.dumps({"sourceCommit": "inventory-commit", "screenshots": [{
            "file": "ref.png", "sha256": hashlib.sha256(self.ref.read_bytes()).hexdigest(),
            "bytes": self.ref.stat().st_size, "width": 2, "height": 2}]}))
        (self.refdir / "CAPTURE.json").write_text(json.dumps({"sourceCommit": "capture-commit", "capturedAt": "time"}))
        self.refpatch = mock.patch.object(diff, "REFERENCE", self.refdir)
        self.invpatch = mock.patch.object(diff, "INVENTORY", self.inventory)
        self.refpatch.start(); self.invpatch.start()
        self.addCleanup(self.refpatch.stop); self.addCleanup(self.invpatch.stop)
        self.actual = self.root / "capture.png"
        png.write_png(self.actual, 2, 2, self.pixels)
        self.out = self.root / "out"
        self.args = SimpleNamespace(reference="ref.png", outdir=self.out, actual=self.actual, input=None,
            engine=self.root / "engine", resource=[], action=[], scroll=None, dpr=None, reduced_motion=False,
            max_changed_pixels=0, url="https://reference.test/")

    def execute(self):
        with contextlib.redirect_stdout(io.StringIO()):
            code = diff.run(self.args)
        return code, json.loads((self.out / "report.json").read_text())

    def test_rgb_sub_filter_and_identical_capture(self):
        self.assertEqual(png.decode_png(self.ref), (2, 2, self.pixels))
        code, report = self.execute()
        self.assertEqual(code, 0)
        self.assertEqual(report["changed_pixels"], 0)
        self.assertEqual(report["capture_source_commit"], "capture-commit")
        self.assertEqual(png.decode_png(self.out / "diff.png")[2], b"\0\0\0\xff" * 4)

    def test_mismatch_threshold_and_diff(self):
        pixels = bytearray(self.pixels); pixels[0] += 5
        png.write_png(self.actual, 2, 2, pixels)
        code, report = self.execute()
        self.assertEqual((code, report["changed_pixels"]), (1, 1))
        self.assertEqual(png.decode_png(self.out / "diff.png")[2][:4], bytes([5, 0, 0, 255]))
        self.args.max_changed_pixels = 1
        self.assertEqual(self.execute()[0], 0)

    def test_bad_dimensions_and_inventory_hash_fail_closed(self):
        png.write_png(self.actual, 1, 1, b"\0\0\0\xff")
        self.assertIn("dimensions", self.execute()[1]["error"])
        self.ref.write_bytes(self.ref.read_bytes() + b"modified")
        self.assertIn("hash/size", self.execute()[1]["error"])
        self.assertFalse((self.out / "diff.png").exists())

    def test_reject_unknown_reference_and_actual_options(self):
        self.args.reference = "unknown.png"
        self.assertIn("inventory", self.execute()[1]["error"])
        self.args.reference = "ref.png"
        self.args.action = ["click:1,1"]
        self.assertIn("cannot be used", self.execute()[1]["error"])
        self.args.reference = "../ref.png"
        with self.assertRaises(ValueError): diff.run(self.args)

    def test_prevent_input_overwrite_and_reference_output(self):
        self.args.outdir = self.root
        self.args.actual = self.root / "actual.png"
        self.args.actual.write_bytes(b"keep")
        with self.assertRaises(ValueError): diff.run(self.args)
        self.assertEqual(self.args.actual.read_bytes(), b"keep")
        self.args.outdir = self.refdir
        with self.assertRaises(ValueError): diff.run(self.args)

    def test_renderer_receives_reference_dimensions_and_resources(self):
        self.args.input = self.root / "page.html"
        self.args.input.write_text("<p>page</p>")
        self.args.actual = None
        resource = self.root / "app.js"; resource.write_text("// test")
        self.args.resource = [f"https://reference.test/app.js={resource}"]
        self.args.action = ["click:1,1"]
        def render(command, **kwargs):
            self.assertEqual(command[command.index("--width") + 1], "2")
            self.assertEqual(command[command.index("--height") + 1], "2")
            self.assertIn(f"https://reference.test/app.js={resource.resolve()}", command)
            png.write_png(pathlib.Path(command[command.index("--out") + 1]), 2, 2, self.pixels)
            return SimpleNamespace(returncode=0, stdout="{}", stderr="")
        with mock.patch.object(diff.subprocess, "run", side_effect=render):
            code, report = self.execute()
        self.assertEqual(code, 0)
        self.assertEqual(report["resources"][0]["url"], "https://reference.test/app.js")
        self.assertEqual(report["renderer_exit"], 0)

    def test_renderer_errors_fail_and_remove_stale_output(self):
        self.execute()
        self.args.input = self.root / "page.html"; self.args.input.write_text("x")
        self.args.actual = None
        with mock.patch.object(diff.subprocess, "run", return_value=SimpleNamespace(returncode=1, stdout="", stderr="bad")):
            code, report = self.execute()
        self.assertEqual(code, 1)
        self.assertIn("bad", report["error"])
        self.assertFalse((self.out / "diff.png").exists())

    def test_real_reference_archive_decodes(self):
        width, height, pixels = png.decode_png(ROOT / "docs/reference/legacy-ui/03-new-tab-page.png")
        self.assertEqual((width, height, len(pixels)), (1396, 834, 1396 * 834 * 4))


if __name__ == "__main__":
    unittest.main()
