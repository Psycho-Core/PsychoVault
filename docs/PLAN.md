# My build plan for the owned PsychoVault browser

This is my plan for rebuilding PsychoVault as my own browser. I am writing it before implementation so I do not repeat the mistake of calling a wrapper, a popup, or a compile success a finished browser.

Status: planning complete, implementation not started. This document is a contract and a sequence of gates. It is not evidence that the engine, Proton, the installer, or Android already works.

## 1. My definition of done

I am done when all of these are true:

1. The browser runs on Windows x64 without Electron, Chromium, Gecko, WebView, Node.js as a browser runtime, or another embedded browser engine.
2. The browser shell, HTML engine, DOM, CSS engine, JavaScript engine, layout, paint, networking, storage, extension engine, vault, updater and process model are written for PsychoVault.
3. The interface matches my saved legacy interface at the reference sizes and states, including the new-tab/home page and all ten settings categories.
4. Every feature and setting in `docs/FEATURE-INVENTORY.md` has a working implementation, a persistence rule, and an acceptance test.
5. Proton VPN ships as the standard bundled extension and passes live sign-in, proxy routing, proxy authentication, fail-closed, DNS and WebRTC tests.
6. My old profiles, workspaces, bookmarks, history, downloads, sessions, settings and vault can be migrated without data loss.
7. The browser passes the security, privacy, stability and performance gates in this plan.
8. GitHub Actions builds the installer, runs the checks, signs the update manifest, publishes checksums and release notes, and does not require me to compile locally.
9. The public documentation is short, honest, written in my voice, and does not pretend a legacy screenshot is a new-engine screenshot.
10. Android is either explicitly deferred or passes the same ownership and feature gates on a real device. I will not call a source port a finished Android browser.

## 2. My non-negotiable requirements

| ID | Requirement | Acceptance evidence |
| --- | --- | --- |
| R1 | Match the old application's responsiveness without using its runtime | Side-by-side performance report on the same hardware, or absolute results with the comparison marked not run |
| R2 | Keep exactly the same UI/interface | Visual regression suite against the reference screenshots, full-page settings captures, keyboard/focus/DPI checks |
| R3 | Keep all previous features and settings | Feature inventory traceability, 125 settings controls, 59 default settings, 105 old IPC behaviours mapped to new services |
| R4 | Proton VPN works and is the standard bundled extension | Live Proton account test, proxy auth test, routing test, fail-closed test, DNS/WebRTC leak report |
| R5 | Every line of browser code is written for this project | Source/provenance audit, dependency audit, no vendored browser engine, no runtime crate that supplies browser behaviour |
| R6 | Every document sounds like me | Editorial review of README, PSYCHO, plan, research, audit, release notes and failure notes |
| R7 | My project guide is called `PSYCHO.md` | File exists at repository root and is the first instruction file for agents |
| R8 | Research and plan before building | `docs/RESEARCH.md`, this plan, pinned sources, rejected options and open risks |
| R9 | Audit and verify the plan | `tools/audit-plan.py`, `docs/PLAN-AUDIT.md`, requirement coverage and inventory checks |
| R10 | Report back with a summary and PR/merge | PR link, merge commit, checks run, limitations and next implementation step |
| R11 | Preserve user data and update trust | Migration fixtures, rollback tests, signature/hash verification tests, no deletion of user files |
| R12 | Free GitHub-hosted builds, one heavy build at a time | Workflow concurrency group, build logs, release assets, failure notes, no parallel heavy builds |

## 3. Architecture I am choosing

I am choosing Rust for the browser core and Windows shell. I want memory safety, explicit ownership, fearless concurrency where it is real, and a compiler that stops large classes of engine bugs before they ship. I am not choosing Rust because it makes the engine easy. It does not. I am choosing it because writing a browser in an unsafe language would add risk I do not need.

I will use the Rust compiler and standard library. I will use Windows platform APIs through hand-written, reviewed FFI declarations. I will not add third-party runtime crates that supply HTML, CSS, JavaScript, TLS, networking, layout, rendering, extension behaviour, or sandboxing. Build-time tools may exist, but they must be pinned, audited, and kept out of the shipped browser.

