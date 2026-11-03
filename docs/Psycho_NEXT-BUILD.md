# My next testable Windows build

I am working through my browser one step at a time, in the order that gets me something I can actually use. I am not treating any merge as a complete browser.

## The order I am working in

| Step | Work | State |
| --- | --- | --- |
| 1 | One executable that renders my pages itself | Done, published as 0.4.0 |
| 2 | Modern JavaScript: promises, async/await, regular expressions, iterators, fetch, modules | Patterns, promises, async/await, iterators, `Symbol`, `matchAll` and `fetch` are in; typed arrays and modules are still to come |
| 3 | CSS and layout: positioning, flexbox, grid, transforms | Done in this build for positioning, flexbox, grid and translate/scale transforms |
| 4 | Browsing fundamentals: cookies, local storage, forms, find in page, zoom | Done in this build: my own cookie jar, local and session storage with a quota, GET and POST forms, the history API, find in page and zoom |
| 5 | More image formats and canvas | Not started |
| 6 | Media: audio, video and Media Source Extensions | Not started |
| 7 | VPN transport, including the TLS-to-proxy path Proton needs | Not started |
| 8 | Security: operating-system containment for page code, certificate policy, ad blocking | Not started |
| 9 | Conformance measurement with Test262 and web-platform-tests subsets | Not started |
| 10 | Published release and a verified updater cycle | Publishing on every build; the install and rollback half is not tested |

## What this build adds over 0.6.1

- **A cookie jar of my own.** I parse `Set-Cookie` myself, with the name, value, domain, path, expiry date, `Max-Age`, `Secure` and `HttpOnly`. A cookie for a domain I am not talking to is refused, a host-only cookie only returns to the host that set it, a `Secure` cookie never goes out over plain HTTP, and a script sees `document.cookie` without the `HttpOnly` entries. My network layer now reads every `Set-Cookie` a response carries and sends the right `Cookie` header on the next request, including subresources and downloads. The third-party toggle really blocks a cross-site cookie now, because my policy knows which host the page belongs to.
- **Local and session storage.** `localStorage` and `sessionStorage` work as properties and as `getItem`, `setItem`, `removeItem`, `clear`, `key` and `length`. Each origin gets its own five megabyte budget, and a write that would pass it raises a quota error a page can catch. Local storage is shared by every tab and saved into my profile; session storage belongs to the tab and goes away with it. Private mode keeps both in memory only.
- **Real form posts.** A form with `method="post"` now builds `application/x-www-form-urlencoded`, `multipart/form-data` or `text/plain` and my host sends it through the same network stack as everything else, then renders the answer in the same tab. Checkboxes only send when they are checked, a submit button no longer sends its own label, and a disabled or unchecked control is skipped.
- **The history API.** `history.pushState`, `replaceState`, `back`, `forward`, `go`, `length` and `state` work on the page's own history, `popstate` fires, and the address bar follows because my host reports the change instead of reloading. A page cannot push an entry for another origin.
- **Find in page.** `Ctrl+F` opens a find bar. I search the text runs my own layout produced, so the highlight lines up with what is painted, `Enter` and `Shift+Enter` step through the matches, the found match scrolls into view, and `Esc` closes it.
- **Zoom.** `Ctrl+=`, `Ctrl+-` and `Ctrl+0` scale the page between 50% and 300%. The page lays out at the zoomed viewport width, so text reflows instead of being stretched, and clicks are converted back into page coordinates.

## What this build adds over 0.5.0

- **Positioning.** `static`, `relative`, `absolute` and `fixed` now work. An absolute box is measured against the nearest positioned ancestor, a relative box keeps its place in the flow but paints where it is told, and a fixed box stays put while the page scrolls because I mark its commands and my painter skips the scroll for them.
- **Flexbox.** Row and column, `row-reverse` and `column-reverse`, `flex-wrap`, `flex-grow`, `flex-shrink`, `flex-basis` and the `flex` shorthand, plus `justify-content` and `align-items` with stretch, centring and end alignment. A single line in a box with a set height fills that height, which is what makes centring work.
- **Grid.** `display: grid` with `grid-template-columns` in pixels, percentages, `fr` units and `repeat()`, plus `gap` and `grid-column: span N`. Rows size themselves to their tallest item.
- **Transforms.** `translate`, `translateX`, `translateY`, `scale`, `scaleX` and `scaleY`, applied about the centre of the box. I have not implemented `rotate`.
- **Fixed defects found while auditing step 2.** `split` ignored its limit argument; `fetch` rejected on an error status instead of resolving with `ok: false`, because my network layer gave no way to tell "a response arrived" from "nothing arrived"; `async` methods in classes and object literals did not parse; `for...of` over a `Map` or a `Set` silently produced nonsense instead of iterating, so I wrote a real iterator walk that spread and `for...of` share; `matchAll` was missing.

### What step 2 still needs

Typed arrays and ES modules are not in. I would rather say that plainly than pretend the step is finished.

## What 0.4.0 added

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

- The engine and page-host tests pass locally and on Windows. They now stand at 385 checks, including pattern matching, promise ordering, async/await and a `fetch` round trip through my page host.
- The single-file gate passes on hosted Windows for the exact commit I publish.
- A local HTTP test proves main-document, stylesheet, script and PNG loading through my page host.
- The Windows artifact contains only my executables, notices and build metadata - no profile, credential or source-control directory.

## Still outside this preview's completion claim

The complete browser still needs the rest of the steps above, the live Proton gate, the remaining legacy features and vault migration, broader compatibility, performance comparison, accessibility, and signed installation and update gates. I will not label those complete because this preview builds.

## What I have learned about containment

I spent a long time on a contained renderer process and did not get it to answer its own probe. The evidence I do have is that the trimmed renderer loaded only kernel32, advapi32, ws2_32, bcrypt and gdi32, and that removing user32 stopped a hard crash - but it still never wrote a frame. I am not going to describe containment as working until a page renders inside it, and I am not going to make it a prerequisite for a browser I can test.
