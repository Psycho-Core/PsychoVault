#!/usr/bin/env python3
"""Interaction/pixel contracts for the focused focus-visible modality subset."""
from __future__ import annotations
import argparse
import json
import pathlib
import subprocess
import sys
import tempfile
import unittest

ROOT = pathlib.Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "tools"))
from Psycho_golden import decode_png

ENGINE = ROOT / "build" / ("pv-engine.exe" if sys.platform == "win32" else "pv-engine")
FIXTURE = ROOT / "tests/fixtures/p2/Psycho_focus_visible.html"
RED = bytes((221, 34, 34, 255))
BLUE = bytes((34, 51, 221, 255))
GREEN = bytes((34, 170, 68, 255))


def render(actions):
    with tempfile.TemporaryDirectory() as directory:
        image = pathlib.Path(directory) / "focus.png"
        cmd = [str(ENGINE), "--input", str(FIXTURE), "--width", "100", "--height", "80", "--out", str(image)]
        for action in actions:
            cmd += ["--action", action]
        result = subprocess.run(cmd, cwd=ROOT, capture_output=True, text=True, encoding="utf-8", timeout=60)
        if result.returncode or result.stderr.strip():
            raise AssertionError(f"engine exit {result.returncode}: {result.stderr}")
        json.loads(result.stdout)
        w, h, pixels = decode_png(image)
        if (w, h) != (100, 80):
            raise AssertionError(f"unexpected dimensions {(w, h)}")
        return tuple(pixels[(y * w + 30) * 4:(y * w + 30) * 4 + 4] for y in (10, 30, 50))


class FocusContractTests(unittest.TestCase):
    def test_pointer_keyboard_and_programmatic_focus(self):
        # Colors follow the CSS cascade and explicit interaction sequence.
        scenarios = [([], (RED, RED, RED)),
                     (["click:10,10"], (BLUE, RED, RED)),
                     (["key:Tab"], (GREEN, RED, RED)),
                     (["click:10,10", "key:Tab"], (RED, GREEN, RED)),
                     (["click:10,10", "key:ArrowDown"], (GREEN, RED, RED)),
                     (["click:10,50"], (RED, RED, GREEN)),
                     (["key:Tab", "key:Enter"], (RED, GREEN, RED)),
                     (["click:10,30"], (BLUE, RED, RED))]
        for actions, expected in scenarios:
            with self.subTest(actions=actions):
                self.assertEqual(render(actions), expected)


if __name__ == "__main__":
    parser = argparse.ArgumentParser()
    parser.add_argument("--engine", type=pathlib.Path, default=ENGINE)
    args, remaining = parser.parse_known_args()
    ENGINE = args.engine.resolve()
    unittest.main(argv=[sys.argv[0], *remaining])
