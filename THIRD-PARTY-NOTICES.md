# Third-party notices

PsychoVault's own code is MIT licensed. Third-party code in `app/` and the Android engine inputs has its own licences, as described below.

## Electron 45.1.0
MIT licensed. Bundles Chromium and Node.js. https://github.com/electron/electron/blob/main/LICENSE
Source for the exact runtime shipped: https://github.com/electron/electron/releases/tag/v45.1.0

## Chromium
BSD 3-Clause and a long list of component licences. The full text ships with the
runtime as `LICENSES.chromium.html` in the install folder. Source:
https://chromium.googlesource.com/chromium/src/

## Node.js
MIT licensed. https://github.com/nodejs/node/blob/main/LICENSE

## Blocklist
The 262-domain list in `app/blocklist.json` is a hand-curated set of known advertising
and analytics hostnames, written for this project. It is not derived from EasyList or
any other filter subscription, and it carries no licence obligation from them.

## Proton VPN browser extension - GPL-3.0

`app/builtin/proton-vpn/` is a build of Proton AG's browser extension, **Proton VPN: Fast &
Secure 1.3.6**, and is licensed **GPL-3.0**, not MIT. It is redistributed here under that licence.

| | |
| --- | --- |
| Copyright | Proton AG |
| Upstream source | https://github.com/ProtonVPN/proton-vpn-browser-extension |
| Commit built | `71d7d86f164a2ee8e50ce5a389c9e5cbe0fd6883` |
| Build | `npm ci --legacy-peer-deps && npm run build`, no source changes |
| Full licence text | `app/builtin/proton-vpn/LICENSE` |

The corresponding source is the commit named above, obtainable from Proton's public repository.
PsychoVault does not modify, patch, or add to that source; a newer upstream commit can be rebuilt
with the same two commands and dropped into the same folder.

PsychoVault's own code is MIT licensed. The extension is a separate program loaded at runtime
through `session.loadExtension()`, and PsychoVault runs without it. A modified or differently
integrated distribution needs its own GPL and third-party licence review. An upstream source link
alone does not guarantee that every corresponding-source or installation-information requirement
has been met for a particular distribution. Proton trademarks and service terms are separate.

No account, subscription, credential or server list ships with it. Signing in is done by you,
in the extension, against Proton's own API.

## Other extensions

Alongside Proton, the desktop app bundles Dark Reader, described below. `Install .crx...` unpacks a file **you** supply
into your own profile folder (`extensions/`). Extension loading is Electron's
`session.loadExtension`. No signature is verified for a `.crx` - see the Settings text.

## Extension API shim

`app/ext-shim.js` and `app/ext-proxy.js` are PsychoVault's own MIT code. They implement
`chrome.proxy` and `chrome.privacy` for extension contexts by calling Electron's
`session.setProxy()` and Chromium's WebRTC IP handling policy. They contain no Proton code.

## Spell-check word list

`app/spell-words.txt` is the first 15,000 entries of `en_50k.txt` from hermitdave/FrequencyWords
(https://github.com/hermitdave/FrequencyWords, MIT license, "(c) 2016 Hermit Dave"), which in turn
derives its counts from the Google Books English n-gram corpus. Words were kept in frequency
order, lower-cased, and limited to ASCII letters and apostrophes; the counts were dropped. The
frequency order is what makes the top suggestion the common one.

An earlier candidate - the 10,000-word list in `first20hours/google-10000-english` - was
downloaded, tested, and then **rejected and removed** before shipping: its LICENSE.md says
educational and personal/research use is permitted but recommends against commercial use without
licensing the data from the Linguistic Data Consortium. PsychoVault is distributed software, so
that list does not belong in it.

## Fonts and icons
No external fonts. Every icon in the interface is inline SVG drawn for this project.
The application icon is an original PsychoVault mark.

## Codecs
Electron ships Chromium's ffmpeg build with H.264 and AAC support. HEVC is not included.
Widevine CDM is not included, so DRM-protected playback is unavailable.

## Dark Reader

Dark Reader 4.9.134 is Copyright (c) 2026 Dark Reader Ltd. and licensed under the MIT License. The full license is included in `app/builtin/dark-reader/LICENSE`. Source: https://github.com/darkreader/darkreader/tree/d12521bb3120ecb0d796248020c497d53b300496.

## Android Chromium foundation - Titanium and Vanadium

The unfinished Android restart includes Titanium Browser for Android build inputs under `android-port/engine/titanium/`, from commit `5e9070a98b48f6b6cb239bf90b8718b91795b7c5`, licensed GPL-2.0. Its licence is included in that directory. The preparation script fetches Vanadium at commit `0202b7badff99434822ba5e32e0d738792868455` separately; Chromium, Vanadium, extensions and other dependencies have their own notices. The staged build process also specifies the Titanium companion extension as a pinned external download; it is not included in this repository.

This is not a complete Android distribution licence review. Before shipping an Android APK I need to verify all applicable component licences, corresponding source, install information, branding and codec obligations for the actual build. See `android-port/PROTON-LICENCE.md` and `android-port/EXTENSION-ENGINE-DECISION.md`.
