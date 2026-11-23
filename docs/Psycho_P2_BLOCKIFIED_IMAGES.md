# P2 image layout: direct flex, grid and positioned children

S063 previously painted an `img` only when its parent sent it through the in-flow image branch. Direct flex items, grid items and positioned images instead enter `block()` and had a box but no image paint. This increment gives those images an intrinsic content size and emits an image command inside the normal box, effect and clipping machinery. The common painter handles `object-fit` and centered clipping in both paths; source selection and density still come from the existing responsive-image selection.

The independent contract `Psycho_p2_contract_tests.py` checks all pixels of a 2×2 checker painted in direct flex, grid and absolute children, including contain letterboxing, and separately checks intrinsic aspect ratio, width and height attributes, and a 2x density candidate. These are small, hand-specified examples, not browser-captured evidence of full replaced-element conformance. Existing image, responsive image, DOM/layout and golden suites also remain relevant.

## Scope and remaining work

This is not the S063 or Phase 2 exit gate. Complex replaced sizing, min/max constraint interactions, flex shrink/align measurement, opposing positioned offsets, object-position, inline baseline/line flow, loading/error and favicon lifecycle and broader SVG behavior still need independent investigation and coverage. The layout engine's existing transform, border, stacking and clipping limitations also apply. Phase 2 still has substantial work beyond images; see [the remaining-work audit](Psycho_PHASE2_REMAINING_AUDIT.md) and newer follow-up documents.

## Validation

Linux: `pv-tests` passed 1,182 engine / 3,029 total checks; image contract 10/10, DOM/layout 7/7, responsive image 1/1 and focus 1/1 passed. All 18 owned and 24 independent golden cases and their test harnesses passed.

Windows: [run 45240678384](https://github.com/Psycho-Core/PsychoVault/actions/runs/45240678384) passed the build, native tests, contracts, goldens, smoke checks and packaging on commit `2adf0ab`. The temporary branch-only push trigger was subsequently removed; this run does not establish manual-dispatch permission. The independent image expectations and existing suites are not a substitute for the full S052–S069 matrix or the Phase 2 gate.
