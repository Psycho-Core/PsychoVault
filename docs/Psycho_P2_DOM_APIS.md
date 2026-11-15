# Phase 2 reference-used DOM APIs

This increment implements the targeted reference-use subset of `dataset`,
`Element.matches()`, `Element.closest()` and `getBoundingClientRect()`; it does
not close the rest of the Phase 2 gate.

## Behavior

- Each element has a stable, live `DOMStringMap`. Reads, writes and deletion
  reflect `data-*` attributes in both directions, with ASCII dash/camel-case
  conversion and stringification of assigned values. Changes invalidate style
  and layout. Missing properties are `undefined`; invalid dash-lowercase setter
  names raise a syntax error. The targeted host hooks also support `in`, direct
  `hasOwnProperty`, `Object.keys`/`values`/`entries`/`assign`, `for...in` and
  `Reflect.deleteProperty` for this map.
- `matches` evaluates the element itself; `closest` checks the element and then
  its ancestors, including disconnected trees, and returns `null` on no match.
  They use the existing selector engine with current hover/active/focus state
  and a `:scope` element; missing arguments and syntax rejected by its parser
  produce script errors.
- `getBoundingClientRect` synchronously refreshes dirty layout, including when
  called during the initial script or immediately after a DOM/style mutation.
  It returns a snapshot with `x`, `y`, `left`, `top`, `right`, `bottom`, `width`
  and `height` in viewport CSS pixels. Document scrolling is subtracted from
  ordinary boxes, fixed boxes remain viewport-relative, and nested scroll offsets
  follow the layout. Hidden or disconnected nodes with no boxes return zeros.
  For inline elements without an element box, it unions existing descendant
  boxes. Text-node box unions now include multiple wrapped lines.

## Tests

`tests/Psycho_test_main.cpp` covers live reflection, selector matching and
synchronous geometry, including detached nodes. The independent, hand-specified
`tests/Psycho_p2_dom_layout_contract_tests.py` contracts exercise the four APIs,
attribute mutation/deletion/enumeration, matching and ancestors, focus/checked
state, invalid syntax, layout before first paint and after mutations, hidden and
detached boxes, multiline inline text, document and nested scroll, and fixed
positioning. Their expected values come from the fixture CSS and specified
interaction sequence rather than captured engine output. The existing Windows
workflow runs this contract file.

Linux GCC validation: **1,130 engine / 2,977 total native checks**, DOM/layout
contracts **7/7**, image contracts **4/4**, responsive-image and focus contracts
**1/1** each, hand-specified goldens **24/24**, owned goldens **18/18**, golden
self-tests **6/6** each, and reference-diff self-tests **8/8** passed.

Windows x64 [run 43724690310](https://github.com/Psycho-Core/PsychoVault/actions/runs/43724690310)
passed the MSVC build, native tests, both golden suites, all contract stages,
native smokes and packaging on commit `d936fd4`. Its temporary branch-only
push trigger was removed afterward; this is not proof of manual-dispatch
permission or complete interactive/reference parity.

## Limits

This builds on the engine's bounded selector parser and existing layout, not a
complete DOM/Selectors/CSSOM implementation. Valid selectors unsupported by the
parser can be rejected; unsupported pseudo-classes, advanced escaping,
namespaces and full syntax-error classification are not covered. Stateful
selector APIs other than these two retain their existing behavior. Geometry
inherits existing line-fragment, transform, zoom, containing-block and clipping
limitations; the inline union is an approximation, not full CSSOM View. The
returned plain DOMRect-shaped snapshot does not implement the DOMRect prototype,
mutable edge accessors or `toJSON`. Dataset property descriptors, prototype-chain
semantics, all exotic names and JavaScript reflection operations outside those
listed above are not claimed. Existing resource/attribute/node limits still apply.

These targeted tests do not establish complete reference fixture correctness,
full S052–S069 feature coverage, interactive Windows behavior or Phase 2
completion.
