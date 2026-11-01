# My document audit

## Scope

I inventoried the 42 tracked project Markdown files, historical harness notes and release-note text files that existed before this Android folder. I scanned them for em dashes, checked the main README and project guide for Android claims, and checked the Windows build guide's version examples against `app/package.json`. This was a mechanical and targeted review, not a line-by-line fact check of every historical test result or a guarantee that every sentence matches my voice.

I did not rewrite third-party licence text, generated assets, the spellcheck word list, or historical harness output. These are not ordinary project prose, and licence wording and test evidence should not be altered to change their voice. The punctuation-only changes to `VERIFICATION.md` do not change its recorded results.

## Changes recorded in the earlier audit

- Kept the new-tab picture near the top of the main README and added an honest link to this Android experiment. No APK, Proton connection or extension support is claimed for it.
- Added the Android folder and its limits to `PSYCHO.md`.
- Replaced em-dash separators in the project prose and verification notes with ` - `. I left licence text and historical harness output alone.
- Changed the Windows build guide's hard-coded 1.0.35 output examples to `<version>`, because the actual filename comes from `app/package.json`.
- Wrote this folder's README and plan in plain first-person language and stated what still needs proof.

## Follow-up

I corrected the Android preview, GitHub Actions and phone-test status in my main README, project guide and Android documentation. I kept the older audit findings as historical evidence instead of changing what was observed at the time.

## Still to do

I have not manually fact-checked all 42 older documents or rewritten every older sentence in first person. Old release notes and verification entries describe historical builds and must not be treated as proof of this Android preview. A fuller editorial and factual review is still needed before saying every document sounds the way I want.
