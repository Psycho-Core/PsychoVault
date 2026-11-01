# PsychoVault Android experiment

I am building an Android version separately from my Windows browser. This is an **experimental source project**, not a finished browser or a published APK. I have not verified this build on a phone yet.

**Proton VPN does not work in this preview.** It is not bundled, connected or protecting traffic. General extensions, Dark Reader, the ad blocker, private browsing, the vault, bookmarks, history, downloads, uploads and settings are not implemented either. Do not use this preview for sensitive browsing or assume it has the privacy or security features of the Windows browser.

## What is here

- `www/` - a small responsive interface inspired by my desktop UI. It uses my desktop icon but does not yet reuse the full desktop interface or have visual parity.
- `android/` - the Capacitor Android project and a custom browser-view plugin. Websites use separate Android WebViews without the Capacitor bridge.
- `PLAN.md` - the work and test gates needed before this can become my Android browser.
- `BUILD-ANDROID.bat` - my experimental Windows debug build entry point. It writes `Release-Notes-Android.txt` even if a prerequisite or build step fails.

This preview uses Android WebView to test the shell, navigation and view placement. Android WebView is not a general extension host. My plan still requires Proton VPN inside PsychoVault and general extension support. I have not proven that the Firefox Proton extension works in GeckoView, and I will not call this preview protected.

## Build on Windows

I need Node.js 22 or newer, Android Studio, a compatible Android SDK and Java/JDK configured for the Android Gradle build. The Gradle wrapper and npm need network access on a first build. I can use my existing repository path - I do not need to move it. I should update Android Studio and install the SDK versions requested by the build.

Double-click `BUILD-ANDROID.bat`. If it succeeds, the debug APK should be at `android\app\build\outputs\apk\debug\app-debug.apk`. If it fails, I read the **first error** in the window and the generated release notes. I have not run this batch file on Windows or verified the APK in this environment.

For a manual build from this folder:

```bat
npm ci
npm run sync
npm test
cd android
gradlew.bat assembleDebug
```

A debug APK is for testing. A shareable release needs a separate signing and update plan. I must not commit signing keys, credentials or real browser data.

## Current limits

- A tab is a separate Android WebView, but storage is not isolated per tab. This is not private browsing or a container implementation.
- Only HTTP and HTTPS navigation is accepted; the Android network policy may block cleartext HTTP. Other schemes and external-app links do not work yet.
- File pickers, downloads, popups, media permissions and Android back-button integration are not implemented. Website data is not migrated from Windows.
- Tab state is not restored after the process is killed. Site security, WebView layering, keyboard and rotation behaviour need real-device testing.
- The app starts at Google and sends searches there. Do not enter confidential information into this prototype.
- This project uses an experimental application ID, so it is not an upgrade path for a future release until the final ID and signing plan are decided.

## Checks

I can run `npm test` for the small URL helper test and `npm run sync` to copy the web assets to Android. Passing those does not prove the Android app compiles or works on a device. I need a real debug build, device tests and the checks in `PLAN.md` before making any release claims.

## GitHub Actions preview

The Android preview workflow builds only a debug APK and uploads it as an Actions artifact. It does not publish a GitHub release or produce a release-signed APK. It audits production dependencies, runs the source tests, checks the APK archive and verifies its signing. It also uploads build notes on failure. Passing those checks does not prove the app works on a phone or that any VPN protection exists.
