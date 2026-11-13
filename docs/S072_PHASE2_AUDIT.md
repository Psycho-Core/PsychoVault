# S072 — Phase 2 audit and verification

Audit date: 2026-11-01. Audited source: `246ab98974982f8e6137c8d618cb3b3170ffb54d` (S070 merge). S072 is an audit label, not a previously planned implementation step.

## Verdict

**Phase 2 is not complete; the exit gate is not demonstrated.** S070 supplies a useful, green *representative regression suite*, but one 160×100 owned image per S052–S069 step is not evidence that each step's feature set is covered. The five S050 contract-derived fixtures reach the paint path and contain expected frame markers; that is not an independent determination that they render correctly. S071, a separately listed visual-diff harness, has not been implemented. Full settings/new-tab pages and screenshot parity are P3/M2 deliverables, not prerequisites to the P2 exit gate.

The audit is not a feature-by-feature standards conformance review. “Targeted” below means the recorded implementation and tests cover a subset, not that the entire planned step is finished. The detailed limitations and named test anchors are in [Psycho_ENGINE-GAPS.md](Psycho_ENGINE-GAPS.md) and the S066–S070 implementation notes. Historical per-step results describe what was known at that time; they do not override later results or establish interactive behavior.

## Fresh Linux verification

On the audited tree, Debian GCC 12.2.0 and Python 3.11.2, all commands below exited zero:

```sh
python3 tools/Psycho_build.py --compiler gcc --target engine --outdir /tmp/pv-s072-audit
python3 tools/Psycho_build.py --compiler gcc --target tests --outdir /tmp/pv-s072-audit
/tmp/pv-s072-audit/pv-tests
python3 tests/Psycho_golden_tests.py
python3 tools/Psycho_golden.py --engine /tmp/pv-s072-audit/pv-engine --outdir /tmp/pv-s072-goldens
python3 tools/Psycho_s050_corpus_audit.py --engine /tmp/pv-s072-audit/pv-engine --outdir /tmp/pv-s072-corpus
```

- `pv-tests`: **1,059 engine checks / 2,906 total**. The two builds emitted GCC variable-tracking notes, not errors.
- Golden runner self-tests: **6/6**. Owned goldens: **18/18**, zero differing pixels; probes and frame checks also passed. These are checked-in engine-generated baselines, not independent browser or reference screenshots. See [golden README](../tests/golden/README.md) for provenance, platform selection and limitations.
- S050 corpus: **5/5 render/marker smoke checks**. The script's JSON `date` is a hard-coded historical value (2026-11-07), not this run's execution date, and its blanket reviewed-deferral rows are a historical S050 snapshot, not a live inventory of subsequently implemented tests. Use the dated follow-ups in the gap audit for status. No correctness claim follows from `result: passed`.
- Output was placed under `/tmp`; the commands regenerate it. Portable Linux builds deliberately do not rasterize substitute text glyphs, so these results cannot validate DirectWrite appearance.

## Windows evidence and boundaries

