#!/usr/bin/env python3
"""Independent, hand-specified reference-contract assertions for image fitting."""
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
FIXTURES = ROOT / "tests/fixtures/p2"
BLUE = bytes((30, 144, 210, 255))
ORANGE = bytes((245, 158, 11, 255))
BORDER = bytes((51, 65, 85, 255))
BACKGROUND = bytes((238, 238, 238, 255))
WHITE = bytes((255, 255, 255, 255))


def render(fixture: str, width: int, height: int, actions: tuple[str, ...] = (),
           svg: bool = False, reduced: bool = False):
    with tempfile.TemporaryDirectory() as folder:
        out = pathlib.Path(folder) / "render.png"
        command = [str(ENGINE), "--input", str(FIXTURES / fixture), "--url", "https://s050.test/page.html",
                   "--resource", f"https://s050.test/pixel.png={FIXTURES / 'pixel.png'}",
                   "--width", str(width), "--height", str(height), "--out", str(out)]
        if svg:
            command.extend(["--resource", f"https://s050.test/mark.svg={FIXTURES / 'mark.svg'}"])
        if reduced:
            command.append("--reduced-motion")
        for action in actions:
            command.extend(["--action", action])
        proc = subprocess.run(command, cwd=ROOT, capture_output=True, text=True, encoding="utf-8", timeout=60)
        if proc.returncode or proc.stderr.strip():
            raise AssertionError(f"engine exit {proc.returncode}; diagnostics: {proc.stderr}")
        frame = json.loads(proc.stdout)
        w, h, pixels = decode_png(out)
        if (w, h) != (width, height):
            raise AssertionError(f"unexpected output size {(w, h)}")
        return frame, pixels


