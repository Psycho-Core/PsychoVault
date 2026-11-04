# My next testable Windows build

I am working through my browser one step at a time, in the order that gets me something I can actually use. I am not treating any merge as a complete browser.

## The order I am working in

| Step | Work | State |
| --- | --- | --- |
| 1 | One executable that renders my pages itself | Done, published as 0.4.0 |
| 2 | Modern JavaScript: promises, async/await, regular expressions, iterators, fetch, modules | Patterns, promises, async/await, iterators, `Symbol`, `matchAll` and `fetch` are in; typed arrays and modules are still to come |
| 3 | CSS and layout: positioning, flexbox, grid, transforms | Done, and this build fixes nine defects my own audit of it found |
| 4 | Browsing fundamentals: cookies, local storage, forms, find in page, zoom | Done in this build: my own cookie jar, local and session storage with a quota, GET and POST forms, the history API, find in page and zoom |
| 5 | More image formats and canvas | Done in this build: my own JPEG decoder, my own SVG renderer and a 2D drawing surface |
| 6 | Media: audio, video and Media Source Extensions | Done in this build: my own WAV and AVI readers, a player, a mixer, source buffers and blob URLs, with the containers I cannot decode named instead of faked |
| 7 | VPN transport, including the TLS-to-proxy path Proton needs | Done in this build: my own sockets, SOCKS5, HTTP `CONNECT`, HTTP framing, SChannel TLS stacked over my own stream, fail-closed routing and a VPN panel |
| 8 | Security: certificate and transport policy, ad blocking, containment | Done in this build: my own Strict-Transport-Security store, mixed content and insecure form refusals, always-HTTPS with a counted fallback, my own filter list engine with per-tab counts and per-site exceptions, and a job object that reports itself as a limit rather than isolation. AppContainer for page code, CSP enforcement and fuzzing are still to come |
| 9 | Conformance measurement with Test262 and web-platform-tests subsets | Test262 measured: 5,009 of 54,058 pass, 17.4 percent of what my engine can attempt. WPT is not run yet because its harness needs modules and async iteration |
| 10 | Published release and a verified updater cycle | Publishing on every build; the install and rollback half is not tested |

## What this build adds over 0.7.0

