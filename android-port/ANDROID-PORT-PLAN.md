# My plan for PsychoVault on Android

## Goal

I want an Android APK of PsychoVault that carries over the Windows browser's interface, features and settings in ways that make sense on Android. The interface should look like my desktop browser and fit the phone screen, with an auto-hiding sidebar. I want users to install extensions generally, the real Dark Reader extension, and Proton VPN inside the browser. I expect the first APK to have bugs and will test it on my phone so they can be fixed.

The current Android folder is a restart, not a working PsychoVault Android browser. It contains pinned Titanium source inputs, experimental UI adapters, library data code and native tab-controller source; the Chromium browser and PsychoVault UI are not connected. The old preview APK exists separately and does not contain this restart.

## Rules for this work

- Work on the Android source without pretending an adapter, button, source test or upstream feature is a working PsychoVault feature.
- Keep the Windows application working and reuse its interface and logic where practical. Record Android-specific differences rather than inventing equivalent behaviour.
- GitHub Actions source checks are authorised. APK builds remain manual until a runner meets the disk/memory needs; report the first real build error and fix it before retrying.
- Use source, unit and integration tests available during development. These do not replace an Android build or phone test.
- Keep keys, Chromium checkouts, build outputs and large downloads out of Git. Preserve notices and review licences before distribution.
- No status should claim Proton protects traffic until routing and failure behaviour have actually been tested.

## 1. Establish the buildable browser foundation

1. Review the pinned Titanium/Vanadium commits and Chromium build configuration, extension patches, licensing and known Android issues. Replace mutable build inputs with pinned and verifiable inputs where practical.
2. Prepare a reproducible Chromium Desktop Android source and patch workflow. Integrate it into this repository without committing a Chromium checkout or build outputs.
3. Implement PsychoVault application identity, branding, icon, package/version strategy and a safe signing configuration. Don't claim that a new signer can update an APK signed with a different key.
4. Create an auditable build entry point and GitHub Actions workflow that remain manually triggered until authorised. Document a $0 self-hosted Linux/WSL2 runner route and its disk, memory and network requirements; assess hosted runners separately. A workflow file is not proof the build succeeds.
5. Make the Windows `.bat` entry point drive the supported setup/build path and produce honest release notes on success and failure. Keep my existing Windows repository path.

**Code-side gate:** configuration, scripts, source patches, signing safeguards and tests exist; known unresolved build dependencies and environment needs are documented. Actual Android compilation still requires a later authorised build.

## 2. Connect the desktop-style browser interface

1. Integrate the desktop UI assets with a trusted Chromium Android browser UI. Port Electron IPC operations to explicit native browser APIs; don't expose the privileged bridge to websites.
2. Connect real tab creation, activation, closure, navigation, address/search bar, home, back, forward and reload to Chromium tabs.
3. Adapt the desktop tab strip, toolbar, menus, settings, icons, themes and accessibility to phone dimensions, scrolling and Android system insets.
4. Wire the sidebar to the real browser. It must open on request and auto-hide after inactivity or page interaction, without blocking browsing; preserve accessible controls and test the native touch and layout path.
5. Cover navigation, tab and sidebar state, concurrency, app backgrounding and rotation with tests. Use screenshot and real-device checks after a test build.

**Code-side gate:** browser controls are connected to real host handlers, not just mock or placeholder methods. Source tests cover the bridge and user actions.

## 3. Port browser features and settings

1. Inventory every desktop feature, default setting and interface action against actual Android handlers; track each as missing, implemented-but-untested, or verified. Keep unsupported OS-specific behaviour explicit.
2. Port new tab and speed dial, bookmarks, history, search, session recovery, tabs, optional workspaces and sidebar services. Address safe data migration rather than assuming the WebView preview's storage transfers.
3. Port downloads, uploads, share, menus, translation, find, zoom and diagnostics using Android-appropriate services and permissions.
4. Implement private browsing, containers and site permissions using genuine Chromium profile/storage isolation. Verify cookies, local storage, lifecycle and cleanup; never label a shared profile private.
5. Integrate the blocker, privacy/security controls, performance and adjustable hibernation, media controls, accessibility, spelling, panic and interface mods. Where an operating-system API prevents a desktop-equivalent control, document the limitation instead of creating a nonfunctional switch.
6. Port the existing PsychoVault vault, not a different password manager: storage, lock and unlock, entry management, encrypted backup/import and autofill. Review Android secure storage, migration, lifecycle and security before recommending real credentials.
7. Implement Android-appropriate update and install behaviour, diagnostics and release notes. Android update prompts and Play Protect cannot be guaranteed away.

