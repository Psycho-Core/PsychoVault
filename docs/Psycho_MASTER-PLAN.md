# My master plan for PsychoVault

This replaces every earlier plan in this repository. `docs/Psycho_PLAN.md`, `Psycho_NEXT-BUILD.md`, `Psycho_STEP7-AUDIT.md`, `Psycho_STEP8-AUDIT.md`, `Psycho_PLAN-AUDIT.md`, `Psycho_RESEARCH.md`, `Psycho_VERIFICATION.md`, `Psycho_TEST-BUILD.md`, `Psycho_RELEASE-NOTES.md` and `Psycho_IMPLEMENTATION-STATUS.md` are deleted on purpose: they described a different product and they kept me busy polishing the wrong layer.

## 1. The short version

**Yes, I can build it.** Not "yes with an asterisk hidden in a footnote" - yes, in the sense you actually care about:

- One installer, `PsychoVault-Setup-<version>.exe`, that unpacks, registers, makes shortcuts, appears in Apps & Features and uninstalls like every other program on Windows.
- A browser window that looks and behaves like my old app: dark navy, cyan accents, tabs, rail, omnibox, workspaces, new tab, all ten settings categories, every feature in my inventory.
- Web pages that load from the real internet and render like a browser, on code written for this project.
- Proton VPN that authenticates, connects, proxies my traffic and fails closed, and a general extension engine so I can add other Chrome/Firefox (MV3/MV2) extensions myself.
- No Chromium, no Electron, no WebView, no Gecko, no renamed browser.

The one thing I will not promise is a date for "every website on earth renders pixel-identical to Chrome". That is a moving standards target tens of millions of lines deep; nobody delivers it quickly and anyone who says otherwise is selling something. What I will deliver is the real thing growing in milestones I can prove, each milestone a working installer I can try.

## 2. Why the last attempt failed - plainly

1. **I built the shell, not the runtime.** The old app felt like Electron because Electron gives you a rendering engine plus an app runtime, and the entire UI is HTML/CSS/JS drawn by that engine. I drew the new UI with Win32 painting calls instead. That can never converge on the screenshots.
2. **I never turned the engine into the platform.** I had an engine and a hand-drawn window; the thing that makes an app like this feel finished - the runtime that hosts UI pages, IPC, sessions, extensions, downloads, updates - was ad hoc inside one file.
3. **The UI was not the source of truth.** The screenshots and the legacy inventory existed, but the build did not render my old HTML/CSS. So "same interface" was judged by eyeball instead of by rendering the same markup.
4. **Nothing proved real-web behaviour.** Every CI check loaded a fixture on localhost. Real DNS, real TLS, real redirects, real compressed pages, real scripts were never exercised, so "it loads pages" was never true evidence.
5. **Packaging contradicted the product.** A portable single exe is not an installer, and the extension payload was not inside it, so Proton was invisible.

The fix is architectural, not cosmetic: **build my own Electron first, then hang my browser UI and my engine on it.** That is what this plan intends to do.

### Starting-point baseline audit — 2026-11-06 (historical)

The repository's existing shell and engine are starting material, not proof that the product works like the reference browser. The current evidence is:

- `ui/` contains only its README; there is no HTML/CSS/JS browser interface yet.
- `shell/Psycho_win_main.cpp` still draws browser chrome with native Canvas calls, and ordinary web tabs still construct an in-process `Session`/`Host` path.
- The new renderer farm is launched by the shell only for blank-page startup probes. It does not yet own ordinary tabs or their input, resize, navigation, and presentation lifecycle.
- Runtime state-machine and in-memory tests are useful contracts, but they do not prove Windows production integration. The Windows build and the real-process recovery/isolation tests remain unrun.
- The engine is a partial starting point, not a Chrome-class engine. `docs/Psycho_CONFORMANCE.md` records 5,009 of 54,058 Test262 tests passing (9.3%), 16 interpreter crashes, 21,845 unsupported tests, and no WPT run. S050's promised file-by-file engine gap table does not exist yet; S008 lists only the UI's initial CSS/DOM needs.
- `installer/` contains a README only. P10 owns the installer, so the M1 promise of a working installer and the statement that every milestone ends in an installer are not currently schedulable.
- The M2 UI promise, P2 engine exit gate, and P3 UI implementation are out of order: the P2 gate asks for settings/new-tab markup before P3 creates `ui/`.

At this 2026-11-06 starting point, P1 was not complete, and the old shell/engine could not be counted as reference parity. The first actual source-to-plan audit is recorded in `docs/Psycho_REFERENCE-CROSSWALK.md`: it checks the pinned inventory commit against the read-only reference repository's current HEAD, records where they diverge, maps the reference surfaces to plan steps and current evidence, and identifies the missing acceptance cases. The crosswalk is not implementation evidence. At that initial audit point, S050's full engine gap table and row-level implementation/test traceability were still outstanding; they are now completed as an audit below, not as engine implementation.

### P1 acceptance update — 2026-11-07

