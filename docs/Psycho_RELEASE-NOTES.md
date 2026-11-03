# PsychoVault {{VERSION}} - Windows x64 development preview

I build my own browser engine. This is a development preview I can test, not a finished browser.

## What I added in this build

- **Cookies of my own.** I parse `Set-Cookie` myself, with domain, path, expiry date, `Max-Age`, `Secure` and `HttpOnly`. A cookie for a domain I am not talking to is refused, a host-only cookie only returns to the host that set it, a `Secure` cookie never goes out over plain HTTP, and a script sees `document.cookie` without the `HttpOnly` entries. My jar is saved with my profile and shared by every tab.
- **Local and session storage.** `getItem`, `setItem`, `removeItem`, `clear`, `key`, `length` and plain property access, with a five megabyte budget per origin. Local storage is shared by every tab and saved; session storage belongs to the tab. Private mode keeps both in memory only.
- **Real form posts.** `method="post"` builds `application/x-www-form-urlencoded`, `multipart/form-data` or `text/plain` and sends it through my own network stack, then renders the answer in the same tab. A ticked box with no value sends `on`, a multiple select sends every chosen option, and a submit button does not send its own label.
- **The history API.** `pushState`, `replaceState`, `back`, `forward`, `go`, `length`, `state` and `popstate`, with my address bar following the change instead of reloading the page.
- **Find in page and zoom.** `Ctrl+F` searches the text my own layout paints, so the highlight lines up with the glyphs. `Ctrl+=`, `Ctrl+-` and `Ctrl+0` zoom between 50% and 300%, and the page lays out again at the zoomed width so text reflows.
- **JPEG, SVG and Canvas 2D.** I wrote my own baseline and extended sequential JPEG decoder with chroma subsampling and restart markers, my own SVG renderer for shapes, paths, transforms and fill or stroke, and a Canvas 2D context that draws into the same raster my pages use.

## What I audited in this build

I went back over my layout, my browsing state and my new image code and measured them instead of reading my own work and agreeing with it. The audit found real defects, and I fixed each one and left a regression check behind.

- An absolute child of a positioned box measured from the border box instead of the padding box, so a bordered container put its children in the wrong place.
- `Max-Age` lost to `Expires` when a server wrote them in that order, so a cookie that should have expired in ten seconds lived for years.
- A ticked checkbox with no value submitted an empty string instead of `on`.
- A `<select multiple>` submitted only its first chosen option.
- A canvas context read `fillStyle` back as `#000000` whatever a script had set, and `context.canvas` returned nothing at all.

## What I verified in this build

- I built with MSVC on a GitHub-hosted Windows runner.
- My engine test suite passed: 460 checks.
- My source audit passed: 207 checks.
- My single executable rendered a fetched page with images from an empty folder containing nothing else, and it left no second file behind.
- My JPEG decoder matched Pillow's decode of the same files to a mean error under 0.2 of 255 across 4:2:0, 4:2:2, 4:4:4 and grayscale files, including ones with restart markers.

## How I run it

I download **PsychoVault.exe** and double-click it. That is the whole install.

My engine and my page host are compiled into that one file, so nothing is unpacked and nothing sits beside it. My CI proves this by copying only `PsychoVault.exe` into an empty folder, rendering a fetched page with images from there, and checking the folder still holds one file afterwards.

## How my updater reads this release

I publish `PsychoVault-update.json` with the version, tag and a SHA-256 for every asset. `PsychoVault.exe` is the update payload, so an update replaces one file. I verify the hash before I offer to install anything.

## What I am not claiming

- YouTube and Netflix do not play. My engine has no media pipeline, no Media Source Extensions and no DRM. DRM will only ever arrive as a separately licensed extension, never inside my engine.
- WebP and progressive JPEG are not decoded. My page says so in its diagnostics rather than showing a blank space without explanation.
- Proton VPN is not bundled. The included VPN bridge is my own code and needs a proxy I point it at.
- My pages render inside my browser process. There is no operating-system containment for page code yet.
- My vault, password manager, translation, media keys and full legacy settings parity are not finished.
- This build is unsigned, so Windows SmartScreen may warn me.

## No warranty

I provide this preview as-is, without warranty.
