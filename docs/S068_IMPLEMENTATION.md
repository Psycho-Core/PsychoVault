# S068 — Canvas 2D subset

**Status (2026-11-01):** A focused software Canvas 2D subset is implemented and Linux-tested. It is not full Canvas 2D conformance; see remaining work below.

## Implemented

- Arbitrary path clipping via `clip()` with nonzero/even-odd rules. Clips intersect, apply to raster writes and `clearRect`, and are restored by `save()`/`restore()`.
- Path coordinates capture the affine transform active when each path segment is constructed. Added `transform()`, `resetTransform()`, complete affine image mapping (including rotation/shear), transformed text placement, transformed `clearRect`, and approximate transformed stroke width.
- Added `textBaseline` state for top/hanging/middle/alphabetic/ideographic/bottom, transformed text raster placement, valid `textAlign` values, and stroke-colored `strokeText` output. (Glyph outline stroking is not implemented.)
- `save()`/`restore()` now includes line dashes and clip state. `getLineDash()` returns the current dash pattern; invalid patterns leave the previous pattern intact. Canvas bitmap resize resets path, state, dash and clip stacks.
- Corrected Canvas arc/ellipse anticlockwise argument handling and retained curve/rounded-rect geometry under active transforms.

Regression coverage in `tests/Psycho_test_main.cpp` exercises transformed paths, a rotated ellipse, affine image pixels, clip intersections and holes, empty clips, clip and dash restoration, transformed clearing, path continuation, and same-size bitmap reset. The existing canvas smoke test covers pixel APIs, gradients and image-to-canvas drawing. A Windows-only check exercises DirectWrite glyph alpha and transformed clipping.

Linux validation: `python3 tools/Psycho_build.py --compiler gcc --target tests --outdir /tmp/pv-s068-verified && /tmp/pv-s068-verified/pv-tests` passed **1,056 engine checks / 2,903 total checks** (before adding the Windows-only test).

## Remaining work / limits

- No `Path2D`, `arcTo`, `ellipse` shape quality beyond sampled polygons, line-cap/join/miter behavior, compositing modes, shadows, image smoothing, or full DOMMatrix/getTransform support.
- Stroke text is rendered using the stroke color but is not an outlined glyph path. Text measurement/metrics and baseline placement are approximations; Linux has no text raster backend.
- Image sampling is nearest-neighbor. Negative source/destination rectangles, crop validation, alpha/compositing edge cases, image-source lifecycle, and browser exception semantics need further conformance work.
- Clipping uses software point-in-path tests with polygonal path approximations; complex/self-intersecting path rules and performance have not been validated against a browser reference. Beyond 64 nested clips, drawing fails closed until restored; dash traversal is capped at 100,000 segments per stroke. Save depth is capped at 64.
- Windows DirectWrite/raster output and end-to-end page canvas painting have not been compiled or interactively tested in this Linux environment.
