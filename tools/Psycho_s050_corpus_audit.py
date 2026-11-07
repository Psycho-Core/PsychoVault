#!/usr/bin/env python3
"""Render the license-safe S050 corpus with pv-engine and emit an audit report.

This is a deterministic fixture/render smoke harness, not a standards suite or
visual parity test. Feature-level semantic gaps and their owned regression tests
are separately recorded as reviewed deferrals in docs/Psycho_ENGINE-GAPS.md.
"""
from __future__ import annotations

import argparse
import json
import pathlib
import subprocess
import sys

ROOT = pathlib.Path(__file__).resolve().parents[1]
FIXTURES = ROOT / "tests/fixtures/p2"

CASES = [
    ("P2TextFallbackFixtureRender", "text-controls.html", "P2 audit — text and controls",
     ["Latin:", "Web address", "Remember workspace"], []),
    ("P2FlexGridFixtureRender", "flex-grid.html", "P2 audit — flex and grid",
     ["Flex card A", "Grid label", "Renderer process row"], []),
    ("P2NestedScrollFixtureRender", "scroll-position.html", "P2 audit — scroll and positioning",
     ["Sticky header marker", "Scroll item 01", "bottom sentinel marker"], []),
    ("P2ImageSvgFixtureRender", "images.html", "P2 audit — images and SVG",
     ["Image, sizing, and SVG audit", "raster marker", "SVG vector marker."], [
         ("https://s050.test/pixel.png", FIXTURES / "pixel.png"),
         ("https://s050.test/mark.svg", FIXTURES / "mark.svg"),
     ]),
    ("P2DynamicDomScriptFixtureRender", "dynamic-dom.html", "P2 audit — dynamic DOM",
     ["External script mutation marker"], [
         ("https://s050.test/fixture.js", FIXTURES / "fixture.js"),
     ]),
]

# Every P0/P1 row in the gap matrix is represented below. The render test only
# proves that its representative fixture reaches the engine's paint output;
# the named semantic regression remains explicitly deferred to its plan step.
FEATURES = [
    ("text-fallback", "P0", "P2TextFallbackFixtureRender", "text-controls.html", "S051", "UiTextShapingFallbackMetrics"),
    ("custom-properties", "P0", "P2TextFallbackFixtureRender", "text-controls.html", "S052", "UiCustomPropertiesCascadeAndMutation"),
    ("css-math", "P0", "P2FlexGridFixtureRender", "flex-grid.html", "S052", "UiCssMathFunctionsAndMixedUnits"),
    ("interactive-pseudo-classes", "P0", "P2TextFallbackFixtureRender", "text-controls.html", "S053", "UiPseudoClassInteractionMatrix"),
    ("box-sizing-intrinsic-percent", "P0", "P2TextFallbackFixtureRender", "text-controls.html", "S054", "UiBoxSizingPercentAndIntrinsicSizing"),
    ("flex-layout", "P0", "P2FlexGridFixtureRender", "flex-grid.html", "S055", "UiFlexReferenceRowsAndWrap"),
    ("grid-layout", "P1", "P2FlexGridFixtureRender", "flex-grid.html", "S056", "UiGridTrackSizingAndAreas"),
    ("positioning-stacking-clipping", "P1", "P2NestedScrollFixtureRender", "scroll-position.html", "S057", "UiPositioningStackingAndClipOrder"),
    ("paint-effects", "P0", "P2TextFallbackFixtureRender", "text-controls.html", "S058", "UiPaintEffectsAndTransformedHitTesting"),
    ("animation-media-motion", "P1", "P2TextFallbackFixtureRender", "text-controls.html", "S064/S065", "UiAnimationMediaAndReducedMotion"),
    ("nested-scroll-scrollbars", "P1", "P2NestedScrollFixtureRender", "scroll-position.html", "S059", "UiNestedScrollWheelAndScrollbar"),
    ("pointer-dispatch", "P0", "P2DynamicDomScriptFixtureRender", "dynamic-dom.html", "S060", "UiPointerDelegationAndDefaultActions"),
    ("form-control-state", "P0", "P2TextFallbackFixtureRender", "text-controls.html", "S061", "UiControlStateLabelAndValidation"),
    ("text-editing-ime", "P0", "P2TextFallbackFixtureRender", "text-controls.html", "S062", "UiCaretSelectionClipboardAndIme"),
    ("images-svg-favicon", "P0", "P2ImageSvgFixtureRender", "images.html", "S063", "UiImageIntrinsicSizingSvgAndFavicon"),
    ("dynamic-dom-mutation", "P1", "P2DynamicDomScriptFixtureRender", "dynamic-dom.html", "S066", "UiDynamicDomAndDataAttributes"),
    ("dataset-selector-geometry", "P0", "P2DynamicDomScriptFixtureRender", "dynamic-dom.html", "S060", "UiDatasetClosestMatchesAndDomRect"),
    ("reference-script-builtins-bridge", "P1", "P2DynamicDomScriptFixtureRender", "dynamic-dom.html", "S066", "UiReferenceScriptApiSmoke"),
    ("inline-style-variable-reflow", "P1", "P2FlexGridFixtureRender", "flex-grid.html", "S052", "UiInlineStyleMutationReflow"),
]


