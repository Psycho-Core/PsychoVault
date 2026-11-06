# My architecture

This is the map for the rebuild that starts at P1 of `docs/Psycho_MASTER-PLAN.md`. It describes my own Electron equivalent (the runtime) and my own Chromium equivalent (the engine), what already exists in this repository, and what each directory is for.

## 1. The shape of the product

```
PsychoVault-Setup-<version>.exe          (my installer, my payload format)
  └── %LOCALAPPDATA%\PsychoVault\
        PsychoVault.exe                  (browser launcher, broker, and sandboxed renderer-child image)
        PsychoVaultProxy.exe             (tunnel + local proxy daemon)
        ui\                              (my HTML/CSS/JS interface)
        extensions\Psycho-VPN-Proton\    (bundled extension, separate licence)
        resources\                       (icons, licences, blocklist)
  └── %LOCALAPPDATA%\PsychoVault\profile\...  (my data: settings, history, vault)
```

The user-facing install remains one `PsychoVault-Setup-<version>.exe`, and the installed browser has one launchable `PsychoVault.exe`. The broker starts renderer workers by re-launching that same image with a private `--pv-renderer-child` argument; Windows gives each child a separate restricted process and AppContainer. There is no separate renderer executable for the user or installer to manage. Nothing is portable-only, nothing unpacks to a temp folder and vanishes, and the extension payload is part of the installed product. Implementation status: the current Windows shell code launches two same-image renderer children and exercises brokered startup-frame probes; regular page tabs still use the legacy in-process session path. The AppContainer's profile-file/socket denials and real-process crash recovery remain unverified until a Windows run.

## 2. Process model

| Process | Owns | Trust | Talks to |
| --- | --- | --- | --- |
| **Broker** (`PsychoVault.exe`) | Windows, tab model, navigation, sessions, permissions, downloads, extension lifecycle, updates, policy | Highest. Never parses untrusted content. | Everything, over typed IPC |
| **Renderer child** (same `PsychoVault.exe`, `--pv-renderer-child`) | HTML/CSS/JS/DOM/layout/paint for one site instance, plus my UI pages | Intended: job object, restricted token, AppContainer with no network capability, no profile path | Broker only through typed IPC |
| **Network service** (thread in broker at M1, process at M8) | DNS, connections, TLS, HTTP, cookies, cache, proxy, HSTS | OS access to sockets only | Broker |
| **Storage service** (thread in broker) | Profile stores, vault, migration, atomic writes | File access to the profile only | Broker |
| **Extension host** (renderer-adjacent process per extension) | MV3 service worker, event dispatch | Sandboxed, permission-gated | Broker, through extension API surface |
| **Proxy daemon** (`PsychoVaultProxy.exe`) | WireGuard tunnel + local SOCKS5/HTTP proxy | Network only, no profile, no UI | Broker, by local socket with auth |
| **GPU/compositor** (thread, process later) | Surfaces, composition, vsync | Validated shared surfaces | Renderers + broker |

Design rules:

- A renderer never gets a file handle, a raw socket, or a window handle. It asks the broker through a capability-scoped channel.
- Internal UI pages (`psychovault://`) run in renderers but with a UI capability set: no web content may impersonate them, and UI pages may not be navigated to a web origin.
- Private sessions get their own renderers, in-memory stores and a policy flag the network service enforces.

## 3. The runtime (my Electron)

### 3.1 Objects the UI scripts see

Electron's app UI is HTML/JS talking to a preload bridge. Mine is the same shape with my names:

| Electron | Mine | Notes |
| --- | --- | --- |
| `BrowserWindow` | `ps.window` | create/close, resize events, focus, fullscreen, DPI |
| `WebContents` / `WebContentsView` | `ps.tab` | navigation, history, loading state, title/favicon |
| `ipcMain` / `ipcRenderer` | `ps.bus.invoke/handle`, `ps.bus.send/on` | typed channels, schema-checked, async by default |
| `contextBridge.exposeInMainWorld` | `ps.expose(name, api)` | frozen object, structured-clone values only |
| `session` | `ps.session` | cookies, cache, proxy, permissions, private flag |
| `app` | `ps.app` | lifecycle, single instance, paths, version |
| `dialog`, `clipboard`, `Menu`, `Tray`, `Notification`, `globalShortcut`, `shell`, `powerMonitor` | `ps.dialog`, `ps.clipboard`, `ps.menu`, `ps.tray`, `ps.notify`, `ps.shortcuts`, `ps.os`, `ps.power` | broker-side implementations |
| `session.loadExtension` | `ps.extensions.install/enable/...` | my extension host |
| `protocol.handle` | `ps.pages.register('psychovault')` | internal page registry |
| `autoUpdater` | `ps.update` | signed manifest, hash, staged install |