- **Nine layout defects my own audit found.** I wrote 50 small pages with a known
  correct answer for each box and compared. `border-bottom: 1px solid #ccc` drew
  nothing because only the `border` shorthand was read. Children inherited
  properties that do not inherit, so a `div` picked up its parent's
  `overflow: hidden`, grid columns and transform. Adjacent vertical margins did
  not collapse. `height: 50%` resolved against the parent's width. `overflow:
  hidden` did not clip, so a tall child painted over the rest of the page.
  Gradients were thrown away by my colour parser. `grid-template-rows`,
  `flex-flow`, `order` and `align-self` were ignored, and `position: sticky`
  moved a box even when nothing had scrolled. All nine are fixed and each one
  has a check.
- **A black bar behind every heading.** While looking at a rendered page I saw
  a dark rectangle behind each heading on a page that only set a background on
  the body. An element that did not ask for a background was painting an opaque
  black box. It now paints nothing, the page canvas is white unless the body
  says otherwise, and my reported geometry comes from a real layout report
  instead of from the paint list.
- **A JPEG decoder of my own.** Baseline and extended sequential, 4:4:4, 4:2:2
  and 4:2:0 chroma with bilinear upsampling, restart markers, and a grayscale
  path. I compare it against Pillow on every fixture: the worst channel error is
  3 out of 255 and the mean is under 0.2.
- **An SVG renderer of my own.** `rect`, `circle`, `ellipse`, `line`, `polygon`,
  `polyline`, `path` with the usual commands, `g` with inherited attributes,
  solid and gradient fills, `viewBox` scaling and transforms.
- **Canvas 2D.** `getContext('2d')` with fills, strokes, paths, arcs, curves,
  gradients, patterns, text, transforms, clipping, `getImageData`,
  `putImageData`, `drawImage` with scaling and source rectangles, `toDataURL`
  and per-frame invalidation, so a canvas only re-uploads when it changed.
- **Images on a page.** `img` and `canvas` take their intrinsic size, respect
  `width` and `height` and aspect ratio, decode through my own code and are
  painted by my own raster.

## What my audit of step 7 changed

I went back through the transport after shipping it and found fifteen defects. The
worst was a download opening its own session with no reference to my proxy, which
is a direct connection from a browser whose panel said it was tunnelling. The
rest are in my audit page: a cookie and a referer that followed a redirect to
another host, a 303 that kept its POST body, a chunk header I could not read
being treated as the end of the body, a short body returned as complete, two
disagreeing Content-Length values, a HEAD reply read as if it had a body, an
unknown scheme falling through to the HTTP path, an unbracketed IPv6 target, a
blocking connect that ignored my timeout, a zero-byte successful read that could
spin my framing forever, revocation requested but never checked, a subject
accessor that returned the wrong value, a routing decision made twice, and a
panel that accepted a plaintext scheme. Each one has a check now.

## What this build adds over 0.9.0

- **A media pipeline I wrote.** My own WAV reader, my own AVI reader for MJPEG
  video with PCM audio, an MP4 box reader that reports what is inside it, a
  player with seek, pause, rate, volume, mute and loop, and a mixer that sums
  every element that is playing. The picture goes through my own JPEG decoder
  and is scaled into the box my layout gave the element.
- **Sound that leaves the machine.** My shell opens one WASAPI shared-mode
  stream, 48 kHz stereo 16 bit with the device's own conversion, and asks the
  page thread for exactly the frames it needs. When nothing plays it writes
  silence rather than repeating old audio.
- **Media Source Extensions.** A page can create a source buffer, append bytes in
  pieces, read back the range it has, end the stream, and attach the whole thing
  to an element through a blob URL. An element that was told to play before the
  data existed starts once enough media arrives.
- **My own VPN transport.** A socket layer, a SOCKS5 client that hands the
  hostname to the proxy unresolved, an HTTP `CONNECT` request, and HTTP/1.1
  framing that understands `Content-Length`, chunked bodies and a body that ends
  when the connection does. A redirect reconnects inside the tunnel instead of
  falling out to a direct request.
- **TLS I can stack.** SChannel does the handshake and the certificate work, with
  a chain built against the name I asked for, and it refuses an untrusted,
  expired, revoked or wrong-name certificate and refuses renegotiation. Because
  my TLS wraps a stream, TLS to the proxy and the origin's TLS inside it are the
  same code path twice.
- **A VPN panel.** Server, scheme, username, masked password, a fail-closed
  toggle and an off switch, all saved with my settings. I type `host:port` in my
  own address bar.

## What this build adds over 0.8.0

- **My own JPEG decoder.** Baseline and extended sequential, Huffman coded, with
  4:4:4, 4:2:2 and 4:2:0 chroma subsampling, grayscale files and restart
  markers. I compared it channel by channel against Pillow's decode of the same
  fixtures and the mean error is between 0.01 and 0.16 of 255. A progressive
  file is refused by name rather than shown with the wrong colours.
- **My own SVG renderer.** `rect`, `circle`, `ellipse`, `line`, `polygon`,
  `polyline`, `path` with lines and curves, nested groups, transforms, `viewBox`
  scaling, fill, stroke, opacity and even-odd fill.
- **Canvas 2D.** One context per canvas element that draws into the same raster
  my pages use. Paths with arcs, quadratic and bezier curves and rounded
  rectangles, fill and stroke with dashes, text with `measureText`, images
  including the nine argument `drawImage`, linear and radial gradients,
  `save`/`restore`, translation, scaling and rotation, `getImageData`,
  `putImageData` and `toDataURL`.
- **What the audit of steps 3, 4 and 5 fixed.** An absolute child measured from
  the border box instead of the padding box, `Max-Age` losing to `Expires`, a
  ticked checkbox sending an empty value, a multiple select sending only one
  option, a canvas reading its own `fillStyle` back as black, and `drawImage`
  ignoring a scaled context. Find in page moved out of my window code and into
  my engine so my test suite can reach it.

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

- The engine and page-host tests pass locally and on Windows. They now stand at 742 checks, including pattern matching, promise ordering, async/await, a `fetch` round trip through my page host, my media readers, and a tunnel driven over a real loopback socket.
- The single-file gate passes on hosted Windows for the exact commit I publish.
- A local HTTP test proves main-document, stylesheet, script and PNG loading through my page host.
- The Windows artifact contains only my executables, notices and build metadata - no profile, credential or source-control directory.

## Still outside this preview's completion claim

The complete browser still needs the rest of the steps above, the live Proton gate, the remaining legacy features and vault migration, broader compatibility, performance comparison, accessibility, and signed installation and update gates. I will not label those complete because this preview builds.

## What I have learned about containment

I spent a long time on a contained renderer process and did not get it to answer its own probe. The evidence I do have is that the trimmed renderer loaded only kernel32, advapi32, ws2_32, bcrypt and gdi32, and that removing user32 stopped a hard crash - but it still never wrote a frame. I am not going to describe containment as working until a page renders inside it, and I am not going to make it a prerequisite for a browser I can test.
