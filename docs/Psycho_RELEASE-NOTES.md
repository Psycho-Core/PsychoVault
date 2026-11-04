# PsychoVault {{VERSION}} - Windows x64 development preview

I build my own browser engine. This is a development preview I can test, not a finished browser.

## What I added in this build

- **An HTTPS policy of my own.** I read `Strict-Transport-Security` from an answer and keep the host and its expiry in my own store. The header only counts when it arrives over HTTPS, because anyone on the path could write it into a plain answer. `max-age=0` takes a promise back, `includeSubDomains` covers the subdomains, and the longest promise I honour is one year. The store is written into my profile as JSON, pruned when I load it, and it decides the scheme of the next request before a socket opens.
- **Mixed content refused instead of half-loaded.** An HTTPS page cannot pull a script, stylesheet, image, font, media file or frame over plain HTTP. I refuse all of them rather than the active ones only, because a page that silently loses its images looks broken and says nothing. A form on an HTTPS page that posts to an HTTP address is refused for the same reason: the body would go out in clear text.
- **Always use HTTPS, with one honest fallback.** A new setting upgrades every request rather than only the hosts that asked. A host with a stored promise never falls back. A host without one gets exactly one retry over HTTP when the upgraded address cannot be reached at all, and I count the fallback so it is visible rather than silent. Loopback is left alone, because my own development server rarely has a certificate.
- **My own filter engine.** I parse the subset of the usual list syntax I actually implement: host anchors with `||`, start and end anchors with `|`, `*` and `^` wildcards, the resource type options, `third-party` and `~third-party`, and `domain=` with exclusions. Exceptions sort ahead of blocks, so an exception always wins. A rule naming an option I have not implemented is refused with the line number and the reason rather than quietly weakened, and a cosmetic rule is refused the same way, because I do not rewrite page elements.
- **A blocker I can watch working.** Every tab counts what its own filter list refused and keeps the text of the rule that did it. My status bar shows `Blocked N here` for the page in front of me, and clicking it shows the rule. My settings page shows the rule count, the exception count, and the running totals for blocked requests, upgrades, mixed content refusals and form refusals.
- **Per-site exceptions that key off the page.** An exception names the site I am on, not the endpoint being fetched. That was wrong until a test of mine proved it: I was comparing the exception against the request's host, so a site I had excused still had its ads blocked unless the ad happened to be on the same host.
- **A list file, if I want one.** My starter list is mine and short, 33 rules covering the advertising and measurement endpoints that turn up on most pages. My settings page takes a path to a list file on disk, loads it on top of mine, and reports how many rules it gave me. I never download a list on my own.
- **A resource limit that says what it is.** My browser puts itself in a job object with a kill-on-unhandled-exception rule, plus a memory cap and a user-time cap when I set them. The settings page states plainly that page code runs in my process and is limited, not isolated. That is the truth of what this build does, and I would rather print it than call a resource cap a sandbox.

## What my audit of step 8 found

I tested the classes first, then wired them into the request path, then tested the path, and the order matters because most of what follows only showed up once the pieces were joined.

- **An exception was compared against the wrong host.** My per-site allowlist checked the request's host instead of the page's host, so excusing a site did nothing for the third-party requests that site made. My own test caught it. It now compares the page host, and falls back to the request host for a navigation where they are the same thing.
- **The always-HTTPS upgrade never happened.** An earlier edit to the shared request check did not apply, and my tests were passing because they only looked at the class, not at the fetch. The test that read the returned address proved it, and the upgrade is now in the one place every request goes through.
- **A job object would have killed my browser when I changed the cap.** My first version set `KILL_ON_JOB_CLOSE`, which ends every process in the job when the last handle closes. Changing the memory cap closed the handle and recreated the job, so changing a setting would have terminated the browser. I removed that rule, keep one job for the life of the process, and rewrite its limits in place.
- **A second thread writing the same counters as the window.** My totals were plain integers touched by a tab's worker thread and read by the window. They are atomics now, and the per-tab counters belong to the tab that owns them.

The full page is in `docs/Psycho_STEP8-AUDIT.md`, including what this environment could not test: no live TLS mixed content, no subscription-sized list, and no isolation test because there is no isolation to test.

## What I checked before calling this build done

- My engine test suite passed: **717 checks**. That is 69 more than the last build, and they cover the HTTPS store and its cap, the mixed content and form refusals, the upgrade and its fallback, the filter parser and matcher, the exceptions, and the block decision taken through the same fetch a tab uses.
- My source audit passed: **250 checks**, including that my filter engine is mine, that I bundle no third-party list, that every request goes through one shared check, and that my panel prints the sentence about isolation instead of a tick.
- My naming audit passed: **21 checks**. My plan audit passed: **377 checks**.
- The Windows executable was compiled and linked by the release workflow from this exact commit, and the release was published with the executable in it. If that build had failed, there would be no release to read this in.

## What I am still not claiming about my security work

- **Page code is not isolated from my browser.** There is no AppContainer profile, no second process and no restricted token for page code in this build. The job object limits memory and CPU; it is not a security boundary, and a page that finds a bug in my engine is in the same process as my window.
- **No CSP is enforced.** A page's `Content-Security-Policy` header is read and its effects are not applied yet, so my own mixed content rule is the only thing standing between a page and a cleartext subresource.
- **My HSTS store only knows what a site told me.** There is no preload list, no `includeSubDomains` inheritance beyond the label match, and clearing my profile clears the store.
- **A fallback is a downgrade by design.** With always-HTTPS on, a host that cannot answer over TLS gets one plain request, and a network attacker can make that happen. I count it, and I would rather it be a counted decision than a silent one.
- **My blocker does not hide page elements.** Cosmetic rules are refused by name, so a page keeps its ad-shaped holes. My list is short and mine, not a subscription, and it does not update itself.
- **No fuzzing yet.** My parsers have unit tests and hand-written edge cases, not a fuzz corpus.
- **The update manifest is not signed.** I verify a SHA-256 before offering an update; signing the manifest itself is still on my plan.

## What I am not claiming

- **YouTube and Netflix do not play.** My engine has my own media pipeline and my own Media Source Extensions, but no H.264 decoder, no AAC decoder, no VP8, VP9 or Opus, and no WebM. The streams those sites send are refused by name. DRM will only ever arrive as a separately licensed extension, never inside my engine.
- **Proton VPN is not bundled.** I have no Proton code in this build. My own transport and a panel I can point at a proxy are what exist, and a panel with a server in it is not proof of a working VPN.
- WebP and progressive JPEG are not decoded. My page says so in its diagnostics rather than showing a blank space without explanation.
- WebRTC is not implemented, so my extension-facing WebRTC policy setting is a stored preference, not a leak prevention I can demonstrate.
- My vault, password manager, translation, media keys and full legacy settings parity are not finished.
- This build is unsigned, so Windows SmartScreen may warn me.

## How I run it

I download **PsychoVault.exe** and double-click it. That is the whole install.

My engine and my page host are compiled into that one file, so nothing is unpacked and nothing sits beside it.

## How my updater reads this release

I publish `PsychoVault-update.json` with the version, tag and a SHA-256 for every asset. `PsychoVault.exe` is the update payload, so an update replaces one file. I verify the hash before I offer to install anything.

## No warranty

I provide this preview as-is, without warranty.
