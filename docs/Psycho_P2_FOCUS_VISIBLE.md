# P2 focus-visible increment

This is an S053/S061 interaction subset, not full focus, keyboard or Phase 2 completion. Previously `:focus-visible` matched every focused element exactly like `:focus`, including a pointer-focused button.

The selector state now tracks whether a focus indicator should be visible. Pointer focus of non-text controls does not match `:focus-visible`; keyboard focus does. Editable text controls match even when pointer-focused. A non-modifier keyboard action on an already focused element enables its indicator, including through the host's scroll-key route. Programmatic focus transfers the prior focus-visible state; without a prior focus it uses the last recorded input modality. The ordinary `:focus` and `:focus-within` states remain independent. A new document resets the state.

This is a practical heuristic, not a complete browser focus algorithm: platform/user accessibility preferences, all input types, shadow DOM, pointer types other than the existing mouse path, reentrant focus events, full focusability/tab order and reverse tab traversal are not covered. Stateful selector matching in DOM query APIs remains a separate limitation. No claim is made about physical assistive technology, accessibility settings or interactive Win32 validation.

`tests/Psycho_p2_focus_contract_tests.py` checks CSS-derived pixels in eight initial/pointer/keyboard/text-edit/programmatic-focus scenarios; these expected colors are not captured engine baselines. Native tests cover selector-state distinction and the host scroll-key path. Linux GCC: **1,105 engine / 2,952 total checks**, focus contracts 1/1 (eight scenarios), responsive image contract 1/1, DOM/layout 3/3, image 4/4, golden self-tests 6/6, reference-diff self-tests 8/8 and owned goldens 18/18. Linux does not rasterize Windows text.

Windows [run 43710107379](https://github.com/Psycho-Core/PsychoVault/actions/runs/43710107379) passed on commit `83f78c6`: MSVC build, engine tests, goldens and self-tests, all four contract suites, automated native smoke and packaging. A temporary branch-only push trigger started the run and was removed afterward; this does not establish manual-dispatch permission.

The Phase 2 exit gate remains open: the owned golden suite is still a narrow sample of S052–S069, and representative reference-contract rendering has not been comprehensively validated.
