# P2 responsive image increment

This targets part of S063, not all image support or Phase 2 completion.

## Implemented subset

`img` now selects ordinary URL candidates with `x` density or positive integer `w` width descriptors. For density candidates, a nonempty `src` supplies the implicit 1x choice if the list has no 1x candidate. For width candidates, the first matching `sizes` entry supplies a source size, or 100vw is used if none applies. CSS lengths and the engine's existing media-query subset are used for `(condition) length` entries. The smallest candidate at or above DPR is chosen, falling back to the largest below it. Invalid descriptors and duplicate densities are ignored; mixed descriptor types are not fully implemented.

The same selection feeds eager/lazy requests, image layout/paint and the existing canvas image argument path. Decoded intrinsic dimensions are divided by the candidate density; explicitly sized images retain their CSS dimensions. A viewport/DPR change invalidates layout and can request a new candidate. Script changes to `src`, `srcset`/`srcSet`, `sizes`, or the corresponding attributes are observed on the next render. Empty image sources no longer cause a request for the current document.

This is not the HTML responsive-image algorithm in full: `picture`/`source`, `currentSrc`, `sizes=auto`, complex media conditions in `sizes`, all candidate error recovery, full comma/whitespace URL tokenization (including data URLs), image loading events and current/pending request lifecycle are not implemented. A `srcset` containing a data URL or longer than 64 KiB falls back to `src`; oversized `sizes` falls back to 100vw. At most 256 valid candidates are considered. Selection does not implement browser network/cache/data-saving heuristics. Existing limitations of image decoding and replaced-element layout remain. See [image-contract follow-up](Psycho_P2_IMAGE_CONTRACT.md).

## Validation

`tests/Psycho_p2_responsive_image_tests.py` independently specifies candidate URLs and output colors for five viewport/DPR combinations: 1x fallback, 2x/3x density candidates, media-conditioned source sizes and width descriptors. It checks both paint commands and actual raster pixels with three local, solid-color images, not engine-generated image baselines. The C++ tests cover candidate boundaries, invalid candidates, CSS math sizes, decoded intrinsic dimensions, script mutation, DPR changes, lazy requests and empty source handling. This does not validate actual network scheduling or interactive image load events.

Linux GCC 12.2: **1,100 engine / 2,947 total checks**; responsive image contract 1/1 (five scenarios), DOM/layout contracts 3/3, existing image contracts 4/4, golden self-tests 6/6, reference-diff self-tests 8/8, owned goldens 18/18 and S050 marker smokes 5/5. The self-tests and marker smokes ran on the immediately preceding unbounded-parser version; the bounded version was rebuilt and reran the full native suite, contracts and goldens. The marker smokes and owned goldens are not independent conformance evidence.

Windows [run 43708314012](https://github.com/Psycho-Core/PsychoVault/actions/runs/43708314012) passed on commit `a18bf71`: MSVC build, engine tests, the goldens and self-tests, all three contract suites, automated native smoke and packaging. A temporary branch-only push trigger started this run and was removed afterward; this does not demonstrate manual-dispatch permission.

The P2 exit gate remains open. The golden suite is still a narrow representative sample of S052–S069, and complete representative reference-contract behavior remains unproved. Real interactive Windows IME, multi-monitor DPI and compositor performance are not established by headless tests.