The first target is Windows x64. Android comes later through the same core, not through a separate third-party browser.

### 3.1 Process model

I want a multi-process browser:

| Process | Responsibility | Trust level |
| --- | --- | --- |
| Broker | Window ownership, policy, permissions, profile access, process lifecycle, update handoff | Most privileged, still least-privilege where possible |
| Renderer | HTML/DOM/CSS/JS/layout/paint for a site instance | Sandboxed, no direct profile or arbitrary network access |
| Extension worker | MV3 service worker and extension event dispatch | Sandboxed, permission-gated, separate from renderer |
| Network service | DNS, HTTP, TLS, proxy, PAC, cookies, cache, HSTS, request filtering | Broker-controlled, no renderer-supplied raw socket access |
| Storage service | Profiles, history, bookmarks, downloads, vault, extension storage | Broker-controlled, transactional |
| GPU/compositor | Surface composition, damage, vsync, raster backend | Sandboxed where practical, validated shared surfaces |

A site instance gets a renderer process based on site, origin policy, frame tree and resource limits. Extensions do not share a renderer with websites. Internal pages do not get website privileges. Private browsing uses separate in-memory stores and separate process policy.

### 3.2 Core modules

I will keep the engine modular so I can test each contract before integration:

| Module | What I own |
| --- | --- |
| `pv-base` | Strings, buffers, error types, time, logging policy, deterministic test helpers |
| `pv-url` | URL parsing, serialization, origin, scheme rules, IDNA boundary, internal `psychovault://` rules |
| `pv-net` | DNS policy, DoH, HTTP/1.1 and HTTP/2, connection pooling, TLS via Schannel, proxy, PAC, cookies, cache, HSTS, Fetch semantics |
| `pv-html` | Tokenizer, tree construction, encoding sniffing, fragment parsing, forms, scripts, navigation hooks |
| `pv-dom` | Nodes, trees, ranges, selectors, events, mutation observers, shadow DOM, browsing contexts |
| `pv-css` | Syntax, selectors, cascade, inheritance, values, computed style, animations, media queries |
| `pv-layout` | Box tree, inline layout, block layout, flexbox, grid, tables, positioning, overflow, fragmentation, text layout |
| `pv-paint` | Display lists, stacking contexts, clips, transforms, filters, images, SVG, canvas, rasterization |
| `pv-js` | Lexer, parser, AST, bytecode, interpreter, garbage collector, builtins, modules, promises, jobs, JIT only after correctness |
| `pv-bindings` | Web IDL-driven binding contracts, expose rules, realm checks, permission checks |
| `pv-storage` | Transactional profile store, history, bookmarks, downloads, sessions, cookies, IndexedDB, extension storage |
| `pv-vault` | Vault encryption, OS-keychain migration, master-password migration, backup/restore, auto-lock |
| `pv-ext` | MV3 manifest, install lifecycle, permissions, service worker, popup, content scripts, messaging, APIs |
| `pv-proxy` | Extension proxy ownership, PAC evaluation, fixed servers, bypass rules, auth challenges, fail-closed policy |
| `pv-rtc` | WebRTC policy, ICE candidate filtering, UDP policy, leak tests; full RTC media only when gated |
| `pv-shell-win` | Win32 windows, input, IME, clipboard, dialogs, notifications, tray/taskbar identity, accessibility |
| `pv-ui` | My chrome layout, tab strip, sidebar, omnibox, panels, settings, new tab, dialogs, themes |
| `pv-update` | Signed manifest verification, download, hash check, installer handoff, backup, rollback |
| `pv-ipc` | Typed messages, capability handles, schema validation, process identity, back-pressure |

I will not create empty modules and call them implemented. A module is implemented only when its acceptance tests pass.

### 3.3 Rendering and text