The P1 exit gate passed in Windows Actions run [42334940299](https://github.com/Psycho-Core/PsychoVault/actions/runs/42334940299), on commit `7265f7e`. The complete run built the Windows executable and passed `pv-tests`; the Windows-only checks exercised two real renderer child processes, measured IPC, killed and replaced a renderer, and empirically verified AppContainer denial of profile-file reads and socket connections. The single-executable renderer launch and package-verifier checks also passed.

S045's same-app acceptance passed in that run: address entry in the engine-rendered `psychovault://newtab/` page invoked the curated bridge, retained the source tab, opened the local fixture as a second tab, and verified that its renderer completed loading and painted an image. The run preserved the flow report and app/fixture captures as artifacts. The final branch cleanup commit `94f896d` only removed the temporary branch-scoped workflow trigger after this successful run; it did not change the acceptance implementation.

**P1 runtime acceptance is complete.** This does not claim reference parity or completion of P2/P3; later-milestone implementation and parity work remain outstanding.

### P2 status — 2026-11-07

S050 is complete as an audit, not as engine implementation. `docs/Psycho_ENGINE-GAPS.md` maps every P0/P1 requirement to exact pinned-reference and engine/test line anchors, five license-safe HTML/CSS/JS/raster/SVG fixtures, a passing `pv-engine` corpus smoke, and an explicit owned/ordered semantic regression deferral. The preserved harness output is `docs/results/Psycho_S050_CORPUS-AUDIT.json`; the refreshed implementation-status crosswalk is `docs/Psycho_REFERENCE-CROSSWALK.md`. This does not claim CSS/DOM/JS conformance, reference screenshot parity, or P2 completion.

**S051 is complete on the supported Windows build.** `Canvas::text_width` and Windows text rasterization use DirectWrite shaping/metrics with Direct2D drawing; the 5×7 bitmap glyph fallback was removed; and nowrap/ellipsis handling plus multilingual metric/raster regressions were added. Linux `pv-tests` passed 817 engine checks / 2,662 total checks. Windows Actions run [42340417295](https://github.com/Psycho-Core/PsychoVault/actions/runs/42340417295) built the code at `2561444`, passed `pv-tests`, rendered fixtures, and passed the existing Windows runtime/package checks. A test ZIP is available in that run's `psychovault-development-2561444e747eb75f5f5ada54c6ff0011015e37ad` artifact. Portable non-Windows rendering now uses approximate layout advances but intentionally does not draw substitute bitmap glyphs; the supported UI text path is Windows DirectWrite.

**Launch/navigation blank-window follow-up:** a missing first frame or failed navigation in the S045 full-page shell path used to leave the window black, because the fallback status was suppressed along with the legacy browser chrome. The shell now shows a loading/failure card with Ctrl+R/Ctrl+T recovery guidance, and the ZIP's README identifies it as a development/test build, not an installer or finished general-use browser. The user's screenshot then exposed the concrete failure: a renderer IPC frame exceeded the default 1 MiB cap even though documents can be 8 MiB. Renderer process IPC now has an explicit 32 MiB bound, with a regression test that transfers a 1.2 MiB document/title through both broker and renderer. Linux tests passed 817 engine checks / 2,664 total checks; Windows run [42344556971](https://github.com/Psycho-Core/PsychoVault/actions/runs/42344556971) passed its build, tests, native-home capture, local fixture render, same-app bridge flow, isolated renderer, and package checks. This does not prove compatibility with arbitrary websites or independently replay the user's exact URL. S052 CSS values and the S053 targeted cascade/interaction subset have local Linux evidence below; the P2 golden-image and browser-parity gates remain open.

### S052 follow-up — CSS values, 2026-11-07

Implemented the S052 CSS-values subset in `engine/Psycho_css.cpp`, with declaration mutation integration in `engine/Psycho_page.cpp` and containing-block percentage evaluation in `engine/Psycho_layout.cpp`. Ordered balanced declaration parsing preserves important/source order; custom properties cascade and inherit, `var()` supports recursion/fallbacks, and inline custom-property changes rebuild styles. The owned length parser handles `calc()`, `min()`, `max()`, `clamp()`, common absolute/font/viewport units, and mixed-unit width/height expressions. Common named/hex/RGB/HSL colors, `currentColor`, and selected margin/padding/border/font/gap shorthand expansions are covered.

Named regressions `UiCustomPropertiesCascadeAndMutation` and `UiCssMathFunctionsAndMixedUnits` were added. Linux `pv-tests` passed **835 engine checks / 2,682 total checks**, including declaration tokenization, containing-block percentage, variable cascade, color, and shorthand regressions. This is targeted behavior, not full CSS conformance: typed calc validation, full color space/name support, percentage bases for margin/padding/min/max, broader shorthand/cascade behavior, the S052-S071 golden-image suite, and browser screenshot parity remain open. The supported Windows build/test/native-render/package gate passed in [Actions run 42349830651](https://github.com/Psycho-Core/PsychoVault/actions/runs/42349830651) on commit `1c6b659`. Its temporary branch-only push trigger was removed afterward; this does not prove manual-dispatch permission.

### S053 follow-up — cascade and interaction subset, 2026-11-07

S053's targeted subset is implemented in the owned DOM/CSS/page/input paths. The cascade now separates UA and author origins, importance, matched-selector specificity (including the actually matched member of a comma-separated selector list), stylesheet source order, and inline declarations. It handles `!important` and the CSS-wide `inherit`, `initial`, `unset`, and `revert` keywords for supported properties and selected expanded shorthands. The selector matcher adds `:not()`, `:nth-child()` An+B/odd/even, `:hover`, `:focus`, `:active`, `:checked`, and `:disabled`; page focus/blur and pointer move/down/up state feed style recomputation, the renderer accepts pointer messages, and the Windows shell forwards mouse movement/down/up to page surfaces.

`UiPseudoClassInteractionMatrix` covers static semantic matching, focus/hover/active and checked-state paint changes, child-index formulas, comma-list specificity, author-vs-UA ordering, importance including inline-important precedence, and all four CSS-wide keywords. The Linux test run passed **867 engine checks / 2,714 total checks**.

This is not a full CSS cascade or selector implementation: there is no separate user-origin stylesheet API, cascade layers, full selector grammar, `:nth-child(... of ...)`, full disabled-fieldset/legend rules, or complete CSS-wide initial-value tables. Unsupported declarations/pseudos, other shorthand families, the S052-S071 golden-image suite, and browser visual parity remain open. S053 has local Linux evidence only; no Windows S053 Actions run is claimed.

### S054 follow-up — box sizing and overflow subset, 2026-11-07

S054 now re-resolves percentage margins/padding against the containing block width; resolves percentage min/max width and height constraints when the corresponding basis is definite; applies min/max and `box-sizing` in content-box versus border-box coordinates; and handles percentage heights on boxes with a definite parent height. Adjacent vertical margins collapse across positive and negative values. Replaced images retain their intrinsic ratio while selected min/max constraints fit their boxes. `overflow:hidden/clip/auto/scroll` clips to the padding edge, clipped content is excluded from pointer hit tests, and offscreen clip push/pop commands still correctly restore painter state.

`UiBoxSizingPercentAndIntrinsicSizing` covers positive/negative margin collapse, percentage margin/padding and heights, content/border-box min/max sizing, intrinsic image fit, clipped hit testing, scrollbar paint markers, and an offscreen nested-clip paint regression. Linux tests passed **879 engine checks / 2,726 total checks**.

This remains a focused subset, not full box-model conformance: parent/empty-block margin collapse and many intrinsic keywords are incomplete; vertical scrollbars are static visual indicators (nested scroll offsets, wheel routing and thumb dragging belong to later work); horizontal scrollbar policy, full overflow-axis combinations, and broad intrinsic sizing remain open. No Windows S054 build is claimed. Golden-image and browser visual parity remain open.

## 3. What "my own Electron" and "my own Chrome" mean here

Electron = (a) a rendering engine, (b) an app runtime that owns windows, processes, IPC, sessions, menus, extensions and updates, (c) the app's UI written as HTML/CSS/JS and rendered by (a).

My equivalents, all written for this project:

| Layer | Electron/Chrome equivalent | Mine | Where the code lives |
| --- | --- | --- | --- |
| App runtime - privileged side | Electron main process + browser process | **Psycho Runtime** (`pvrt`): window host, tab model, navigation, sessions/profiles, IPC broker, preload bridge, extension host, download service, update service, dialogs, tray, menus | `runtime/` |
| App runtime - unprivileged side | Renderer process | **Renderer host**: engine instance per site instance, sandboxed, talks only through typed IPC | `runtime/` + `engine/` |
| Rendering engine | Blink + V8 | **Psycho Engine**: HTML, CSS, DOM, layout, paint, JS, media, canvas | `engine/` |
| Bundle of platform services | Chromium net service, storage, GPU | **Platform**: network/TLS/proxy/DNS, profile stores, extension API surface, vault, spell | `platform/` |
| App UI written as web pages | `app/ui/*.html` in the old app | **My UI**: tab strip, rail, omnibox, new tab, settings, dialogs, palette, extension manager - HTML/CSS/JS rendered by my engine | `ui/` |
| OS integration | Windows shell | **Native shell**: window creation, input, IME, clipboard, DirectWrite/Direct2D surfaces, tray, notifications | `shell/` |
| Installer/updater | NSIS + updater (old app) | **My installer and updater**, written for this project | `installer/` |

**The decision that changes everything:** the browser's own interface is HTML/CSS/JS, exactly like the old app, and my engine renders it. That is the same trick Electron uses, minus Electron. It makes the screenshots a mechanical target instead of an impression, and every CSS/DOM feature my own UI needs is a feature real websites need too - the effort pays twice.

Interim rule: while the engine cannot yet render the full chrome UI, I may ship a scaffolding window (plain frame, no fake chrome) so I can test pages and services. Scaffolding is never presented as the interface, and it is deleted at M2.

## 4. Non-negotiables (updated)

1. Every line of browser, runtime, engine, UI and installer code is written for this project. Allowed: the C++ compiler and standard library, Windows SDK/OS APIs (Win32, DirectWrite, Direct2D, Schannel, CNG, Winsock), build tooling that never ships inside the product.
2. Extensions are the only third-party code inside the product, and they stay separately identified, licensed and sandboxed (Proton VPN is the standard bundled one). Extension code is never merged into my own sources.
3. The interface is a contract: `docs/reference/legacy-ui/` screenshots plus the legacy HTML/CSS geometry. Same layout, spacing, colours, typography, icons, focus states, interactions.
4. Every feature and setting in `docs/Psycho_FEATURE-INVENTORY.md` gets an implementation and an acceptance test. A control that does nothing is a bug, not a placeholder.
5. Proton VPN is proven by behaviour: sign-in, connect, real proxied traffic, fail-closed, teardown.
6. The product ships as an **installer**, never as a portable exe.
7. No GitHub Actions builds until the milestone gates in section 6 say the outputs are worth building. Workflows are parked to manual dispatch in this change.
8. Evidence, not adjectives: every claim carries a test name and its output. Passed, failed, blocked and not run are reported separately.
9. Documents are written in my voice; the reference repository is read-only reference, never imported code.

## 5. Milestones - what I can try at each one

| Milestone | Deliverable | Proof I will run |
| --- | --- | --- |
| **M1 - Runtime vertical slice** | A runnable Windows development build: one launchable browser executable, an engine-rendered minimal app page, one brokered tab, navigation and diagnostics. This is not an installer. | Build and launch on Windows; app UI and a local web fixture render through the real renderer process; tab input/resize and IPC are exercised; process recovery and isolation evidence is recorded |
| **M2 - My UI on my engine** | Browser chrome (rail, tab strip, omnibox, status bar) plus new tab rendered from my own HTML/CSS/JS | Pixel diff against `legacy-ui/01-03` at reference sizes; the UI actions use the production runtime bridge |
| **M3 - Real web** | Real sites load over HTTPS with DNS, redirects, compression, cookies, cache; error pages tell the truth | Site corpus screenshots from a real network run |
| **M4 - Settings parity** | All ten settings categories, 125 controls, wired to real behaviour | Control-by-control acceptance matrix; visual diff of each category |
| **M5 - Features parity** | Tabs/workspaces/private/split/palette/bookmarks/history/downloads/find/zoom/rail/performance/recovery/shortcuts | Inventory acceptance matrix, no empty handlers |
| **M6 - Extensions** | MV3 host: install, service worker, content scripts, messaging, proxy, storage, popup pages, manager UI | My test extensions + Proton installs and opens |
| **M7 - Proton VPN** | Drawdown: my own WireGuard client, my own proxy daemon, extension-driven proxy control, fail-closed | Local controlled proxy tests + live Proton account test |
| **M8 - Product** | Vault migration, updater with signed manifests, installer repair/upgrade, accessibility, performance budgets | Upgrade/rollback tests, migration fixtures, measured budgets |
| **M9 - Release** | Signed installer on Releases, notes and checksums | Clean-machine install test, then publish |

Each milestone ends with a runnable build and evidence; milestones after P10 may be packaged as installers. M1 is a developer build, not an installer. I must not describe the product as installable before the installer exists.

## 6. The steps

Rules for reading this list: an `S` number is one unit of work with one piece of evidence. Steps are ordered by dependency, not by glamour. A phase is complete only when its exit gate is met. Nothing in this list gets marked done because it compiles.

### P0 - Ground truth (this change)

- **S001** Delete the superseded plan, audit and status documents; keep one plan (this file), one architecture file, one reference file, one build file, the feature inventory, the conformance file.
- **S002** Remove scratch patch scripts and any generated artefacts from the tree.
- **S003** Park every GitHub Actions workflow to manual `workflow_dispatch` with a comment saying builds resume at M2.
- **S004** Delete stale `arena/*` branches on the remote; keep `Master` and the active session branch.
- **S005** Record the module map and directory contract in `docs/Psycho_ARCHITECTURE.md`.
- **S006** Record the reference-intake rules and the parity contracts in `docs/Psycho_REFERENCE.md`.
- **S007** Record build, installer and CI policy in `docs/Psycho_BUILD-AND-RELEASE.md`.
- **S008** Extract the CSS/DOM feature list my old UI actually uses (flex, grid, custom properties, calc, absolute/sticky/fixed, overflow scrolling, border-radius, box-shadow, transitions, transforms, media queries, form controls) into the engine requirement list.
- **S009** Extract the runtime API contract: map all 21 preload namespaces, 80 invoke channels, 105 main handlers and the reference's event channels individually to typed schemas, owner services, sender/capability checks, errors and tests. The counts alone are not a completed contract.
- **S010** Pin the reference commits (legacy app, Proton extension, WPT, Test262) in one file so nothing drifts.
- **S011** Freeze release publishing: no new tags until M9.
- **S012** Write the decision log's first entries (why runtime-first, why UI-as-HTML, why installer, why no CI yet).
- **S013** Maintain `docs/Psycho_REFERENCE-CROSSWALK.md`: trace the read-only reference's observable behavior and artifacts to plan steps, implementation evidence and named tests; keep the pinned inventory and current reference-head differences distinct; refresh the gap rows before a milestone is called reference-complete. The initial 2026-11-06 audit is recorded there.

**Exit gate:** repository contains only documents that describe this plan; no automated build can start by accident.

### P1 - Runtime foundation (my Electron, privileged side)

- **S020** Create `runtime/` with the process/thread model: one broker process, N renderer processes, one network service, one storage service, one extension host, one GPU/compositor thread.
- **S021** Define the message bus: framed length-prefixed messages, typed channel IDs, capability handles, per-channel schema validation, back-pressure, structured errors.
- **S022** Implement process launch/supervision: create renderer with a job object and restricted token, heartbeat, timeout, kill, restart, crash attribution.
- **S023** Implement the singleton app model: single-instance lock, second-launch argument forwarding, graceful quit, session save.
- **S024** Implement `BrowserWindow` equivalent: create/destroy/resize/move/focus/minimise/maximise/fullscreen, DPI changes, multi-monitor placement, taskbar identity.
- **S025** Implement surface presentation: engine paints into a shared surface; broker composites it; no page code touches the window handle.
- **S026** Implement the tab model in the broker: ordered tab list, groups, pinning, mute, discard, tab-to-window move, window-to-window move, recently-closed stack.
- **S027** Implement `WebContents` equivalent: navigation controller, session history, back/forward, reload/stop, title/favicon/URL notifications, loading state, error state.
- **S028** Implement my internal scheme `psychovault://`: page registry, privileged-origin rules, no web access to internal origins without a permission capability.
- **S029** Implement the preload bridge: per-page isolated script context, `window.psycho` namespace, `contextBridge`-style curated exposure, freeze semantics, no direct broker access.
- **S030** Implement the app-layer JS host: run UI scripts in the renderer with the extension-less privileges they need (UI origin), errors surfaced to the broker log.
- **S031** Implement broker services: dialogs (open/save/message), clipboard, notifications, tray, application menu, context-menu plumbing.
- **S032** Implement keyboard shortcut resolution at the broker (not the page) with a single registry, so shortcuts behave the same everywhere.
- **S033** Implement IME and dead-key handling end to end (compose, candidate window position, commit, cancel).
- **S034** Implement focus and activation rules: window focus, tab focus, page focus, dialog modality, background throttling policy.
- **S035** Implement the download service: start, progress, pause/resume (where supported), cancel, open, reveal, save-as, history.
- **S036** Implement the permission service: site permissions by origin, prompts, per-profile persistence, deny-by-default list from the legacy defaults.
- **S037** Implement private sessions: separate in-memory stores, no disk writes, separate renderer policy, extensions off by default with an explicit switch.
- **S038** Implement diagnostics: `psychovault://nettest` (DNS, TCP, TLS, HTTP, proxy, DNS-leak checks with named stages) and `psychovault://version` (runtime, engine, profile, flags, hashes).
- **S039** Implement crash and hang handling: renderer crash page, hang detection, restart tab, bounded restart loop, persisted crash record.
- **S040** Implement structured logging: one file per service, rotation, redaction rules, export bundle for support.
- **S041** Implement the command line: URLs, `--profile`, `--private`, `--diagnostics`, `--language`, no shell injection, argument validation.
- **S042** Write the runtime's unit and integration tests: IPC schema, process supervision, navigation, session history, permission checks.
- **S043** Write a broker-side test harness that a renderer must pass before the UI can use it (round-trip echo, navigation, crash/recover).
- **S044** Route ordinary production tab navigation through the brokered renderer process. Carry input, resize, navigation, loading/error state and rendered output across the same production IPC path; remove the legacy in-process `Session` path as the renderer for ordinary web tabs.
- **S045** Build a minimum real app-UI vertical slice in `ui/` (HTML/CSS/JS) rendered by the owned engine, with a curated bridge for address entry and tab actions. First verify the engine can render this deliberately small page; pull any required engine capability into P1 rather than assuming it exists. Keep the native shell to OS window/input/presentation duties; prove one screenshot at a reference viewport and a local web page in the same running app. This is an architecture/flow proof, not a claim of visual parity; full parity remains M2/P3.
- **S046** Run the complete Windows acceptance path and preserve its output: build the one launchable executable, run `pv-tests`, exercise the real app vertical slice, terminate/recover a renderer, measure process IPC, and empirically test profile-file and socket denial. Record unsupported or failed checks honestly; do not close P1 on portable-model tests alone.

**Exit gate:** the browser starts from one user-launchable executable; its visible app UI is an engine-rendered HTML page; ordinary tabs are owned by real brokered renderer processes; browser actions and page output cross the production IPC boundary; two real renderers run and one is killed/replaced; measured IPC and empirical Windows profile-file/socket denial evidence pass. The actual app flow and all evidence must be demonstrated by the Windows acceptance run. This gate is intentionally stronger than a process-farm startup probe.

### P2 - Engine to a usable rendering baseline (the "Chrome side", part one)

Order matters: my own UI's CSS comes first, then the web's common CSS, then the long tail.

- **S050** Audit the engine against S008 and the actual reference UI/page use in `app/ui/index.html`, `newtab.html`, `settings.html`, `renderer.js` (at pinned source commit), plus a representative page corpus. For each required HTML/CSS/DOM/JS/API behavior, record the consuming selector/callsite, current engine implementation file, missing behavior, priority and named regression test; do not limit this audit to the initial CSS checklist.
- **S051** Text: move to DirectWrite for shaping, metrics, line breaking, ellipsis and fallback; kill any bitmap-font fallback path in UI rendering.
- **S052** CSS values: custom properties (`var()`), `calc()`, `min()/max()/clamp()`, color/units/percentage resolution, shorthand expansion.
- **S053** Cascade and inheritance: origin/importance ordering, specificity, `!important`, `inherit/initial/unset/revert`, pseudo-classes used by my UI (`:hover`, `:focus`, `:active`, `:checked`, `:disabled`, `:not`, `:nth-child`). A targeted subset is implemented and Linux-tested locally; full CSS conformance and visual parity remain open.
- **S054** Box model completeness: margins collapsing, percentage heights, min/max sizes, box-sizing, overflow clipping, scroll containers with visible scrollbars. A targeted subset is implemented and Linux-tested locally; full intrinsic sizing and nested scrolling remain open.
- **S055** Flexbox: direction, wrap, grow/shrink/basis, alignment, gaps, baseline, ordering, absolute children inside flex. A targeted subset is implemented and Linux-tested (888 engine checks / 2,735 total checks); intrinsic min-content sizing, exact glyph baselines, difficult percentage/wrap combinations, full absolute-child static positioning, and visual parity remain open.
- **S056** Grid: tracks, `grid-template-areas`, auto-placement, spans, gaps, alignment; enough for my sidebar/settings layouts. A targeted subset is implemented and Linux-tested (898 engine checks / 2,745 total checks); auto-repeat, advanced intrinsic sizing and full grid conformance remain open.
- **S057** Positioning and stacking: static/relative/absolute/fixed/sticky, containing blocks, z-index stacking contexts, clipping order, fixed to viewport, sticky offsets. A targeted subset is implemented and Linux-tested (907 engine checks / 2,754 total checks); nested scrollports, transformed containing blocks, full stacking-context ordering and fixed overflow escape remain partial.
- **S058** Painting: `border-radius`, `box-shadow`, `outline`, background layers (color/gradient/image), `filter` basics, `opacity` groups, `transform` (translate/scale/rotate) in the paint and hit-test paths. A targeted subset is implemented and Linux-tested (916 engine checks / 2,763 total checks); full CSS semantics and visual parity remain open (see `Psycho_ENGINE-GAPS.md`).
- **S059** Scrolling: wheel, scrollbar rendering and dragging, keyboard scroll, smooth scrolling policy, scroll anchoring, nested scroll, overscroll behaviour. A targeted vertical subset is implemented and Linux-tested (931 engine checks / 2,778 total checks); scrolling is immediate and full anchor preservation, horizontal scrolling and browser scroll semantics remain open (see `Psycho_ENGINE-GAPS.md`).
- **S060** Hit testing and event dispatch: pointer, mouse, keyboard, focus, hover/active state machines, capture/bubble, default actions, `preventDefault/stopPropagation`. A targeted event subset is implemented and Linux-tested (937 engine checks / 2,784 total checks); pointer capture APIs, full keyboard defaults and complete DOM event semantics remain open (see `Psycho_ENGINE-GAPS.md`).
- **S061** Forms and controls rendered by the engine: `input` types used by my UI (text, checkbox, radio, range, number, password, search, file trigger), `select` popup, `textarea`, `button`, `label`, validation states, `:focus-visible`, disabled states. A targeted subset is implemented and Linux-tested (951 engine checks / 2,798 total): labels, checkbox/radio state, select value/index and keyboard selection, plus required/email/number/range validity. Dedicated range/file/select-popup UI, complete type semantics, full constraint validation and focus-visible input-modality behavior remain open (see `Psycho_ENGINE-GAPS.md`).
- **S062** Selection, caret, and text editing: click/drag/word/line selection, caret blink, clipboard cut/copy/paste, undo/redo basics, IME integration with S033. A targeted subset is implemented and Linux-tested (968 engine checks / 2,815 total): UTF-16 selection APIs, click/drag selection, caret/selection paint, editing/navigation, undo/redo, native clipboard forwarding, and composition preedit/commit/cancel. Word/line gestures, full TSF candidate placement, dead keys and rich clipboard remain open (see `Psycho_ENGINE-GAPS.md`).
- **S063** Images and SVG: decode paths, scaling, lazy loading, `srcset`/`sizes`, SVG shapes/paths/styles, favicon pipeline. A targeted `loading=lazy` viewport-fetch subset is implemented; SVG/CSS conformance, `object-fit`, `srcset`/`sizes`, and favicon lifecycle remain incomplete.
- **S064** Media queries and viewport: width/height/aspect/`prefers-*`, scrollbars affecting layout, zoom levels, `devicePixelRatio` handling. Implemented and regression-tested for viewport/resolution media queries, profile-driven preferences, classic scrollbar gutter layout, browser zoom/DPI DPR, and JS viewport values; remaining advanced media-query grammar and nested/native scrollbar details are recorded in `Psycho_ENGINE-GAPS.md`.
- **S065** Animation basics: transitions with timing functions and `animation` keyframes, driven off the compositor clock, respecting reduced motion. Targeted opacity/color/size transitions and single-name keyframes now run from the host pump with reduced-motion suppression; broader property/event/list semantics remain open (see `Psycho_ENGINE-GAPS.md`).
- **S066** Incremental layout and damage-based repaint so UI interaction does not repaint the world. First-pass layout-result reuse, paint-command damage regions, and clipped Win32 repaint/presentation are implemented; Linux engine regressions pass. Dirty-subtree layout and Windows validation remain open (see `Psycho_ENGINE-GAPS.md` and `S066_IMPLEMENTATION.md`).
- **S067** Compositing: layerisation for scrolled/transformed/fixed content, tile invalidation, vsync-synced presentation, dropped-frame instrumentation.
- **S068** Canvas 2D subset (paths, text, images, transforms, clipping) - needed by pages and later by my own charts.
- **S069** Fonts: bundled UI family (my licence-clean choice), fallback chain, weight/smoothing, measurements stable across DPI.
- **S070** Rendering test suite: my own golden-image tests for layout primitives, one per S052-S069 item, runnable locally.
- **S071** Visual diff harness: render reference viewports, compare to `legacy-ui` captures, produce a diff map and a pass/fail report.

**Exit gate:** the engine golden-image suite covers the S052-S069 feature set and is green on the supported Windows build. Representative test fixtures derived from the reference UI's CSS/DOM contract render correctly. The actual settings/new-tab HTML and full screenshot parity are delivered and tested in P3/M2, not assumed to exist before P3.

### P3 - My UI on my engine (the interface contract)

- **S080** Author `ui/` as plain HTML/CSS/JS: design tokens (colours, spacing, radii, typography) taken from the screenshots and the legacy stylesheet.
- **S081** Build the app frame: rail, tab strip, omnibox, status bar, update banner, panel container, resizer, at the reference geometry (84px chrome, 44px rail, 22px status bar, 330px panel).
- **S082** Build the new-tab page: logo, greeting, search box, speed-dial tiles, service rail icons, empty states.
- **S083** Build the ten settings categories with all 125 interactive controls, exact defaults/types/labels and persistence from `docs/Psycho_FEATURE-INVENTORY.md`, grouping and copy from the reference. Include the eight dynamic editors and all six clear-data actions; a visible control is not complete until its state, effect, validation, disabled/error state and reset scope are wired.
- **S084** Build dialogs: confirmations, prompts, file pickers, permission prompts, error dialogs, with focus traps and keyboard control.
- **S085** Build the panels: downloads, extensions, performance, vault, history, bookmarks, workspaces, command palette, find bar, translate.
- **S086** Build the tab strip behaviours: drag to reorder, drag out to window, new-tab button, close, pin, mute, context menu.
- **S087** Build the rail: services with real favicons, ad-block toggle, Proton button, settings, private window, ordering rules from the old app (ad blocker above Proton, both above Settings).
- **S088** Wire the UI to the runtime through `window.psycho` namespaces; one typed channel per action; no UI-to-broker shortcuts.
- **S089** Implement the visual state machine: hover/pressed/focus/disabled for every control, keyboard navigation order, visible focus rings.
- **S090** Implement text scaling, zoom, and window-size responsiveness using the same CSS the web uses.
- **S091** Visual regression: per-category diffs at reference sizes plus 125% and 150% DPI.
- **S092** Interaction regression: scripted keyboard/mouse journeys for tabs, settings, palette, panels.
- **S093** Retire the Win32-drawn chrome scaffolding completely.

**Exit gate:** side-by-side screenshots of old and new settings pages are hard to tell apart, and every control changes real state.

### P4 - Network service (real web, part one)

- **S100** DNS: resolution policy, caching with TTL, IPv4/IPv6 preference, failure modes, IPv6-only fallback, Hosts file, proxy DNS rules.
- **S101** HTTP/1.1 client: request builder, header normalisation, chunked transfer, content-length mismatches, keep-alive pool, timeouts, cancellation.
- **S102** Content decoding: gzip, deflate, brotli (my own decoder with test vectors), charset detection, body size limits, truncation reporting.
- **S103** Redirects, retries, auth challenges (Basic, Digest where sites demand it, proxy auth), 3xx policy, loop detection.
- **S104** TLS via Schannel: handshake, SNI, ALPN, session reuse, certificate chain validation, pinning hooks, error taxonomy mapped to UI copy.
- **S105** HTTP/2: framing, my own HPACK, multiplexing, flow control, priorities, server push rejection, connection coalescing.
- **S106** Cookies: jar semantics (domain/path/secure/httponly/samesite), expiry, persistence, private separation, partitioning.
- **S107** Cache: HTTP cache semantics (freshness, validators, no-store/private), memory and disk tiers, revalidation, eviction.
- **S108** HSTS, mixed content, upgrade-insecure-requests, CSP enforcement basics, referrer policy, COOP/COEP/CORP headers.
- **S109** Proxy: fixed servers, bypass lists, PAC script evaluation, SOCKS5 (with auth), HTTPS proxies, fail-closed semantics, proxy auth prompts.
- **S110** Certificate error UI and override policy (with explicit risk copy), pinning failure handling, TLS version floor.
- **S111** Error taxonomy for the user: offline, DNS failure, refused, TLS failure, proxy failure, timeout, blocked, too-many-redirects - each with the stage named.
- **S112** Real-network test rig: scripted fetches against a real corpus on my machine, recorded headers/bodies, replayable offline.
- **S113** Network service tests: unit tests for each S100-S111 behaviour plus a live-internet suite that skips cleanly when offline.

**Exit gate:** `psychovault://nettest` passes every stage against the real internet on my machine, and a real page renders from the live network.

### P5 - Documents, scripts and the modern web (part two)

- **S120** HTML parser completeness: quirks handling, foster parenting, tables, templates, foreign content, encoding sniffing, fragment parsing.
- **S121** DOM API breadth driven by real pages: tree mutation, attributes/dataset, classList, forms, text, ranges, traversal, `NodeList`, live collections.
- **S122** Events: full event model with capturing, passive listeners, focus/blur, pointer/touch basics, `beforeunload` policy, `hashchange`/`popstate` routing.
- **S123** Script loading and execution order: parser-blocking vs deferred vs async, module scripts, inline handlers, error propagation, CSP hooks.
- **S124** JS engine level 1: ES2015 classes, modules, template literals, destructuring, spread, default/rest params, arrow scoping, `let/const` TDZ, generators/iterators, `for...of`.
- **S125** JS engine level 2: promises, `async/await`, microtask queue ordering, `Map/Set/WeakMap/WeakSet`, `Symbol`, `Proxy` where libraries require it, `Reflect` subset.
- **S126** JS built-ins breadth: `Object/Array/String/Number/Math/JSON/RegExp/Date/Error`, typed arrays and `ArrayBuffer`, `Intl` subset sufficient for dates and numbers.
- **S127** Web APIs level 1: `fetch`, `XMLHttpRequest`, `FormData`, `URL`, `URLSearchParams`, `Blob`, `File`, `atob/btoa`, `TextEncoder/TextDecoder`, timers.
- **S128** Web APIs level 2: `localStorage`, `sessionStorage`, `indexedDB` basics, `WebSocket`, `EventSource`, `history`, `location`, `navigator` surface.
- **S129** Web components: `customElements`, shadow DOM basics, slots, `MutationObserver`, `IntersectionObserver` basics.
- **S130** Workers: dedicated worker, module worker, message ports, termination; service worker registration and lifecycle (needed by MV3 and modern sites).
- **S131** Canvas/animation frame plumbing: `requestAnimationFrame` tied to the compositor clock.
- **S132** Media: image formats beyond PNG/JPEG (WebP, GIF animation, AVIF detection with graceful failure), audio/video via OS codecs with honest capability reporting, `<video>` controls.
- **S133** Forms and navigation: GET/POST form submission, file inputs, autofill hooks reserved for the vault, target handling, downloads triggered by navigation.
- **S134** Accessibility baseline: role/name computation for my own UI, tab order, screen-reader exposure of controls, high-contrast mode.
- **S135** Site corpus workstream: a curated list of real pages (documentation, shopping, social, video, mail, maps), each with a screenshot, a failure list and a fix list, re-run after every engine change.

**Exit gate:** at least the documentation tier and the settings/new-tab UI render correctly, and the corpus list shows which sites work, which partly work and which fail - published honestly.

### P6 - Storage, profile and migration

- **S140** Profile layout: versioned directories, atomic writes, journal, backup rotation, corruption recovery.
- **S141** Settings store: typed schema with migration from the legacy 59 default keys; no silent defaults changing meaning.
- **S142** Bookmarks, history, downloads, session, workspaces, service rail, icons: real stores with the legacy shapes preserved.
- **S143** Vault: port the existing vault to the runtime, keep scrypt(N=16384,r=8,p=1)+AES-256-GCM compatibility, migration from `vault.bin`/`vault.key.json`, auto-lock, backup/restore, CSV import/export.
- **S144** Migration tool: read an old profile in read-only mode, copy into the new layout, verify counts, never delete the source, produce a migration report.
- **S145** Data clearing: the six legacy clear buttons with exact scopes and confirmation copy.
- **S146** Storage service tests: atomicity under simulated power loss, migration fixtures from a real legacy profile, vault round-trips.

**Exit gate:** my old profile migrates, the vault unlocks with the old master password, and nothing is lost on an interrupted write.

### P7 - Feature parity (all of it, wired)

- **S150** Tabs: new/close/activate/reopen/pin/mute/move/drag, tab previews, recently closed, restore-on-launch; match pinned-tab ordering, middle-click close, double-click pin, context actions and keyboard navigation. Implement tab islands: child tabs join the opener's group and group collapse never hides the active tab; test collapse/expand without requiring collapse state to survive restart.
- **S151** Workspaces: create/switch/rename/close/menu, per-workspace tab sets, persistence, keyboard shortcuts. Separately implement named containers with per-tab cookie, localStorage and cache isolation; containers are not workspaces and require cross-container leak tests.
- **S152** Private windows: full flow including downloads policy, no history writes, separate cookie jar.
- **S153** Split view and panel layout with the reference geometry.
- **S154** Omnibox: URL detection, search, suggestions from history/bookmarks, multiple validated custom search shortcuts, keyboard selection and tab-to-search.
- **S155** Bookmarks and history UIs with import/export.
- **S156** Downloads UI: list/control/clear/open/reveal/directory picker/save-as dialog.
- **S157** Find in page, zoom controls, page translation (or an explicit, honest removal if the old provider cannot be used lawfully - decided and documented, not silently dropped).
- **S158** Rail services: catalogue and custom URLs, add/remove/reorder, real favicons/brand colours, open as normal tabs, open-in-window/reload/copy/remove actions and ordering rules.
- **S159** Performance panel: live per-tab CPU/memory, hot tabs, media and sleep exclusions, CPU limiter, download bandwidth cap (do not imply general traffic shaping), battery-saver transitions, media keys/auto-pause, sidebar services, panic button, PNG/PDF capture and data-usage table. Panic must not persist browsing state while active.
- **S160** Ad and tracker blocker: my 262-domain list, per-site shield with counters, allow-list, custom domains, bundled toggle.
- **S161** Site permission editor (exact-origin camera/microphone/location/notifications, save/remove/default), global denied-permission list, container assignment UI, session recovery (normal windows only; never private), error log, diagnostics, proxy status/self-test and VPN-check entry points.
- **S162** Keyboard shortcuts and context menus for every surface, documented in the app. Cover tabs, links, images, selections and text fields (including cut/copy/paste/select-all), focus/keyboard journeys, and accessibility names/states.
- **S163** Legacy parity matrix: every control and behavior row marked passed/failed/blocked/not run with a named test and evidence link. Trace 125 controls, 59 defaults, eight dynamic editors, six clear actions, 21 namespaces/80 invokes/105 handlers plus events, 15 data stores and all 23 visual captures; do not count a present control or API stub as passed.
- **S164** Reference spelling workflow: page-field underlines, right-click suggestions and add-to-dictionary, bulk word editing, and honest language support; test editable fields and user-word overrides.
- **S165** Vault workflow parity: generator, entry CRUD, reveal/copy, auto-lock, duplicate-safe CSV import, encrypted backup/restore, and manual HTTPS fill only for the saved host (including the documented `www` equivalence), never HTTP or a different origin. Add same-host/lookalike/cross-origin and secret-redaction tests.
- **S166** Translation interaction and privacy contract: choose the configured target, ask before an external provider receives page data/URL, disclose the provider, open the translated result separately and retain the source page; if no lawful provider is available, show a truthful unavailable state and document the deviation.
- **S167** Verify sidebar-service workflows and global media routing: favicon/brand, open/new-window/reload/copy/remove, media keys, and auto-pause when another tab starts audio; include normal/private session and origin checks.

**Exit gate:** the parity matrix has no "empty handler" and no untested control.

### P8 - Extension engine (general, MV3 + legacy MV2)

**Entry gate: a published completion audit of everything before this point**
(M0-M7 exit gates all green, conformance counts published, one clean full
build, every test passing, the evidence table filled in). Extensions start
only when that audit exists - by the standing rule that audit comes first,
then build and test, then extension work.

- **S170** Manifest parsing/validation for MV2 and MV3, permissions, host permissions, CSP, web-accessible resources, `key` handling, stable IDs.
- **S171** Install pipeline: directory install, CRX3 unpacking with signature check, enable/disable/remove/restore, update check, provenance record.
- **S172** Extension service worker runtime: registration, events, wake/sleep, alarms, `fetch`, `importScripts`, termination, crash recovery.
- **S173** Content scripts: match patterns, `run_at`, isolated worlds, DOM access boundaries, message passing with the page and the worker.
- **S174** Extension pages: popup, options, side panel rendered by my engine with extension-origin privileges and strict CSP.
- **S175** Core APIs: `runtime`, `storage` (local/sync-as-local/session), `tabs`, `windows`, `scripting`, `notifications`, `idle`, `action/browserAction`, `contextMenus`, `alarms`, `cookies`, `downloads`, `webNavigation`.
- **S176** `webRequest` with blocking semantics where needed, rule evaluation order, auth challenges (`onAuthRequired` async blocking exact-challenger rule).
- **S177** `proxy` API: settings get/set/clear, level of control, ownership, change/error events, PAC support, rollback semantics.
- **S178** `privacy` API for WebRTC policy wired to the real RTC policy implementation.
- **S179** Extension manager UI: list, details, permissions display, toggle, remove, restore, update, error log.
- **S180** Compatibility rig: my own test extensions per API, a real-world MV3 extension (Dark Reader class), and the reference's bundled Proton VPN, proxy-switch and Dark Reader-class compatibility subjects; record capability-level results without copying their code.
- **S181** Extension isolation tests: an extension cannot reach the profile, vault, other extensions' storage or another site's data without permission. Also test proxy/privacy/tab permission denial, exclusive proxy ownership, rollback after partial proxy failure, bounded/rejecting CRX archive parsing and download open/reveal path validation.

**Exit gate:** Proton installs, opens its popup, and a third-party extension I add works within its permission set.

### P9 - Proton VPN, for real

- **S190** Confirm the extension contract: which proxy settings Proton's build sets, what auth challenge it answers, what state it reports.
- **S191** Implement my own WireGuard client: Curve25519, ChaCha20-Poly1305, BLAKE2s, Noise IK handshake, transport keys, keepalive, rekey - against published test vectors.
- **S192** Implement my own tunnel daemon: config ingestion, endpoint selection, UDP transport, packet routing, MTU handling, reconnect/backoff.
- **S193** Implement my own local proxy front-end: SOCKS5 and HTTP CONNECT listeners, auth, per-connection accounting, fail-closed when the tunnel is down.
- **S194** Wire `chrome.proxy` from the extension to my proxy service with truthful ownership, status and teardown.
- **S195** DNS policy under the tunnel: no leaks (all lookups through the tunnel or the proxy's resolver), IPv6 handling, leak test page.
- **S196** WebRTC leak policy enforcement and its UI disclosure.
- **S197** Kill switch and recovery: network change, sleep/resume, tunnel drop, browser restart, extension crash - traffic never escapes unproxied.
- **S198** Proton account flow: sign-in through the extension, session persistence, certificate/config retrieval, no credential logging.
- **S199** Local test harness: mock proxy with controlled failures (bad auth, timeout, TLS error, mid-session drop) and assertions per failure.
- **S200** Live test: real Proton account, connect, verify exit IP through the tunnel, verify fail-closed, disconnect cleanly. Blocked without an account - reported as blocked, never as passed.

**Exit gate:** the VPN test report shows a real exit-IP change and a fail-closed proof, and the local harness proves every failure mode.

### P10 - Installer and updater (the thing I actually install)

- **S210** Design the install layout: `%LOCALAPPDATA%\PsychoVault` per-user install (no admin), Program Files option for machine-wide, payload manifest with hashes.
- **S211** Build my own `PsychoVault-Setup-<version>.exe`: welcome page, licence, directory choice, progress, finish, cancel-safe, no partial installs.
- **S212** Registration: uninstall entry in Apps & Features, Start Menu folder, desktop shortcut, file associations (http/https/html), optional default-browser registration with the Windows prompt.
- **S213** Uninstaller: removes files and shortcuts, keeps profile unless asked, removes registrations, leaves no orphans.
- **S214** Silent and repair modes: `/S`, `/repair`, `/uninstall`, exit codes documented.
- **S215** Upgrade path: in-place upgrade preserving profile, rollback on failure, version stamping, stale-file cleanup.
- **S216** Updater: signed manifest verification, access-token state for private feeds, no download before explicit consent, hash-checked installer, staged install, backup/rollback, progress, and reference banner actions (Install/Later/dismiss) with accurate failure reasons. Keep updater payload handling separate from ordinary Save As downloads.
- **S217** Installer tests: clean VM install, upgrade over old version, uninstall, repair, silent install - recorded with screenshots and logs.
- **S218** First-run experience: profile creation, migration offer, default settings, privacy explanation, no telemetry.

**Exit gate:** a clean Windows machine installs from my setup exe, appears in Apps & Features, launches, upgrades and uninstalls correctly.

### P11 - Performance and robustness

- **S220** Budgets: cold start, new tab, tab switch, scroll smoothness, memory per tab, CPU idle, installer size - measured, published, regressions flagged.
- **S221** Startup work: deferred services, parallel profile load, lazy extension host, precompiled UI assets.
- **S222** Rendering performance: layout/paint profiling, damage tracking, tile cache, compositor scheduling, off-main-thread raster where it pays.
- **S223** JS performance: bytecode, inline caches, hidden classes, GC tuning; JIT only after correctness gates.
- **S224** Memory: per-tab accounting, tab discarding, cache limits, leak hunting with stress journeys.
- **S225** Fuzzing and hardening: HTML/CSS/JS/URL/WireGuard parsers, malformed input corpora, crash triage.
- **S226** Sandbox verification: a renderer exploit test suite that must fail to read files, sockets, or the vault.
- **S227** Long-run stability: 24-hour session with tabs, video, VPN, extensions; no leaks, no hangs.

**Exit gate:** measured budgets met or honestly missed with numbers, and the sandbox suite passes.

### P12 - Conformance and honesty

- **S230** Pin WPT and Test262 revisions; publish pass/fail/skip by subsystem, updated per engine change.
- **S231** Publish the site corpus report with screenshots and known failures.
- **S232** Publish the parity matrix (settings, features, extensions, VPN) with evidence links and a refreshed `Psycho_REFERENCE-CROSSWALK.md`; list reference-only differences and security-preserving deviations explicitly.
- **S233** Publish performance numbers next to the old app's measured numbers.
- **S234** Rewrite release notes in my voice: what works, what does not, what was not tested.

**Exit gate:** every claim in the notes points at a reproducible test.

### P13 - Release

- **S240** Re-enable CI for the gates that are now real: engine unit tests, golden images, runtime integration, installer smoke (manual-dispatch Windows job).
- **S241** Build the installer in CI, verify hashes, attach artefacts, sign the manifest.
- **S242** Publish `PsychoVault-Setup-<version>.exe` with checksums and notes.
- **S243** Install from the published artefact on a clean machine and use it for a day before calling it a release.

### P14 - After Windows

- **S250** Android platform layer on the same engine and runtime contracts (deferred until Windows milestones are met).

## 7. Rules I work by

1. **Runtime first, then UI, then web breadth.** No new features on top of a broken layer.
2. **One workstream at a time.** Finish a step's evidence before opening the next.
3. **Evidence or silence.** Every step lists the test that proves it. Compiles ≠ works; renders ≠ correct; installed ≠ tested.
4. **No CI builds until M2.** Local builds and local tests only, then the workflow comes back with real gates.
5. **Scaffolding is labelled.** Anything temporary is marked in code and removed on schedule.
6. **The screenshots are the spec.** If my UI differs, my UI changes - not the reference.
7. **The extension boundary stays clean.** Third-party extension code is never merged, never trusted, always permission-gated.
8. **Never destroy user data.** Migration reads first, writes second, keeps backups.

## 8. What I am not doing

- Not using Electron, Chromium, Gecko, WebKit, Servo, WebView2, CEF, Node or any browser runtime.
- Not renaming, vendoring or laundering another engine.
- Not shipping a portable exe again.
- Not claiming a feature because a control exists or a status label changed.
- Not running builds in GitHub Actions to generate the appearance of progress.
- Not promising a specific date for universal Chrome parity - I promise measured, milestone-by-milestone progress instead.

## 9. Honest ceilings

- **Universal site fidelity:** achievable only asymptotically; the corpus report will always list failures. I will keep the list visible instead of hiding it.
- **DRM video:** Widevine-class DRM cannot be implemented by me; streaming services that require it will not play. I will say so in the UI when it happens.
- **WireGuard and TLS:** TLS is Windows Schannel (OS API, allowed). WireGuard I implement myself from the specification and test vectors, which is real work but bounded.
- **Extensions:** MV3 coverage grows extension by extension. `chrome.*` surface that an extension uses but I have not implemented will be reported per extension, not hand-waved.

## 10. First action after this plan

Start P1/S020-S021: create `runtime/` with the broker/renderer skeleton and the typed IPC bus, and the `psychovault://nettest` host path so the first real-network evidence can be produced on my machine. No CI, no release, no public claim until M1's installer installs on my machine.