def run_case(engine: pathlib.Path, outdir: pathlib.Path, width: int, height: int,
             case: tuple) -> dict:
    test_id, filename, title, markers, resources = case
    fixture = FIXTURES / filename
    image = outdir / f"{fixture.stem}.png"
    command = [str(engine), "--input", str(fixture), "--url", f"https://s050.test/{filename}",
               "--width", str(width), "--height", str(height), "--out", str(image)]
    for url, path in resources:
        command.extend(["--resource", f"{url}={path}"])
    proc = subprocess.run(command, cwd=ROOT, text=True, capture_output=True)
    if proc.returncode:
        raise RuntimeError(f"{test_id} failed ({proc.returncode}):\n{proc.stderr}\n{proc.stdout}")
    try:
        frame = json.loads(proc.stdout.strip().splitlines()[-1])
    except (IndexError, json.JSONDecodeError) as error:
        raise RuntimeError(f"{test_id}: pv-engine returned invalid frame JSON: {error}")
    if frame.get("title") != title:
        raise RuntimeError(f"{test_id}: title {frame.get('title')!r}, expected {title!r}")
    paint = frame.get("paint")
    if not isinstance(paint, list) or not paint:
        raise RuntimeError(f"{test_id}: empty paint list")
    image_paint_count = sum(1 for item in paint if int(item.get("kind", -1)) == 3)
    if filename == "images.html" and image_paint_count < 2:
        raise RuntimeError(f"{test_id}: expected external raster and SVG image paint commands, saw {image_paint_count}")
    painted_text = " ".join(" ".join(str(item.get("text", "")) for item in paint).split())
    missing = [marker for marker in markers if marker not in painted_text]
    if missing:
        raise RuntimeError(f"{test_id}: marker(s) absent from paint output: {missing!r}; "
                           f"diagnostics={proc.stderr.strip()!r}")
    if not image.is_file() or image.stat().st_size < 100:
        raise RuntimeError(f"{test_id}: missing or unusable PNG output {image}")
    return {
        "test_id": test_id,
        "fixture": f"tests/fixtures/p2/{filename}",
        "result": "passed",
        "document_title": title,
        "asserted_paint_markers": markers,
        "paint_command_count": len(paint),
        "image_paint_command_count": image_paint_count,
        "image_file_bytes": image.stat().st_size,
        "supplied_resources": [url for url, _ in resources],
        "engine_diagnostics": [line for line in proc.stderr.splitlines() if line.strip()],
    }


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--engine", type=pathlib.Path, help="existing pv-engine; otherwise build it")
    parser.add_argument("--outdir", type=pathlib.Path, default=pathlib.Path("/tmp/PsychoVault-S050"))
    parser.add_argument("--report", type=pathlib.Path,
                        help="JSON report path (default: OUTDIR/s050-corpus-audit.json)")
    parser.add_argument("--width", type=int, default=1396)
    parser.add_argument("--height", type=int, default=834)
    args = parser.parse_args()
    outdir = args.outdir.resolve()
    outdir.mkdir(parents=True, exist_ok=True)
    engine = args.engine.resolve() if args.engine else outdir / "build" / "pv-engine"
    if not args.engine:
        subprocess.run([sys.executable, "tools/Psycho_build.py", "--target", "engine",
                        "--compiler", "auto", "--outdir", str(engine.parent)], cwd=ROOT, check=True)
    if not engine.is_file():
        raise RuntimeError(f"pv-engine not found: {engine}")
    results = [run_case(engine, outdir, args.width, args.height, case) for case in CASES]
    feature_rows = [{
        "feature": name, "priority": priority, "fixture_render_test": test_id,
        "fixture": f"tests/fixtures/p2/{fixture}",
        "semantic_regression": regression, "reviewed_deferral": {
            "owner": "Engine / P2 rendering", "priority": priority, "target_step": step,
            "reason": "Fixture render is only a paint-path smoke; the named semantic assertion is not yet implemented.",
        },
    } for name, priority, test_id, fixture, step, regression in FEATURES]
    report = {
        "audit": "S050 pv-engine fixture corpus render audit",
        "result": "passed",
        "scope": "5 hand-authored license-safe fixtures; render and frame-marker smoke only",
        "date": "2026-11-07",
        "reference_revision": "Psycho-Core/PsychoVault---REFERANCE-ONLY@51a0d717d2feb23ba91126db5e756a08a54e4486",
        "viewport": {"width": args.width, "height": args.height},
        "engine": "pv-engine",
        "fixture_count": len(results),
        "fixture_results": results,
        "p0_p1_feature_rows": feature_rows,
        "feature_row_count": len(feature_rows),
        "limits": [
            "No claim of CSS/DOM/ECMAScript conformance or reference visual parity.",
            "A fixture render passing does not mean its listed semantic regression has passed.",
            "Every P0/P1 behavior remains owned and ordered as a reviewed deferral until its named regression is implemented.",
            "The screenshots are generated in OUTDIR and are not committed; this JSON report is the preserved run output.",
        ],
    }
    report_path = args.report.resolve() if args.report else outdir / "s050-corpus-audit.json"
    report_path.parent.mkdir(parents=True, exist_ok=True)
    report_path.write_text(json.dumps(report, indent=2) + "\n", encoding="utf-8")
    print(json.dumps(report, indent=2))
    print(f"Preserved report: {report_path}")
    return 0


if __name__ == "__main__":
    try:
        raise SystemExit(main())
    except (OSError, RuntimeError, subprocess.CalledProcessError) as error:
        print(f"S050 corpus audit failed: {error}", file=sys.stderr)
        raise SystemExit(1)
