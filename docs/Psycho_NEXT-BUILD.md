# My next testable Windows build

I am working through my browser one step at a time, in the order that gets me something I can actually use. I am not treating any merge as a complete browser.

## The order I am working in

| Step | Work | State |
| --- | --- | --- |
| 1 | One executable that renders my pages itself | Done, published as 0.4.0 |
| 2 | Modern JavaScript: promises, async/await, regular expressions, fetch, modules | Patterns, promises, async/await and fetch are in this build; modules, iterators and typed arrays are still to come |
| 3 | Browsing fundamentals: cookies, local storage, forms, find in page, zoom | Not started |
| 4 | More image formats and canvas | Not started |
| 5 | CSS and layout correctness: flexbox, grid, positioning, overflow | Not started |
| 6 | Media: audio, video and Media Source Extensions | Not started |
| 7 | VPN transport, including the TLS-to-proxy path Proton needs | Not started |
| 8 | Security: operating-system containment for page code, certificate policy, ad blocking | Not started |
| 9 | Conformance measurement with Test262 and web-platform-tests subsets | Not started |
| 10 | Published release and a verified updater cycle | Partly built, not yet published |

## What this build adds over 0.4.0

- My own pattern engine in `engine/Psycho_regex.cpp`: parser, classes and ranges, `\\d \\w \\s` and their negations, anchors with the multiline flag, greedy and lazy quantifiers, capturing and named groups, back references, lookahead, escapes and the usual flags. Every match runs against code points decoded from UTF-8 and stops at a step and nesting limit instead of looping forever.
- Pattern literals lex as patterns rather than as division, and `match`, `search`, `split`, `replace` and `replaceAll` accept a pattern, honour the global flag, expand `$1` to `$99` and ``$&``, and accept a function as the replacement.
- Promises with a state, a result and a reaction queue: `then`, `catch`, `finally`, `resolve`, `reject`, `all`, `race`, `allSettled` and `queueMicrotask`. Queued work runs when the current script or timer finishes, and my collector treats queued work as a root so a promise cannot be freed while it is still waiting.
- `async` and `await` on function declarations, expressions and arrows, including the `async x => x` form.
- `fetch()` that goes through my own network policy and hands back a response with `ok`, `status`, `headers.get`, `text()` and `json()`.

I want to be straight about one difference: my engine runs one statement at a time, so `await` finishes its promise before the rest of the current script continues, instead of returning to the event loop first. While a script waits, my page still fires timers that are due, so `await new Promise(r => setTimeout(r, 50))` works. My page also refuses to wait forever: an await that nothing can settle raises an error after five seconds rather than freezing the tab.

## What step 1 changed

- My engine, my page host and my shell now link into one `PsychoVault.exe`. There is no renderer process, no payload to unpack and no resource compiler in the build.
- My page host owns one engine page per tab on that tab's worker thread, fetches through my network policy, decodes images in my process and reports frames straight to my window.
- I deleted the helper process, the framed IPC and the payload loader rather than leaving dead code behind. My git history keeps that work if I want the isolation boundary back later.
- My single-file gate now copies only `PsychoVault.exe` into an empty folder, renders a fetched page with images, checks the capture is a real PNG, and checks the folder still holds exactly one file afterwards.

## Evidence required before I hand over a preview

- The engine and page-host tests pass locally and on Windows. They now stand at 337 checks, including pattern matching, promise ordering, async/await and a `fetch` round trip through my page host.
- The single-file gate passes on hosted Windows for the exact commit I publish.
- A local HTTP test proves main-document, stylesheet, script and PNG loading through my page host.
- The Windows artifact contains only my executables, notices and build metadata - no profile, credential or source-control directory.

## Still outside this preview's completion claim

The complete browser still needs the rest of the steps above, the live Proton gate, the remaining legacy features and vault migration, broader compatibility, performance comparison, accessibility, and signed installation and update gates. I will not label those complete because this preview builds.

## What I have learned about containment

I spent a long time on a contained renderer process and did not get it to answer its own probe. The evidence I do have is that the trimmed renderer loaded only kernel32, advapi32, ws2_32, bcrypt and gdi32, and that removing user32 stopped a hard crash - but it still never wrote a frame. I am not going to describe containment as working until a page renders inside it, and I am not going to make it a prerequisite for a browser I can test.
