# Reference-to-rebuild crosswalk

Audit date: 2026-11-06. This document turns the read-only reference into requirements for the independently written browser. It is a traceability/gap record, not a source-code port.

## 1. Source and precedence

| Material | Revision / evidence | Use |
| --- | --- | --- |
| Read-only reference repository | `Psycho-Core/PsychoVault---REFERANCE-ONLY`, `Master` HEAD `51a0d717d2feb23ba91126db5e756a08a54e4486` | Current behavior, README, verification notes, UI, tests, installer and implementation for direct inspection. `VERIFICATION.md` records prior Linux/Xvfb runs and explicit Windows, live VPN and other limits; its historical results do not prove this rebuild. |
| Current behavior/spec files | `README.md`, `VERIFICATION.md`, `app/ui/{index.html,newtab.html,settings.html,renderer.js,style.css}`, `app/{main.js,preload.js,page-preload.js}`, `installer.nsi`, `tools/*` | The README and verification document define user-visible workflows and stated claims; UI and bridge files define controls, layout and channels; main/installer/tests expose actual behavior, checks and setup lifecycle. Reference tests are behavioral evidence only, not tests of this rebuild. |
| Captured legacy contract | `Psycho-Core/PsychoVault` commit `503a596bd89b5aaac979d4f6f86f8465e24c192d`, recorded in `docs/reference/legacy-inventory.json` | Stable baseline for the existing 10-category settings inventory and exact old UI/API/store counts. All seven recorded UI/main/preload file hashes were verified against this commit. |
| Local visual fixtures | `docs/reference/legacy-ui/` (23 images) | Comparison targets; use the original viewport sizes and note any changed reference-head captures. |

The reference-only repository's current `Master` is not byte-identical to the inventory's pinned source: `index.html` and `newtab.html` match, while settings, stylesheet, renderer, main process, preload and installer differ. The current head has since changed the resource-panel name/bridge from `Performance`/`perf` to `GX`/`gx` and removed several protections in its extension proxy, CRX unpacking and download-open paths. The crosswalk therefore keeps the pinned inventory as the count/schema baseline, records current-head user-visible behavior as additional requirements, and does **not** treat weaker internal security behavior as a requirement. Preserve the rebuild's stricter permission, path, archive, isolation and failure-handling requirements. No reference source was copied into the working tree.

## 2. Surface-by-surface requirements and coverage

`Planned` means a corresponding plan item exists; it does not mean implemented. Current evidence comes from `python3 tools/Psycho_audit-inventory.py` and the repository layout on the audit date. Its PASS status means evidence was found for an inventory row, not that reference parity or end-to-end correctness passed.

### UI, settings and visual behavior

| Reference requirement | Plan coverage | Current implementation / test finding |
| --- | --- | --- |
| Browser frame, tabs, omnibox, nav controls, status, update banner, panels/resizer, window controls | S045 minimum flow; S081, S086, S088-S093 full UI | No app HTML/CSS/JS in `ui/`; native-drawn chrome remains. UI parity and production bridge journeys are untested. |
| New-tab page, search, speed dial, rail/service icons and empty states | S082, S087, S154, S158 | Speed dial/search evidence exists; service rail/catalogue/favicon behavior is deferred. |
| Ten settings categories and 125 interactive controls, plus six clear-data actions | S083, S091-S092, S145, S163 | Category shells are evidenced, but this does not prove each control's default, persistence, effect, disabled state or reset scope. Three clear scopes (cookies, site storage, cache) are not exposed; session clear is partial. |
| Exact 59 defaults, types, migration and persistence | S141, S144, S146 | Many keys are supported; the inventory audit identifies partial/deferred keys including custom search list, site permissions, history opt-out, translations, sleep exclusions, enhancement, bandwidth cap, service rail, media controls, DoH, containers and custom CSS. |
| Dynamic editors: permissions, containers, data usage, extensions/proxy, services, speed dial, vault entries | S083, S145, S151-S161, S170-S181 | Some exist in other forms; service, container, permission, proxy and usage editors are absent or partial. No reference-workflow UI test exists. |
| Screenshots and geometry: 23 captures, 84px chrome, 44px rail, 22px status, 330px panel, both 1396x834 and 1600x1000 captures | S071, S080-S081, S090-S091 | Reference images are present. No owned-engine full UI screenshot/diff exists yet. |

