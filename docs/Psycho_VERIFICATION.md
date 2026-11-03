# My verification record

I separate what passed from what failed, what is blocked and what I have not run.

## Passed locally

- My GCC build of the engine, my page host and my test suite, and a full Clang/MinGW cross-build of the Windows browser executable.
- My engine test suite: **436 checks passed**. This covers URL/JSON/UTF-8, DOM, CSS, my JavaScript interpreter, PNG decode, layout, proxy routing, PAC evaluation, extension permissions, download naming, profile persistence, my bundled VPN bridge, and my page host loading a page, fetching its subresources, decoding an image and reporting frames, my cookie jar, my web storage quota and a real form post.
- My source-ownership and boundary audit: **181 passed, 0 failed**. This now includes the single-executable promise: no second executable, no resource compiler, no helper process.
- My file-naming audit: **20 passed, 0 failed**.
- My plan audit: **377 passed, 0 failed**.
- Windows cross-compilation of every module with Clang/MinGW targeting `x86_64-windows-gnu`, including a full link of `PsychoVault.exe`, which catches MSVC-class errors before I spend a hosted build.

## My audit of steps 1 and 2

I went back over the first two steps with real checks rather than reading my own
code and agreeing with it.

**Step 1, one executable.** I listed every file my build produces, searched for a
second process, a resource compiler or a sibling renderer, and read what my shell
actually starts. My build compiles three targets: the headless engine, the tests
and one `PsychoVault.exe` that links the engine, my page host and my window code
into one file. My shell never calls `CreateProcess`; the only `ShellExecuteW` is
the handler for an external protocol such as `mailto:`. My packaging script
requires exactly `PsychoVault.exe`, my verifier checks the ZIP contains it, and
both hosted workflows copy that one file into an empty folder and render a page
from it. That is the evidence I have, and it matches what I claim.

**Step 2, modern JavaScript.** I wrote 78 script checks and 60 pass. The ones that
failed were real, and I fixed six of them in this build (they are listed below).
What is still missing is listed under "Blocked or not implemented" so I do not
pretend the step is finished: typed arrays, generators, modules, real `Symbol`
identity, object spread inside a literal, computed method names, lookbehind in
patterns, `\u{...}` escapes and `structuredClone`.

## Real bugs my step 2 audit found and I fixed

- **`fetch` treated an error status as a network failure.** A 404 resolved in a browser; mine rejected the promise. My network layer had no way to say "a response arrived" versus "nothing arrived", so I made that explicit with status zero and now only a missing response rejects.
- **`split` ignored its limit argument.** `'a,b,c'.split(',', 2)` returned three items instead of two.
- **`for...of` over a `Map` or a `Set` produced nonsense.** It walked object properties and handed back native functions. I wrote one iterator walk and made spread, `for...of` and `Array.from` share it, so a non-iterable now raises a `TypeError` instead of inventing values.
- **`async` methods in classes and object literals did not parse.** They were read as a property named `async`.
- **`matchAll` was missing**, and `Symbol` did not exist at all.

## Real bugs my step 3 audit found and I fixed

I checked my layout against what a browser would produce with 50 small pages and
compared the box each one should get. The audit found nine real defects.

- **A longhand border was ignored.** Only `border` and `border-*-width` were read,
  so `border-bottom: 1px solid #ccc`, which is how most pages draw a divider,
  drew nothing. I now parse `border-top`, `border-right`, `border-bottom` and
  `border-left`, and I classify each part of the shorthand as a length, a style
  word or a colour. My colour parser answers black for any word it does not know,
  which is why the style word used to swallow the width.
- **Children inherited properties that do not inherit.** Every element started
  from a copy of its parent's style, so a `div` inherited `overflow: hidden`,
  `grid-template-columns`, `transform` and `flex-grow` from its parent. I now
  reset every non-inherited property before the cascade runs.
- **Adjacent vertical margins did not collapse.** Two blocks with a 20px bottom
  margin and a 30px top margin were 50px apart instead of 30px.
- **A percentage height resolved against the parent's width.** `height: 50%`
  inside a 100px tall parent gave the viewport width's half instead of 50px.
  It now resolves against the containing block's height, and only when that
  height is definite.
- **`overflow: hidden` did not clip.** A child taller than its parent painted
  straight over the rest of the page. My paint pass now keeps a clip stack, and
  a check paints a page and reads the pixels below the box to prove it.
- **Gradients were dropped.** `background: linear-gradient(...)` became a
  transparent box because my colour parser saw "linear-gradient" and gave up.
  I keep the text, parse it in my paint pass, and shade it through the same
  raster my canvas uses.
- **`grid-template-rows` was ignored**, so every grid row was as tall as its
  content even when a page asked for fixed heights.
- **`flex order` and `align-self` were ignored**, and the `flex-flow` shorthand
  was not read at all.
- **`position: sticky` moved a box by `top` even when nothing had scrolled.**
  I now leave a sticky box in the flow and treat the scroll clamp as a gap I
  have not built.

## My step 5 verification

- **JPEG.** I decoded every fixture and compared it channel by channel against
  Pillow's decode of the same file. Worst channel error is 3 of 255, mean under
  0.2, on 4:2:0, 4:2:2, 4:4:4 and grayscale files, including ones with restart
  markers.
- **SVG.** A red rectangle, a green circle, a blue triangle and a transformed
  group render at the exact pixels I expect, and a point outside every shape
  stays transparent.
- **Canvas 2D.** A script that fills, strokes, arcs, writes text, copies one
  canvas into another with `drawImage` and reads pixels back with
  `getImageData` produces the pixel colours I assert. A 1:1 `drawImage` copy is
  an exact copy, and an 8 pixel wide image scaled to 16 keeps its edge at the
  new midpoint.
