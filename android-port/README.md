# Restarting PsychoVault for Android

I removed the experimental Capacitor/WebView Android source and started again using the extension-capable Chromium Android approach. The previously published preview APK is unchanged and does not have extension or Proton support. This folder does not yet produce a PsychoVault APK.

## Starting point

`engine/titanium/` contains the unmodified browser build and extension-host patches from Titanium Browser for Android at commit `5e9070a98b48f6b6cb239bf90b8718b91795b7c5`. Titanium is GPL-2.0; its licence and upstream README are included. Its Vanadium submodule is pinned to commit `0202b7badff99434822ba5e32e0d738792868455` in `engine/UPSTREAM.json`. Chromium, Vanadium and individual extensions have their own licences and notices. Review all redistribution obligations and the effects of combining this code before distributing a build.

The upstream source is a full Chromium Android browser, not a drop-in WebView library. It still contains Titanium branding and settings. It is not PsychoVault's interface, has not been built here, and does not yet bundle or verify Dark Reader or Proton. I must integrate the desktop-style interface and features and test extension installation rather than claiming the upstream code completes the port. See `EXTENSION-ENGINE-DECISION.md` for the engine comparison and outstanding work.

## Preparing source - not a build

On Linux or WSL2, `scripts/prepare-engine.sh /absolute/path/outside/the/repository` copies the pinned Titanium inputs and downloads the pinned Vanadium revision into a new workspace. It refuses to overwrite an existing path. The script does not fetch Chromium or build, sign, publish or run an APK. It needs Git and network access to GitHub. The default workspace is `~/psychovault-android-engine`; keep large dependencies and keys outside this Git repository.

The upstream `.gclient` fetches Chromium build dependencies and filter lists from external services when its build runs. The preparation script replaces the mutable Titanium extension hook in the staged copy with a version-pinned, SHA-256-checked download; the actual CRX download has not been tested in this sandbox. Other build inputs are not all pinned or reviewed yet. The upstream `build.sh` installs packages, modifies its checkout and expects signing credentials; do not run it as a PsychoVault release build. I need to replace that flow with a pinned, auditable build and release process before it is ready.

## What still needs doing

- Integrate PsychoVault's own responsive desktop UI and features with Chromium's browser, tab and extension APIs.
- Implement and test extension management, consent, updates and popups; integrate the real Dark Reader extension.
- Port and verify the blocker, vault, privacy, downloads, settings and session behaviour. Include and verify Proton VPN as a default in-browser extension.
- Establish licence compliance, secure signing, update compatibility, data migration, build notes and a repeatable Windows `.bat` workflow.
- Build only after permission; test installation, updates, extensions, security and the UI on a phone.

Chromium documents at least 100 GB free disk and an x86-64 Linux host; more than 16 GB RAM is recommended. A free self-hosted WSL2 or Linux build may be possible on suitable existing hardware; no successful build is claimed. The old Android GitHub workflows were removed because they built the deleted WebView prototype and would not build this engine.

The old preview may be signed with an incompatible key. Changing to a new Android browser or signer can require a separate install or uninstall, and uninstalling can delete its local data. Do not uninstall without backing up anything important. Play Protect prompts cannot be guaranteed to disappear.

## Desktop UI adaptation in progress

`scripts/prepare-ui.cjs` creates a separate copy of the existing desktop chrome, keeping its HTML, CSS and renderer rather than replacing them with a new mobile design. It adds a phone layout and an auto-hiding rail controller. The rail starts hidden, opens from the tab strip, closes after five seconds or an outside touch, and closes when a rail button is selected. The future Chromium host must forward website touches to it. Unit tests cover the controller; no phone or Chromium integration has been tested.

`ui/desktop-bridge.js` and `ui/internal-page-bridge.js` map the desktop chrome and internal-page preloads to native transport channels, respectively. It is an adapter contract, not a native implementation. The generated UI deliberately does not start the desktop renderer when `window.pv` is absent. The Chromium-side trusted UI, bridge transport, page touch forwarding and layout integration are not implemented yet. Some native tab-controller commands exist as source, but they are not connected to the UI or tested on Android. The bridge must never be exposed to arbitrary websites. No browser or extension functionality follows from this UI preparation alone.

To generate the UI for development without touching the repository or building an APK: `node scripts/prepare-ui.cjs /absolute/path/to/new/output`. The target must not already exist. This output is not a working browser and is not packaged by the current engine scripts.

## Desktop feature verification

The new Chromium foundation is not yet a PsychoVault browser. `scripts/audit-desktop-settings.cjs` inventories the desktop default settings and marks each user-facing setting as missing its PsychoVault Android host implementation; Chromium upstream may independently provide some similar features. The inventories and adapter tests check coverage, not actual functionality. The chrome and internal-page bridge contracts now match the desktop preload methods and argument shapes, but neither adapter is wired to a native host. Generated assets include the desktop new-tab, settings and recovery pages; those pages are not yet connected or secured by a Chromium internal-page handler.

## Pinned extension build input

The staged engine source uses the upstream Titanium companion extension `v0.0.1` with its release SHA-256 recorded in `engine/psychovault/dependencies.json`. The download is size-limited and the hash is checked before the upstream bundler reads it. Unit tests cover successful verification, mismatches and size limits using synthetic data; staging the pinned Vanadium checkout and hook was also tested. I could not download the real GitHub release asset in this environment, so I have not verified the artifact bytes, bundle, Chromium signature handling or an APK. This companion extension is not Dark Reader or Proton.

## Library data code

The restart includes a bookmarks and history data module and an IndexedDB storage adapter. The desktop and internal-page bridge contracts can route their library calls through it, and it avoids recording private or non-remembered visits when the host supplies those profile facts. It serializes changes within one owner and does not replace good data on a write failure. This isn't connected to Chromium yet. Before using it, the browser host must provide a dedicated trusted origin, own a single library instance, and supply genuine profile and committed-navigation events. Otherwise private history isolation and cross-window consistency are not established. No Android device has tested this code.

The staged GN configuration now uses `com.psychocore.psychovault` rather than Titanium's package or the former experimental preview package. That means it is a separate app from the earlier preview; it does not migrate that app's data. Package naming does not fix a signer mismatch, and signing, update compatibility and the reported install conflict still need device verification.

## Chromium tab controller source

The staged source now adds `PsychoTabController.java` to Chromium's Android Java target and gives `ChromeTabbedActivity` a way to create it after the tab model exists. The controller has a strict operation allowlist for opening, selecting and closing tabs and for HTTP/HTTPS navigation, back, forward, reload and stop. It uses the currently selected Chromium tab model, so tab IDs from the other profile aren't acted on. It can produce a snapshot of that model. Calls must be made on Chromium's UI thread.

This is native controller source, not a connected browser UI. The privileged host must authenticate the UI document before invoking it, resolve address-bar text and internal PsychoVault pages, deliver state changes, and connect the desktop-style chrome to the real browser layout. The new-tab method currently opens Chromium's own page, not the PsychoVault new tab. I checked that the source installer applies to the pinned Chromium files and parses as Java, but I have not compiled it against Chromium or tested the controller on Android.
