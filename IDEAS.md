# PsychoVault — feature and extension ideas

Ordered by what I'd actually build next, not by what sounds impressive. Effort is honest:
**S** = an afternoon, **M** = a few days, **L** = a real project.

---

## Part 1 — browser features

### Privacy and VPN

**1. Per-site proxy rules (split tunnelling)** — *M*
Right now the Proton VPN proxy is all-or-nothing: turning it on sends every tab through the tunnel.
Streaming sites and banks often break on a VPN, so people turn it off entirely and lose the
protection everywhere. A "sites that skip the VPN" list, plus a per-site toggle in the page menu,
is the single biggest quality-of-life win available. Electron gives you a clean handle for this:
each window already has its own `session` partition, and `session.setProxy({ proxyRules })` can be
applied per partition — so a "no-VPN" window is possible without touching the extension at all.

**2. DNS-over-HTTPS setting** — *S*
Electron supports `session.setProxy` + custom resolvers, and Chromium exposes DoH via the
`dns-over-https` setting. One dropdown (Off / Cloudflare / Quad9) plus a little ping readout. Cheap,
visible, and it makes the privacy story concrete instead of abstract.

**3. Leak test page in Settings** — *S*
A built-in page that shows your current public IP, DNS resolver and WebRTC-visible address, with a
before/after comparison when you toggle the tunnel. This also settles the standing question of
whether the VPN is actually changing your IP, without needing my guesswork.

**4. Per-site permissions panel** — *M*
Camera, microphone, notifications, geolocation, clipboard — Chromium's default prompts are ugly and
easy to click through. A per-site list with a global "deny everything by default" switch, backed by
`session.setPermissionRequestHandler`, is straightforward and very visible.

### Browsing

**5. Reader mode** — *S/M*
Strip a page to text, one column, adjustable width and font, saved reading position. Chromium has a
built-in implementation that Electron can enable, and there is a fallback using a DOM rewrite in
`page-preload.js`.

**6. Session restore** — *M*
Currently a crash loses your tabs. Write the open-tab list to the profile on a debounce and offer
"Restore previous session" on next start. Small file, big perceived reliability.

**7. Tab search / switcher (Ctrl+Shift+A)** — *S*
The command palette exists; a dedicated tab switcher that fuzzy-matches titles and URLs across all
workspaces is mostly reuse of code you already have.

**8. Reader-friendly screenshot tool** — *S*
Visible page / full page / selected region, copied to clipboard or saved via the existing Save As
dialog. `webContents.capturePage()` plus one UI overlay.

**9. Download manager upgrades** — *M*
Pause/resume, retry, "open containing folder" (exists), and a real speed/time readout. Downloads
already funnel through one module, so this is contained work.

**10. Search engine management** — *S*
Add/edit/delete search engines and a keyword shortcut system (`gh foo` searches GitHub). The
omnibox parser is already there; it just needs a settings UI.

### Performance ("Performance" rail button)

**11. Tab hibernation** — *M*
Hot tabs are already tracked. Add "sleep after N minutes idle", which unloads the renderer and
restores it on click. The honest, measurable version of what gaming browsers advertise.

**12. Live performance HUD** — *S*
Small overlay: per-tab memory, process count, GPU vs software rendering, and a network graph. Great
for showing off, and useful for diagnosing the "why is it slow" question.

**13. Panic button upgrades** — *S*
Current panic clears what it clears; let the user choose the exact set (cookies, cache, history,
downloads, vault lock) and show a receipt of what was erased.

### Extension system

**14. A real install flow from a URL** — *M*
Right now extensions come from a folder or a `.crx`. A "paste a Chrome Web Store link" box that
detects the extension ID, explains that Chrome's store cannot install into Electron, and points at
the unpacked/source path would remove the biggest point of confusion.

**15. Extension permissions viewer** — *S*
Show what each extension asked for in its manifest in plain English, next to its enable toggle. Cheap
trust-building, and it is the thing every browser review asks about.

**16. Extension update check** — *M*
For the bundled Proton extension: compare the installed copy against a version stamped in the repo
and offer to update. Today it is frozen at whatever was built in.

### Release and updates

**17. Update channels** — *S*
Stable / Beta, so you can check a fix without it becoming the default for everyone.

**18. Portable mode** — *S*
A `--portable` flag or a marker file next to the exe that puts the profile in `.\profile` instead of
`%APPDATA%`. Ideal for a USB stick and for the build you just made in `build\payload`.

**19. Crash report viewer** — *S*
Local-only crash log with "copy details" — no upload. Turns "it crashed" into a usable bug report.

---

## Part 2 — extension ideas

These are the Chromium extensions that actually work under Electron 45 (MV3, no `chrome.proxy`
beyond the shim that already exists, no `.crx` auto-install from the Web Store). Bundling means
shipping the built/unpacked extension in `app/builtin/<name>/` and pre-enabling it, exactly the way
Proton VPN is shipped now.

### Bundle-worthy, in order

1. **Proton VPN** — already bundled. The open work is making its proxy path actually change the IP.
2. **uBlock Origin Lite** (MV3) — the built-in blocker is a 262-domain list; uBO Lite brings real
   filter lists and per-site switches. Big upgrade in blocking quality for one bundled folder.
3. **Bitwarden** — the local vault covers passwords, but people want their existing ones. Works as
   an unpacked MV3 extension; would need the popup-in-a-tab path that Proton already uses.
4. **Dark Reader** — universal dark mode for sites that ignore `prefers-color-scheme`. Fits the
   dark navy look and is the most-requested "nice" feature in any browser.
5. **SponsorBlock** — skips sponsor segments; small, MV3-clean, and immediately noticeable.
6. **ClearURLs / Neat URL** — strips tracking parameters from links. Tiny, complements the blocker.
7. **Return YouTube Dislike** — trivial to package, very visible for the audience this browser is
   clearly aimed at.
8. **Privacy Badger** — heuristic tracker blocking, learns per site. Heavier, but genuinely
   different from a static list.
9. **LocalCDN / Decentraleyes** — serves common libraries locally, cuts third-party calls.
10. **A "read later" extension of your own** — 150 lines, MV3, no dependencies; uses the existing
    sidebar panel and works offline. The best demonstration that the extension system is real.

### Extension-adjacent platform work

- **Write one first-party extension as a reference.** A tiny, well-commented MV3 extension in
  `app/builtin/` that shows an action badge, a popup page and `chrome.storage` — doubles as a test
  fixture and as documentation for anyone writing extensions for PsychoVault.
- **Support `chrome.scripting` + service-worker preloads.** Electron 35+ has
  `session.registerPreloadScript({type: 'service-worker'})`, but it does not run under
  `--no-sandbox` — worth resolving before claiming broad MV3 compatibility.
- **Proxy API shim, done properly.** `chrome.proxy` is what Proton VPN depends on. The shim today is
  partial; finishing it is the difference between "the extension installs" and "the VPN works".

---

## Part 3 — if only five things get built

1. **Per-site proxy rules** — makes the VPN usable day to day.
2. **Leak test page** — proves the tunnel works, once and for all.
3. **uBlock Origin Lite bundled** — biggest single jump in blocking quality.
4. **Session restore** — the cheapest way to feel solid.
5. **Portable mode** — makes your own build genuinely portable, and makes testing painless.
