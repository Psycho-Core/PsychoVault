# My verification record

I am separating what passed from what failed, what is blocked, and what I have not run.

## Passed

- Local GCC engine tests: 33 checks passed.
- Local GCC address/undefined-behaviour sanitizer run: 33 checks passed, no sanitizer report.
- Local source-boundary audit: 68 passed, 0 failed.
- Local plan audit: 377 passed, 0 failed.
- Windows hosted MSVC build: browser, renderer, headless CLI, tests and sandbox probe compiled.
- Windows hosted engine tests: passed.
- Windows hosted headless fixture: rendered a PNG from HTML/CSS/JavaScript.
- Windows hosted native home capture: passed.
- Windows hosted native fetched-page capture: passed, and my smoke test now fails if the fetched page does not produce a frame.

## Failed and fixed

I fixed actual hosted failures instead of hiding them: MSVC environment discovery, Windows header order, narrowing warnings, JSON number lifetime, profile serialization, renderer worker handle capture, canvas clipping, AppContainer attribute count, and sandbox probe linking/output.

## Open or not run

- AppContainer file-isolation harness: blocked on the hosted console runner. The child process exits with STATUS_DLL_INIT_FAILED before it can report token/file state. My native GUI fetched-page smoke test does require a renderer frame and passed, but I am not counting that as a file-denial proof.
- Direct network socket denial probe: not run in the current probe. I am not claiming direct network denial from the file-isolation test.
- Proton VPN sign-in, proxy routing/auth, fail-closed behaviour, DNS and WebRTC policy: not run.
- Full reference UI parity: not run.
- Pinned WPT/Test262 subsets: not run.
- Android, installer, signing, updater and migration: not run.

## Build links

- PR #63: https://github.com/Psycho-Core/PsychoVault/pull/63
- Development workflow: https://github.com/Psycho-Core/PsychoVault/actions/workflows/owned-engine.yml
