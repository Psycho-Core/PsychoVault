# Phase 2 text-control follow-up

The S050 `text-controls.html` fixture puts its address input directly inside a
flex row. That route previously skipped the inline-control painter: the input
box had no text content height and its value was not painted. Text-like input
items laid out through the block path now receive a font-sized intrinsic content
height and a clipped, input-owned text command. The reference-derived contract
checks that the fixture's initial address and a typed suffix reach the paint
frame; it does not claim that its complete screenshot matches a browser.

Password inputs render a bullet per decoded Unicode code point instead of their
value. The editable value remains unchanged for scripts, form submission and
editing. Caret positioning and selection widths use the displayed bullets, and
the default copy/cut action refuses to export or delete password selections;
paste and composition still operate on the real value. Native tests cover a
non-BMP character, selection, copy/cut, editing and composition, while frame
contracts check inline and flex inputs and a visible placeholder.

This is a bounded fix, not complete form-control or password conformance. It
does not implement grapheme-cluster masking, native password reveal features,
horizontal text scrolling, complete control appearance, validation, clipboard
event semantics or OS-level clipboard/IME integration. Frame inspection and
the DOM intentionally still expose the real value to the engine's trusted
owner; masking is not encryption. Other flex controls, including buttons and
checkboxes, are not fixed by this change. The Phase 2 golden and reference-render
exit gate remains open.
