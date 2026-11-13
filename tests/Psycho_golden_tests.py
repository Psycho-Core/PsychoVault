#!/usr/bin/env python3
"""Self-tests for the golden runner's image I/O and failure behavior."""
import contextlib
import importlib.util
import io
import json
import pathlib
import tempfile
import unittest

ROOT = pathlib.Path(__file__).resolve().parents[1]
spec = importlib.util.spec_from_file_location("pv_golden", ROOT / "tools/Psycho_golden.py")
golden = importlib.util.module_from_spec(spec)
spec.loader.exec_module(golden)


class GoldenHarnessTests(unittest.TestCase):
    def test_png_round_trip_and_corruption(self):
        with tempfile.TemporaryDirectory() as folder:
            path = pathlib.Path(folder) / "image.png"
            pixels = bytes([255, 0, 0, 255, 0, 255, 0, 128])
            golden.write_png(path, 2, 1, pixels)
            self.assertEqual(golden.decode_png(path), (2, 1, pixels))
            data = bytearray(path.read_bytes())
            data[30] ^= 1
            path.write_bytes(data)
            with self.assertRaises(ValueError): golden.decode_png(path)

    def test_pixel_diff_and_dimensions(self):
        source = bytes([0, 0, 0, 255, 100, 200, 30, 255])
        altered = bytes([0, 0, 0, 255, 90, 200, 30, 255])
        changed, diff = golden.compare(source, altered)
        self.assertEqual(changed, 1)
        self.assertEqual(diff, bytes([0, 0, 0, 255, 10, 0, 0, 255]))
        with self.assertRaises(ValueError): golden.compare(source, b"short")

    def test_missing_engine_is_failure_with_report(self):
        with tempfile.TemporaryDirectory() as folder, contextlib.redirect_stdout(io.StringIO()):
            out = pathlib.Path(folder)
            self.assertEqual(golden.run(out / "missing-engine", out / "results"), 1)
            report = json.loads((out / "results/report.json").read_text(encoding="utf-8"))
            self.assertEqual(report["failed"], 18)
            self.assertTrue(all(case["result"] == "failed" for case in report["cases"]))

    def test_baseline_output_is_refused(self):
        with self.assertRaises(ValueError): golden.run(pathlib.Path("no-engine"), golden.BASELINES / "common")

    def test_complete_step_mapping(self):
        self.assertEqual(set(golden.CASES), {f"s{number:03d}" for number in range(52, 70)})


if __name__ == "__main__":
    unittest.main()
