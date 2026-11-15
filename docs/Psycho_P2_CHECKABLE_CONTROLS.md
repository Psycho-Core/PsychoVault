# Phase 2 blockified checkable controls

Checkbox and radio inputs that enter the block layout path (including direct
flex and grid items and positioned controls) previously painted only their CSS
box, regardless of checked state. They now have a 14px intrinsic content size
for automatic flex/grid measurement and a geometric indicator in the content
box when checked. The indicator uses the computed text color, follows the
control's dimensions and CSS box painting, and is suppressed by
`appearance: none`. The existing DOM checked state and activation remain the
source of truth. A checkbox uses a square indicator; a radio uses a round one.

Native tests inspect paint commands, authored dimensions and color, click toggles,
disabled controls, flex, grid, positioned inputs and `appearance: none`. The
portable rendering contract checks actual pixels for checked, unchecked and
disabled flex items before and after clicks. It is a hand-specified regression
for this implementation, not a browser screenshot or native-theme comparison.

Linux GCC validation: 1,143 engine / 2,990 total checks; image and checkable
contracts 5/5, DOM/layout contracts 7/7, focus and responsive-image contracts
1/1 each, independent goldens 24/24, owned goldens 18/18 and both golden
harness self-tests 6/6. Windows x64
[run 43728671267](https://github.com/Psycho-Core/PsychoVault/actions/runs/43728671267)
passed the build, native tests, rendering contracts and goldens, smoke tests and
packaging on commit `a612671`. The branch-only push trigger used for that run
was removed in the next commit; this does not demonstrate manual-dispatch
permission or physical input-device testing.

This is a targeted repair, not full checkable-control conformance. Inline
controls still use the older text-based painter; sizing and appearance are not
native-browser parity, including disabled styling, accent-color, indeterminate
and all CSS appearance behavior. Other forms and editing gaps, including select
popup, range and file controls, validation, word/line gestures and physical OS
IME/clipboard integration, remain open.