- **Images on a page.** `img` with a JPEG and an SVG takes their intrinsic
  sizes, and my paint list carries an image command with the right rectangle.
- **Regression suite.** 436 checks pass, including a page that paints a
  gradient, clips an overflowing child and reads the resulting pixels.

## Real bugs my step 5 work found and I fixed

- **A black bar behind every heading.** An element that never asked for a
  background painted an opaque black rectangle, so every heading and paragraph
  on a page with a dark body got a black box behind it. I now paint a background
  only when the page asked for one, and a page with no body background renders
  on white.
- **`drawImage` read outside the source image.** My sampler clamped the *scaled*
  coordinate instead of the source coordinate, so a copy at a non-integer scale
  could read a row past the end of the image.

## Real bugs my step 4 work found and I fixed

- **A form post would have been replayed as a GET.** My first version of `method="post"` pushed the action URL into my navigation list as well as handing the body to my host, so the shell would have followed it with a second, bodiless request. A post now goes one way only, and a check asserts that no navigation is queued with it.
- **`document.cookie` ignored its own attributes.** My first document setter threw away `path` and `expires`, so a script that set a cookie for `/account` silently wrote it for the whole site, and a script that set an expiry got a session cookie. Script assignments now parse the same attributes as a header, except that `HttpOnly` is refused.
- **My storage quota check double counted.** The first version subtracted an existing item and then compared against the full total again, so replacing a value near the limit failed. A replacement now only pays for the difference, and a check covers it.

## Real bugs my own tests found and I fixed

- **My expression parser corrupted state.** I classified every operator ending in `=` as a compound assignment, so `===`, `==`, `!=`, `<=` and `>=` overwrote the value they were comparing. My VPN bridge exposed it: a `configure` message turned into a boolean. I now classify only real compound assignments and added 24 regression checks. This would have broken most real websites.
- **My Chrome-style callback dispatch invoked a details object as a function.** `proxy.settings.clear({})` would throw. I now pick the last function argument.
- **A denied extension API killed the whole worker.** I threw a C++ exception that escaped the interpreter. I now raise a catchable JS `Error`.
- **My event dispatch was blind to `JsError`** because it does not derive from `std::exception`. I now catch it and surface it in the UI.
- **Three of my required gates never ran.** My shell silently ignored `--expect-image`, `--sandbox-test` and `--sandbox-report`, so those checks proved nothing. My shell now parses them and refuses any option it does not understand.
- **My separate renderer could not start inside its own sandbox.** It unpacked correctly and the loader reported the right DLLs, but the contained process never answered. Rather than keep fighting it, I removed the split: my engine now renders inside my one executable, which is what I actually asked for.
- **A flex row in normal flow was laid out against the wrong axis.** When I rendered a plain page with a heading, a paragraph and a flex row, every card in the row sat at the top of the body and the row itself reported zero height. My row layout advanced one cursor and used it for both axes, so the row's `x` became its children's `y`. I now keep a main cursor and a cross cursor, place each item with the cross offset already worked out, and measure the row's height from the cross cursor. One new check covers a row between two paragraphs.
- **My download filename sanitiser left `..` runs.** Harmless once separators were gone, but I collapse them now.
- Four stale references after my rename, two of which would have made CI fetch a 404 fixture.

## Verified on hosted Windows, earlier runs

- MSVC build, engine tests, headless PNG render, native home capture and a fetched page with CSS/JS/PNG.

## Not run yet for this version

- The hosted MSVC build and full gate set for this exact commit. I run these before I publish, and the single-file gate now measures the one-executable promise directly.
- Address/undefined-behaviour sanitizers over my proxy, extension and download code.
- A real download over the network on Windows, and a real proxy connection to a live proxy server.
- A full updater install-and-rollback cycle.

## Blocked or not implemented, and I do not claim otherwise

- **Operating-system containment.** My pages render inside my browser process. I removed the separate contained renderer because it never started reliably, so this is a real security gap I have to close again later.
- **YouTube and Netflix.** No media pipeline, no Media Source Extensions, no DRM. Netflix needs a licensed Widevine module, which can only ever be a separate extension.
- **Proton VPN.** Not bundled and not verified. My own VPN bridge is bundled instead and is clearly labelled as mine.
- **TLS-to-proxy and SOCKS5.** I refuse these rather than leak a direct connection. Proton uses TLS-to-proxy, so this is a real gap.
- Modern JavaScript beyond my step 2 work: no modules, no generators, no typed arrays, no object spread inside a literal, no `Symbol` identity beyond a string key, and no lookbehind or `\u{...}` in patterns.
- Vault, passwords, translation, WebRTC, media, workers, IndexedDB.
- **WebP and progressive JPEG.** `img` decodes PNG, baseline JPEG and SVG. A WebP or a progressive JPEG is reported as unsupported rather than shown wrong.
- **Canvas and media beyond what I have.** Canvas 2D draws, but there are no WebGL contexts, no `OffscreenCanvas` and no workers to draw in. Sticky scrolling, `rotate()` transforms and 3D transforms are not applied.
- Full legacy UI parity, visual regression, accessibility audit, performance comparison, Android, signing and installer.

## Where I look for evidence

- Development workflow: https://github.com/Psycho-Core/PsychoVault/actions/workflows/Psycho_owned-engine.yml
- Release workflow: https://github.com/Psycho-Core/PsychoVault/actions/workflows/Psycho_release.yml
- Releases and update manifest: https://github.com/Psycho-Core/PsychoVault/releases
