# Android and desktop feature audit

## Scope and evidence

I compared the current Android source with `app/main.js`, `app/blocking-rules.js`, `app/blocklist.json`, the desktop UI and the Android plugin and interface. I did not inspect or run a new APK. The published sidebar debug APK predates the current uncommitted changes. It does not contain the source-only features listed below. Source tests do not prove Java compiles or that anything works on a phone.

## Feature comparison

| Feature | Desktop | Android source | Result |
| --- | --- | --- | --- |
| Interface, rail, tabs | Electron desktop interface and settings | Simplified responsive recreation and auto-hiding rail | Not the same UI; layout and touch still need device checks |
| Navigation and tabs | Electron tab views | Separate WebViews, HTTP/HTTPS navigation, back, forward and reload | Partial; Android back, popups, external links, errors and lifecycle remain open |
| Session | Desktop session recovery and restore | Optional restoration of up to 50 addresses and selected tab | Partial; no back/forward stacks, default differs and process-death handling is untested |
| Bookmarks and history | Desktop stores and UI, history can be disabled | Local WebView storage, list, removal and clear history | Partial; no history-off setting or desktop data migration; not encrypted |
| Search and appearance | Custom search, desktop themes, accent, contrast and motion controls | Google, DuckDuckGo and Bing; five source themes | Partial; search default differs, desktop settings and UI absent |
| VPN | Bundled Proton extension plus Electron extension proxy and authentication shims | Button only warns; WebView has no extension host | Missing. No Proton connection or routing claim is valid |
| Extensions and Dark Reader | Bundled Dark Reader and general extension integration with compatibility limits | No extension host | Missing |
| Domain blocker | Domain list, custom domains and top-level-site exemptions in Electron request handler | Source copies the same bundled list; native WebView interception and controls added | Unbuilt and untested. Redirect and service-worker coverage differs, so not desktop parity |
| Downloads and uploads | Desktop download controls and native file selection | Initial Android file picker and DownloadManager code | Unbuilt and untested; authenticated and blob downloads, capture and folder selection remain unsupported |
| Vault | Existing local desktop vault and backup/migration | Experimental encrypted local vault in source | Partial; no autofill, edit, backup UI, profile migration or device/security testing |
| Privacy and security | Desktop private sessions, containers, permissions and privacy settings | Website views isolated from the trusted bridge, but share WebView storage | Missing private mode, containers, permission controls and desktop privacy settings. No isolation or leak claim |
| Workspaces, sidebar services and speed dial | Desktop controls and persistence | No equivalent | Missing |
| Performance, media, translation, spelling, panic button and mods | Desktop feature controls | No equivalent | Missing |
| Updates and diagnostics | Desktop updater and diagnostics | No Android updater or equivalent diagnostics | Missing; Android update requires same application ID and compatible signing key |
| Signing and installation | Desktop release signing workflow | Source-only stable signing configuration and manual workflow | Unverified; first install with a different key cannot update an incompatible installed build, and uninstall can delete its data |

## Checks for this audit

- Android Node source tests: 20 passed. Blocker Java assertions are static and do not execute its matcher.
- `npm run sync`: passed. The copied blocker list matches `app/blocklist.json` byte for byte.
- `npm audit --omit=dev --audit-level=moderate`: no production vulnerabilities reported.
- Java source parsed with a syntax parser. It has not been compiled against the Android SDK.
- No new APK, native test, phone test, VPN authentication, routing or leak test was performed. No GitHub Actions run was started.

## Important limitations

The Android blocker uses WebView's `shouldInterceptRequest`; this is not Electron's request interception. In particular, redirect and service-worker coverage is not equivalent. It does not provide cosmetic filtering. Failed list loading disables its policy and reports an error.

The current WebView foundation cannot run the desktop Proton extension or generally install Chrome extensions. A full extension-capable browser foundation is needed and must be integrated, maintained and tested before Proton can be claimed to work. See `ENGINE-EVALUATION.md` and `PLAN.md`. I should not call this a finished port until the missing features and their behaviour have been implemented and verified.

## Later source update

I added an experimental vault and tested its encrypted backup codec with synthetic desktop-compatible vectors in both directions. The source is unbuilt, no Android Keystore integration or backup UI exists, and it must not yet be used for real credentials. See `README.md`.
