# My next testable Windows build

I am working through my browser one step at a time, in the order that gets me something I can actually use. I am not treating any merge as a complete browser.

## The order I am working in

| Step | Work | State |
| --- | --- | --- |
| 1 | One executable that renders my pages itself | Done in this slice |
| 2 | Modern JavaScript: promises, async/await, regular expressions, modules | Not started |
| 3 | Browsing fundamentals: cookies, local storage, forms, find in page, zoom | Not started |
| 4 | More image formats and canvas | Not started |
| 5 | CSS and layout correctness: flexbox, grid, positioning, overflow | Not started |
| 6 | Media: audio, video and Media Source Extensions | Not started |
| 7 | VPN transport, including the TLS-to-proxy path Proton needs | Not started |
| 8 | Security: operating-system containment for page code, certificate policy, ad blocking | Not started |
| 9 | Conformance measurement with Test262 and web-platform-tests subsets | Not started |
| 10 | Published release and a verified updater cycle | Partly built, not yet published |

## What step 1 changed

- My engine, my page host and my shell now link into one `PsychoVault.exe`. There is no renderer process, no payload to unpack and no resource compiler in the build.
- My page host owns one engine page per tab on that tab's worker thread, fetches through my network policy, decodes images in my process and reports frames straight to my window.
- I deleted the helper process, the framed IPC and the payload loader rather than leaving dead code behind. My git history keeps that work if I want the isolation boundary back later.
- My single-file gate now copies only `PsychoVault.exe` into an empty folder, renders a fetched page with images, checks the capture is a real PNG, and checks the folder still holds exactly one file afterwards.

## Evidence required before I hand over a preview

- The engine and page-host tests pass locally and on Windows.
- The single-file gate passes on hosted Windows for the exact commit I publish.
- A local HTTP test proves main-document, stylesheet, script and PNG loading through my page host.
- The Windows artifact contains only my executables, notices and build metadata - no profile, credential or source-control directory.

## Still outside this preview's completion claim

The complete browser still needs the rest of the steps above, the live Proton gate, the remaining legacy features and vault migration, broader compatibility, performance comparison, accessibility, and signed installation and update gates. I will not label those complete because this preview builds.

## What I have learned about containment

I spent a long time on a contained renderer process and did not get it to answer its own probe. The evidence I do have is that the trimmed renderer loaded only kernel32, advapi32, ws2_32, bcrypt and gdi32, and that removing user32 stopped a hard crash - but it still never wrote a frame. I am not going to describe containment as working until a page renders inside it, and I am not going to make it a prerequisite for a browser I can test.