The legacy inventory recorded 21 preload namespaces and 105 IPC handlers. Those become my channel groups: `tab`, `nav`, `win`, `bookmarks`, `history`, `settings`, `find`, `zoom`, `view`, `downloads`, `ui`, `ws`, `clip`, `rail`, `spell`, `updates`, `proton`, `extensions`, `perf`, `vault`, `translate`. Each channel gets a schema in `runtime/ipc/` and a test in `runtime/tests/`.

### 3.2 IPC contract

- Framing: 4-byte length, 2-byte channel id, payload (CBOR-style minimal encoder written here, not third-party).
- Every message carries a capability handle. The broker checks the handle against the sender's process identity before dispatch.
- Requests are async with cancellation; events are fire-and-forget; both have back-pressure counters.
- Errors are structured: `{ code, stage, detail, retryable }` so the UI can say something true.

### 3.3 What already exists that I reuse

`platform/Psycho_host.cpp`, `Psycho_sandbox.*`, `Psycho_extension.*`, `Psycho_proxy.*`, `Psycho_transport.*`, `Psycho_tls.cpp`, `Psycho_network.*`, `Psycho_profile.*`, `Psycho_vault.*`, `Psycho_spell.*`, `Psycho_download.*` - these are mine and tested against unit contracts. They move under the runtime's service layer rather than being rewritten. `shell/Psycho_win_main.cpp` currently does too much (window, chrome drawing, services, settings UI); it is split into `runtime/` (broker) and `shell/` (Win32 surfaces only), and its hand-drawn chrome is removed at P3/S093.

## 4. The engine (my Chromium)

Pipeline, one renderer per site instance:

```
bytes ──► encoding sniff ──► HTML tokenizer ──► tree builder ──► DOM
                                                              │
 CSS text ──► parser ──► cascade ──► computed style ◄──────────┘
                                   │
                              layout (box tree, line boxes, flex, grid, tables)
                                   │
                              paint (display list, stacking contexts)
                                   │
                              raster (tiles) ──► compositor ──► window surface
                                   ▲
        JS (parser → bytecode → interpreter) ──► DOM/style/event bindings
```

Existing engine modules: `Psycho_base` (strings/buffers/errors), `Psycho_page` (tokenizer/tree builder/navigation hooks), `Psycho_dom`, `Psycho_css`, `Psycho_layout`, `Psycho_raster`, `Psycho_canvas`, `Psycho_image`, `Psycho_jpeg`, `Psycho_svg`, `Psycho_js_parser`, `Psycho_js_runtime`, `Psycho_regex`, `Psycho_media`, `Psycho_mse`, `Psycho_storage`, `Psycho_cookies`, `Psycho_security`.

The gap list from S008/S050 is what P2 works through. In priority order, because my own UI needs it before any website does: DirectWrite text, custom properties and `calc`, cascade completeness, box model edge cases, flexbox, grid, positioning/stacking, painting effects (radius, shadow, outlines), overflow scrolling, hit testing, form controls, selection/caret/editing, images/SVG, media queries, transitions, incremental layout, compositing. Then the web's long tail.

## 5. My UI as web pages

- `ui/` holds plain HTML/CSS/JS: `chrome.html`, `newtab.html`, `settings.html`, `panel-downloads.html`, `panel-extensions.html`, `panel-performance.html`, `dialog-*.html`, plus `tokens.css`, `components.css`, `app.js`.
- The broker loads them through `psychovault://` with a UI capability set. They are ordinary pages to the engine: no privileged drawing path, no native widgets.
- Geometry from the reference: 84px chrome height, 44px rail, 22px status bar, 330px panel. Colours, type scale and spacing come from `docs/reference/legacy-ui/` and are recorded in `ui/tokens.css` with the screenshot names as comments.
- The legacy app's `app/ui/style.css` is reference material for measurements and behaviour. My CSS is written fresh; nothing is copied.
- While P2 is incomplete, a scaffolding window (plain frame, no fake chrome) hosts pages. It is deleted at S093.

## 6. Extension host

