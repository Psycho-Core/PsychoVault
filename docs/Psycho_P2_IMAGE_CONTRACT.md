# P2 image-contract follow-up

This is a targeted S063 improvement and verification, not Phase 2 completion. The reference-derived S050 `images.html` fixture uses a bordered 64×48 image with `object-fit: contain` and an external SVG. Previously the engine stretched the raster and did not paint the image box's background/border; a block-displayed image in normal flow could render as an empty block. The headless CLI also supplied an SVG without a MIME type, and the declaration-free `mark.svg` was not detected as SVG: the old S050 check counted an image **command**, even when it painted a missing-image placeholder.

## Implemented

- For the existing in-flow image path, `fill`, `contain`, `cover`, `none`, and `scale-down` use intrinsic image dimensions, centered at the initial `object-position: 50% 50%`, with a content-box clip. The CSS initial value is `fill`; `inherit`, `initial` and `unset` work through the existing cascade. Inline style mutation recomputes the fit.
- In-flow images paint their solid background and individual border-width sides, and use the image path when declared `display:block`. External SVG resources beginning directly with an `<svg>` root (with optional whitespace or UTF-8 BOM) are decoded without requiring an XML declaration or MIME type.
- These changes use existing image decoding and rasterization. `srcset`/`sizes`, `object-position`, full inline/replaced-element flow, block margin collapsing/auto-centering and images as flex/grid/positioned items remain open. The in-flow border painting is a rectangular, single-color subset: rounded/differently styled borders and full image effects are not implemented. Image interpolation uses the existing nearest-neighbor rasterizer. Inline SVG, advanced SVG styling/paths, favicon lifecycle and broad SVG conformance remain separate gaps.

## Independent contract checks

`tests/Psycho_p2_contract_tests.py` contains explicit hand-derived expectations, not renderer-generated baselines:

1. Every pixel of a 120×240 image-fit fixture is compared with geometry and colors derived from the 2×2 source pixels and CSS box/fit values. It covers all five modes, borders, background, centering and cover clipping.
2. A 1×1 content-box case checks `scale-down` shrinks and `none` clips an oversized source.
3. A live-click case checks explicit inheritance, non-inheritance via `unset`/`initial`, invalid-value fallback, and style mutation from contain to cover.
4. The original S050 `images.html` is rendered with its real raster and declaration-free SVG. The test independently checks fit geometry and content-box centering, and probes the SVG's green circle and pale fill to distinguish actual decode from a placeholder. Text placement is not asserted; SVG probes are relative to the image command because text metrics differ between platforms.

These checks cover only the named image scenarios. They do not independently verify every behavior represented by the original five S050 fixtures, nor do they establish broad S052–S069 feature-set coverage or visual parity.

## Reproduce

```sh
python3 tools/Psycho_build.py --compiler gcc --target engine --outdir /tmp/pv-phase2-fit
python3 tools/Psycho_build.py --compiler gcc --target tests --outdir /tmp/pv-phase2-tests
/tmp/pv-phase2-tests/pv-tests
python3 tests/Psycho_p2_contract_tests.py --engine /tmp/pv-phase2-fit/pv-engine
python3 tests/Psycho_golden_tests.py
python3 tests/Psycho_reference_diff_tests.py
python3 tools/Psycho_golden.py --engine /tmp/pv-phase2-fit/pv-engine --outdir /tmp/pv-phase2-goldens
python3 tools/Psycho_s050_corpus_audit.py --engine /tmp/pv-phase2-fit/pv-engine --outdir /tmp/pv-phase2-corpus
```

Linux GCC 12.2 / Python 3.11 results: `pv-tests` **1,059 engine / 2,906 total**, new contract tests **4/4**, golden self-tests **6/6**, reference-diff self-tests **8/8**, owned goldens **18/18**, S050 historical smoke **5/5**. The smoke report's static historical metadata and blanket deferral rows are not live evidence of semantic status; see [S072 audit](S072_PHASE2_AUDIT.md). Windows automation is required separately; Linux does not validate DirectWrite glyph appearance or interactive Windows behavior.