I will use DirectWrite for font enumeration, glyph data and text measurement boundaries. I will use Direct2D/Direct3D as platform graphics APIs where appropriate. I still own layout, line breaking, paint order, damage, compositing and the browser's rendering pipeline.

I will start with a correct software/raster path and a simple compositor, then add GPU acceleration behind the same display-list contract. I will not make GPU acceleration a prerequisite for correctness.

### 3.4 Networking and TLS

I will use Schannel for TLS and Windows certificate validation. I will not write my own TLS record layer, cipher suites, or certificate verifier. I own the browser behaviour around TLS: origin checks, mixed content, HSTS, certificate error UI, proxy TLS, connection reuse, and request scheduling.

I will implement HTTP/1.1 first, then HTTP/2. HTTP/3/QUIC is a later gate unless a required Proton or site-corpus test proves it is needed earlier. I will not fake HTTP/3 by relabelling HTTP/2.

### 3.5 Cryptography and vault

I will use Windows CNG for AES-GCM, hashing, random bytes and signature verification where supported. I will use DPAPI for the old OS-keychain vault path where that is what the old data requires. I will not invent a block cipher, a hash, a signature scheme, or a password KDF.

For the master-password path, I must preserve compatibility with the old scrypt parameters `N=16384`, `r=8`, `p=1` and AES-256-GCM. If Windows CNG does not provide scrypt directly, I will implement only the scrypt construction over reviewed primitives and test it against known vectors and old fixtures before migration. I will not change the KDF silently during migration.

## 4. UI parity plan

I want the same interface, not a new interpretation. The reference archive is `docs/reference/legacy-ui/`. The old geometry included an 84px chrome height, 44px rail, 22px status bar and 330px panel. I will treat those as starting measurements, then verify against screenshots at multiple DPI settings.

I will build the chrome UI as a native retained-mode interface drawn by my own UI layer. Internal pages may be rendered by my engine once it is capable, but they must not depend on a third-party browser component. The new-tab page, settings, vault, downloads, history, bookmarks, Performance and VPN checks must match the reference layout, spacing, colours, typography, icons and focus states.

I will keep the settings categories unchanged:

| Category ID | Label I will keep |
| --- | --- |
| `general` | General |
| `privacy` | Privacy & security |
| `downloads` | Downloads |
| `extensions` | Extensions |
| `performance` | Performance |
| `appearance` | Appearance |
| `spelling` | Spelling |
| `passwords` | Passwords |
| `accessibility` | Accessibility & language |
| `about` | About |

My UI gates:

- Pixel-diff tests at 1600x1000 and 1440x940, plus at least one high-DPI case.
- Full-page settings captures for all ten categories, not only the first viewport.
- Keyboard navigation, focus rings, screen-reader names, high contrast and reduced motion.
- Tab overflow, pinned tabs, private badge, workspace chips, split view, command palette, find bar, update banner and toast states.
- Sidebar order: ad blocker above Proton, both above Settings. The resource panel is named Performance.
- No legacy screenshot may be published as a new-engine screenshot.

## 5. Feature parity plan

I will use `docs/FEATURE-INVENTORY.md` as the contract. Every old IPC handler must map to a new typed service call or be explicitly removed with my approval. I do not want silent feature loss.

The feature groups are:

