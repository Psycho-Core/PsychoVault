# My implementation status

I am building the browser in the open, but I am not calling this finished. This page records what the current source actually does and what still has to pass.

## Implemented in source

| Area | Current implementation | Evidence I still require |
| --- | --- | --- |
| Base | URL parsing/resolution, UTF-8, JSON, atomic profile writes, OS random bytes | Unit tests and fuzzing |
| HTML/DOM | Tokenizer, tree builder, entities, forms, selectors, DOM mutation | Pinned WPT subsets |
| CSS | Syntax, cascade, inheritance, box model, block/inline/flex layout, media queries | Pinned WPT subsets and visual tests |
| JavaScript | Parser, tree-walking interpreter, closures, objects/arrays, mark-sweep GC, timers, DOM events | Pinned Test262 subsets and long-run stability |
| Painting | Display list, software raster, owned PNG decoder/encoder, image blitting, Windows OS text rendering | Visual regression and performance tests |
| Shell | Native Windows window, per-tab renderer sessions, omnibox, Unicode input/paste, stop/reload, home, bookmarks/history, General settings | Full reference UI parity and hosted interaction tests |
| Networking | Broker-owned WinHTTP GET, TLS validation, redirects, size limits | Proxy, cookies, cache, POST, streaming and leak tests |
| Process model | Renderer executable, framed binary-resource IPC, private-desktop AppContainer launcher, job object, required token/file/TCP/UDP probe | Hosted sandbox proof, escape tests and crash recovery |
| Profile | Validated owned-profile JSON files, corruption backups, atomic replacement, private mode, active-tab session restore | Migration fixtures and rollback |
| Builds | GitHub-hosted Windows build, engine tests, sandbox probe, GUI captures | Repeated green runs and signed release gates |

## Not implemented yet

I have not implemented the extension host, Proton VPN, PAC/proxy ownership, WebRTC, full JavaScript/CSS compatibility, non-PNG image formats, media, canvas, workers, IndexedDB, Web Crypto, vault migration, updater signing, installer, Android, or the full 125-control settings contract.

I will not show fake controls for those features. I will add them when the behaviour behind them exists.

## Language decision

My earlier plan proposed Rust. I am implementing the first engine in dependency-free C++20 because the available compiler lets me compile and test the engine immediately, and the Windows SDK gives me direct access to the platform APIs I need. I am recording this as a change, not pretending the plan always said C++.

I am using RAII, bounded containers, explicit limits, compiler warnings, address/undefined-behaviour sanitizers and a restricted renderer process. I am not claiming C++ gives me Rust's memory-safety guarantees.

## Current verification

- Local GCC engine tests: 33 checks passed.
- Local GCC address/undefined-behaviour sanitizer run: 33 checks passed, no sanitizer report.
- Windows hosted build: PR #63 builds and runs the engine tests, headless render and native GUI captures. My direct Winsock sandbox probe failed DLL initialization under AppContainer, so I split the gate: file/token isolation is tested, direct network denial is not claimed. See `docs/VERIFICATION.md`.
- Proton live test: not run.
- Full UI parity: not run.
- Android: not run.
