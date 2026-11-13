#!/usr/bin/env python3
"""Render a viewport or compare a capture with an inventoried legacy screenshot."""
from __future__ import annotations

import argparse
import hashlib
import json
import pathlib
import platform
import struct
import subprocess
import sys
import zlib

from Psycho_golden import compare, decode_png, write_png

ROOT = pathlib.Path(__file__).resolve().parents[1]
REFERENCE = ROOT / "docs/reference/legacy-ui"
INVENTORY = ROOT / "docs/reference/legacy-inventory.json"


def sha256(path: pathlib.Path) -> str:
    return hashlib.sha256(path.read_bytes()).hexdigest()


def run(args: argparse.Namespace) -> int:
    output = args.outdir.resolve()
    reference = REFERENCE / args.reference
    if pathlib.Path(args.reference).name != args.reference:
        raise ValueError("reference must be an inventoried screenshot filename, not a path")
    input_paths = [reference, INVENTORY, REFERENCE / "CAPTURE.json"]
    if args.input:
        input_paths += [args.input, args.engine]
    if args.actual:
        input_paths.append(args.actual)
    resources = []
    for resource in args.resource:
        url, sep, filename = resource.partition("=")
        if not sep or not url or not filename:
            raise ValueError("resource must be URL=FILE")
        resources.append((url, pathlib.Path(filename).resolve()))
        input_paths.append(pathlib.Path(filename))
    # Refuse all potential overwrites, including through symlinked paths.
    targets = [output / name for name in ("actual.png", "diff.png", "report.json")]
    if any(path.is_symlink() for path in targets):
        raise ValueError("output files must not be symlinks")
    if any(path.resolve() in targets for path in input_paths):
        raise ValueError("output files would overwrite an input")
    if output == REFERENCE.resolve() or REFERENCE.resolve() in output.parents:
        raise ValueError("output directory must not be inside the reference archive")
    output.mkdir(parents=True, exist_ok=True)
    for target in targets:
        target.unlink(missing_ok=True)

    report = {
        "schema": 1, "result": "failed", "reference": args.reference,
        "policy": {"metric": "exact RGBA changed pixels", "max_changed_pixels": args.max_changed_pixels},
        "platform": platform.platform(),
        "warning": "Visual comparison only; matching capture state, scale, font environment and content must be established by the operator. Not a P2 or P3 parity claim.",
    }
    try:
        inventory = json.loads(INVENTORY.read_text(encoding="utf-8"))
        capture = json.loads((REFERENCE / "CAPTURE.json").read_text(encoding="utf-8"))
        matches = [s for s in inventory["screenshots"] if s["file"] == args.reference]
        if len(matches) != 1:
            raise ValueError("reference is not uniquely recorded in the screenshot inventory")
        item = matches[0]
        digest = sha256(reference)
        if digest != item["sha256"] or reference.stat().st_size != item["bytes"]:
            raise ValueError("reference screenshot does not match inventory hash/size")
        width, height, expected = decode_png(reference)
        if [width, height] != [item["width"], item["height"]]:
            raise ValueError("reference dimensions do not match inventory")
        report.update({"reference_sha256": digest, "capture_source_commit": capture["sourceCommit"],
                       "capture_timestamp": capture["capturedAt"],
                       "inventory_source_commit": inventory["sourceCommit"],
                       "viewport": [width, height]})
        actual_path = targets[0]
        if args.input:
            command = [str(args.engine.resolve()), "--input", str(args.input.resolve()), "--url", args.url,
                       "--width", str(width), "--height", str(height), "--out", str(actual_path)]
            for url, path in resources:
                command.extend(["--resource", f"{url}={path}"])
            for action in args.action:
                command.extend(["--action", action])
            if args.scroll is not None:
                command.extend(["--scroll", str(args.scroll)])
            if args.dpr is not None:
                command.extend(["--dpr", str(args.dpr)])
            if args.reduced_motion:
                command.append("--reduced-motion")
            report["renderer_command"] = command
            report["input_sha256"] = sha256(args.input)
            report["resources"] = [{"url": url, "sha256": sha256(path)} for url, path in resources]
            proc = subprocess.run(command, cwd=ROOT, text=True, capture_output=True, timeout=120)
            report["renderer_exit"] = proc.returncode
            report["renderer_diagnostics"] = proc.stderr.strip()
            if proc.returncode or proc.stderr.strip():
                raise RuntimeError(f"renderer failed or emitted diagnostics: exit {proc.returncode}: {proc.stderr.strip()}")
            frame = json.loads(proc.stdout)
            if not isinstance(frame, dict):
                raise ValueError("renderer returned invalid frame")
        else:
            if resources or args.action or args.scroll is not None or args.dpr is not None or args.reduced_motion:
                raise ValueError("renderer options cannot be used with --actual")
            actual_path = args.actual.resolve()
            report["actual_source"] = str(actual_path)
        aw, ah, actual = decode_png(actual_path)
        if (aw, ah) != (width, height):
            raise ValueError(f"actual dimensions {aw}x{ah} do not match reference {width}x{height}")
        report["actual_sha256"] = sha256(actual_path)
        changed, diff = compare(expected, actual)
        write_png(targets[1], width, height, diff)
        report.update({"changed_pixels": changed, "total_pixels": width * height,
                       "changed_fraction": changed / (width * height), "diff": str(targets[1]),
                       "result": "passed" if changed <= args.max_changed_pixels else "failed"})
        if report["result"] == "failed":
            report["error"] = f"{changed} changed pixels exceed limit {args.max_changed_pixels}"
    except (OSError, ValueError, KeyError, TypeError, RuntimeError, subprocess.TimeoutExpired,
            struct.error, zlib.error) as error:
        report["error"] = str(error)
    targets[2].write_text(json.dumps(report, indent=2) + "\n", encoding="utf-8")
    print(f"{report['result']}: {report.get('error', 'within comparison limit')}; report: {targets[2]}")
    return 0 if report["result"] == "passed" else 1


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--reference", required=True, help="filename in docs/reference/legacy-ui and inventory")
    source = parser.add_mutually_exclusive_group(required=True)
    source.add_argument("--input", type=pathlib.Path, help="HTML to render with pv-engine")
    source.add_argument("--actual", type=pathlib.Path, help="existing PNG capture (e.g. native window)")
    parser.add_argument("--engine", type=pathlib.Path, default=ROOT / "build" / ("pv-engine.exe" if sys.platform == "win32" else "pv-engine"))
    parser.add_argument("--outdir", type=pathlib.Path, required=True)
    parser.add_argument("--url", default="https://reference.test/")
    parser.add_argument("--resource", action="append", default=[], metavar="URL=FILE")
    parser.add_argument("--action", action="append", default=[])
    parser.add_argument("--scroll", type=float)
    parser.add_argument("--dpr", type=float)
    parser.add_argument("--reduced-motion", action="store_true")
    parser.add_argument("--max-changed-pixels", type=int, default=0)
    args = parser.parse_args()
    if args.max_changed_pixels < 0:
        parser.error("--max-changed-pixels cannot be negative")
    try:
        return run(args)
    except (OSError, ValueError) as error:
        print(f"reference diff: {error}", file=sys.stderr)
        return 1


if __name__ == "__main__":
    sys.exit(main())