- MV3 primary, MV2 compatibility for older extensions I may want.
- Manifests validated, IDs stable, permissions displayed, host permissions enforced at the network service (not politely requested from the extension).
- Service worker runtime runs in my JS engine in an extension process, woken by events, terminated on idle, with alarms.
- Content scripts run in isolated worlds with a DOM-only bridge; messaging goes through the broker.
- `proxy` ownership is exclusive and reported truthfully; a second extension trying to take the level of control gets an error, exactly like Chrome reports it.
- Proton VPN is a bundled extension with its own licence and provenance record (`extensions/Psycho-VPN-Proton/Psycho_PROVENANCE.md`).

## 7. Network and VPN boundary

- TLS: Windows Schannel (OS API, explicit in the dependency audit). I own the browser behaviour around it.
- HTTP/1.1 and HTTP/2 are mine, including HPACK.
- Proxy: my own SOCKS5/HTTP CONNECT client, my own PAC evaluator, fail-closed semantics.
- Proton: the extension sets proxy settings; my proxy daemon carries the WireGuard tunnel with my own WireGuard implementation (Curve25519, ChaCha20-Poly1305, BLAKE2s, Noise IK). No third-party tunnel binary ships.
- DNS under the tunnel: resolution policy is owned by the network service so leaks are a policy failure, not a site's choice.

## 8. Storage and migration

- Profile: versioned JSON stores with atomic replace, plus binary stores for vault and caches.
- Legacy migration reads the old layout (`settings.json`, `bookmarks.json`, `history.json`, `downloads.json`, `session.json`, `workspaces.json`, `vault.bin`, `vault.key.json`, `crash-recovery.json`, `extensions/`, `icons/`) read-only, writes the new layout, verifies counts, keeps the source untouched.
- Vault keeps scrypt parameters and AES-256-GCM compatibility so the old master password still opens it.

## 9. Installer and updater

- `installer/Psycho_Setup.cpp`: my own Win32 setup program with a payload manifest, per-user default install, optional machine-wide, uninstall registration, shortcuts, file associations, optional default-browser registration, `/S`, `/repair`, `/uninstall`.
- `installer/Psycho_Uninstall.exe` built from the same source with a different mode flag.
- Updater: signed manifest, hash verification, staged download, backup, rollback, banner in the UI, no silent elevation.
- `tools/Psycho_package.py` builds the payload; the old portable-exe single-file gate is deleted with the portable model.

## 10. Directory contract

| Directory | Purpose | Status |
| --- | --- | --- |
| `engine/` | My rendering engine | Exists, coverage grows in P2/P5 |
| `platform/` | Network, TLS, proxy, profile, storage, vault, spell, downloads, sandbox | Exists |
| `shell/` | Win32 window, input, IME, clipboard, DirectWrite/Direct2D surfaces | Exists, trimmed to OS integration |
| `runtime/` | Broker, IPC, tab model, preload bridge, service layer, extension host | **New** - created at P1 |
| `ui/` | My HTML/CSS/JS interface | Minimal renderer-backed vertical slice in P1; full interface and reference parity in P3 |
| `installer/` | Setup and uninstall programs, payload manifest | **New** - created at P10 |
| `extensions/` | Bundled third-party extensions, separately licensed | Exists |
| `tools/` | Build, package, audits, conformance runners | Exists, repurposed |
| `tests/` | Unit, golden-image, integration, corpus harnesses | Exists, grows |
| `docs/` | Plan, architecture, reference, inventory, conformance, build | Rewritten now |

## 11. What gets deleted, and why

- Hand-drawn chrome in `shell/Psycho_win_main.cpp` (settings pages, drawn rows, drawn toggles): replaced by `ui/`.
- Portable single-file packaging: replaced by the installer.
- The "standalone exe renders a fetched page from an empty folder" gate: describes the wrong product.
- Ad-hoc per-feature code paths that bypass the runtime: every feature goes through a typed channel so it can be tested.

## 12. Dependency audit boundary (explicit)

| Allowed | Examples |
| --- | --- |
| Language and standard library | C++20, `<string>`, `<vector>`, `<filesystem>` |
| Windows OS APIs | Win32, DirectWrite, Direct2D, D3D11, Schannel, CNG, Winsock, COM |
| Build tooling (never shipped) | cl.exe, link.exe, Python build scripts, image/diff tools for tests |
| Third-party extensions (shipped, separated) | Proton VPN build, extensions I install |

| Not allowed | Why |
| --- | --- |
| Any browser engine or runtime | The product is my own engine |
| Any TLS/crypto library | Schannel/CNG are OS; the rest is written here |
| Any HTTP/2, HTML, CSS, JS library | Written here |
| Any tunnel binary (including wireproxy) | My own WireGuard/proxy instead |
