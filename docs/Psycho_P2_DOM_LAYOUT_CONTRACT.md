# P2 DOM and layout contract follow-up

This is a targeted P2 improvement, **not Phase 2 completion**. S050 diagnostics exposed that starting a paragraph used to close a containing `div`, so the scroll fixture's paragraphs were outside the scroller. Explicit root attributes were discarded, the body default margin was missing, and auto-width right/bottom-positioned boxes filled the containing block instead of anchoring their actual laid-out size.

## Scope

- In document parsing, block/paragraph start tags and paragraph end tags close an open paragraph only within button scope. Explicit `html`, `head` and `body` attributes are retained. Fragment parsing and comprehensive HTML5 tree construction remain outside this change.
- The UA body margin defaults to 8px and can be overridden by author CSS.
- Out-of-flow auto-width elements use the existing intrinsic measurement, bounded by available width. Opposing offsets stretch auto dimensions; right/bottom-only offsets use the actual laid-out box and margins for anchoring. This is a **subset/estimate** of CSS positioned layout, not full shrink-to-fit or constraint resolution. Text measurement may wrap a badge that Chrome keeps on one line, and other layout/text metric divergences remain.
- `pv-engine --inspect` (added earlier on this branch) reports first-match node attributes and layout geometry, not necessarily live control state.

## Independent contracts and limits

`tests/Psycho_p2_dom_layout_contract_tests.py` checks the original S050 fixtures: fixed horizontal flex/grid track geometry independently measured in Windows Chrome 157 ([run 43703135149](https://github.com/Psycho-Core/PsychoVault/actions/runs/43703135149)); external script mutation, body/target widths and horizontal positions (Chrome [run 43703238336](https://github.com/Psycho-Core/PsychoVault/actions/runs/43703238336)); and scoped scrolling and right/bottom anchoring derived from fixture CSS. These are selective checks, not complete visual or semantic equivalence. Text, forms, clipping, sticky behavior, small viewport sizing and many other features still need independent validation. Windows Chrome y coordinates and text metrics are not asserted cross-platform. Separate native C++ tests cover scoped paragraph closure, root attributes, default/overridden body margin, and some positioning constraints.

## Linux validation

GCC: 1,083 engine / 2,930 total checks; DOM/layout contracts 3/3; image contracts 4/4; golden self-tests 6/6; reference-diff self-tests 8/8; owned goldens 18/18; historical S050 marker smokes 5/5. Owned goldens are regression checks, not independent proof of correctness or complete S052–S069 coverage. Linux lacks the Windows text raster backend.

Windows [run 43704602323](https://github.com/Psycho-Core/PsychoVault/actions/runs/43704602323) passed on commit `a55dcd2`: MSVC builds, native tests, owned goldens and their self-tests, image and DOM/layout contracts, automated native smoke and packaging. A temporary push trigger scoped to this branch started the run and was subsequently removed; this does not establish manual-dispatch permission.

The P2 exit gate remains open: broad feature-set golden coverage and independently verified representative contracts are incomplete; physical Windows interaction, IME, multi-monitor DPI and performance are not demonstrated by the headless tests. S071's comparison harness does not imply screenshot parity.
