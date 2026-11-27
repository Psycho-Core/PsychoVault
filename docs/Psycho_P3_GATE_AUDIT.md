# P3 exit-gate audit

Written 2026-11-27 against the master plan's P3 exit gate, after the work recorded in the P3 status paragraphs of `Psycho_MASTER-PLAN.md`. **Verdict: P3 is not closed. The gate is not met on any of its three clauses.** This document says what is true, what is not, and what closing P3 would still take. It does not change the gate.

## The gate

> side-by-side screenshots of old and new chrome, new-tab and all ten settings pages are hard to tell apart at the reference sizes, every control whose backing service exists changes real state, and every other control is listed with the step that will wire it. "Every one of the 125 controls changes real state" is the M4 gate, not this one.

## Clause 1 - old and new are hard to tell apart at the reference sizes: not met

What I measured is an exact-RGBA changed-pixel count between my render and the legacy capture, on the Windows runner with real glyphs ([run 45978038507](https://github.com/Psycho-Core/PsychoVault/actions/runs/45978038507)). That metric counts every one-level shade difference in a gradient, so it overstates visual difference; it is a measurement, not a parity claim, and **I have not looked at any diff image or screenshot** (the artifact host is blocked from this environment). The numbers still show where the work is not done.

| Page (reference, 1396x834 unless noted) | Differing pixels | Share |
|---|---|---|
| New tab (03) | 334,522 (earlier run) | 28.7% |
| Settings: General (04) | 127,168 | 10.9% |
| Settings: Privacy (05) | 178,723 | 15.4% |
| Settings: Downloads (06) | 119,922 | 10.3% |
| Settings: Extensions (07) | 453,487 | 38.9% |
| Settings: Performance (08) | 337,052 | 28.9% |
| Settings: Appearance (09) | 220,314 | 18.9% |
| Settings: Spelling (10) | 252,447 | 21.7% |
| Settings: Passwords (11) | 259,902 | 22.3% |
| Settings: Accessibility (12) | 251,340 | 21.6% |
| Settings: About (13) | 404,611 | 34.8% |
| Browser frame (02, 1440x940) | 12,192 | 0.9% |

Reading: the frame is close to its capture. General, Downloads and Privacy are in the range where remaining differences may be mostly text rendering. Extensions, Performance and About are far off, and I believe the cause is structural: the captures were one viewport tall, so the grouping, copy and spacing of those categories below the fold is my reconstruction (`ui/README.md` says so). The new-tab page is the weakest at 28.7%.

Also unverified here: the `*-window` variants of the captures (window-framed screenshots) are not compared at all; no comparison at 125% or 150% display scale was made on Windows (`tests/Psycho_ui_responsive_tests.py` checks layout at those ratios in the engine, not pixels).

## Clause 2 - every control whose backing service exists changes real state: not met

The inventory has 109 settings controls: 43 buttons and 66 others, of which 51 hold a value I catalogued (the rest are transient editor inputs and secrets). `tools/Psycho_gen_settings_catalog.py` generates the catalog; `runtime/settings` validates and applies.

- **Wired (26 of 51 value controls):** search engine, home page, restore tabs, workspaces, block ads, third-party cookies, Do Not Track, referrer, autoplay, WebRTC policy, download folder, ask where to save, download notifications, battery saver, sleep minutes, theme, accent, hide sidebar, high contrast, reduce motion, tab close buttons, panic button and URL, spell check, built-in spelling, vault lock minutes. All go through `psycho.settings` and the same validation and apply code. The saved profile was re-read on Windows for one of them (the "Restore tabs" switch). Linux unit tests exercise `restore`, `search`, `vaultLockMin`, `sleepTabsMin` and `panicUrl` plus the refusal paths; **the other 21 are not individually tested**.
- **Not wired although a backing field or service exists today** (so these violate the clause as written): `cpuLimit` (profile has `cpu_limit`), `clearOnExit` and `remember` (history on/off), `spellUserWords`, `spellLangs`, `spellDictUrl` (the spelling service and profile words exist), `vaultEnabled` (outside the 51: the catalog excludes it), every vault control (the vault service exists), `translateTarget`, `denyPerms`, `blockAllowSites`, `blockCustomDomains`, `blockUseBundled`, the extension and proxy buttons (the extension host exists), and the "Clear browsing history" button on the settings page (the history panel in the frame can clear it; the settings button is not wired).
- **Buttons:** none of the 43 inventory buttons on the settings page is wired. The six clear-data actions and eight dynamic editors (`site permission editor`, `container list`, `data usage table`, `proxy switch list`, `extension list`, `sidebar service catalogue and list`, `speed dial tiles`, `vault entry list`) are markup placeholders.
- **Does the changed value take effect?** For several wired keys the profile changes but nothing in the running browser reads it yet (theme, accent and hide-sidebar do not restyle the frame; high contrast and reduce motion are read only when a page session starts). The clause says "changes real state"; state changes, behaviour mostly does not, and the plan's own wording for S083 says effects arrive in P7. I count the state change, not the effect.

## Clause 3 - every other control is listed with the step that will wire it: partly met

The settings page marks every unwired value control with an owner, but the owner is one generic string (`S141-S145`), not the individual step, and it covers only the 25 unwired value controls, not the 43 buttons or the eight editors. There is no per-control matrix. That list does not exist yet.

## Evidence per step

| Step | Status | Evidence | Open |
|---|---|---|---|
| S080 tokens | done | `ui/tokens.css`, `components.css` measured from captures; settings markup tests | none known |
| S081 app frame | mostly done | frame page on by default; Windows smoke presses its tab, tab-menu, dialog, panel, find and palette controls through the real pointer path; status bar; crash recovery | update banner, resizer, workspace chip, shield count, window-drag region is an estimate, the rail's service icons are **static demo markup, not the user's services** |
| S082 new tab | layout only | renders at 28.7% pixel difference; shell serves it | tiles are static, not the profile's speed dial; greeting and empty states missing |
| S083 settings | markup done, wiring partial | 109 controls present, 26 of 51 value controls wired | everything under Clause 2 |
| S084 dialogs | alert, confirm, prompt done | focus trap, Escape, Enter; page alerts use it; Windows smoke answers one | permission prompts (no producer, S128/S161); native pickers stay native; page `confirm()`/`prompt()` cannot block on an async dialog |
| S085 panels | bookmarks, history, downloads, find bar, command palette | Windows smoke opened a history row, closed the find bar, ran a palette command | extensions, performance, vault, workspaces, translate panels; download progress bars and open/show-in-folder |
| S086 tab strip | new, close, activate, pin, mute, context menu | Windows smoke pinned a tab from its menu | drag to reorder, drag out to a window, a real muted indicator |
| S087 rail | ad-block toggle | headless test only | Proton button, private window, real services with favicons, ordering rules |
| S088 bridge wiring | settings and the frame's own actions | schemas, curated profiles, shell-side allow-list, refusals tested | everything else |
| S089 interaction states | done for frame and shared components | engine tests: hover tint, red close hover, keyboard focus ring and none on pointer focus | disabled styling is CSS only and untested; high-contrast variant not built |
| S090 scaling and responsiveness | tests only | `tests/Psycho_ui_responsive_tests.py`: 3 sizes x 3 ratios, no overflow, frame geometry fixed | no text-scale control in the UI; frame does not reflow below 800px; no Windows DPI evidence |
| S091 visual regression | report only | per-category and frame diff in CI, numbers above | no thresholds, no owned baselines for these pages, no 125%/150% DPI images |
| S092 interaction regression | partial | `--smoke-chrome` and `--smoke-settings` journeys on Windows; 31 frame-page behaviour tests | no palette typing, panel filtering or tab-reorder journey on Windows |
| S093 retire Win32 chrome | started | 753 lines of unreachable drawing code deleted | the legacy key, hit and settings-editing state remains in `shell/Psycho_win_main.cpp`; the file is still 6,400 lines |

## Testing totals

Linux: 4,583 checks in `pv-tests`, 18 owned and 28 independent goldens, the P2 contract suites, 31 frame-page tests, 8 settings-markup tests, 3 settings-wiring tests, 5 responsiveness tests. Windows: the same suites plus the services, tab-model, address-bridge, settings and frame smokes, all green in the run above.

## Defects found by this phase (all fixed, each with a test or CI evidence)

JS engine: `var` was block-scoped; `for (name in ...)` did not parse; `for (var k in ...)` did not declare `k`. DOM: no `firstChild`/`lastChild`/sibling properties or `removeChild`; no `document.activeElement`; no `input.type`. Input: Enter and Space did nothing on a focused button or link; a press on a label's child did not toggle its control; hit testing chose the smallest box instead of the topmost painted element. Shell: repainting the whole window for every pumped frame (about 12 paints a second at about 80 ms each) starved the timer. Earlier: gradients, inline `svg`, `text-align`, single-side borders, transparent control backgrounds.

## What I have not verified

No screenshot or diff image has been looked at by me. No accessibility check (screen reader, tab order across the whole app, high contrast) was made. IME composition into the frame's own inputs was not tried. Nothing was measured for memory or startup cost of the second renderer the frame adds. The 2-second first-navigation cost noted at P1 is unexplained.

## What closing P3 would take

Either build the rest (a per-control owner matrix; wire the controls whose service exists; the extension, vault, workspaces, performance and translate panels; drag reorder; real rail services and speed dial; review the diffs and bring Extensions, Performance and About and the new-tab page into range), or scope the gate as D012 did for P2. I have **not** made that scoping call. One reading I would offer for your review: "hard to tell apart" is judged by you from the diff images and screenshots, which I cannot see; "every control whose backing service exists" is limited to the 26 already wired plus the ones listed above as having a service today; "listed with the step" is the per-control matrix, which I should write next either way.

## Update 2026-11-27 (settings persistence)

[Run 45995979447](https://github.com/Psycho-Core/PsychoVault/actions/runs/45995979447) passed on Windows. Clause 2 moved, but the clause is still not met.

- **All 51 value controls now persist.** The 26 that had a profile field keep it; 7 more got a dedicated one (remember history, CPU limit, spelling languages, dictionary source, personal words, never-block sites, extra blocked domains); the other 18 are stored in a validated extra-settings bag in the profile (`extraSettings`). The service rejects a bad value with a reason instead of accepting it. A unit test saves, reopens and reads every one back, and probes every catalogued key with a value of its own kind.
- **Remember history is real:** with it off, no visit is recorded (both the profile and the shell's own history push check it).
- **Buttons now wired:** Save words, Reload status, Save blocker lists, Save shortcuts, Apply mod, Clear, and Clear history (which asks twice, then clears through the broker). That is 7 of the 43 buttons.
- **Storing is not acting.** A value in the bag changes nothing in the running browser yet (for example sound volume, bandwidth cap, bundled domain list, translate target, containers). I have not audited which of the 26 older fields the browser acts on; a grep shows several are read in only one place. The plan's P7 steps own the effects.
- **Still not wired:** the vault controls and entry editor, the extension and proxy buttons, the download folder picker, the six clear-data actions, the update token, and all 8 dynamic editors. Custom search shortcuts are saved as text but not yet parsed into engines.
