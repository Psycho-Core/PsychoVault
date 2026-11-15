# Hand-specified Phase 2 pixel goldens

These 24 deterministic 160×100 scenarios supplement the [owned-baseline suite](README.md). `tools/Psycho_independent_goldens.py` constructs each expected RGBA image from explicit, hand-specified rectangles and compares **every pixel**, without recording the engine's output as the expectation. It checks image dimensions, renderer diagnostics and selected event/title, damage, compositor and paint-command metadata. On mismatch it writes the actual, expected and difference PNGs and `report.json`; it never edits fixtures or expected definitions. Tests do not depend on third-party packages.

```sh
python3 tools/Psycho_build.py --compiler gcc --target engine --outdir /tmp/pv-engine
python3 tests/Psycho_independent_golden_tests.py
python3 tools/Psycho_independent_goldens.py --engine /tmp/pv-engine/pv-engine --outdir /tmp/pv-independent-goldens
```

On Windows use `python` and `build/pv-engine.exe`; the manual-only owned-engine workflow runs both suites. All images are deliberately text-free or contain fully transparent text, avoiding cross-platform glyph raster differences. The scrollbar track/thumb and input border use this engine's existing palette: they are hand-specified project-design expectations, **not** browser-native widget or screenshot references. The `image` source is the independently checked 2×2 fixture in `tests/fixtures/p2/pixel.png`.

| Step | Specific new image/metadata assertions | Not established by these assertions |
|---|---|---|
| S052 | Inherited custom property, `calc()` mixing px and percent, percentage size and padding | Full units, `min`/`max`/`clamp`, colors and shorthand grammar |
| S053 | `:nth-child`, `:not`, `!important`, hover states | Full cascade, selectors and inheritance |
| S054 | Adjacent margin collapse, border-box padding, overflow clip | Parent/empty collapse, intrinsic sizes, all percentage/overflow cases |
| S055 | Flex order, wrap and gap | Full axis/baseline, intrinsic, shrink and constraint behavior |
| S056 | Named areas, fixed tracks, span and gap | Auto-placement, implicit tracks and complete grid sizing |
| S057 | Absolute overlap, z-order, clipped descendant and fixed viewport offset | Sticky, transformed containing blocks and all stacking cases |
| S058 | Translation and opacity compositing | Radius, gradients, shadows, filters, rotate/scale and full hit testing |
| S059 | Nested viewport before/after wheel, clip, project scrollbar/thumb placement | Horizontal, anchor preservation, smooth policy and dragging |
| S060 | Hover, click bubbling, DOM style mutation and input events | Complete pointer/keyboard/default action semantics |
| S061 | Label activation, checkbox state/`:checked` and change event | Native control appearance, select/range/file UI, full validity |
| S062 | Text input mutation and input event | Visible caret/selection, clipboard, gestures, physical IME |
| S063 | Contained 2×2 raster in an independently defined colored box | Responsive variants, SVG details, favicon lifecycle; separate targeted image contracts cover more |
| S064 | Viewport media width and DPR resolution at 1x/2x | Physical monitor changes, zoom and advanced media grammar |
| S065 | Reduced-motion suppression | Normal animation/transition timing and events |
| S066 | Local damage after hover/click; unchanged frame produces no damage | Dirty-subtree layout and real performance |
| S067 | Fixed/effect metadata and damage tiles | Retained surfaces, real present/drop telemetry and pacing |
| S068 | Canvas fill, clipping, save/restore and translation | Full paths, images, text and transforms |
| S069 | Family/size/weight metadata with transparent text at 1x/2x | DirectWrite glyphs, fallback and physical DPI; see platform golden in [README](README.md) |

## Results and discovery

Linux GCC 12.2: 24/24 hand-specified goldens and 6/6 runner self-tests; 18/18 original owned goldens, 6/6 original runner self-tests, 8/8 reference-diff self-tests, all four existing contract suites, and **1,109 engine / 2,956 total** native checks. Linux does not rasterize text glyphs.

The first Windows run [43717343094](https://github.com/Psycho-Core/PsychoVault/actions/runs/43717343094) **failed** both transparent-font cases (470 pixels each): `color:transparent` had an opaque alpha, so Windows drew black glyphs. The engine now preserves transparent black through inherited and `currentColor` text. The S062 Windows override had 23 black text pixels in a transparent input; after inspection it was removed in favor of the unchanged, already checked-in common baseline, with no threshold relaxation. Native regressions check the alpha and paint commands; the font goldens exercise actual Windows glyph drawing with zero alpha.

The corrected [Windows run 43718257971](https://github.com/Psycho-Core/PsychoVault/actions/runs/43718257971) passed on commit `2d6d500`: MSVC builds, native tests, 18 owned and 24 hand-specified goldens and their self-tests, existing contract suites, native smoke and packaging. A temporary branch-only push trigger started the run and was removed afterward; manual-dispatch permission was not demonstrated.

This table is a **scenario index**, not evidence that the full S052–S069 feature set is covered. These synthetic tests supplement, but do not replace, independent verification of S050 reference-derived fixtures. The Phase 2 gate remains open until the broader implementation, feature mapping and reference correctness have been resolved and demonstrated on Windows.
