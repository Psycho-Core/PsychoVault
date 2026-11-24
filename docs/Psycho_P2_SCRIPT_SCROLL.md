# P2 scrolling: scripted vertical scroll subset

This increment adds element `scrollTop`, `clientHeight`, and `scrollHeight` to the existing vertical nested-scroll layout. Script assignments clamp to the measured range, retain fractional offsets, normalize non-finite input to zero, and trigger a new layout. `overflow:hidden` is script-scrollable without drawing a scrollbar or consuming wheel/keyboard input; `overflow:clip` is not script-scrollable. Positions are clamped again if content shrinks. Nested scroll state and an active scrollbar drag are reset when a new document is loaded, whose parser may reuse node IDs.

Direct positioned descendants are painted after in-flow children, so they now move with the local scroll offset too. Commands and boxes marked viewport-fixed are not translated by that offset. The normal existing clip and stacking limitations still apply to fixed content.

## Coverage

`Psycho_script_scroll.html` and the DOM/layout contract assert hand-specified metrics, fractional scrolling, clamping, script behavior for hidden versus clip, shrinking content, and geometry for normal, positioned and fixed boxes. The paint contract checks representative clipped pixels independently. A native regression checks that a wheel over hidden overflow reaches the enclosing scrollport and that navigation does not reuse its old local offset.

## Validation

On Linux, the native suite passed 1,185 engine / 3,032 total checks; the DOM/layout contracts passed 8/8, paint/image contracts 11/11, responsive-image and focus contracts 1/1 each. All 18 owned and 24 independent golden cases and their harness self-tests passed.

[Windows run 45431206060](https://github.com/Psycho-Core/PsychoVault/actions/runs/45431206060) passed build, native tests, contracts, goldens, smoke checks and packaging on commit `d2ad8c7`. The temporary branch-only push trigger was subsequently removed; it does not demonstrate manual workflow-dispatch permission. These checks do not prove complete S059 or the Phase 2 exit gate.

## Remaining work

This is not a complete implementation of S059 or Phase 2. Horizontal scrolling, document/root CSSOM scrolling, `scrollTo`/`scrollBy`, scroll events and smooth scrolling, scroll anchoring, separate overflow axes, positioned descendants contributing to scrollable extent, and complete nested/fixed clipping and transformed geometry remain open. Non-scrollport `scrollHeight` and `clientHeight` are derived from the existing approximate box layout, not a complete CSSOM View implementation. The current engine also draws overlay-like vertical tracks instead of reserving classic scrollbar width inside nested boxes. See [the Phase 2 audit](Psycho_PHASE2_REMAINING_AUDIT.md) for the wider implementation and verification gaps.
