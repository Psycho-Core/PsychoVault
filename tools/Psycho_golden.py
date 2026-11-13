#!/usr/bin/env python3
"""Run the owned S052-S069 rendering regression corpus; no external libraries.

These are checked-in project baselines, not browser/reference screenshots. A
candidate mode writes reviewable output elsewhere, never overwrites baselines.
"""
from __future__ import annotations
import argparse
import json
import pathlib
import struct
import subprocess
import sys
import zlib

ROOT = pathlib.Path(__file__).resolve().parents[1]
FIXTURES = ROOT / "tests/golden/fixtures"
BASELINES = ROOT / "tests/golden/baselines"
WIDTH, HEIGHT = 160, 100
WHITE = [255, 255, 255, 255]
GREEN = [34, 170, 68, 255]
RED = [221, 34, 34, 255]
BLUE = [34, 51, 221, 255]
# Explicit semantic probes complement image comparisons so an accidental
# baseline update cannot turn an obviously wrong feature state into a pass.
CASES = {
    "s052": {"probes": [(10, 10, [224, 68, 34, 255]), (0, 0, [34, 68, 170, 255])]},
    "s053": {"actions": ["hover:10,10"], "probes": [(10, 10, GREEN)]},
    "s054": {"probes": [(2, 2, [34, 34, 204, 255]), (15, 15, [238, 119, 51, 255]), (90, 30, WHITE)]},
    "s055": {"probes": [(10, 10, RED), (50, 10, GREEN), (110, 10, BLUE)]},
    "s056": {"probes": [(10, 10, RED), (60, 10, GREEN), (10, 40, BLUE), (60, 40, [255, 170, 0, 255])]},
    "s057": {"probes": [(15, 15, RED), (35, 25, BLUE), (90, 60, [204, 204, 204, 255])]},
    "s058": {"probes": [(20, 20, [238, 119, 51, 255]), (10, 10, WHITE), (9, 30, [34, 51, 221, 255])]},
    "s059": {"actions": ["wheel:10,10,40"], "probes": [(10, 10, GREEN), (10, 45, BLUE)]},
    "s060": {"actions": ["click:10,10"], "title": "clicked", "probes": [(10, 10, GREEN)]},
    "s061": {"actions": ["click:10,10"], "probes": [(10, 30, GREEN)]},
    "s062": {"actions": ["click:10,10", "text:x", "click:130,80"], "title": "x", "probes": [(10, 30, GREEN)]},
    "s063": {"image": True, "probes": [(10, 10, [255, 0, 0, 255])]},
    "s064": {"dpr": 2, "probes": [(10, 10, GREEN)]},
    "s065": {"reduced": True, "probes": [(10, 10, GREEN)]},
    "s066": {"actions": ["hover:10,10"], "damage": True, "probes": [(10, 10, GREEN), (10, 40, BLUE)]},
    "s067": {"actions": ["render"], "compositor": True, "probes": [(20, 20, RED), (75, 20, [144, 153, 238, 255])]},
    "s068": {"canvas": True, "probes": [(2, 2, RED), (20, 20, GREEN), (60, 20, RED)]},
    "s069": {"font": True, "probes": [(90, 30, GREEN)]},
}


