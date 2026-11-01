# Restarting PsychoVault for Android

I removed the experimental Capacitor/WebView Android source and started again using the extension-capable Chromium Android approach. The previously published preview APK is unchanged and does not have extension or Proton support. This folder does not yet produce a PsychoVault APK.

## Starting point

`engine/titanium/` contains the unmodified browser build and extension-host patches from Titanium Browser for Android at commit `5e9070a98b48f6b6cb239bf90b8718b91795b7c5`. Titanium is GPL-2.0; its licence and upstream README are included. Its Vanadium submodule is pinned to commit `0202b7badff99434822ba5e32e0d738792868455` in `engine/UPSTREAM.json`. Chromium, Vanadium and individual extensions have their own licences and notices. Review all redistribution obligations and the effects of combining this code before distributing a build.

The upstream source is a full Chromium Android browser, not a drop-in WebView library. It still contains Titanium branding and settings. It is not PsychoVault's interface and has not been built here. The source now stages the existing Dark Reader and Proton bundles for Chromium's unpacked extension loader, but neither extension has been installed or tested on Android. I must integrate the desktop-style interface and features and test extension installation rather than claiming the upstream code completes the port. See `EXTENSION-ENGINE-DECISION.md` for the engine comparison and outstanding work.

## Preparing source - not a build

On Linux or WSL2, `scripts/prepare-engine.sh /absolute/path/outside/the/repository` copies the pinned Titanium inputs and downloads the pinned Vanadium revision into a new workspace. It refuses to overwrite an existing path. The script does not fetch Chromium or build, sign, publish or run an APK. It needs Git, Python 3, Node.js and network access to GitHub. It also generates the packaged UI assets in the staged workspace. The default workspace is `~/psychovault-android-engine`; keep large dependencies and keys outside this Git repository.

The upstream `.gclient` fetches Chromium build dependencies and filter lists from external services when its build runs. The preparation script replaces the mutable Titanium extension hook in the staged copy with a version-pinned, SHA-256-checked download; the actual CRX download has not been tested in this sandbox. Other build inputs are not all pinned or reviewed yet. The upstream `build.sh` installs packages, modifies its checkout and expects signing credentials; do not run it as a PsychoVault release build. I need to replace that flow with a pinned, auditable build and release process before it is ready.

## What still needs doing

- Integrate PsychoVault's own responsive desktop UI and features with Chromium's browser, tab and extension APIs.
- Implement and test extension management, consent, updates and popups; integrate the real Dark Reader extension.
- Port and verify the blocker, vault, privacy, downloads, settings and session behaviour. Include and verify Proton VPN as a default in-browser extension.
- Establish licence compliance, secure signing, update compatibility, data migration, build notes and a repeatable Windows `.bat` workflow.
- Build only after permission; test installation, updates, extensions, security and the UI on a phone.

Chromium documents at least 100 GB free disk and an x86-64 Linux host; more than 16 GB RAM is recommended. A free self-hosted WSL2 or Linux build may be possible on suitable existing hardware; no successful build is claimed. The old Android GitHub workflows were removed because they built the deleted WebView prototype and would not build this engine.

The old preview may be signed with an incompatible key. Changing to a new Android browser or signer can require a separate install or uninstall, and uninstalling can delete its local data. Do not uninstall without backing up anything important. Play Protect prompts cannot be guaranteed to disappear.

## Trusted Chromium host - source integration, not device-verified

I added a dedicated `chrome://psychovault-chrome` WebContents for the desktop-style chrome and a separate `chrome://psychovault-pages` origin for the PsychoVault new tab. The host creates regular and off-the-record chrome WebContents from the selected Chromium profile, attaches their ContentView to the activity, and keeps them outside the website TabModel. The activity installer creates the host after native/toolbar initialization and destroys its bridges, observers, delegates and WebContents on activity destruction.

The Java bridge is injected with an exact-origin matcher and a runtime method annotation. Every command also checks the live WebContents, its profile against the currently selected model, and its exact committed document path/profile query. Calls are marshalled to Chromium's UI thread. Website tabs do not receive the chrome bridge. Internal new-tab documents receive a narrower handler that can navigate their own tab and read the limited new-tab settings; they cannot activate/close other tabs or change chrome layout. Unsupported desktop operations return an error, and the principal unsupported controls are disabled rather than showing fabricated success.

The first connected slice covers new tab, tab selection/closure requests, address/search resolution, home, back, forward, reload and stop. Tab-model and per-tab observers publish selected-profile snapshots, including URL/title/loading/navigation availability. Closure remains a request: Chromium can still ask for confirmation. The resolver permits HTTP/HTTPS and the exact profile-matched PsychoVault new-tab URL, not arbitrary privileged schemes. Settings now persist per regular profile through a native SharedPreferences store keyed by the profile directory basename; incognito reads/writes its original profile preferences and never records history. The connected settings are search engine/custom shortcuts, home page, remember history, theme/accent, speed dial, hide sidebar, high contrast, reduce motion and always-show tab close buttons. Every other desktop setting is refused by the native allowlist and disabled in the Android settings page. Bookmarks and history are connected in the trusted chrome document through the existing IndexedDB library; the host emits a visit only after a committed, non-error, non-same-document primary-main-frame navigation in the regular profile while remember is on. Find in page, tab pin/mute, extension list/enable/disable/remove, bundled Proton/Dark Reader staging and opening Proton's popup are connected in source. Downloads, private-window creation, blocker controls, vault, workspaces and the remaining desktop settings are not connected yet. Proton opening is not a connection toggle and is not evidence of protection.

