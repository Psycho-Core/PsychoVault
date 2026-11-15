# Nested sticky and wheel-event follow-up

This is a targeted S057/S059/S060 increment, not completion of the requested
layout, painting, scrolling or event work.

Vertical sticky constraints now use the nearest ancestor with `overflow:auto`,
`scroll`, or `hidden` as the scrollport, falling back to the viewport. Sticky
movement is limited to its own paint range and boxes, instead of shifting later
sibling paint. Direct blockified sticky children paint after normal in-flow
siblings, inside the parent clip. Measurement-only layout preserves its caller's
scroll, fixed-box and paint-range metadata. Tests cover a scrollport offset from
the viewport, a visible overlap, a later sibling outside the scroller, and the
existing viewport sticky behavior.

Wheel input dispatches a bubbling, cancelable pixel-mode `wheel` event with
coordinates and delta before default scrolling. `preventDefault()` suppresses
both nested scrolling and host document fallback. Explicit `{passive:true}`
listeners cannot cancel defaults. The target is re-hit-tested after an event
mutates layout. Native tests exercise the event, passive and active listeners,
and the host fallback; a hand-specified pixel test covers canceled scrolling
and the sticky overlap.

Linux validation: 1,161 engine and 3,008 total native checks; six image/paint
contracts, seven DOM/layout contracts, focus and responsive-image contracts,
18 owned goldens and 24 independent goldens passed.

Limitations: sticky placement and stacking are still simplified; horizontal
insets, full containing-block constraints, non-blockified sticky content,
transforms and z-index/stacking-context interactions are not covered as browser
conformance. Hit testing still uses simplified axis-aligned area selection. Wheel
handling only carries vertical deltas, has no default-passive policy, and does
not implement complete listener options or full cross-container residual-delta
semantics. Horizontal scrolling, anchoring, smooth-scroll policy, pointer
capture/cancel, broader keyboard/default actions, and the remaining box, flex,
grid, clipping, effects and transformed-hit-testing gaps are not implemented by
this increment. Existing goldens do not prove full browser visual parity.
