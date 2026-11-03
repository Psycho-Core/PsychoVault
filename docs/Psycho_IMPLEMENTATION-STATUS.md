# My implementation status

I am building this in the open. This page records what my source actually does and what still has to pass. I do not call this a finished browser.

## Implemented in my own code

| Area | What I wrote | Evidence |
| --- | --- | --- |
| Base | URL parsing/resolution, UTF-8, JSON, atomic writes with corruption backups, OS random, monotonic clock | 436 local checks |
| HTML/DOM | Tokenizer, tree builder, entities, forms, selectors, mutation | Local checks; WPT not run |
| CSS and layout | Syntax, cascade, inheritance, box model with margin collapsing, block and inline flow, flexbox (direction, wrap, grow, shrink, justify, align, order, align-self, gaps), grid (pixels, percentages, `fr`, `repeat`, gaps, spans, fixed rows), positioning (relative, absolute, fixed, sticky), gradients, overflow clipping, translate and scale transforms, media queries | 50 geometry checks plus 22 and 28 case audits; WPT not run |
| JavaScript | Parser, tree-walking interpreter, closures, objects/arrays, classes with `extends`/`super`, destructuring, template literals, spread, Map/Set, getters/setters, optional chaining, logical assignment, mark-sweep GC, timers, DOM events, promises and `Promise.any`, `async`/`await`, my own pattern engine, iterators, `Symbol` keys, `fetch` | 78 script checks, 60 pass; no typed arrays, generators or modules; Test262 not run |
| Painting | Display list, software raster, owned PNG, JPEG and SVG decoding, gradients, overflow clipping, canvas 2D blitting, OS text | Headless and native captures; JPEG worst channel error 3 of 255 against Pillow |
| Shell | Native window, per-tab page on its own worker thread, omnibox, Unicode input/paste, stop/reload, bookmarks, history, downloads panel, all ten settings categories | Native captures; full UI parity not run |
| Networking | Broker WinHTTP with TLS validation, redirects, size limits, proxy routing, proxy auth, DNT/referrer/UA policy, host block lists | Local policy checks; live proxy not run |
| Proxy/VPN | Fixed servers, bypass lists incl. CIDR and globs, PAC evaluated in my own interpreter, fail-closed routing | Local routing and PAC checks |
| Extensions | MV2/MV3 manifest validation, path-traversal rejection, default-deny permissions, gated `chrome.runtime/storage/proxy/webRequest/privacy/alarms/action`, bundled install, persisted storage | Local permission and end-to-end bridge checks |
| VPN bridge | My own bundled provider extension driving my real proxy, storage, WebRTC policy and auth-provider paths | End-to-end local check |
| Downloads | Threaded transfers, progress, cancel, safe filenames, collision handling, partial-file commit, persisted list | Local naming and policy checks; live download not run |
| Page model | One page host per network tab on its own worker thread, engine renders in-process, images handed over without encoding | Local checks driving my page host directly; OS-level containment not implemented |
| Browsing state | My own cookie jar (domain, path, expiry, `Secure`, `HttpOnly`, third-party policy), local and session storage with a per-origin quota, `GET` and `POST` form submission with urlencoded, multipart and text/plain bodies, `history.pushState`/`replaceState`/`back`/`forward`, find in page over my paint list, per-tab zoom | Local checks incl. jar matching, storage quota and a real form post body; live site sign-in not run |
| Profile | Validated JSON, corruption backups, private-mode separation, active-tab restore, cookies and local storage persisted with the rest of my settings | Local round-trip checks |
| Release | Version file, ZIP + bare executables + SHA-256 + update manifest, gated publish, post-publish asset verification | Release workflow; first publish pending |

## Not implemented, and I show honest status instead of fake controls

- **Media.** No `<video>`/`<audio>` pipeline, no Media Source Extensions, no DRM. YouTube and Netflix do not play. DRM can only ever arrive as a separately licensed extension, never inside my engine.
- **Proton VPN.** Not bundled. Proton's extension is TypeScript needing their Node toolchain, and its worker needs modern JavaScript my engine lacks. My own VPN bridge is bundled and clearly labelled as mine.
- **TLS-to-proxy and SOCKS5.** I refuse these rather than leak a direct connection. This is a real gap because Proton uses TLS-to-proxy.
- **Modern JavaScript.** No ES modules, typed arrays, Proxy, generators, tagged templates or labelled break. Promises, async/await, regular expressions, iterators, `Symbol` keys and `fetch` are in. `Symbol` is a distinct key string rather than a separate primitive type, and `await` completes before the rest of the current script continues, which is the one place my job ordering differs from a browser.
- **Vault and passwords.** Not implemented. I refuse to show a password field that stores nothing, and I have migrated no existing vault data.
- WebRTC stack, workers, IndexedDB, Web Crypto, translation, spelling dictionary, media keys, mod CSS.
- **Canvas limits.** 2D drawing works, but there is no WebGL, no `OffscreenCanvas`, and no worker to draw from.
- **Transforms.** `translate` and `scale` work; `rotate` and 3D transforms do not. A sticky box stays in the flow instead of sticking to an edge while scrolling.
- **Image formats.** PNG, baseline and extended JPEG and SVG decode in my own code. WebP and progressive JPEG do not.
- **Browsing state gaps.** My third-party test compares hosts, not registrable domains, so `cdn.example.com` on `example.com` counts as third party. I do not implement `SameSite` yet, a file input is not uploaded, and `localStorage` has no `storage` event between tabs. Find in page searches the text runs my layout paints, so text a page paints on a canvas is not searchable. Zoom is per tab and is not saved with the session.
- Installer, code signing, updater install-and-rollback, Android, full 125-control settings parity.

## My naming rule

Every file I author starts with `Psycho_` or `PsychoVault`. I keep only these conventional names: `README.md`, `PSYCHO.md`, `NOTICE.txt`, tool-required dotfiles, the captured legacy evidence under `docs/reference/`, and `manifest.json` inside an extension folder because the extension format requires that exact filename. My `tools/Psycho_audit_names.py` enforces this and checks that no stale pre-rename reference survives.
