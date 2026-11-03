# My next testable Windows build

I am continuing from PR #63. I am not treating that merge as a complete browser.

## What I am fixing next

1. I am making the AppContainer file and network denial tests required again. I am fixing process startup and handle ownership rather than hiding a failed sandbox check.
2. I am keeping each tab's page and renderer alive when I switch tabs. I am adding real stop/reload, restored active-tab state, Unicode input and useful navigation errors.
3. I am preserving binary resource bytes and implementing an owned PNG decoder with bounded decompression. I am fixing image layout and painting instead of drawing an image placeholder for a valid PNG.
4. I am strengthening profile persistence: validation, corruption recovery, atomic replacement, private-mode separation and settings that actually change request or browser behaviour.
5. I am bringing the native new-tab and settings presentation closer to my saved screenshots. I will show an honest unavailable state for a feature whose backend is not implemented.
6. I am adding repeatable tests and a portable Windows ZIP with the browser, renderer, test instructions and SHA-256 checksums. The ZIP is a development preview, not a signed release.

## Evidence required before I hand over the preview

- The owned engine unit and regression tests pass locally and on Windows.
- Address/undefined-behaviour sanitizer tests pass locally.
- A real AppContainer child reports its token, cannot read a broker-owned sentinel and cannot create a network socket. These are required checks.
- A local HTTP test proves main-document, stylesheet, script and PNG resource loading through the broker.
- Native GUI tests prove that a tab switch keeps its page, input reaches the intended tab, an actual network failure is reported as a failure and private mode does not write browsing data.
- The Windows artifact contains only the built executables, notices and test/build metadata - no profile, credential or source-control directory.

## Still outside this preview's completion claim

The complete browser still needs the full extension host and live Proton gate, the remaining legacy features and vault migration, broader JavaScript/CSS compatibility, performance comparison, accessibility and signed installation/update gates. I will not label those complete because this preview builds.

## Research for this slice

I reviewed Microsoft's Win32 documentation at `MicrosoftDocs/win32` commit `ca8274decead49403d70ceeed4af8d4ca74eaf11`, especially AppContainer isolation, process/window-station connection and WinHTTP handle lifetime. I am using those platform APIs, not copying a browser implementation. I am keeping renderer handles owned by the worker that uses them, using overlapped broker I/O, and giving sandbox children only the access they need.
