# S071 — reference screenshot diff harness

The harness compares a rendered HTML viewport or an existing native capture with an inventoried screenshot in `docs/reference/legacy-ui/`. It reads the archived capture's dimensions, hash, byte count and provenance, renders at the same pixel dimensions (when `--input` is used), and emits `actual.png`, `diff.png` and `report.json`. For `--actual`, the existing input is not copied; the report records its path and hash. The reference must match the committed inventory; a different-sized image, render failure, diagnostics, invalid PNG or absent/corrupt reference fails rather than being resized or silently accepted. Non-interlaced RGB8 and RGBA8 PNGs are supported. The engine path supports URL resources, interactions, scroll, DPR and reduced-motion options; see `--help`.

Comparison is exact RGBA by default. Each changed pixel counts once if any channel differs; the diff is black for equal pixels and red with intensity equal to the largest channel delta otherwise. `--max-changed-pixels N` explicitly changes the acceptance limit; its value and the observed count are recorded in the report. A nonzero exit means a difference above the limit or an error. No crop, mask, alignment, per-channel threshold or automatic reference update is applied. Select an appropriate limit only after review; one should not turn a visibly different screenshot into a pass merely to make a job green.

## Commands

```sh
python3 tools/Psycho_build.py --target engine --outdir build
python3 tests/Psycho_reference_diff_tests.py
python3 tools/Psycho_reference_diff.py --reference 03-new-tab-page.png \
  --input ui/index.html --url https://reference.test/index.html \
  --resource https://reference.test/app.js=ui/app.js \
  --engine build/pv-engine --outdir build/reference-diff
```

The last command is **a diagnostic demonstration expected to fail**, not a parity test: `ui/index.html` is the S045 vertical slice, not the old new-tab page. On Linux the engine also does not rasterize glyphs. For a native/full-window comparison, supply a separately captured PNG via `--actual FILE` instead of `--input` and `--engine`, choosing a window screenshot, not a content-only screenshot. Do not use a reference image as the actual screenshot except to test the comparator itself.

Use the corresponding content-only screenshot for an engine page and the corresponding window screenshot for a native capture. The harness does not automate page selection, shell chrome, settings state, physical DPI, fonts, capture timing or operating-system environment. The operator must document and reproduce those factors; identical pixel dimensions alone do not prove matched state or viewport scale. The archive includes only viewport captures for longer settings pages, and its `CAPTURE.json` source commit differs from the inventory's source commit; both are recorded rather than conflated. Full UI implementation, state mapping and screenshot parity belong to P3/M2.

## Evidence and scope

On Linux, eight self-tests cover RGB filter decoding, equal and unequal images, limits and diff output, hash and dimension errors, renderer command construction and failure cleanup, safe output paths, and decoding the real archived new-tab image. An additional check decoded all 23 inventoried screenshots and verified their sizes, dimensions and hashes. The 18 existing owned golden cases still pass. A real engine invocation against the 1396×834 archived new-tab screenshot using the S045 slice failed as expected, with 1,164,264 / 1,164,264 pixels differing at the default exact limit. This is a sanity check of the real render/diff path, **not** a meaningful measure of new-tab implementation or P2 parity: the compared pages and font environments differ.

Windows automated verification: [run 43524590439](https://github.com/Psycho-Core/PsychoVault/actions/runs/43524590439) passed the browser/test builds, engine tests, both harness self-test suites, all 18 owned goldens, native smokes and package verification on commit `509f498ae567601311aa119becdfbf9a692e082d`. Its temporary branch-only push trigger was removed; manual dispatch permission was not demonstrated. The Windows self-tests include real archive decoding and a mocked renderer, not an end-to-end Windows render against the archive. Earlier runs [43522397133](https://github.com/Psycho-Core/PsychoVault/actions/runs/43522397133) and [43523222394](https://github.com/Psycho-Core/PsychoVault/actions/runs/43523222394) failed in the new self-test because its assertion compared a Windows short temporary path to the renderer's correctly resolved long path; the assertion was fixed before the passing run.

The harness supplies S071's comparison mechanism; it does not close the P2 exit gate. The current goldens remain narrow, the reference-contract corpus still needs independent correctness assertions, and Windows interactive/DPI/presentation evidence remains outstanding. See [S072 audit](S072_PHASE2_AUDIT.md) for the baseline assessment.