**Code-side gate:** each non-VPN desktop feature and setting has a real implementation or an explicitly documented Android limitation, with tests of behaviour and failure paths. Source tests are not device verification.

## 4. General extensions and Dark Reader

1. Integrate the Chromium extension host, installation from supported sources, management, updates, enable/disable/remove, permission prompts, popups and options pages. Preserve the isolation between website content, browser UI and extension privileges.
2. Integrate the actual Dark Reader extension with correct licensing, provenance, identity and update handling. Don't replace it with a home-grown dark-mode filter.
3. Add tests for installation, permissions, restart persistence, extension UI and at least one independent extension in addition to Dark Reader. Account for phone-incompatible extensions without claiming every extension works.

**Code-side gate:** the actual host integration and test cases are in the source; successful installation and page behaviour remain to be verified in a built app.

## 5. Proton VPN inside PsychoVault

1. Integrate the authorised Proton browser extension as the planned default in-browser VPN, with compliant source/licence notices, correct identity, installation, update and removal behaviour. Do not rely on a separate system VPN app.
2. Connect its sidebar button, popup, authentication and Chromium proxy/permission APIs. Never collect passwords or tokens in chat, logs or diagnostics.
3. Add test cases for authentication, proxy control, connection and reconnection, conflicts, failures and traffic routing. Plan device tests for DNS and WebRTC leaks; display unprotected or unknown state when protection is not established.
4. Do not claim service compatibility or leak protection until those tests are run successfully against the actual app and service.

**Code-side gate:** integration and test procedures exist; real Proton authentication and traffic protection require later live tests.

## 6. Code-side completion review before asking to build

1. Recheck the full desktop feature/settings inventory, native handler implementations, interface, security boundaries, licences, build setup and signing/migration decisions.
2. Run available automated tests, static checks and desktop regression tests; fix failures. Report exactly what each test does and doesn't prove.
3. Record blockers or unavoidable Android differences. Don't label the source build-ready if the native host, feature handlers, dependency inputs or build workflow are still missing.
4. Report readiness honestly. Source-check Actions are authorised; APK builds still need a suitable runner and must produce build notes on success or failure.

## 7. Authorised GitHub Actions test build and phone feedback

1. Once authorised, run the Android test-build workflow on a runner that actually meets its storage, memory and network requirements. If the free hosted runner cannot, use a suitable self-hosted runner; don't assume extra hosted resources are free.
2. Produce an APK, build notes on success or failure, checksums and signing information. Report the first actionable build error and fix it before retrying.
3. Install and test on my phone. Check desktop-style UI and auto-hide, navigation and lifecycle, data and privacy, each feature/settings group, extensions and Dark Reader. Test Proton separately with my consent and actual network/leak checks.
4. Use my bug reports from the installed version to fix issues and repeat builds/tests with permission. Test update compatibility and data preservation with the chosen signer before distributing a release.

## Current status

The trusted Chromium chrome/new-tab/settings/recovery origins, origin/profile-checked Java bridge, tab/navigation loop, state observers, native overlay/layout source, Android back callback and activity lifecycle installer exist. Source handlers now cover downloads, blocker/privacy policy, workspaces, session recovery, the master-password vault with backup/import/recovery, performance controls, general extension ZIP/CRX import with permission prompts, and 57 of the 59 desktop settings; the remaining two are the extension manager inventory and the internal settingsVersion marker, not missing host handlers. The last ten settings use Android adaptations (exact-origin permissions, download duty-cycle cap, available-directory picking, background notifications, HTTPS bdic mirror, custom dictionary words, release dismissal, media session/auto-pause and named ephemeral OTR containers). Proton and Dark Reader are staged for Chromium's unpacked loader; Proton opens its own popup and has a real proxy-pref path, but connection/protection is unverified. The current checks are 31 Android Node tests, 19 Python source tests, 185 desktop Node tests, both static audits clean, and 14 Java files parsed with javalang (syntax only). Java/C++ have not been compiled against Chromium and the native layout/touch path has not run on a phone. There is no test-ready PsychoVault APK from this restart.
