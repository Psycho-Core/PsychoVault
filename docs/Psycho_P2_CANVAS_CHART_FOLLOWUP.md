# Canvas chart/page follow-up

This focused S068 increment addresses observable drawing gaps without claiming
full Canvas 2D conformance:

- `fill('evenodd')` applies the even-odd rule across the current path; the
  default remains nonzero. Both solid and gradient fills use the same path mask.
- `drawImage` maps fractional source rectangles directly to the destination,
  clips source overflow proportionally, and handles negative source and
  destination dimensions without flipping. Affine transforms, current clip,
  alpha and source-over blending continue to apply. This avoids allocating a
  potentially large intermediate crop. Sampling remains nearest-neighbor.
- Out-of-range and non-finite `globalAlpha` assignments are ignored rather
  than clamped or retained as NaN.
- The common `px` Canvas font shorthand passes the chosen family list and
  numeric weight to both text measurement and the glyph rasterizer. A Windows
  test distinguishes Arial and Courier New; portable builds have approximate
  metrics and deliberately do not rasterize glyphs.

`Psycho_canvas_crop_winding.html` drives the real page Canvas API, and the
hand-specified contract checks the entire rendered pixel canvas for source
clipping, fractional nearest-neighbor sampling, negative dimensions, affine
image drawing through a clip, even-odd holes, nonzero fills and invalid alpha.
Native tests additionally cover alpha blending, degenerate inputs and font
metrics. The expectations are geometric project contracts, not captured browser
screenshots; nearest-neighbor is explicitly the current implementation policy.

Linux GCC: 1,182 engine / 3,029 total native checks; eight image/paint
contracts, seven DOM/layout contracts, focus and responsive-image contracts,
18 owned goldens, 24 independent goldens and both golden harness self-tests
passed. Windows x64 [run 45131902005](https://github.com/Psycho-Core/PsychoVault/actions/runs/45131902005)
passed the build, native tests (including the DirectWrite font-family check),
contracts, goldens, smokes and packaging on `9f347c8`. The temporary branch-only
push trigger was removed afterwards; this does not establish permission to
dispatch the parked workflow manually.

Still open: smoothing/filtering quality, comprehensive fractional-source and
transform behavior against browser oracles, glyph outline `strokeText`, full
font shorthand and text metrics/baselines, line caps/joins, `arcTo`, Canvas
compositing modes and shadows, and broader clipping and chart scenarios where
required by actual consumers. `Path2D` and complete browser conformance are not
automatic Phase 2 requirements. See `S068_IMPLEMENTATION.md` for the previous
subset and its remaining limits.