1. Tabs and windows: new, close, activate, reopen, pin, mute, move, drag, private windows, split view, session restore.
2. Workspaces: list, switch, create, close, rename, menu, persistence, private-window exclusion.
3. Navigation: omnibox, suggestions, custom search shortcuts, back/forward/reload/stop/home, internal pages.
4. Bookmarks and history: add/remove/list/search/clear, private exclusion, migration.
5. Downloads: list, pause/resume/cancel, open/reveal only for recorded paths, directory choice, Save As, notifications, migration.
6. Privacy: ad blocking, bundled/custom domain lists, site exceptions, third-party cookies, DNT/GPC, autoplay, referrer stripping, DoH, WebRTC policy, containers, per-site permissions, clear-on-exit, data usage and clearing.
7. Extensions: install unpacked/CRX, enable/disable/remove, popup/options, content scripts, storage, messaging, permissions, Proton restore, proxy status and self-test.
8. Performance: CPU limit, battery saver, sleeping tabs, exclusions, bandwidth cap, hot tabs, media controls, panic button, snapshots, sidebar services.
9. Appearance: themes, accent, sidebar visibility, mods, video/image enhancement, sounds, media keys, services, speed dial, favicons.
10. Spelling: built-in checker, languages, dictionary source, user words, context-menu suggestions.
11. Passwords/vault: enable, auto-lock, master password, OS-keychain path, unlock/lock, entries, generate, backup/restore, CSV import, migration.
12. Accessibility/language: translation target, high contrast, reduced motion, tab close visibility, IME and screen-reader support.
13. About/updates: version, error log, diagnostics, update check/install/later/dismiss, token state, signed manifest, rollback.
14. Recovery: crash recovery, interrupted migration recovery, running marker, safe restart.

A setting is not done when it renders. It is done when changing it alters behaviour, persists correctly, migrates correctly, and has a test.

## 6. Web-platform implementation sequence

I will build the engine in correctness-first slices:

1. URL, encoding, Fetch data model, HTML tokenizer and tree construction.
2. DOM tree, events, selectors, forms, navigation and session history.
3. CSS syntax, cascade, values, box model, block/inline layout, text and paint.
4. JavaScript parser, interpreter, core builtins, promises, modules and job queue.
5. Flexbox, grid, tables, positioning, overflow, transforms, transitions and animations.
6. Canvas, SVG, images, fonts, media boundaries and accessibility tree.
7. Workers, Service Workers, IndexedDB, Web Crypto, Notifications, XHR and Streams.
8. WebRTC policy and media only after the network and sandbox gates are stable.
9. Performance work: bytecode, inline caches, JIT, layout caching, raster caching, compositor scheduling and tab discarding.

I will pin WPT and Test262 revisions. I will publish pass/fail/skip counts by subsystem. I will not claim full compatibility from a curated subset.

## 7. Extension engine plan

I need a general MV3 extension engine, not a Proton-only shim.

The extension host must provide:

- Manifest parsing and validation, including MV3 service worker, action, permissions, host permissions, CSP, externally connectable matches and web-accessible resources.
- Stable extension identity and key handling.
- Install, enable, disable, remove, restore and update lifecycle.
- Sandboxed service worker with event registration, lifecycle, alarms/timers policy, fetch, importScripts and termination rules.
- Popup and options pages rendered by my engine with extension-origin privileges.
- Content scripts with isolated worlds, match patterns, run_at rules and message passing.
- Permission prompts and default-deny enforcement.
- Storage areas with profile/private separation.
- Tabs, windows, scripting, runtime, notifications, idle, action/browserAction and webRequest surfaces required by the inventory.
- Proxy API with ownership, level-of-control, change events, error events, rollback and clear semantics.
- Privacy API for WebRTC policy, backed by the real network/RTC policy.

I will test with small extensions I write for each API, then with Proton. A Proton popup loading is only an early smoke test.

## 8. Proton VPN plan

Proton is the standard bundled extension. I will keep it separately identified, licensed and sandboxed. I will pin the extension source and build output, verify hashes, and provide the corresponding source and notices required by its licence.

The Proton gate requires:

1. The pinned Proton extension installs and starts its MV3 service worker.
2. The popup renders and sign-in works with a real Proton account.
3. `chrome.proxy.settings.get/set/clear` reports truthful ownership and state.
4. Fixed-server and PAC modes behave correctly, including split tunnelling and bypass rules.
5. HTTPS proxy connections use the expected proxy host and port, including the 4443 range and Secure Core 443 where applicable.
6. `webRequest.onAuthRequired` with `asyncBlocking` supplies credentials only to the exact known proxy challenger.
7. Bad credentials, proxy timeout, proxy TLS failure and network change fail closed for proxied traffic.
8. DNS behaviour matches the proxy policy. I will test for local DNS leaks where the proxy is supposed to resolve the target.
9. WebRTC policy prevents unauthorized local/public candidate leaks according to the selected policy.
10. Disconnect, disable, remove, restore and crash recovery leave the proxy in a truthful state.
11. The browser discloses Proton's own extension behaviour, including any telemetry it performs. I will not claim extension traffic is PsychoVault telemetry, but I also will not hide it.

