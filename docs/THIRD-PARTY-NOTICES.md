# Third-party notices

PsychoVault's own code (`app/`) is MIT licensed. It runs on top of the following.

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

## wireproxy (legacy file in the repository, not shipped)

`app/tools/wireproxy.exe` is a legacy windtf/wireproxy v1.1.3 binary, ISC-licensed. It is
**not** packaged in the installer or the easy build, and PsychoVault does not start it. The
VPN path is the bundled Proton VPN browser extension, not a local wireproxy child process.

- Source: https://github.com/windtf/wireproxy (ISC)
- The 10,666,496-byte exe kept in the repository: sha256
  `b176b561fd8bf15d828fcab484cfd5b4fb941cb9f61807901ca64b955af27e1f`

WireGuard is a registered trademark of Jason A. Donenfeld; this project is not affiliated with it.

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
