# My verification record

I am separating what passed from what failed, what is blocked, and what I have not run.

## Passed

- Local GCC engine tests: 61 checks passed on the current preview branch (previous merged baseline: 33).
- Local GCC address/undefined-behaviour sanitizer run: 33 checks passed on the merged baseline. I am rerunning sanitizers for the current preview before I call it verified.
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

- Current preview hosted Windows build: not run yet at the time I wrote this note. I made the AppContainer token/file/TCP/UDP test a required step and added a browser fixture that requires a decoded PNG image.
- Current preview tab-session, Unicode-input and private-profile behaviour: local code and unit checks exist, but I still need the hosted GUI evidence for the complete interaction path.

- Previous AppContainer file-isolation harness: blocked on the hosted console runner. The child process exited with STATUS_DLL_INIT_FAILED before it could report token/file state. I changed the launcher to use a private window station/desktop, valid inherited standard handles, overlapped broker I/O and a framed renderer probe. I am not counting the fix as proven until the required hosted test passes.
- Direct TCP/UDP socket denial probe: implemented in the renderer probe, but hosted evidence is pending. I am not claiming direct network denial until the required test passes.
- Proton VPN sign-in, proxy routing/auth, fail-closed behaviour, DNS and WebRTC policy: not run.
- Full reference UI parity: not run.
- Pinned WPT/Test262 subsets: not run.
- Android, installer, signing, updater and migration: not run.

## Build links

- PR #63: https://github.com/Psycho-Core/PsychoVault/pull/63
- Development workflow: https://github.com/Psycho-Core/PsychoVault/actions/workflows/owned-engine.yml