I will run a local test proxy with controlled authentication and failure modes before live Proton testing. Live Proton testing needs an account and network access. If it is not run, I will mark it not run.

## 9. Security and privacy plan

My security gates:

- Renderer and extension processes run in AppContainer or an equivalent restricted sandbox with job objects and restricted handles.
- IPC messages are typed, size-limited, schema-validated and bound to process identity.
- Websites cannot access broker, vault, extension, filesystem or privileged internal APIs.
- Extension permissions are enforced at the dispatcher, not only in the UI.
- Proxy credentials are never exposed to websites or logged.
- Downloads open/reveal only recorded paths.
- CRX/ZIP installation has entry-count, path, compressed-size and uncompressed-size limits.
- Internal pages use strict CSP and separate privileges.
- Updates require a signed manifest and hash verification before install.
- Vault keys are zeroized where practical and never written to logs.
- Crash reports and diagnostics are redacted by default.
- Fuzzing covers URL, HTML, CSS, JS parsing, IPC, manifest, CRX/ZIP, PAC and proxy auth.
- Sandbox escape tests cover handle leakage, filesystem access, network bypass, process injection and privilege escalation.

My privacy gates:

- No PsychoVault telemetry by default.
- Private browsing does not persist history, cookies, cache, site storage, extension storage or session data unless I explicitly choose a design exception.
- Clear-on-exit and per-site permissions are enforced by the storage/network layers.
- DoH, WebRTC, referrer and cookie settings change real request behaviour.
- The ad blocker is local and does not make remote calls.
- Extension network behaviour is disclosed separately.

## 10. Migration plan

I will migrate data before I ask anyone to replace the old browser.

The migration gates:

- Read old `settings.json` schema versions and apply deterministic upgrades.
- Preserve bookmarks, history, downloads, sessions, workspaces and extension state.
- Read both vault paths: OS-keychain and master-password.
- Preserve encrypted backup compatibility or provide a tested one-way migration with backup.
- Recover interrupted migrations using a journal.
- Never delete the old profile during migration.
- Run migration against synthetic fixtures and, with permission, a copied real profile.
- Produce a migration report with counts, skipped records and recovery state.

I will not migrate by importing the old Electron runtime. I will read the data formats and write my own migration code.

## 11. Performance plan

I want the browser to feel like the old application. I will measure before I claim.

The performance gate:

- Cold start, warm start, new tab, tab switch, omnibox latency, scroll frame pacing, idle CPU, idle memory, 10-tab memory, 30-tab memory, sleep/wake, page load, extension overhead and shutdown are measured.
- Tests run on the same hardware, power mode, DPI and network conditions.
- The old application is used only as a reference benchmark where available, not as a shipped runtime.
- Results include median, p95 and worst observed value.
- A regression against the reference blocks release unless I explicitly accept it.
- If the reference cannot be run, the report says so and uses absolute budgets.

I will not claim low RAM because the architecture looks smaller. I will not claim fast because a hello-world window opens quickly.

## 12. Build and release plan

I want free GitHub-hosted builds. The first release workflow will:

1. Run on Windows x64 with a pinned Rust toolchain and pinned actions.
2. Use one concurrency group for heavy builds so only one runs at a time.
3. Run formatting, lint, unit tests, engine conformance subsets, extension tests, migration tests and sandbox tests.
4. Build the browser and installer.
5. Run installer, update, rollback and smoke tests.
6. Generate checksums, signed update manifest, release notes and failure notes.
7. Publish a release only when the required gates pass.
8. Cache toolchains and dependencies by exact version and source hash, never by a mutable label alone.

