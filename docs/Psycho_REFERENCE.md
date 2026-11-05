# My reference material and parity contract

## 1. Where the reference lives

| Source | Location | What I use it for |
| --- | --- | --- |
| Old application source (read-only) | `https://github.com/Psycho-Core/PsychoVault---REFERANCE-ONLY` (a working clone is kept outside this repository) | Behaviour, measurements, feature lists, IPC surface, installer behaviour. Never code to copy. |
| Legacy UI screenshots | `docs/reference/legacy-ui/` in this repository | The visual contract: new tab, browser chrome, all ten settings categories, single-window captures |
| Legacy inventory | `docs/reference/legacy-inventory.json` and `docs/Psycho_FEATURE-INVENTORY.md` | The functional contract: 10 categories, 125 interactive controls, 59 default settings, 21 preload namespaces, 105 IPC handlers, 23 screenshots, data stores |
| Proton VPN extension build | `extensions/Psycho-VPN-Proton/` (pinned, provenance in `Psycho_PROVENANCE.md`) | The standard bundled extension and the VPN acceptance target |
| Old installer script | `installer.nsi` in the reference repository | Behaviour to reproduce in my own installer (registry, shortcuts, upgrade, rollback), not code to reuse |

## 2. Rules for using the reference

1. Read it, measure it, describe it, then write my own implementation. No file from the reference repository is copied into this one.
2. Measurements (heights, paddings, colours, timings, store formats) are transcribed into my documents with their source screenshot or file named.
3. Where the old app used a third-party component, the reference tells me the behaviour I must reproduce, not the component I may ship:
   - `wireproxy.exe` (third-party WireGuard client): my replacement is my own WireGuard client and proxy daemon (P9).
   - Electron: my replacement is my own runtime and engine (P1/P2).
   - Chromium extensions: my replacement is my own WebExtensions host (P8).
4. Dark Reader in the old app was a patched third-party extension. It stays a compatibility test subject, never bundled without its licence and provenance record.

## 3. How the old app worked (so I can rebuild the same shape)

- The window was a `BrowserWindow` whose content was the app's own HTML (`app/ui/index.html`, `style.css`, `renderer.js`). The tab strip, rail, omnibox, status bar, panels and settings were all HTML drawn by Chromium.
- Each tab was a `WebContentsView` attached to that window, positioned in the area the HTML chrome reserved.
- Privileged work lived in the main process (`app/main.js`, 4160 lines) and was exposed to the UI through `app/preload.js` namespaces (`tab`, `nav`, `win`, `settings`, `vault`, `extensions`, `proton`, ...).
- Extensions were loaded with `session.loadExtension`. Bundled ones were `proton-vpn`, `proxy-switch` and a patched `dark-reader`.
- VPN traffic was carried by a bundled `wireproxy.exe` child process exposing SOCKS5 on localhost; the extension's `chrome.proxy` settings pointed at it.
- Updates came from a signed manifest (`update-public-key.pem`), downloaded, hash-checked, staged and installed by a helper script with backup and rollback.
- The installer was NSIS, per-user into `%LOCALAPPDATA%\PsychoVault`, with Start Menu shortcuts, an uninstall registration, upgrade-safe payload replacement and `/S`-style silent behaviour.

The rebuild keeps this shape exactly - runtime, HTML UI, engine, extension host, proxy daemon, installer - with every component written for this project.

## 4. The visual contract

- Reference geometry: 84px chrome height, 44px rail, 22px status bar, 330px panel.
- Theme: dark navy surfaces, cyan accents, 1px low-contrast borders, 6-8px radii, compact type scale, inline SVG icons only.
- Ordering rules I must keep: ad blocker above Proton, both above Settings.
- Panels: `Performance` is the name for the resource-control panel.
- Every control's default value, persistence rule and label comes from the inventory.

The full-page captures in the reference repository cover whole settings pages; the archive here is partial. Where a capture is missing, I take the measurement from the legacy HTML/CSS and record the assumption in the inventory.

## 5. The functional contract

`docs/Psycho_FEATURE-INVENTORY.md` is the checklist. Nothing is "done" until its row says passed with a test name:

- 125 interactive settings controls across 10 categories, plus 6 data-clear actions.
- 59 default settings keys with their default values and types.
- 21 preload namespaces / 80 invoke channels / 105 main-process handlers, re-expressed as my runtime channels.
- Chrome features: tabs, workspaces, private windows, split view, command palette, omnibox suggestions, bookmarks, history, downloads, find, zoom, translation, sidebar services, performance panel, ad blocker, Proton, spelling, vault, updates, diagnostics, session recovery, shortcuts, context menus.
- Data stores: settings, bookmarks, history, downloads, session, workspaces, vault (binary + key), migration record, crash recovery, error log, extension log, icons.

## 6. Parity levels I will report

| Level | Meaning |
| --- | --- |
| **Match** | Renders and behaves the same; visual diff within threshold; acceptance test passes |
| **Works, differs** | Behaves correctly but with a stated visual or interaction difference I have not closed yet |
| **Partial** | Some behaviours implemented; un-implemented parts listed |
| **Not built** | Nothing there yet; no control is allowed to look interactive in this state |
| **Blocked** | Cannot be tested here (for example live Proton sign-in without an account) |

A control may never be displayed as working while its row says Partial, Not built or Blocked. If the behaviour is missing, the control is missing or visibly disabled with a reason.
