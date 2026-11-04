# PsychoVault {{VERSION}} - Windows x64 development preview

I build my own browser engine. This is a development preview I can test, not a finished browser.

## What I added in this build

- **A conformance runner of my own.** `tools/Psycho_conformance.cpp` walks a Test262 tree and runs every test through my parser and my interpreter. It reads each test's front matter for its flags, its includes and the error it expects, gives every test the harness files it asks for, and writes one line per test. Each test runs in its own child process on Linux, so a test that crashes my interpreter is recorded as a crash instead of ending the measurement.
- **A measured number.** Test262 at `831359a4192ef83b8c50f91363d2ff739fe550f6`: 54,058 tests ran, **5,009 passed**, 1,062 failed an assertion, 22,672 threw something unexpected, 16 crashed my interpreter, and 25,299 were skipped with the reason named (modules and strictness flags for 3,454, features I do not have for 21,845). That is 17.4 percent of the tests my engine can attempt. The full page is `docs/Psycho_CONFORMANCE.md`, and the numbers came from a run, not from an estimate.
- **Six language pieces the measurement proved were missing.** A `switch` statement (my parser did not have one at all), the comma expression, the `Function` constructor, error values that are objects with a `name` and answer `instanceof`, a `RangeError` a page can catch when recursion runs out of stack, and `Object.assign` refusing a null target instead of dereferencing it. Those six moved the corpus from 4,783 passes to 5,009 and the crash count from 23 to 16.
- **A bug in my own headless path.** The four-argument `fetch_url` dropped its timeout and its byte limit and forwarded zeros, so any request made that way refused its own body as too large. It was found by my new tunnel test, not by luck.
- **A tunnel test that dials.** My VPN page has a "Test my tunnel" button that makes one real request through the configured server on a worker thread and prints what came back - the status, the scheme and the server it used, or the exact reason it failed. A connected label nobody tested is not evidence, so the panel now carries the words of the last attempt.
- **A conformance job.** `.github/workflows/Psycho_conformance.yml` builds the runner on a hosted runner, fetches the corpus at the pinned commit, runs it and keeps the per-test record as an artifact. The number in my documents can be re-derived from a run.

## About Proton VPN

I did not add Proton VPN in this build, and I am not going to describe a panel as a connection.

Their extension source is not reachable from here: `ProtonVPN/browser-extension`, `ProtonVPN/extension` and `protonvpn/browser-extension` all answer 404 through the GitHub API, and I will not reconstruct their client from a description. Even with the source in hand, a live session needs an account and a reachable Proton endpoint, and this environment has neither, so "connected" could only ever have been a label I typed.

What I did instead is make the thing a Proton endpoint would use testable: my own SOCKS5 and HTTPS proxy transport, a fail-closed route, and a connect test that dials the configured server, sends a real request through it and reports the answer in the server's own words. If you point it at a Proton endpoint on your machine and press the button, it will tell you whether the tunnel came up rather than claiming it did.

## What I checked before calling this build done

- My engine test suite passed: **742 checks**, 24 more than the last build, including the six language pieces above and the tunnel probe against a loopback SOCKS5 proxy.
- My tunnel probe was tested two ways: a real SOCKS5 proxy on a loopback port answers, and a dead port is reported as a refusal with a reason. With no server configured it says there is nothing to dial.
- My source audit passed: **262 checks**. My naming audit passed: **23 checks**. My plan audit passed: **377 checks**.
- The Windows executable was compiled, tested and published by the release workflow from this commit.

## What I am not claiming

- **YouTube and Netflix do not play.** My engine has my own media pipeline and Media Source Extensions, but no H.264, AAC, VP8, VP9 or Opus decoder and no WebM. DRM can only ever arrive as a separately licensed extension, never inside my engine.
- **Proton VPN is not bundled and no Proton session was run.** What exists is my own transport and a test that dials whatever server I configure.
- **My conformance number is a language number, not a browser number.** Test262 does not measure my HTML, CSS or layout, and I have not run a WPT subset: their harness needs modules and async iteration, which my engine does not have yet.
- **The sixteen crashes are real.** They are recorded per test in the report and are on my list; a crash in my interpreter is worse than a missing feature.
- WebP and progressive JPEG are not decoded. My page says so in its diagnostics rather than showing a blank space without explanation.
- WebRTC is not implemented, so my extension-facing WebRTC policy setting is a stored preference, not a leak prevention I can demonstrate.
- Page code runs in my browser process. A job object limits its memory and CPU time; it is not isolation.
- My vault, password manager, translation, media keys and full legacy settings parity are not finished.
- This build is unsigned, so Windows SmartScreen may warn me.

## How I run it

I download **PsychoVault.exe** and double-click it. That is the whole install.

My engine and my page host are compiled into that one file, so nothing is unpacked and nothing sits beside it.

## How my updater reads this release

I publish `PsychoVault-update.json` with the version, tag and a SHA-256 for every asset. `PsychoVault.exe` is the update payload, so an update replaces one file. I verify the hash before I offer to install anything.

## No warranty

I provide this preview as-is, without warranty.
