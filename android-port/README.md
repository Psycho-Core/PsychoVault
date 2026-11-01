# PsychoVault Android experiment

I am building an Android version separately from my Windows browser. This is an **experimental preview**, not a finished browser. I have published a debug APK for testing. The earlier preview was reported to open and browse on a phone; that does not verify the new changes or the missing features.

**Proton VPN does not work in this preview.** It is not bundled, connected or protecting traffic. General extensions, Dark Reader and private browsing are not implemented either. The current unbuilt source includes an experimental, incomplete vault. The current unbuilt source has an experimental domain blocker; the previously published APK does not. Only a small subset of settings is available in the current source. Do not use this preview for sensitive browsing or assume it has the privacy or security features of the Windows browser.

## What is here

- `www/` - a small responsive interface inspired by my desktop UI. It uses my desktop icon but does not yet reuse the full desktop interface or have visual parity.
- `android/` - the Capacitor Android project and a custom browser-view plugin. Websites use separate Android WebViews without the Capacitor bridge.
- `PLAN.md` - the work and test gates needed before this can become my Android browser.
- `BUILD-ANDROID.bat` - my experimental Windows debug build entry point. It writes `Release-Notes-Android.txt` even if a prerequisite or build step fails.

This preview uses Android WebView to test the shell, navigation and view placement. Android WebView is not a general extension host. My plan still requires Proton VPN inside PsychoVault and general extension support. I have not proven that the Firefox Proton extension works in GeckoView, and I will not call this preview protected.

## Bookmarks, history and settings - source changes

The current source adds a bookmark button and a menu for bookmarks and history. I can remove individual bookmarks and history entries, or clear the history list. Settings has a search engine, theme selector and an optional open-tab restore switch. Restore is off by default. When enabled it saves up to 50 tab addresses and the selected tab, but not each tab's back/forward history. Turning it off clears the saved session from the browser data. The list and settings are stored locally in the trusted interface's WebView storage, not in the desktop profile or an encrypted vault. URL paths and query strings, including restored tab addresses, may contain sensitive information. There is no private mode; clearing history does not clear website cookies, cache or other site data.

These changes passed JavaScript tests with mocked native calls. They have not been built into a new APK or verified on a phone. They are not yet equivalent to my desktop bookmark, history or settings features.

The Android launcher icon in the current source now uses my PsychoVault icon instead of the default Capacitor icon. It still needs a device check.

## Sidebar

The sidebar starts hidden. I tap the menu button next to the logo to open it. It hides after five seconds, when I tap the menu button again, or when I touch the page, toolbar or tab strip. The web page should use the freed space. I still need to check the new behaviour on a phone.

## Build on Windows

I need Node.js 22 or newer, Android Studio, Android SDK 36 and JDK 21 configured for the Android Gradle build. The Gradle wrapper and npm need network access on a first build. I can use my existing repository path - I do not need to move it. I should update Android Studio and install the SDK versions requested by the build.

Double-click `BUILD-ANDROID.bat`. If it succeeds, the debug APK should be at `android\app\build\outputs\apk\debug\app-debug.apk`. If it fails, I read the **first error** in the window and the generated release notes. I have not run this batch file on Windows or verified the APK in this environment.

For a manual build from this folder:

```bat
npm ci
npm run sync
npm test
cd android
gradlew.bat assembleDebug
```

A debug APK is for testing. See [SIGNING.md](SIGNING.md) for the stable signing plan and the warning about losing app data on uninstall. The signing workflow has not been verified yet. I must not commit signing keys, credentials or real browser data.

## Current limits

- A tab is a separate Android WebView, but storage is not isolated per tab. This is not private browsing or a container implementation.
- Only HTTP and HTTPS navigation is accepted; the Android network policy may block cleartext HTTP. Other schemes and external-app links do not work yet.
- File picker and download support is unbuilt and unverified. Popups, media permissions and Android back-button integration are not implemented. Website data is not migrated from Windows.
- Opt-in tab-address restoration has mock tests only; it does not restore navigation history. Process death, site security, WebView layering, keyboard and rotation need real-device testing.
- The app starts at Google. Searches use Google by default; the current source lets me choose DuckDuckGo or Bing. Do not enter confidential information into this prototype.
- This project uses an experimental application ID, so it is not an upgrade path for a future release until the final ID and signing plan are decided.

## Checks

I can run `npm test` for the small URL helper test and `npm run sync` to copy the web assets to Android. Passing those alone does not prove the Android app compiles or works on a device. The earlier APK built in Actions and was reported working on a phone, but I still need the checks in `PLAN.md` before calling this a finished browser.

## GitHub Actions preview

The Android preview workflow builds only a debug APK and uploads it as an Actions artifact. The requested preview can also be published as a GitHub pre-release; it is not a release-signed APK. It audits production dependencies, runs the source tests, checks the APK archive and verifies its signing. It also uploads build notes on failure. Passing those checks does not prove the app works on a phone or that any VPN protection exists.

## Upload and download work - source only

The Android source now attempts file uploads through the Android file picker and starts ordinary HTTP and HTTPS downloads with Android DownloadManager. This is not yet built or phone-tested. Uploads accept selected content URIs with read access; capture and folder selection are not implemented. Closing a tab cancels its pending upload callback.

Downloads do not forward browser cookies or authorization headers, so signed-in downloads can fail. Blob and data downloads are not supported. Android 10 and newer use the public Downloads folder; on Android 9 and older downloads use app-specific storage and are deleted if the app is uninstalled. This is not yet the desktop downloads manager, and a started message does not mean the transfer completed.

## Domain blocker - source only

The source now copies the desktop domain list at sync and applies domain matching in Android WebView request interception. The blocker panel has switches for blocking and the bundled list, plus custom blocked domains and allowed top-level sites. This code has not been built or device-tested. Android WebView does not intercept all redirect and service-worker requests, so it cannot be assumed to match desktop coverage. It does not implement cosmetic filtering or a general extension blocker. If the list cannot load, the source reports that blocking is unavailable rather than claiming it is enabled.

## Vault work - source only

The current source has an experimental password-protected local vault with create, unlock, lock, add and remove controls. Its encrypted records use scrypt and AES-GCM in the desktop backup format; synthetic interoperability tests run in both directions against the desktop codec. The source has not been compiled, security-reviewed or tested on a phone. Do not put real passwords in it yet.

There is no autofill, editing, vault backup import/export UI, desktop profile migration, Android Keystore integration, configurable auto-lock timer or recovery if the master password is lost. Closing the panel and page lifecycle events request a lock, but memory erasure and background locking are not verified. Uninstalling the app removes its local vault, and there is no export UI yet. The included `scrypt-js` dependency is MIT licensed; its licence is copied into the app assets at sync.