I will keep `PsychoVault_easy_build.bat` as a double-click entry point that delegates to the supported build path. It must not ask me to move my checkout from `D:\Blakes_SHIT\#Psycho-core_REPOS\PsychoVault`.

I will not publish a release because a PR merged. I will publish because the build and tests passed.

## 13. Android plan

Android is not the first release. I will reuse the owned core and write the Android platform layer myself: windowing, input, IME, clipboard, notifications, storage, process model and packaging. I will not use WebView or another browser as the engine.

Android is done only when the same ownership, UI, feature, Proton, security, privacy, performance and migration gates pass on a real device. Until then, I will call it deferred or experimental.

## 14. Delivery phases

| Phase | What I build | Exit gate |
| --- | --- | --- |
| P0 | Guide, research, inventory, plan, audit | This plan passes audit and is merged |
| P1 | Repo skeleton, CI, IPC, process broker, sandbox smoke test | Hello-world renderer is sandboxed and cannot read profile or network directly |
| P2 | URL, Fetch model, HTML/DOM, CSS block/inline, JS interpreter core | Local corpus renders and passes pinned WPT/Test262 subsets |
| P3 | Native chrome UI, tabs, windows, navigation, new tab, settings shell | Visual parity for chrome and new tab; settings navigation matches reference |
| P4 | Network service, TLS, cookies, cache, proxy, PAC, storage | HTTP/HTTPS/proxy tests pass; fail-closed proxy test passes |
| P5 | Extension host and Proton integration | Proton installs, signs in, routes, authenticates and fails closed in live test |
| P6 | Full feature parity, vault, migration, updates, recovery | All inventory items pass; migration and update rollback pass |
| P7 | Performance, security, privacy, accessibility hardening | Measured gates pass; fuzzing and sandbox tests pass |
| P8 | Windows release candidate | Installer, updater, release notes, checksums and signed manifest pass |
| P9 | Android platform layer | Real-device gates pass or Android remains explicitly deferred |

I am not giving a fake calendar estimate. The sequence is fixed; the duration depends on measured engine progress. I will report evidence at each gate.

## 15. Risks and how I am handling them

| Risk | Handling |
| --- | --- |
| Modern web compatibility is enormous | Pin specs/tests, build subsystem gates, publish honest coverage, do not claim full compatibility early |
| JavaScript performance | Correct interpreter first, then bytecode, caches, JIT and profiling; no JIT before correctness |
| CSS layout complexity | WPT layout suites, reference corpus, incremental layout only after correct layout |
| Proton auth/proxy behaviour | Local controlled proxy first, live Proton gate, fail-closed default, exact challenger checks |
| Sandbox escape | AppContainer/job objects, typed IPC, handle audits, fuzzing, escape tests |
| Vault migration loss | Fixtures for both paths, journal recovery, backup before migration, no old-profile deletion |
| UI drift | Screenshot contract, full-page captures, pixel and interaction tests |
| Build time | GitHub-hosted Windows, one heavy build, pinned toolchain, correct caching, no Chromium-scale dependency tree |
| Licence/provenance | Own code audit, extension notices, pinned Proton source, no copied browser engine |
| Overclaiming | Every report separates passed, failed, blocked and not run |

## 16. What I am not doing

- I am not using Electron as the new browser runtime.
- I am not embedding Chromium, Gecko, WebView, Servo, WebKit or another browser engine.
- I am not using Node.js as the extension or page runtime.
- I am not copying another browser's source and renaming it.
- I am not shipping empty functions, fake VPN status, fake performance numbers or test-only browser APIs.
- I am not deleting user profiles, vaults or installers to make a clean build.
- I am not calling a merged PR a working browser.
- I am not calling a successful compile a Proton test.
- I am not calling Android done because the core compiles.

## 17. Immediate next step after this plan

The next implementation step is P1: create the Rust workspace, the typed IPC boundary, the broker/renderer process split, the Windows sandbox smoke test, and the first GitHub Actions check. I will not start P2 until P1 proves that a renderer can be isolated and that the build pipeline is honest.