def decode_png(path: pathlib.Path) -> tuple[int, int, bytes]:
    data = path.read_bytes()
    if data[:8] != b"\x89PNG\r\n\x1a\n":
        raise ValueError(f"{path}: invalid PNG signature")
    at, idat, width, height, channels = 8, bytearray(), 0, 0, 0
    while at + 12 <= len(data):
        size = int.from_bytes(data[at:at + 4], "big")
        kind = data[at + 4:at + 8]
        payload = data[at + 8:at + 8 + size]
        if at + 12 + size > len(data) or zlib.crc32(kind + payload) != int.from_bytes(data[at + 8 + size:at + 12 + size], "big"):
            raise ValueError(f"{path}: corrupt PNG chunk")
        at += size + 12
        if kind == b"IHDR":
            width, height, depth, color, compression, filtering, interlace = struct.unpack(">IIBBBBB", payload)
            if depth != 8 or color not in (2, 6) or (compression, filtering, interlace) != (0, 0, 0):
                raise ValueError(f"{path}: only noninterlaced RGB8 or RGBA8 PNG is supported")
            channels = 3 if color == 2 else 4
        elif kind == b"IDAT":
            idat.extend(payload)
        elif kind == b"IEND":
            break
    if not (0 < width <= 8192 and 0 < height <= 8192 and channels):
        raise ValueError(f"{path}: invalid dimensions or missing IHDR")
    stride = width * channels
    expected_size = height * (stride + 1)
    decompressor = zlib.decompressobj()
    raw = decompressor.decompress(idat, expected_size + 1)
    if len(raw) != expected_size or not decompressor.eof or decompressor.unused_data:
        raise ValueError(f"{path}: unexpected image data size")
    out = bytearray(width * height * channels)
    previous = bytearray(stride)
    for y in range(height):
        filter_type = raw[y * (stride + 1)]
        row = bytearray(raw[y * (stride + 1) + 1:(y + 1) * (stride + 1)])
        for x in range(stride):
            a = row[x - channels] if x >= channels else 0
            b = previous[x]
            c = previous[x - channels] if x >= channels else 0
            if filter_type == 1: predict = a
            elif filter_type == 2: predict = b
            elif filter_type == 3: predict = (a + b) // 2
            elif filter_type == 4:
                p = a + b - c
                pa, pb, pc = abs(p - a), abs(p - b), abs(p - c)
                predict = a if pa <= pb and pa <= pc else b if pb <= pc else c
            elif filter_type == 0: predict = 0
            else: raise ValueError(f"{path}: unsupported PNG filter {filter_type}")
            row[x] = (row[x] + predict) & 255
        out[y * stride:(y + 1) * stride] = row
        previous = row
    if channels == 3:
        rgba = bytearray(width * height * 4)
        for pixel in range(width * height):
            rgba[pixel * 4:pixel * 4 + 3] = out[pixel * 3:pixel * 3 + 3]
            rgba[pixel * 4 + 3] = 255
        out = rgba
    return width, height, bytes(out)


def write_png(path: pathlib.Path, width: int, height: int, rgba: bytes) -> None:
    if len(rgba) != width * height * 4:
        raise ValueError("invalid image size")
    def chunk(kind: bytes, payload: bytes) -> bytes:
        return struct.pack(">I", len(payload)) + kind + payload + struct.pack(">I", zlib.crc32(kind + payload))
    raw = b"".join(b"\0" + rgba[y * width * 4:(y + 1) * width * 4] for y in range(height))
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_bytes(b"\x89PNG\r\n\x1a\n" + chunk(b"IHDR", struct.pack(">IIBBBBB", width, height, 8, 6, 0, 0, 0)) +
                     chunk(b"IDAT", zlib.compress(raw, 9)) + chunk(b"IEND", b""))


def compare(expected: bytes, actual: bytes) -> tuple[int, bytes]:
    if len(expected) != len(actual) or len(actual) % 4:
        raise ValueError("image sizes differ")
    changed = 0
    diff = bytearray(len(actual))
    for i in range(0, len(actual), 4):
        magnitude = max(abs(expected[i + j] - actual[i + j]) for j in range(4))
        if magnitude: changed += 1
        diff[i:i + 4] = bytes((magnitude, 0, 0, 255)) if magnitude else b"\0\0\0\xff"
    return changed, bytes(diff)


def validate(case: dict, frame: dict, rgba: bytes) -> None:
    for x, y, expected in case["probes"]:
        at = (y * WIDTH + x) * 4
        actual = list(rgba[at:at + 4])
        if actual != expected:
            raise ValueError(f"pixel ({x},{y}) is {actual}, expected {expected}")
    if "title" in case and frame.get("title") != case["title"]:
        raise ValueError(f"title {frame.get('title')!r} != {case['title']!r}")
    if case.get("damage") and (frame.get("fullDamage") or not frame.get("damageTiles")):
        raise ValueError("expected localized incremental damage tiles")
    comp = frame.get("compositor", {})
    if case.get("compositor") and (frame.get("fullDamage") or frame.get("damageTiles") != [] or
                                    comp.get("fixedLayerCommands", 0) < 1 or comp.get("effectLayers", 0) < 1):
        raise ValueError("expected fixed/effect metadata and no damage on unchanged frame")
    paint = frame.get("paint", [])
    if case.get("image") and not any(c.get("kind") == 3 for c in paint):
        raise ValueError("image paint command missing")
    if case.get("canvas") and not any(c.get("kind") == 5 for c in paint):
        raise ValueError("canvas paint command missing")
    if case.get("font") and not any(c.get("kind") == 2 and "PsychoVault UI" in c.get("fontFamily", "") and
                                    c.get("fontWeight") == 600 for c in paint):
        raise ValueError("font family or numeric weight missing from frame")