[Windows run 43515852499](https://github.com/Psycho-Core/PsychoVault/actions/runs/43515852499) passed build, tests, the six golden self-tests, all 18 goldens, native smoke and packaging on commit `e923556aefaab4366f202ea6684c269036dc5344`. A direct tree comparison with the audited merge commit shows only the temporary workflow push trigger removal and changes to `docs/Psycho_ENGINE-GAPS.md` and `docs/S070_IMPLEMENTATION.md`; engine, test, fixture and baseline files are identical. It is therefore relevant automated evidence for those files, **not a run on the merge commit itself**. The permanent workflow is manual-only; the successful run used a temporary push trigger and does not establish permission to dispatch manually.

Windows-specific baselines for S061, S062 and S069 address actual platform raster differences; no comparison tolerance was relaxed. The run is not evidence of interactive IME/clipboard behavior, physical multi-monitor DPI transitions, real DWM frame timing or performance. The earlier per-step “no Windows validation” entries in the gap audit are historical; this later automated Windows run validates the combined build and suite, but does not close their interactive/performance caveats.

## Step-by-step assessment

For S052–S069, the fresh Linux run and the cited Windows run each pass the respective owned S070 image case and associated probes; `pv-tests` adds semantic regressions. Neither an individual case nor a passing shared suite establishes every listed feature or edge case. The examples below are blockers/limits, not a complete backlog.

| Step | Evidence / current state | Important remaining scope or verification |
|---|---|---|
| S050 | Reference contract and callsite/engine matrix, five authored corpus fixtures, 5/5 smoke | Independently check fixture geometry, paint and interactions against explicit expected behavior; smoke alone is not correctness. Reconcile individual named semantic deferrals using the later gap-audit entries, not the script's blanket snapshot. |
| S051 | DirectWrite shaping/fallback implementation; earlier [Windows run 42340417295](https://github.com/Psycho-Core/PsychoVault/actions/runs/42340417295) and later combined run | Full Unicode line breaking/hyphenation and independent text rendering validation remain limited; Linux has no glyph raster. |
| S052 | Targeted values/custom-properties implementation and tests; golden | Percentage bases, dimensional validation and computed-value edge cases remain partial. |
| S053 | Targeted cascade/selectors and tests; golden | Full origin/cascade/selector semantics and visual correctness are not established. |
| S054 | Targeted box model and tests; golden | Full margin collapse, intrinsic sizing, percentage cases and overflow remain partial. |
| S055 | Targeted flex and tests; golden | Intrinsic sizing, exact baselines, percentage/wrap combinations and absolute-child positioning remain partial. |
| S056 | Targeted grid and tests; golden | Auto-repeat, advanced intrinsic sizing and full placement/sizing behavior remain open. |
| S057 | Targeted positioning/stacking and tests; golden | Nested-scrollport sticky, transformed containing blocks and complete stacking/clipping remain partial. |
| S058 | Targeted painting/effects and tests; golden | Complete layer, effect, transform and hit-test semantics and visual correctness remain open. |
| S059 | Targeted vertical nested scroll, wheel, keyboard and scrollbar tests; golden | Horizontal scroll, smooth policy, true anchor preservation and full wheel/scrollbar semantics remain open. |
| S060 | Targeted pointer/keyboard/event dispatch tests; golden | Pointer capture APIs, full focus/tab/keyboard defaults and complete DOM event semantics remain open. |
| S061 | Targeted label/control state and validation tests; golden | Dedicated range/file UI, select popup, full validation and focus-visible modality remain open. |
| S062 | Targeted editor/selection/composition tests; golden | Word/line gestures, grapheme navigation, candidate placement and real Windows IME/clipboard interaction need work/validation. |
| S063 | Targeted lazy-image behavior and image golden | `srcset`/`sizes`, object-fit, SVG corpus coverage and favicon lifecycle remain incomplete. |
| S064 | Targeted viewport/media/preferences/DPR tests; golden | Advanced grammar, native scrollbar details and physical DPI behavior need further coverage. |
| S065 | Targeted animation/reduced-motion tests; golden tests reduced motion | Broader properties, event/list semantics and timing under actual presentation remain open. |
| S066 | Layout-result reuse, damage regions and clipped repaint; golden checks localized damage | Dirty-subtree layout and interactive Windows repaint/performance validation remain open. |
| S067 | Damage tiles, layer metadata, DWM pacing and approximate counters; golden checks metadata | Persistent layer surfaces, actual presentation/drop telemetry and interactive timing/performance remain open. |
| S068 | Focused Canvas 2D subset and tests; golden | Paths, clipping, text and images are subsets, not complete Canvas behavior; see [S068 notes](S068_IMPLEMENTATION.md). |
| S069 | Bundled Inter, DirectWrite private font loading and CSS-pixel tests; platform golden | Physical multi-monitor DPI transitions, broader fallback/smoothing and independent visual validation remain open. |
| S070 | Runner, 18 fixtures, probes, exact comparisons, diff outputs, self-tests, Windows workflow | Broaden to the **feature set**, with explicit feature-to-case mapping and meaningful edge/interaction coverage; owned baselines are regression evidence rather than an independent oracle. |
| S071 | Reference captures and metadata exist in `docs/reference/legacy-ui/` | No harness renders matching reference viewports and produces comparison maps plus pass/fail reports. Do not conflate S070's owned-baseline diffs with S071. |

## Gate disposition and next evidence

| Requirement | Disposition | Evidence needed to close |
|---|---|---|
| Golden suite covers S052–S069 feature set | **Not demonstrated** | Trace planned features and relevant interactions to semantic and image cases; add missing cases, including negative/edge states, with reviewed expected results. Implement or explicitly resolve required gaps against the plan. |
| Suite green on supported Windows | **Targeted suite green; full gate not demonstrated** | Run the expanded, reviewed suite on the supported Windows build and preserve commit/run identity and results. |
| Representative reference-contract fixtures render correctly | **Not demonstrated** | Define independent expected geometry/paint/DOM/interaction outcomes for representative contract fixtures; run and review assertions or independently sourced visual references, including Windows text. |
| S071 listed deliverable | **Open** | Implement reproducible viewport/capture mapping, diff maps and explicit pass/fail reporting with capture provenance and a documented comparison policy. Its existence is not the same as full P3 parity. |

Interactive Windows validation for IME, physical DPI and presentation/performance should be recorded separately with environment, actions and outcomes; automated headless cases cannot stand in for it. Keep reference screenshot provenance and licensing clear, and do not bless new engine-produced baselines as correctness without independent review.
