#!/usr/bin/env python3
"""Self-tests for hand-specified Phase 2 image golden contracts."""
from __future__ import annotations
import contextlib
import importlib.util
import io
import json
import pathlib
import sys
import tempfile
import unittest
from types import SimpleNamespace
from unittest import mock

ROOT = pathlib.Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "tools"))
spec = importlib.util.spec_from_file_location("independent_goldens", ROOT / "tools/Psycho_independent_goldens.py")
golden = importlib.util.module_from_spec(spec)
spec.loader.exec_module(golden)


class IndependentGoldenTests(unittest.TestCase):
    def test_expected_canvas_layers_and_validation(self):
        pixels = golden.expected_pixels([(0, 0, 3, 2, golden.RED), (1, 1, 2, 1, golden.BLUE)])
        def at(x, y):
            i = (y * golden.W + x) * 4
            return tuple(pixels[i:i + 4])
        self.assertEqual(at(0, 0), golden.RED)
        self.assertEqual(at(1, 1), golden.BLUE)
        self.assertEqual(at(3, 1), golden.WHITE)
        with self.assertRaises(ValueError): golden.expected_pixels([(159, 0, 2, 1, golden.RED)])
        with self.assertRaises(ValueError): golden.expected_pixels([(0, 0, 1, 1, (0, 0, 0, 256))])

    def test_step_index_is_not_a_claim_of_complete_coverage(self):
        steps = {step for case in golden.CASES.values() for step in case["steps"]}
        self.assertEqual(steps, {f"S{n:03d}" for n in range(52, 70)})
        for name, case in golden.CASES.items():
            self.assertTrue(case["steps"] and case["rects"])
            fixture = golden.FIXTURES / (case.get("fixture", name) + ".html")
            self.assertTrue(fixture.is_file())

    def test_missing_renderer_fails_with_report(self):
        with tempfile.TemporaryDirectory() as directory, contextlib.redirect_stdout(io.StringIO()):
            root = pathlib.Path(directory)
            self.assertEqual(golden.run(root / "missing", root / "out"), 1)
            report = json.loads((root / "out/report.json").read_text(encoding="utf-8"))
            self.assertEqual(report["failed"], len(golden.CASES))
            self.assertTrue(all(item["result"] == "failed" for item in report["cases"]))

    def test_mismatch_writes_diff_and_success_removes_stale_files(self):
        with tempfile.TemporaryDirectory() as directory, contextlib.redirect_stdout(io.StringIO()):
            root = pathlib.Path(directory)
            case = {"steps": ["S052"], "rects": [(0, 0, 1, 1, golden.RED)]}
            expected = golden.expected_pixels(case["rects"])
            altered = bytearray(expected)
            altered[0] = 0
            content = [bytes(altered)]
            def render(command, **kwargs):
                golden.write_png(pathlib.Path(command[command.index("--out") + 1]), golden.W, golden.H, content[0])
                return SimpleNamespace(returncode=0, stdout="{}", stderr="")
            with mock.patch.object(golden, "CASES", {"example": case}), mock.patch.object(golden.subprocess, "run", side_effect=render):
                output = root / "out"
                self.assertEqual(golden.run(root / "engine", output), 1)
                report = json.loads((output / "report.json").read_text(encoding="utf-8"))
                self.assertEqual(report["cases"][0]["changed_pixels"], 1)
                self.assertTrue((output / "example.diff.png").is_file())
                self.assertTrue((output / "example.expected.png").is_file())
                content[0] = expected
                self.assertEqual(golden.run(root / "engine", output), 0)
                self.assertFalse((output / "example.diff.png").exists())
                self.assertFalse((output / "example.expected.png").exists())

    def test_frame_checks_are_not_silently_skipped(self):
        with self.assertRaises(ValueError): golden.check_frame({"damage": True}, {"fullDamage": True})
        with self.assertRaises(ValueError): golden.check_frame({"idle": True}, {"damageTiles": [{"x": 0}]})
        with self.assertRaises(ValueError): golden.check_frame({"font": True}, {})
        with self.assertRaises(ValueError): golden.check_frame({"title": "expected"}, {"title": "wrong"})
        golden.check_frame({"damage": True}, {"fullDamage": False, "damageTiles": [{"x": 0}]})

    def test_test_source_and_repo_root_output_are_refused(self):
        with self.assertRaises(ValueError): golden.run(pathlib.Path("no-engine"), ROOT)
        with self.assertRaises(ValueError): golden.run(pathlib.Path("no-engine"), ROOT / "tests/golden")


if __name__ == "__main__":
    unittest.main()
