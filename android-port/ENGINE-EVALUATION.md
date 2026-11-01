# Android browser engine investigation

The current Capacitor experiment uses Android WebView. It does not provide the Chromium extension host that the desktop browser and Proton extension need. Adding more buttons to it will not complete this port. I have not tested Proton or general extensions in either candidate below.

## Candidates checked

- [Titanium Browser](https://github.com/jqssun/android-titanium-browser/tree/5e9070a98b48f6b6cb239bf90b8718b91795b7c5) is a Chromium Android patch set, GPL-2.0 according to GitHub, based on Vanadium. Its README describes Chrome Web Store and local extension installation. This is a full browser build, not a drop-in Android WebView component. A [user reported Proton sign-in looping](https://github.com/jqssun/android-titanium-browser/issues/118); I have not reproduced or resolved it.
- [Bare Browser](https://github.com/BareBrowser/bare-browser/tree/4a9b2d9f96a636ba7c771caff0f36f3ce16e9453) is a Chromium Android patch set. GitHub lists its repository licence as BSD-3-Clause; Chromium, extensions, and other bundled components have their own notices and terms that must also be checked. Its build instructions say Chromium needs at least 100 GB free. Extension support is described by the project, but Proton compatibility has not been verified here.

These are candidates for investigation, not recommendations to ship or proof of compatibility. Neither has been integrated into PsychoVault. Replacing WebView means porting the desktop interface and features onto a full browser foundation, not simply plugging an extension into this experiment. It also needs a review of licences, security updates, signing, version codes, app data migration and build costs.

## What I can verify here

This environment has about 20 GB free, no Java or Android SDK configured, and no Docker command available. I cannot build either Chromium foundation here as it stands. I did not start a GitHub Actions build because the user asked me to minimise usage and neither candidate has been integrated or shown to run Proton. Before choosing a base, I need a reproducible build environment with the upstream requirements and tests of extension installation, Proton authentication and actual traffic routing, failure and leak behaviour on a device. A Chromium fork will also need ongoing security patch maintenance.

## Titanium source inspection

I fetched the Titanium repository at the pinned commit above and inspected its build scripts rather than assuming it was a reusable library. It applies Vanadium and Titanium patches to a complete Chromium checkout, builds `chrome_public_apk`, and uses an Android Chrome extension host and native extension popup code. Its bundled-extension mechanism stages signed CRX files as external extensions. The existing PsychoVault Proton directory is an unpacked extension, not a signed CRX that can simply be substituted into that mechanism. Repackaging it under the existing Proton extension ID would require a compatible installation mechanism or a valid signing key; the extension's public manifest key is not a private signing key.

The upstream scripts fetch Chromium and depot_tools from `chromium.googlesource.com` and also fetch Android/Chromium dependencies and filter lists from other hosts. The current sandbox restricts outbound access to a small allowlist and a direct request to Chromium's source host failed. This checkout has about 20 GB free, no Java, no Docker and no Android SDK configured; the Chromium foundation also needs substantially more disk than the current sandbox provides. I did not run the upstream script, install software system-wide, import its GPL patches into PsychoVault, or trigger Actions. A source mirror alone would not establish that all the other dependencies are reachable or that the resulting build supports Proton.

This is a concrete build-environment and integration blocker, not a licence decision or proof that Android Proton is impossible. The existing extension's GPL licence does not itself provide Proton's private signing key or grant separate trademark rights. Any eventual redistribution needs an appropriate review of source, licence, bundled assets and trademarks.