def run(engine: pathlib.Path, output: pathlib.Path, candidates: bool = False) -> int:
    output = output.resolve()
    if output == BASELINES.resolve() or BASELINES.resolve() in output.parents:
        raise ValueError("output directory must not be inside the checked-in baselines")
    output.mkdir(parents=True, exist_ok=True)
    platform = "windows" if sys.platform == "win32" else "portable"
    report = {"schema": 1, "platform": platform, "viewport": [WIDTH, HEIGHT], "cases": []}
    for name, case in CASES.items():
        result = {"id": name.upper(), "result": "failed"}
        try:
            image = output / (name + ".png")
            image.unlink(missing_ok=True)
            (output / (name + ".diff.png")).unlink(missing_ok=True)
            cmd = [str(engine), "--input", str(FIXTURES / (name + ".html")),
                   "--url", f"https://golden.test/{name}.html", "--width", str(WIDTH),
                   "--height", str(HEIGHT), "--out", str(image)]
            if case.get("dpr"): cmd += ["--dpr", str(case["dpr"])]
            if case.get("reduced"): cmd.append("--reduced-motion")
            if case.get("image"): cmd += ["--resource", f"https://golden.test/pixel.png={FIXTURES / 'pixel.png'}"]
            for action in case.get("actions", []): cmd += ["--action", action]
            proc = subprocess.run(cmd, cwd=ROOT, text=True, capture_output=True, timeout=30)
            if proc.returncode:
                raise RuntimeError(f"renderer exited {proc.returncode}: {proc.stderr.strip()}")
            if proc.stderr.strip():
                raise RuntimeError(f"renderer diagnostics: {proc.stderr.strip()}")
            frame = json.loads(proc.stdout)
            if not isinstance(frame, dict): raise ValueError("renderer frame is not a JSON object")
            w, h, pixels = decode_png(image)
            if (w, h) != (WIDTH, HEIGHT): raise ValueError(f"actual dimensions {w}x{h}")
            validate(case, frame, pixels)
            result["actual"] = str(image)
            if candidates:
                write_png(output / "candidates" / (name + ".png"), w, h, pixels)
                result["result"] = "candidate"
            else:
                platform_baseline = BASELINES / platform / (name + ".png")
                baseline = platform_baseline if name == "s069" or platform_baseline.exists() else BASELINES / "common" / (name + ".png")
                ew, eh, expected = decode_png(baseline)
                if (ew, eh) != (w, h): raise ValueError(f"baseline dimensions {ew}x{eh}, actual {w}x{h}")
                changed, diff = compare(expected, pixels)
                result["baseline"] = str(baseline.relative_to(ROOT) if ROOT in baseline.parents else baseline)
                result["changed_pixels"] = changed
                if changed:
                    diff_path = output / (name + ".diff.png")
                    write_png(diff_path, w, h, diff)
                    result["diff"] = str(diff_path)
                    raise ValueError(f"{changed} pixels differ; see {diff_path}")
                result["result"] = "passed"
        except (OSError, RuntimeError, ValueError, subprocess.TimeoutExpired, zlib.error, struct.error) as error:
            result["error"] = str(error)
        report["cases"].append(result)
        print(f"{result['id']}: {result['result']}" + (f" — {result['error']}" if "error" in result else ""))
    failed = sum(case["result"] == "failed" for case in report["cases"])
    report["failed"] = failed
    (output / "report.json").write_text(json.dumps(report, indent=2) + "\n", encoding="utf-8")
    print(f"{len(CASES) - failed}/{len(CASES)} {'candidates generated' if candidates else 'passed'}; report: {output / 'report.json'}")
    return 1 if failed else 0


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--engine", type=pathlib.Path, default=ROOT / "build" / ("pv-engine.exe" if sys.platform == "win32" else "pv-engine"))
    parser.add_argument("--outdir", type=pathlib.Path, default=ROOT / "build/golden-results")
    parser.add_argument("--candidates", action="store_true", help="write candidate PNGs to OUTDIR/candidates, never change baselines")
    args = parser.parse_args()
    return run(args.engine.resolve(), args.outdir.resolve(), args.candidates)


if __name__ == "__main__":
    sys.exit(main())
