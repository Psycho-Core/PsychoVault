# Android port audit

## Result

The Android folder is an early experiment, not a correctly verified port of my desktop browser. I cannot claim that it compiles, runs on a phone, protects traffic with Proton or supports extensions. I found a critical dependency advisory and several defects in the prototype. I did not change the app code during this audit.

## Findings

### Critical - vulnerable Capacitor Android dependency

`package.json` and the lockfile pin `@capacitor/android` to 8.3.4. `npm audit --omit=dev --audit-level=moderate` reports one critical advisory, [GHSA-rvm3-566m-v7fv](https://github.com/advisories/GHSA-rvm3-566m-v7fv). The advisory says certain attacker-controlled links opened in the trusted Capacitor WebView can load remote content at the app origin, giving that content access to registered native plugins. This version is affected even when the Capacitor HTTP plugin is disabled.

I have not demonstrated an exploit against this particular prototype. Its separate website WebViews are relevant to the attack surface, but do not remove the need to upgrade and test the trusted interface. The advisory lists patched 8.x versions including 8.4.3 and 8.5.1. Update compatible Capacitor packages together, then rebuild and retest before distributing any APK.

### High - Proton and extensions are missing

`www/app.js` only displays an explanatory message when Proton is pressed. `BrowserPlugin.java` uses Android WebView; it does not load the desktop Proton extension or provide a general extension host. The ad blocker, Dark Reader, vault and private browsing are also absent. These are documented limitations, but they mean the user's required port is not complete. Do not claim that Proton protects traffic or replace it with a system-wide VPN without agreement.

### High - Proton warning can disappear

`www/index.html` initially warns that Proton does not protect traffic, but `show()` in `www/app.js` replaces that same warning for other actions. In a JavaScript harness, clicking the ad blocker changed the entire notice to `The desktop ad blocker is not implemented in this preview.` There was then no persistent Proton warning in that notice. Keep the VPN state in a separate persistent element and test state transitions.

### High - rapid tab operations can navigate the wrong tab

`www/app.js` creates a tab, awaits asynchronous operations, and then calls `navigate()` without a tab ID. `BrowserPlugin.navigate()` uses whichever tab is active when its UI-thread task runs. In a mocked asynchronous JavaScript harness, two quick new-tab clicks produced `create 2`, `create 3`, `navigate 3`, `navigate 3`: tab 2 received no initial navigation. This demonstrates a UI ordering bug in the harness, not a device test. Route operations to an explicit tab ID and test concurrent create, select, close and navigation on Android.

The native `create()` method also reads the mutable `tabs` map before posting to the Android UI thread while other tab operations access it on that thread. Capacitor invokes plugin methods on a handler thread. Move the duplicate check and all map access onto one thread and test it.

### Medium - hostnames with ports are rejected

`www/url.js` checks for a URL scheme before its hostname-with-port branch. As a result, `destination('example.com:8080')` and `destination('localhost:8080')` both return `null`. The existing URL test does not cover either case. Preserve the restriction against unsafe schemes while adding tests for hostnames, ports, IPv6, malformed URLs and search terms.

### Medium - address and error state need work

The native plugin emits page URL updates on page finish or title receipt, but does not report page-start, loading failures, certificate state or an explicit navigation error to the interface. The address bar can therefore lag navigation and does not provide a trustworthy security indicator. Test redirects, failed loads and TLS errors on a device before treating it as browser-grade navigation.

### Medium - Android back, lifecycle and layout are unverified

The project has no browser-specific Android back handling or session restore. Browser view placement depends on CSS coordinates, Android density and native view stacking. Insets, keyboard, rotation, accessibility scaling, WebView renderer crashes and process death need real-device tests. The supplied UI is a simplified new interface, not the existing desktop UI adapted to the phone.

### Medium - current tests do not verify the Android app

The four Node tests cover the URL helper and source-string checks. They do not compile Java, start an Android app, test the native bridge or establish the website/UI security boundary. The source check for absence of `addJavascriptInterface` in the browser plugin is useful, but is not a malicious-page or iframe security test. The Windows batch file has not been run on Windows.

## Checks run

- `npm ci --ignore-scripts --no-audit --no-fund` - passed.
- `npm test` - 4 tests passed.
- `npm run sync` - passed; this copies assets and config, it does not compile the Android app.
- `npm audit --omit=dev --audit-level=moderate` - failed with the critical Capacitor advisory above.
- `./android/gradlew -p android assembleDebug` - failed immediately because Java and `JAVA_HOME` are missing. No APK was produced. The Android SDK is also not configured here; later build steps remain unknown.
- Mocked JavaScript interaction checks reproduced the rapid-tab and disappearing-warning problems. They are not Android device tests.
- Desktop `node tools/audit.mjs` reported no defects; 185 desktop Node tests passed. These do not verify Android.

## Recommended order

1. Update the affected Capacitor dependencies together and check the advisory again.
2. Fix persistent VPN warnings, URL parsing and tab-operation races; add behavioural tests.
3. Install the Android build prerequisites, compile the debug APK and run native/device tests, including the bridge boundary and UI layering.
4. Implement and test the existing desktop UI on different screen sizes.
5. Investigate an extension-capable browser engine and prove real Proton authentication and routing, failure behaviour, DNS and WebRTC before calling it protected.
6. Complete the remaining features, signing and update tests before release. Do not add GitHub Actions unless requested.

## Follow-up audit - 1 November 2026

I checked the current source again. The findings above have not been fixed: Capacitor is still pinned to 8.3.4, the Proton button still only shows a warning, the shared notice can still be replaced, the native navigation method still uses the active tab, and the hostname-with-port examples still return `null`.

- Clean dependency install, four Android Node tests and Capacitor sync passed.
- The production dependency audit still reports one critical advisory, GHSA-rvm3-566m-v7fv.
- `android/gradlew assembleDebug` stopped immediately because Java is unavailable. Android compilation, APK installation and device tests are still unverified.
- The desktop audit reported no defects and all 185 desktop Node tests passed. These do not prove the Android port works.

No application code or GitHub Actions were changed in this follow-up.

## Changes before the Actions preview build

I updated the Capacitor packages from 8.3.4 to 8.6.0. `npm audit --omit=dev` now reports no vulnerabilities. A full `npm audit` still reports four moderate development dependency findings through the Capacitor CLI, glob and minimatch; those are not included in the Android runtime dependency audit. I did not use `npm audit fix --force` to downgrade the CLI.

The Proton warning now has its own element, URL tests cover hostnames with ports, and navigation and toolbar commands pass explicit tab IDs. The duplicate-tab check now runs on the Android UI thread. These source changes need Android build and device verification. Other tab concurrency, lifecycle, extension support, privacy and UI parity findings remain open. The old findings above record the earlier audit, not the current state of these particular changes.

## GitHub Actions debug build

Run [41275853664](https://github.com/Psycho-Core/PsychoVault/actions/runs/41275853664) built the debug APK after correcting a Java compilation error. Actions passed the production dependency audit, source tests, APK archive check and Android signature verification, and uploaded the APK and build notes. The Actions artifact download was not accessible from this sandbox, so I did not independently inspect or install its contents locally. No phone, Proton traffic, extension, privacy or end-to-end security tests ran. This is not a release-signed APK.
