# `ui/` - browser-owned pages

These are the pages my engine renders for the browser's own interface (P3). They are plain HTML, CSS and JavaScript, embedded into the one executable by `tools/Psycho_build.py` and served by the shell at `psychovault://newtab/` and `psychovault://settings/`.

| File | What it is |
| --- | --- |
| `tokens.css` | design tokens (colours, radii, type), measured from the legacy captures |
| `components.css` | cards, switches, buttons, fields, setting rows |
| `index.html`, `app.js` | the new-tab page; address entry goes through the curated `psycho.tab.new` bridge only |
| `settings.html` | **generated** by `tools/Psycho_gen_settings.py` from `docs/reference/legacy-inventory.json`; do not edit by hand |
| `settings.js` | category navigation only |
| `fixtures/local-page.html` | a local web page for the S045 flow |

## What is true and what is not

- The new-tab page and the settings markup were laid out against `docs/reference/legacy-ui/03-new-tab-page.png` and `04-settings-general.png`. Geometry (the search box, tiles, cards, sidebar items) matches within a few pixels in a headless render; I could not compare text on Linux.
- The settings page carries all 109 inventory controls with their ids, kinds, types, option lists and defaults (`tests/Psycho_ui_settings_tests.py` checks this). **None of them is wired**: no control reads or writes the profile yet.
- Card grouping and descriptions are only exact where a capture showed them (General, the top of Privacy and Appearance). Everything below a captured viewport is my reconstruction from the control labels, and the inventory repeats a neighbour's label for several controls, corrected in `COPY` in the generator. Full-page captures of every category would let me replace the guesses.
- The six data-clear buttons and the 21 `window.psycho` namespaces are not in this page yet.

## Checks

```sh
python3 tools/Psycho_gen_settings.py --check    # the generated file is current
python3 tests/Psycho_ui_settings_tests.py        # every inventory control is present
python3 tools/Psycho_s045_ui_smoke.py --engine build/pv-engine   # headless render of the new tab
python3 tools/Psycho_reference_diff.py --reference 03-new-tab-page.png --input ui/index.html \
  --url psychovault://newtab/ --resource psychovault://newtab/tokens.css=ui/tokens.css \
  --resource psychovault://newtab/components.css=ui/components.css --resource psychovault://newtab/app.js=ui/app.js \
  --engine build/pv-engine --outdir /tmp/newtab-diff
```
