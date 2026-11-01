# My Android port plan

## Goal and non-negotiable requirements

I want my existing PsychoVault interface on Android, automatically adjusted to the phone screen. I want the same colours, rail, tabs, icons and overall layout, not a different Ionic-themed browser. I want Proton VPN inside PsychoVault as the default browser VPN and I want extensions generally, not only Proton. A separate system-wide VPN app is not a substitute. I want the Windows browser to keep working as it does now.

This folder is a starting experiment, not the finished port. Its simplified interface is not yet my desktop UI. Proton and extensions are not implemented and no VPN protection is claimed. This experiment uses WebView to test basic layout and navigation; it does not settle the final browser engine.

## Phase 1 - Layout and trusted interface

1. Inventory the current desktop interface, `window.pv` calls, state events, assets, menus, themes and Electron-specific behaviour. Make a compatibility table before wiring each feature.
2. Extract shared interface files where practical, without breaking the Electron desktop build. Add a platform adapter instead of exposing Electron APIs to websites.
3. Preserve my rail, tabs, toolbar, icons and settings appearance. Make tabs and panels fit narrow screens using scrolling or controlled sizing. Account for system bars, cutouts, keyboard, rotation, display scaling and accessibility.
4. Measure page bounds from the actual interface and update the native browser view on layout changes. Test native view stacking, touch, overlays and coordinate conversion on real devices. Do not rely on screenshots alone.
5. Compare screenshots at agreed viewport sizes and manually test touch, back gestures, menus, many tabs and large text. Document any difference before calling the UI the same.

## Phase 2 - Browser engine and Proton decision gate

1. Keep the trusted Capacitor interface separate from untrusted website views. Prototype both Android WebView and a custom GeckoView host as needed. Compare extension APIs, site compatibility, update size and maintenance.
2. Android WebView does not supply the desktop extension host. Do not present this experiment as evidence that extensions work.
3. Investigate a suitable Proton Firefox extension build, licensing, installation and its required APIs. Test a small independent extension first. Check how extension popups, storage, permissions, tabs, proxy and authentication are provided by the host.
4. Verify a local authenticated proxy with synthetic credentials before testing Proton. Then test real Proton sign-in with user consent, actual network routing, reconnect and failure behaviour. Independently test DNS and WebRTC. A popup, status icon or changed IP alone is not proof of complete leak protection.
5. Test general extension installation separately. Firefox/Android API support and signed downloaded extensions differ from Chromium. Document exactly which extensions and APIs were tested.
6. If Proton does not work inside the browser, stop and report the blocker. Do not switch to a system VPN or ship a fake protected state.

## Phase 3 - Browser functionality and data

- Implement navigation, tabs, session restore, downloads, file upload, external links, permissions and safe Android back handling.
- Implement bookmarks, history and optional workspaces. Design migration separately; never overwrite desktop data.
- Design private sessions and container separation. Test cookies, storage, process death, cleanup and restart before making privacy claims.
- Port the blocker using engine-specific request handling and measure coverage, including redirects and service workers. Do not promise parity based on a domain matcher alone.
- Test an appropriate Dark Reader extension build, not just the desktop Chromium bundle.
- Implement the existing vault using Android secure storage. Verify the desktop encrypted backup format using synthetic test vectors, and test failure and recovery. Never log vault content or credentials.
- Keep diagnostic reports copyable and credential-free. Review paths, URLs and tokens before sharing.

## Phase 4 - Build, signing and release

- Make `BUILD-ANDROID.bat` repeatable on my Windows path and write release notes on every success or failure. Test it on Windows and confirm the first failure is visible.
- Add automated unit, native and device tests without adding GitHub Actions until I ask. Build a debug APK first; verify on a real phone.
- Decide the final application ID, release signing key storage, backups, update process and upgrade compatibility before sharing a release. Never commit keys.
- Test installation and signed updates, including Android approval prompts. Do not promise silent updates.
- Document licences, bundled sources, patches and notices. Recheck Android distribution rules before release.

## Minimum evidence before claiming a release

A successful clean APK build, real-device UI and navigation checks, accessibility and lifecycle checks, tests for the trust boundary and data isolation, real extension and Proton traffic tests, vault/backup tests, and an install/update test. Report failures and untested features explicitly. Do not claim Proton, an ad blocker, private browsing or leak protection works because the control is visible.

## References I used for the plan

- [Capacitor Android workflow](https://capacitorjs.com/docs/android) and [custom native code](https://capacitorjs.com/docs/android/custom-code).
- [Android WebView bridge risks](https://developer.android.com/privacy-and-security/risks/insecure-webview-native-bridges) and [WebView request interception](https://developer.android.com/reference/android/webkit/WebViewClient).
- [GeckoView](https://mozilla.github.io/geckoview/) and its [extension controller](https://mozilla.github.io/geckoview/javadoc/mozilla-central/org/mozilla/geckoview/WebExtensionController.html).
- [Proton browser extension](https://protonvpn.com/support/browser-extensions) and [Firefox proxy API](https://developer.mozilla.org/en-US/docs/Mozilla/Add-ons/WebExtensions/API/proxy).
- [Android command-line builds](https://developer.android.com/build/building-cmdline) and [app update requirements](https://developer.android.com/google/play/app-updates).
