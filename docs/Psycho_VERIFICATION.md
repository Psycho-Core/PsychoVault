# My verification record

I am separating what passed from what failed, what is blocked, and what I have not run.

## Passed

- Local GCC engine tests: 61 checks passed on the current preview branch (previous merged baseline: 33).
- Local GCC address/undefined-behaviour sanitizer run: 61 checks passed on the current preview, no sanitizer report.
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

- Hosted Windows preview build run 41574713466: passed. It built with MSVC, ran 61 engine checks, rendered the headless fixture, captured the native home window, fetched a local page with CSS/JS/PNG through the sandboxed renderer, and passed the required AppContainer token/file/TCP/UDP gate from the browser GUI process. I restored conservative job UI restrictions and passed the final preview check in run 41575214798.
- Tab-session, Unicode-input and private-profile behaviour: local code and unit checks exist, and the hosted GUI fetched-page/image smoke passed. I still need a dedicated hosted interaction test for tab switching, Unicode input and private-profile writes before I call those fully verified.

- The old console AppContainer harness was blocked by STATUS_DLL_INIT_FAILED. I fixed the launcher with a private low-labelled window station/desktop, valid inherited standard handles, overlapped broker I/O and a framed renderer probe. The required gate now runs from the browser GUI process, which is the same context that renders pages, and it passed in run 41574713466.
- Direct TCP/UDP socket denial probe: passed in the required hosted GUI sandbox gate in run 41574713466. This proves the probe child could not create TCP or UDP sockets; it is not a full Proton or WebRTC leak test.
- Proton VPN sign-in, proxy routing/auth, fail-closed behaviour, DNS and WebRTC policy: not run.
- Full reference UI parity: not run.
- Pinned WPT/Test262 subsets: not run.
- Android, installer, signing, updater and migration: not run.

## Build links

- PR #63: https://github.com/Psycho-Core/PsychoVault/pull/63
- Development workflow: https://github.com/Psycho-Core/PsychoVault/actions/workflows/owned-engine.yml

- Preview build links: https://github.com/Psycho-Core/PsychoVault/actions/runs/41574713466 and https://github.com/Psycho-Core/PsychoVault/actions/runs/41575214798
