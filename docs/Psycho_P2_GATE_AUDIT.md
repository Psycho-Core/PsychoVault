# P2 exit-gate audit

Written 2026-11-25 against the master plan's P2 exit gate. It replaces the "gate not yet demonstrated" verdict in `S072_PHASE2_AUDIT.md`, which stays as history.

## The gate, as I am reading it

1. The engine golden-image suite covers the S052-S069 feature set and is green on the supported Windows build.
2. Representative fixtures derived from the reference UI's CSS/DOM contract render correctly.

The plan also says what the gate is not: full CSS conformance, the real settings and new-tab HTML, or reference screenshot parity. Those are P3/M2.

**My scoping call, which you should review:** "the feature set" means each step's behavior as my reference UI and the documentation tier need it, not every property in each specification. Anything outside that is listed below with the phase that owns it. I made this call; the plan did not make it for me.

## Evidence per step

"Native" below means inline checks in `tests/Psycho_test_main.cpp` and `runtime/tests/`, found by searching for the area (animation, canvas, selection, composition, damage, tile, text_width); they are not individually named test functions. Three kinds of check, kept apart: an **owned golden** records the engine's own output (a regression guard, not proof of correctness); an **independent golden** compares against rectangles I worked out from the CSS rules before looking at engine output; a **contract test** asserts boxes or pixels worked out from the rules. Windows evidence is the run named at the end.

| Step | Owned golden | Independent goldens | Contract tests |
|---|---|---|---|
| S052 values | s052 | values | native `UiCustomPropertiesCascadeAndMutation`, `UiCssMathFunctionsAndMixedUnits`; layout `min/max/clamp` probe |
| S053 cascade | s053 | cascade, events_hover | native `UiPseudoClassInteractionMatrix` |
| S054 box model | s054 | box, collapse, aligncenter | layout: margin collapsing (first child, last child, empty block, negative, blockers), inline-block text-align, overflow forms |
| S055 flex | s055 | flex | layout: shrink; probes for wrap, order, grow, alignment, nesting (all matched CSS) |
| S056 grid | s056 | grid, gridauto, gridlines | layout: repeat, implicit rows, span, negative lines |
| S057 positioning | s057 | position | DOM/layout contract: sticky, fixed, absolute |
| S058 paint | s058 | effects | image contract, controls contract |
| S059 scrolling | s059 | scroll_initial, scroll_wheel | layout: every overflow form scrolls and clips |
| S060 events | s060 | events_* | DOM/layout contract, focus contract |
| S061 forms | s061 | control_* | controls contract: default sizes, checked and radio marks, range value and click, select chevron |
| S062 editing | s062 | edit_* | native selection, editing and composition tests |
| S063 images | s063 | image | image and responsive-image contracts |
| S064 viewport | s064 | media_1x, media_2x | native media-query tests |
| S065 animation | s065 | motion_reduced | native animation tests |
| S066 repaint | s066 | events_hover, events_idle | native damage tests |
| S067 compositing | s067 | effects, events_idle | native tile and layer metadata tests |
| S068 canvas | s068 | canvas | native canvas tests |
| S069 fonts | s069 | font_1x, font_2x | native font tests |
| S070 golden suite | the 18 above | the 28 above | `Psycho_golden_tests.py`, `Psycho_independent_golden_tests.py` runner self-tests (6 each) |
| S071 visual diff | n/a | n/a | `Psycho_reference_diff_tests.py` (8 self-tests, mocked renderer); the CLI has not compared a real engine render with a real `legacy-ui` capture, because no matching P3 view exists yet |

## What this session fixed because the probes showed it was wrong

Each was found by rendering a small page and comparing it with what the CSS rules say, then fixed and given a test:

- `overflow-x`, `overflow-y` and the two-value `overflow` shorthand were ignored, so `overflow-y:auto` panels neither clipped nor scrolled.
- Margins did not collapse between a block and its first or last child, or through an empty block.
- `grid-auto-rows` and `grid-auto-columns` were unsupported (implicit rows had height 0), and negative grid lines (`1 / -1`) were wrong.
- Default checkboxes were 80x24 empty boxes, and range and select showed nothing of their state; a click on a range did nothing.
- `text-align` did not move inline-blocks.

## Not done, by step, and where it goes

| Step | Not implemented | Owner |
|---|---|---|
| S053 | cascade layers, full selector grammar (`:nth-child(... of ...)`, `:is`, `:where`), user-origin sheets | P5 S120-S121 |
| S054 | `min-content`/`max-content` sizing keywords, table layout (a table is 100% wide and its cells do not share a row height) | P5 S120 |
| S055 | baseline alignment, automatic minimum size from content | P5 |
| S056 | `auto-fit`/`auto-fill`, named lines, grid intrinsic sizing beyond the simple cases | P5 |
| S057 | sticky inside nested scrollports, a transformed box as the containing block for fixed content | P5 |
| S058 | per-corner radii, `filter: blur()`, blurred, multiple and accurate inset shadows | P3 where the reference UI needs them (the Direct2D painter must change too) |
| S059 | horizontal scrolling and scrollbars, scroll anchoring, smooth scrolling | P3/P5 |
| S060 | pointer capture and cancel, touch | P5 S122 |
| S061 | select popup, file chooser, full constraint validation; controls still lay out one per line in the inline path | P3 S084 (dialogs and pickers) |
| S062 | word and line selection gestures, physical IME candidate placement | P3/P7 |
| S063 | broad SVG coverage, `picture`/`currentSrc`, the favicon lifecycle | P5 S132, P7 |
| S064 | physical monitor DPI changes | P11 |
| S065 | transitions and keyframes beyond opacity, colour, size and rotation; animation events | P5 |
| S066 | dirty-subtree layout | P11 S222 |
| S067 | retained layer surfaces, real present and drop telemetry | P11 S222; see also D009 |
| S068 | `Path2D`, `arcTo`, true stroke text, join and cap styles | P5 |
| S069 | italic, `@font-face`, per-character family lists | P5 |

## What this audit does not establish

- **Visual parity.** No settings or new-tab page exists yet; the S071 diff harness has only run against fixtures.
- **Performance, beyond one finding.** First navigation takes about 2-3 s on a cold Windows runner. It is not a regression in the renderer process: the original P1 run measured 35 ms, run 42340417295 (the commit that moved text to DirectWrite, S051) measured 3.1 s, and the second renderer in every run takes about 0.1 s. The cost is DirectWrite start-up on first text layout, almost certainly the system font enumeration. I made the system collection lazy (a page whose first family is the bundled UI font no longer touches it) and the first navigation barely moved (2.1 s to 1.9 s in run 45915799347), so the system font enumeration is not the main cost. My timing line in the Windows test ran after other tests had already used DirectWrite and read about 0.1 ms, so it measured nothing. Still open: the bundled font load, the Direct2D factory or something else on first text layout; it needs a timer inside the renderer child.
- **Real text.** The goldens are deliberately text-free so they compare across platforms; glyph shaping, fallback and wrapping are checked only by the native tests and the platform golden.
- **Independence of the owned goldens.** They lock in current output. The independent goldens and contract tests are the ones that can disagree with the engine, and they cover a representative slice, not every combination.

## Verdict

Under the scoping call above, both halves of the gate are met: each of S052-S069 has an owned golden, at least one independent golden or worked-out contract, and a green Windows run, and the five reference-derived fixtures have contract tests. The exclusions are real gaps, they are named, and each has an owner. If you read "the S052-S069 feature set" more broadly than I did, P2 is not done and the table above is the remaining work.