The chrome is a transparent overlay above Chromium's compositor. DOM-measured chrome regions receive touches; other touches fall through to the native page and notify the auto-hiding rail. The existing native top-control height remains the page's top inset; extra chrome height and an open side panel adjust compositor margins. The source keeps the desktop rail order: Performance/ad blocking, Proton, then Settings. This layout and touch path has unit coverage for its measurements, but has not been rendered or gesture-tested on Android.

`scripts/prepare-ui.cjs` still copies the real desktop renderer/CSS rather than creating a separate mobile product. It now adds the trusted bootstrap and extracts the copied new-tab script so the WebUI CSP can forbid inline scripts. The native asset source serves an explicit allowlist with no directory traversal or arbitrary APK-file access. The new-tab and settings pages are served from the page origin with separate role allowlists; recovery is not served. The native asset allowlist is checked against the staged BUILD.gn file list by a regression test.

To generate UI assets alone: `node scripts/prepare-ui.cjs /absolute/path/to/new/output`. The target must not already exist. `prepare-engine.sh` generates these assets inside its external staged workspace and installs the Java/C++/GN integration through the existing source hook. None of this is an APK or a claim that Chromium compiles yet.

## Desktop feature verification

The new Chromium foundation is not yet a PsychoVault browser. `scripts/audit-desktop-settings.cjs` inventories the desktop default settings and marks each user-facing setting as missing its PsychoVault Android host implementation; Chromium upstream may independently provide some similar features. The inventories and adapter tests check coverage, not actual functionality. The bridge contracts still cover the desktop method names, but only the first navigation slice has native handlers. The new-tab and settings pages have a restricted internal-page host; recovery is not served or connected yet.

## Pinned extension build input

The staged engine source uses the upstream Titanium companion extension `v0.0.1` with its release SHA-256 recorded in `engine/psychovault/dependencies.json`. The download is size-limited and the hash is checked before the upstream bundler reads it. Unit tests cover successful verification, mismatches and size limits using synthetic data; staging the pinned Vanadium checkout and hook was also tested. I could not download the real GitHub release asset in this environment, so I have not verified the artifact bytes, bundle, Chromium signature handling or an APK. This companion extension is not Dark Reader or Proton.

## Library data code

The restart includes a bookmarks and history data module and an IndexedDB storage adapter. The desktop and internal-page bridge contracts can route their library calls through it, and it avoids recording private or non-remembered visits when the host supplies those profile facts. It serializes changes within one owner and does not replace good data on a write failure. It is now wired into the trusted chrome document and fed by host committed-navigation events. IndexedDB read-modify-write transactions keep two chrome windows from losing updates. Private history isolation and disk persistence still need a device test. No Android device has tested this code.

The staged GN configuration now uses `com.psychocore.psychovault` rather than Titanium's package or the former experimental preview package. That means it is a separate app from the earlier preview; it does not migrate that app's data. Package naming does not fix a signer mismatch, and signing, update compatibility and the reported install conflict still need device verification.

## Chromium tab controller source

`PsychoTabController.java` now runs behind the trusted host rather than being an unconnected activity factory. It retains the selected-model boundary and strict argument checks, resolves the PsychoVault new-tab URL for the current profile, and exposes navigation/tab snapshots to the host. The source installer registers the activity lifecycle hooks, Java files, WebUI configs and packaged assets. I checked installer anchors and Java syntax; I have not compiled Java/C++ against Chromium or tested navigation on a phone.

## Checks and remaining gate

On 1 November 2026 I ran 31 Android Node tests, 14 Python source tests, 185 desktop Node tests, the desktop channel/settings audit and the Android static source audit. The new tests cover address resolution, request correlation/timeouts/limits, profile-specific internal URL display, layout measurements, generated bootstrap/CSP adaptation and installer failure boundaries. I also parsed all five Android Java files with `javalang`; that is syntax parsing, not Chromium compilation. An authorized GitHub Actions source/build workflow was added. No APK, device test, extension install or Proton/leak test has passed.

Before a test build I still need a real Chromium compile, native layout/touch/lifecycle verification, and the remaining feature/settings handlers. In particular, native toolbar resource capture, rotation/insets, touch fallthrough, profile switching and internal-page navigation need device or Chromium integration tests. The build/signing/dependency gates remain open; this source slice does not make the port release-ready.

## Extension and VPN source

I stage the existing desktop Proton VPN and Dark Reader bundles as APK assets and load them through Chromium's unpacked extension path. Proton keeps its manifest key and extension ID; I do not sign a CRX or impersonate a store signature. Removal of a bundled extension is remembered per profile, and Restore brings it back. The trusted chrome can list, enable, disable, remove and open extension pages. The Proton button opens Proton's own popup; it does not fake a connect/disconnect result. Chromium supplies the extension host, proxy and privacy APIs on this engine, but sign-in, proxy routing, DNS/WebRTC behaviour and failure handling still need a real device test. I will not call the Android browser VPN-protected before those pass.
