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

## Extensions

Nothing from an extension store is bundled. `Install .crx...` unpacks a file **you** supply into
your own profile folder (`extensions/`), and the app ships no extension code. Extension loading is
Electron's `session.loadExtension`. No signature is verified for a `.crx` - see the Settings text.

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