def expected_fit_image() -> bytes:
    """Independent geometry: five 80x40 content boxes, two-pixel borders.

    The source is a 2x2 checker. The CSS initial position is 50% 50%; contain
    produces a centered 40x40 image, cover an 80x80 image clipped to 80x40,
    none/scale-down stay 2x2, and fill stretches to 80x40. This checks the
    whole canvas, not a baseline captured from the engine.
    """
    width, height = 120, 240
    out = bytearray(WHITE * (width * height))
    def put(x, y, color):
        offset = (y * width + x) * 4
        out[offset:offset + 4] = color
    def source(x, y):
        return BLUE if x == y else ORANGE
    for index in range(5):
        top = index * 44
        for y in range(top, top + 44):
            for x in range(84):
                if x < 2 or x >= 82 or y < top + 2 or y >= top + 42:
                    color = BORDER
                else:
                    color = BACKGROUND
                    if index == 0 and 22 <= x < 62:
                        color = source((x - 22) // 20, (y - top - 2) // 20)
                    elif index == 1:
                        color = source((x - 2) // 40, (y - top + 18) // 40)
                    elif index in (2, 3) and 41 <= x < 43 and top + 21 <= y < top + 23:
                        color = source(x - 41, y - top - 21)
                    elif index == 4:
                        color = source((x - 2) // 40, (y - top - 2) // 20)
                put(x, y, color)
    return bytes(out)


class P2ImageContractTests(unittest.TestCase):
    def test_fit_boxes_clipping_and_entire_pixel_canvas(self):
        self.assertEqual(decode_png(FIXTURES / "pixel.png"), (2, 2, BLUE + ORANGE + ORANGE + BLUE))
        frame, actual = render("Psycho_object_fit_contract.html", 120, 240)
        self.assertEqual(frame["title"], "P2 contract: image fit and box")
        commands = [c for c in frame["paint"] if c["kind"] == 3]
        rectangles = [(c["x"], c["y"], c["w"], c["h"]) for c in commands]
        self.assertEqual(rectangles, [(22, 2, 40, 40), (2, 26, 80, 80), (41, 109, 2, 2),
                                      (41, 153, 2, 2), (2, 178, 80, 40)])
        self.assertEqual(sum(c["kind"] == 6 for c in frame["paint"]), 4)
        self.assertEqual(sum(c["kind"] == 7 for c in frame["paint"]), 4)
        expected = expected_fit_image()
        if actual != expected:
            pixel = next(i for i in range(120 * 240) if actual[i*4:i*4+4] != expected[i*4:i*4+4])
            self.fail(f"pixel ({pixel % 120},{pixel // 120}) expected {list(expected[pixel*4:pixel*4+4])}, "
                      f"got {list(actual[pixel*4:pixel*4+4])}")

    def test_original_s050_image_fixture_decodes_and_fits_both_resources(self):
        frame, pixels = render("images.html", 1396, 834, svg=True)
        images = [c for c in frame["paint"] if c["kind"] == 3]
        self.assertEqual(len(images), 2)
        raster, vector = images
        self.assertEqual(raster["url"], "https://s050.test/pixel.png")
        self.assertEqual((raster["w"], raster["h"]), (48, 48))
        self.assertEqual(vector["url"], "https://s050.test/mark.svg")
        self.assertAlmostEqual(vector["w"], 64)
        self.assertAlmostEqual(vector["h"], 64 * 48 / 72)
        # The SVG has no XML declaration. Its green circle and pale rectangle
        # must appear in the raster, not just an image command/placeholder.
        def pixel(x, y):
            at = (int(y) * 1396 + int(x)) * 4
            return pixels[at:at + 4]
        self.assertEqual(pixel(vector["x"] + vector["w"] / 2, vector["y"] + vector["h"] / 2),
                         bytes((21, 128, 61, 255)))
        self.assertEqual(pixel(vector["x"] + 5, vector["y"] + 5), bytes((220, 252, 231, 255)))
        self.assertEqual(pixel(raster["x"] + 10, raster["y"] + 10), BLUE)
        clips = [c for c in frame["paint"] if c["kind"] == 6]
        for image in images:
            clip = next(c for c in clips if c["node"] == image["node"])
            self.assertEqual((clip["w"], clip["h"]), (64, 48))
            self.assertAlmostEqual(image["x"] + image["w"] / 2, clip["x"] + 32)
            self.assertAlmostEqual(image["y"] + image["h"] / 2, clip["y"] + 24)

    def test_scale_down_shrinks_and_none_is_clipped(self):
        frame, pixels = render("Psycho_object_fit_small.html", 8, 8)
        images = [c for c in frame["paint"] if c["kind"] == 3]
        clips = [c for c in frame["paint"] if c["kind"] == 6]
        self.assertEqual([(c["x"], c["y"], c["w"], c["h"]) for c in images],
                         [(0, 0, 1, 1), (-0.5, 0.5, 2, 2)])
        self.assertEqual([(c["x"], c["y"], c["w"], c["h"]) for c in clips], [(0, 0, 1, 1), (0, 1, 1, 1)])
        # No source pixel may escape either 1x1 content box.
        for y in range(8):
            for x in range(8):
                if x != 0 or y >= 2:
                    self.assertEqual(pixels[(y * 8 + x) * 4:(y * 8 + x + 1) * 4], WHITE)

    def test_canvas_crop_affine_clip_and_fill_rule_pixels(self):
        frame, actual = render("Psycho_canvas_crop_winding.html", 40, 30)
        self.assertEqual(frame["title"], "alpha:0.5")
        red, green, blue = bytes((255, 0, 0, 255)), bytes((0, 255, 0, 255)), bytes((0, 0, 255, 255))
        expected = bytearray(WHITE * (40 * 30))
        def rect(x, y, w, h, color):
            for py in range(y, y + h):
                for px in range(x, x + w):
                    at = (py * 40 + px) * 4
                    expected[at:at + 4] = color
        # Integer source clipping and negative dimensions grow without flipping.
        rect(8, 0, 4, 4, red); rect(12, 0, 4, 4, green)
        rect(0, 5, 4, 4, green); rect(4, 5, 4, 4, blue)
        rect(8, 5, 4, 4, red); rect(12, 5, 4, 4, green)
        # Current canvas sampling is nearest-neighbor; fractional source
        # positions must not be rounded to an integer temporary crop first.
        rect(16, 5, 2, 1, green); rect(18, 5, 2, 1, blue)
        rect(20, 12, 4, 2, red); rect(20, 14, 4, 2, green)
        rect(0, 12, 12, 12, red); rect(4, 16, 4, 4, WHITE)
        rect(26, 0, 12, 10, blue)
        if actual != expected:
            pixel = next(i for i in range(40 * 30) if actual[i*4:i*4+4] != expected[i*4:i*4+4])
            self.fail(f"pixel ({pixel % 40},{pixel // 40}) expected {list(expected[pixel*4:pixel*4+4])}, "
                      f"got {list(actual[pixel*4:pixel*4+4])}")

    def test_rotation_animation_paints_and_respects_reduced_motion(self):
        # A 20x10 rectangle at (20,20) is rotated 90 degrees around (30,25)
        # one quarter into a linear turn. These are independently specified
        # interior pixels, not a screenshot captured from the engine.
        def pixel(pixels, x, y):
            return pixels[(y * 60 + x) * 4:(y * 60 + x + 1) * 4]
        frame, animated = render("Psycho_spin_animation.html", 60, 60)
        _, reduced = render("Psycho_spin_animation.html", 60, 60, reduced=True)
        self.assertTrue(any(c["kind"] == 11 and 88 < c["rotation"] < 92 for c in frame["paint"]))
        red = bytes((255, 0, 0, 255))
        self.assertEqual(pixel(animated, 30, 17), red)
        self.assertEqual(pixel(animated, 21, 25), WHITE)
        self.assertEqual(pixel(reduced, 30, 17), WHITE)
        self.assertEqual(pixel(reduced, 21, 25), red)

    def test_nested_sticky_paint_and_cancelable_wheel(self):
        def pixel(pixels, x, y):
            return pixels[(y * 160 + x) * 4:(y * 160 + x + 1) * 4]
        before, p0 = render("Psycho_nested_sticky_wheel.html", 160, 200)
        canceled, pc = render("Psycho_nested_sticky_wheel.html", 160, 200, ("wheel:20,50,30",))
        moved, p1 = render("Psycho_nested_sticky_wheel.html", 160, 200, ("wheel:20,50,40",))
        self.assertEqual(canceled["title"], "wheel:30")
        self.assertEqual(moved["title"], "wheel:40")
        self.assertEqual(p0, pc)
        self.assertEqual(pixel(p1, 20, 40), bytes((18, 52, 86, 255)))
        self.assertEqual(pixel(p1, 20, 60), bytes((171, 205, 239, 255)))
        self.assertEqual(pixel(p1, 20, 120), bytes((255, 0, 0, 255)))
        self.assertNotEqual(p0, p1)

    def test_flex_checkable_raster_tracks_click_state(self):
        # Inspect actual pixels, not just :checked styling or DOM state. No
        # text glyphs or platform-specific native theme are involved here.
        def pixel(pixels, x, y):
            return pixels[(y * 100 + x) * 4:(y * 100 + x + 1) * 4]
        _, before = render("Psycho_flex_checkable.html", 100, 40)
        _, after = render("Psycho_flex_checkable.html", 100, 40, ("click:10,10",))
        _, again = render("Psycho_flex_checkable.html", 100, 40,
                          ("click:10,10", "click:10,10", "click:70,10"))
        ink = bytes((18, 52, 86, 255))
        self.assertEqual(pixel(before, 10, 10), WHITE)
        self.assertEqual(pixel(before, 30, 10), ink)
        self.assertEqual(pixel(before, 50, 10), ink)
        self.assertEqual(pixel(before, 70, 10), ink)
        self.assertEqual(pixel(after, 10, 10), ink)
        self.assertEqual(pixel(again, 10, 10), WHITE)
        self.assertEqual(pixel(again, 70, 10), ink)

    def test_inherit_unset_initial_invalid_and_live_mutation(self):
        before, _ = render("Psycho_object_fit_mutation.html", 120, 220)
        after, _ = render("Psycho_object_fit_mutation.html", 120, 220, ("click:10,10",))
        def rects(frame):
            return [(c["x"], c["y"], c["w"], c["h"]) for c in frame["paint"] if c["kind"] == 3]
        self.assertEqual(before["title"], "before")
        self.assertEqual(after["title"], "changed")
        self.assertEqual(rects(before), [(20, 20, 40, 40), (0, 60, 80, 40),
                                         (0, 100, 80, 40), (0, 140, 80, 40)])
        self.assertEqual(rects(after), [(0, 0, 80, 80), (0, 60, 80, 40),
                                        (0, 100, 80, 40), (0, 140, 80, 40)])
        # The changed cover image must be clipped to its own content box.
        clips = [c for c in after["paint"] if c["kind"] == 6]
        self.assertEqual([(c["x"], c["y"], c["w"], c["h"]) for c in clips], [(0, 20, 80, 40)])


if __name__ == "__main__":
    parser = argparse.ArgumentParser()
    parser.add_argument("--engine", type=pathlib.Path, default=ENGINE)
    args, remaining = parser.parse_known_args()
    ENGINE = args.engine.resolve()
    unittest.main(argv=[sys.argv[0], *remaining])