Settings-category mapping (the legacy inventory's source of truth is `docs/Psycho_FEATURE-INVENTORY.md`):

| Category | Behavior to preserve | Plan owner(s) | Current gap indicated by audit |
| --- | --- | --- | --- |
| General | Search engine and multiple custom shortcuts; startup restore; workspaces; home page | S082-S083, S141, S151, S154 | Custom search is a single shortcut, not the reference list. |
| Privacy & security | Blocker lists/shield; history opt-out; per-origin camera/mic/location/notification; third-party cookies, DNT/GPC, autoplay, WebRTC/DoH, referrer, global deny list, containers, delete-on-exit and six clear scopes | S036, S083, S100/S106/S108-S109, S145, S160-S161 | Per-origin editor/global deny list/containers/DoH and cookie/storage/cache clear UI are missing; history opt-out is missing. |
| Downloads | Folder picker, Save As, notifications, transfer controls, open/reveal/clear | S035, S083, S156 | Core download rows/actions are evidenced; reference-accurate notifications, choices and path-safe open/reveal still need acceptance coverage. |
| Extensions | Folder/CRX install, list/status, toggle/remove/restore, proxy status/self-test and VPN checks | S083, S170-S181, S190-S200 | Folder/list/toggle have evidence; CRX, page opening, removal/restore/status and public-IP/VPN checks are incomplete. |
| Performance | Per-tab CPU/memory, sleep/wake/exclusions, CPU/bandwidth limits, battery auto mode, media controls, panic, services, switches, PNG/PDF capture, usage table | S083, S159, S220-S227 | Basic rows only; hot tabs, media/services/switches/snapshot and several limiters are partial or absent. |
| Appearance | Theme/accent, hide rail, custom chrome CSS, video/image enhancement, sounds/volume, media keys/auto-pause, panic URL, service catalogue, rail favicons, speed-dial tiles | S080-S090, S087, S158-S159 | Theme/accessibility and speed-dial evidence exists; custom CSS, enhancement, sound, media-key routing, service rail/favicons are missing. |
| Spelling | In-page underlines, suggestions and add-to-dictionary context action; language/word configuration | S083, S164 | Status/add/remove are evidenced; reference interaction and configured-language behavior are not proven. |
| Passwords | Unlock/lock/auto-lock, entry CRUD, generator, reveal/copy, manual HTTPS fill, CSV import, encrypted backup/restore | S085, S143, S164 | Vault core and status are evidenced; the complete UI workflow and safe same-host HTTPS fill are not established by the inventory audit. |
| Accessibility & language | Translation target and explicit page-translation action; contrast, reduced motion, tab-close visibility, keyboard/focus behavior | S064-S065, S083, S090, S134, S157 | Accessibility settings have some evidence; translation is absent and screen-reader/focus parity needs tests. |
| About | Diagnostics/log, update check/token, verified update banner with Install/Later/dismiss | S038-S040, S083, S216, S230-S232 | Diagnostics and most update actions exist; private-feed token state is partial; a complete signed-update journey is not proven. |

### Browser workflows not visible from counts alone

The current reference README/verification material gives these behavior details. They must be test cases, not just UI labels:

| Workflow | Required observable behavior | Plan coverage / required clarification |
| --- | --- | --- |
| Tabs and tab islands | Drag reorder/out-to-window; pinned tabs stay at the head; middle-click closes; double-click pins; context actions; child tabs join collapsible islands without hiding the active tab. | S026-S027, S086, S150. Expand acceptance cases for pinned ordering, gestures, islands and collapse safety. Island-collapse persistence is not claimed by the reference; do not require it as parity. |
| Workspaces, containers and private windows | Workspaces keep independent tab sets; named containers isolate cookies, localStorage and cache per tab; private sessions write no disk data and are excluded from recovery. | S037, S142, S151-S152. Containers are not equivalent to workspaces; add explicit per-container storage isolation and cross-container leak tests. |
| Omnibox and command palette | URL/search detection, history suggestions, custom HTTPS search shortcuts, keyboard selection, palette actions and tab-to-search. | S154, S162. Explicitly test keyboard journeys, custom shortcut validation and tab-to-search. |
| Downloads | Save As or configured unused filename; live progress/speed, pause/resume where supported, start/finish feedback, reveal/open only a recorded downloaded file; updates are separate from ordinary Save As. | S035, S156, S216. Test path traversal/arbitrary-file rejection and update-vs-download behavior. |
| Vault | Manual fill only on HTTPS for the saved hostname (allowing the reference's `www` equivalence), never cross-origin; generator, copy/reveal, auto-lock, duplicate-safe CSV import and passworded encrypted backup. | S143. Extend its acceptance list with same-host, HTTP, lookalike-host, cross-origin, auto-lock and import/restore cases. |
| Page translation | Ask before sending the current page URL/content to an external translator; disclose recipient; leave original page intact and open translated result separately. | S157 currently says implement or document removal. Decide provider/data flow explicitly; do not silently transmit or claim local translation. |
| Performance and panic | Live per-tab resource view; sleep/wake/exclusions; honest distinction between download bandwidth cap and general traffic shaping; battery saver on unplug; panic hides tabs and writes no browsing state while active. | S159, S224. Define measurements and limit scope; test panic persistence/recovery and battery transitions. |
| Sidebar services | Catalogue and custom URLs, favicons/brand color, open as normal tabs, open/new-window/reload/copy/remove actions, media keys and auto-pause on other audio. | S087-S088, S158-S159. Add service lifecycle and media-routing tests, including origin/session behavior. |
| Spelling | Underlines in editable page fields, right-click suggestions and add-to-dictionary, bulk-edit personal words; report unsupported languages honestly. | S164 (new). Test against fixtures with misspellings, editable fields and user words. |
| Context menus/accessibility | Tab/link/image/selection/text-field menus; cut/copy/paste/select-all; visible focus, keyboard tab navigation, screen-reader names, high contrast and reduced motion. | S033, S060-S062, S089, S134, S162. Add a surface-by-surface keyboard, focus and context-menu matrix. |
| Recovery and diagnostics | Recover recent normal-window tabs only; never restore private tabs; bounded snapshot age; expose logs/diagnostics and explain failures. | S038-S040, S039, S161. Test private exclusion, snapshot age, corrupted state and log redaction. |

### Runtime bridge and feature/API mapping

The pinned inventory is **21 preload namespaces, 80 invoke channels and 105 main handlers**. `S009` names the contract but must map each individual channel/handler to a schema, owning service, authorization rule and test. The high-level ownership crosswalk is:

| Namespace / handler families | Plan owner(s) | Current audit status |
| --- | --- | --- |
| `tab`, `nav`, `win` | S022-S027, S034, S041, S044 | Tabs/navigation/private windows evidenced, but production UI-to-renderer flow is not. |
| `bookmarks`, `history`, `settings` | S040, S141-S146, S155 | Core stores/actions evidenced; exact defaults, migration and privacy scopes need item-level tests. |
| `find`, `zoom`, `view` | S060, S064, S085, S157 | Find/zoom evidenced; split-view handler is deferred. |
| `downloads` | S035, S156 | Main operations evidenced; test end-to-end policy and open/reveal safety. |
| `ui`, `ws`, `clip` | S031-S032, S081, S084, S088-S092, S151, S162 | Workspaces/clipboard evidenced; full menu and production UI command routing are incomplete. |
| `rail` | S087, S158 | Deferred; service rail and icon cache/catalogue not rebuilt. |
| `spell` | S164 | Core calls evidenced; reference page interaction is not. |
| `updates` | S040, S083, S216 | Partial: token path and full verified-install journey. |
| `proton` | S190-S200 | Toggle exists; real Proton network behavior is not proved. |
| `extensions` | S170-S181 | Partial; full API/security compatibility and manager workflows remain. |
| `perf` (or current reference-head `gx`) | S159, S220-S227 | Partial; most current reference resource-panel behavior is absent. Use the intended user-facing behavior, not the namespace spelling as an API design constraint. |
| `vault` | S143, S164 | Core actions evidenced; complete workflow and fill policy need coverage. |
| Additional main handlers: `app:*`, `data:*`, `proxy:*`, `recovery:*`, `update:*`, `vpncheck:*`, vault CRUD/transfer | S038-S040, S145, S161, S170-S181, S190-S200, S216 | Diagnostics/recovery/proxy self-test partially present; clear-data scopes, proxy/VPN details and update token are partial/deferred. |

The reference exposes events as well as invoke calls (tab state/hover/load failures, downloads, workspaces, spell, icons, extensions, update progress/availability and vault status). Treat these as part of the bridge contract: event names, payload schema, sender authorization, ordering, unsubscribe/lifetime and crash/reload behavior must be covered, not only request/reply IPC.

### Data, extensions, network, VPN and installation

| Contract | Plan owner(s) | Audit conclusion / missing acceptance evidence |
| --- | --- | --- |
| 15 legacy profile artifacts: settings, bookmarks, history, downloads, session, workspaces, vault data, migration/recovery records, logs, extension data, icons and running marker | S140-S146 | Storage plan covers principal stores but should specify each artifact's migrate/ignore/rebuild policy, versions, counts and privacy behavior. Keep private data out of normal stores. |
| Network errors, redirect/TLS/compression/cookie/cache/proxy semantics; diagnostics name failing stage | S038, S100-S113 | Broad plan coverage; no real-network site corpus run yet, and current conformance report is not a substitute for network evidence. |
| Ad/tracker blocking and per-site counters | S160 | Main feature exists in some form; verify exact bundled/custom/allow-list semantics and counter reset on navigation. |
| Extension installs, permissions, content scripts, workers, proxy/privacy APIs, bundled Proton/proxy-switch/Dark Reader compatibility | S170-S181 | Extension roadmap is broad. Add reference compatibility cases for each bundled extension/API and enforce permission and ownership boundaries more strictly than unsafe reference-head paths. |
| Proton account/connect/proxy, DNS/WebRTC policy, kill switch, failures and teardown | S190-S200 | A toggle or status is not proof; retain the planned real exit-IP and fail-closed gates. VPN check page must disclose third-party endpoints. |
| Per-user installer, shortcuts/registration, silent install, upgrade/rollback/uninstall preserving profile | S210-S218 | Functional coverage is planned; no installer source exists yet. Compare every observed NSIS behavior, but author a new implementation. |
| Signed updater, staged hash verification, backup/rollback, token for private feeds, banner Install/Later/dismiss; no download before consent | S216 | Plan says signed manifest and banner but does not explicitly require the reference consent/token/state journey; add it. |

## 3. Security divergence: do not copy weaker behavior

Inspection of the current reference-only head versus the pinned baseline shows removals of extension permission checks (including `proxy`, `privacy` and tab-data access), exclusive proxy ownership and rollback, CRX archive entry/size/bounds checks, and validation that opened/revealed downloads are recorded downloads. These are implementation details, but they have direct security consequences. The rebuild's plan already requires capability checks, archive/signature checks, extension isolation and fail-closed VPN behavior; strengthen their tests to prove denial and rollback, and do not regress them to match the weaker reference head.

## 4. Required plan changes from this audit

1. Add/maintain this crosswalk as the source-to-plan map; the feature inventory alone records counts and does not prove that a plan task or test owns every behavior.
2. Expand S050 from the short S008 CSS list to an engine audit against the actual reference UI's HTML/CSS/JS usage (`index.html`, `newtab.html`, `settings.html`, `renderer.js`) and representative page corpus. Each gap must cite the consuming file/selector/API, engine implementation file, test and priority.
3. Make S083/S141/S163 acceptance row-level: all 125 settings controls, 59 defaults/types, eight dynamic editors, six clear actions, 80 invoke channels, 105 handlers, bridge events, 15 stores and 23 captures must link to implementation and named tests. A present control/category is not a pass.
4. Add explicit behavior coverage for tab islands and container storage isolation, reference spell interactions, full vault fill safety, translation consent/disclosure, and sidebar/media workflows. These are not fully specified by current broad task bullets.
5. Expand extension and updater gates to include the current reference's user-visible workflows while preserving stricter security properties; clearly distinguish externally observable parity from implementation differences.
6. Reconcile the current reference-head `GX` naming with the pinned inventory's `Performance` naming in UI copy. Keep `Performance` for now as the pinned visual/functional baseline; do not expose an unimplemented panel under either label.

## 5. Current baseline summary

As of this audit, `python3 tools/Psycho_audit-inventory.py` reports 155 evidence-checked, 48 partial, 36 deferred, 0 failed of 239 inventory items. This is not a parity pass count. The inventory itself finds the UI is not rebuilt, ordinary tabs still use an in-process path, numerous settings/features/IPC handlers are partial or deferred, and the owned engine has 5,009/54,058 Test262 tests passing (9.3%), 16 interpreter crashes, 21,845 unsupported tests and no WPT run (`docs/Psycho_CONFORMANCE.md`). Existing `tests/Psycho_test_main.cpp` and `runtime/tests/*` exercise engine/runtime contracts; `tools/Psycho_audit-inventory.py` audits inventory evidence. There is no rebuilt browser UI yet, so no current reference screenshot/interaction test; the installer directory is README-only, so no installer acceptance test exists. The reference repository's `VERIFICATION.md` and `tools/*.test.cjs` are useful test-intent/source evidence, not proof for this independent codebase. Windows production integration and the reference-to-new-UI screenshot/interaction runs remain outstanding. No milestone should be called reference-complete until the applicable row-level crosswalk tests pass.
