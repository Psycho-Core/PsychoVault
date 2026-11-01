# PsychoVault

I'm Psycho-Core. This is the browser I wanted: one I actually wrote, not someone else's
browser with a new coat of paint. The application - the tabs, the omnibox, the session
store, the blocker, the sidebar, the command palette, the split view - is code in this
repository. The rendering engine underneath is Chromium, shipped with Electron, the same
way Brave uses Chromium and Vivaldi uses Chromium.

Dark navy, cyan accents, everything in a rail down the left. Fast enough to forget about.

![PsychoVault new tab page](docs/screenshot-newtab.png)

This is an earlier capture of the new tab page - the current interface may look different.

I am also experimenting with an Android port in [`android-port/`](android-port/README.md).
I published an experimental debug APK, and the earlier preview was reported working on a phone.
It is not a finished browser, and new changes still need testing.
Proton VPN and extensions do not work in that preview. My Windows browser is separate.

Screenshots of the running app are in `docs/` - the new tab page, the command palette, a
private window, Settings, the Performance panel, the extension manager, a tab held mid-drag,
the downloads panel mid-transfer. None of them are mock-ups, and each working feature is
followed by its assertion in `VERIFICATION.md`.

Settings now has nineteen focused sections grouped into ten categories: General, Privacy & security,
Downloads, Extensions, Performance, Appearance, Spelling, Passwords, Accessibility & language, and
About. The category navigation keeps the long list out of your way and remembers where you left off.
Every control still writes to disk.

Pinned tabs keep the same width as regular tabs, while retaining the pin marker, omitting
the close button, and restoring their pinned state after a restart.

## What it is

- **A real browser application.** `main.js` owns windows and tabs, `preload.js` is the
  only bridge to the chrome UI, `ui/` is the interface. No `userChrome.css`, no patched
  engine archives, no rebranded installer of somebody else's product.
- **Chromium inside.** Video, WebGL, canvas, extensions-grade site compatibility and
  modern JavaScript all come from the Chromium build inside Electron 45. Sites behave
  the way they behave in Chrome.
- **Everything local.** Bookmarks, history and settings are JSON in your user data
  folder. Nothing is uploaded, because there is no server to upload to.

## Features

| Area | What you get |
| --- | --- |
| Tabs | Horizontal strip, **drag a tab to move it** (a caret shows the gap it will drop into; pinned tabs stay a block at the head, keep the same width as regular tabs, and the order survives a restart), right-click menu (pin, duplicate, mute, split, move to window, close others), double-click to pin, middle-click to close, `Ctrl+Shift+T` to reopen |
| Address bar | URL or search, live history suggestions, four built-in search engines and custom HTTPS shortcuts |
| Blocking | 262-domain bundled list, checked on every request, counted **per page** on the shield, resets on navigation |
| Private windows | Separate in-memory session, violet chrome, `Ctrl+Shift+P`, nothing written to disk |
| Split view | Two pages side by side in one window, `Ctrl+\` |
| Command palette | `Ctrl+K`, 42 commands, fuzzy filter, falls through to a web search |
| Workspaces | Chips in the tab row: click to switch, double-click to rename, **`x` on the chip to close it** (or right-click the chip for the menu, or middle-click), `+` for a new one, `Ctrl+1..9`; each keeps its own tab set across restarts, and the last workspace clears its tabs instead of vanishing |
| Errors | Main-process exceptions are written to `errors.log` in the profile and the app keeps running, instead of Electron's modal error dialog |
| Passwords | Local vault: scrypt + AES-256-GCM under a master password (or the OS keychain where one exists), manual HTTPS fill from the field menu, auto-lock, CSV import, encrypted backups and no sync |
| Sidebar | Bookmarks, history and downloads in a panel that pushes the page instead of covering it |
| Find in page | `Ctrl+F`, live match count |
| Zoom | Per tab, `Ctrl+plus`, `Ctrl+minus`, `Ctrl+0` |
| Page translation | Choose one of 20 target languages in Settings, then choose **Translate current page** from the top-right Menu button or command palette. PsychoVault asks before sending the current page URL to Google Translate; it opens a translated copy in a new tab and leaves the original alone. Google may fetch the page, and some sites may not translate correctly. |
| Automatic updates | On Windows x64, checks GitHub Releases at startup. It asks before downloading, saves the installer to Downloads without a Save As prompt, verifies an Ed25519-signed update manifest and the installer SHA-256 against a public key bundled with PsychoVault, then opens the verified installer. The repository is public, so release checks do not need a token; a read-only one is only needed if you point the updater at a private fork (the `PsychoVault_Updater` Actions secret at build time, or paste one in Settings - About). GitHub answers 404 to an anonymous release lookup on a private repository, so a private one needs the token. The token gates *access*; the signature gates *trust* - one never substitutes for the other. **Settings - About - Check now** runs the same check on demand and reports the reason it failed. A verified update is offered in a **banner across the top of the browser** - Install, Later, or ✕ to dismiss that version for good - rather than a dialog box over your page. Nothing downloads until Install is pressed. |
| Accessibility | High-contrast browser controls, less animation in PsychoVault and pages that honor reduced-motion CSS, visible keyboard focus, arrow/Home/End tab navigation with screen-reader tab names, and an option to keep tab-close buttons visible. Find these in **Settings → Accessibility & language**. |
| Downloads | For ordinary downloads I can ask where to save each file, or automatically save in the chosen download folder under an unused name. The Save As window is provided by the operating system. Browser updates use Downloads separately. Live percentage in the rail badge, a message when a file starts (naming the folder) and when it finishes, a system notification if the window is behind something, and a panel that states where files go with a **Change** button. Active transfers show bytes and speed, and can be paused or resumed when supported; retry after restart is not supported |
| Settings | Nineteen sections in ten navigable categories: General, Privacy & security, Downloads, Extensions, Performance, Appearance, Spelling, Passwords, Accessibility & language, and About. Includes search engine, blocking, cookies, tracking signals, autoplay, history, governor, Save As downloads, delete-on-exit, profile cleanup, vault, extensions, browser appearance, translation language, and accessibility controls |
| Performance | Live per-tab memory and CPU (`Ctrl+Shift+H` or the speedometer in the rail), sleep any tab from the panel, CPU priority limiter, custom sleep time and hostname exclusions, bandwidth cap on downloads, battery saver that switches itself on when you unplug |
| Tab islands | A tab opened from a page joins that page's island; the group gets a collapse control in the strip, and the tab you are reading is never hidden by a collapse |
| Containers | Named cookie jar + localStorage + cache per tab (Work, Shopping, Social, Banking, or your own), assigned from the tab menu, dot in the strip, so two identities on one site never touch |
| Snapshot | `Ctrl+Shift+5` or the tab menu: capture the page to a PNG, or save it as a PDF, straight to your Downloads folder |
| Panic button | `F12` tucks every tab away and shows your escape page; `F11` brings the set back. Nothing is written to disk while panicked |
| RGX | Contrast/saturation/brightness filters on `video` and `img` elements, two switches, off by default |
| Sidebar services | Up to 14 services in the rail, each with the site's own favicon and brand colour (a picker has Discord, Twitch, YouTube, YouTube Music, X, Reddit, GitHub, WhatsApp, Telegram, Spotify, Slack, Messenger, ICQ and more; anything else you can add by URL), opened as ordinary tabs, so logins and video players keep working; global media keys route to them and auto-pause the music when another tab starts making sound; right-click a button for open / new window / reload / copy / remove |
| Spelling | Red wavy underlines in every text field you type in, suggestions and "Add to dictionary" in the right-click menu, and a "your words" box in Settings you can edit in bulk. It is PsychoVault's own checker - a 15,000-word frequency list plus your words, run in the page - because an Electron build cannot load or download dictionaries (tested: no `downloadDictionary`, `<profile>/Dictionaries` is not read, the download URL is never fetched) |
| Extensions | Chrome-style extensions, installed from an unpacked folder or **.crx** (unpacked here because Electron refuses crx files), loaded into every session including containers, and managed with enable / disable / remove. **`chrome.proxy` and `chrome.privacy` are supplied by PsychoVault's own shim**, so browser VPN extensions can actually set a proxy and harden WebRTC instead of failing silently. Everything else an extension asks for that this engine lacks is named in its card. **Settings - Extensions - Check what proxy is in use** reports what each profile would really do with your traffic, so a VPN that says connected but changes nothing can be told apart from one that is not connected |
| Personality | Synthesised keyboard sounds (no audio files in the repo), six accent colours, and a custom stylesheet box that repaints the chrome
| Governor | CPU priority limiter and tab sleeping, so background tabs stop costing RAM |
| Context menus | Right-click a tab, a link, an image, selected text, or a text field (Cut / Copy / Paste / Select all all the way through) |

## Privacy and recovery controls

I added the VPN check page under Settings - Extensions - Open VPN checks. Public IP checks use ipapi.co. The DNS button opens a separate BrowserLeaks test, and the optional WebRTC check contacts Google's STUN server. Those services receive network information when you run them. Results are not a guarantee against leaks.

The built-in blocker is not uBlock Origin. Click the shield beside the address bar to allow or block on the current normal-window site, and manage the bundled and custom domain lists in Settings - Privacy. The shield count is blocked requests on that page; its tooltip also shows the session total. Custom lists accept domains, not uBlock filter syntax.

I added exact-origin camera, microphone, location and notification rules under Privacy. The global permission deny list still applies when a site is set to Default. The vault can import common CSV exports without replacing existing site/username entries, and can save and restore password-protected encrypted backups. CSV files contain plaintext passwords. Keep the backup password safe - it cannot be recovered. The vault only fills manually on HTTPS, for the saved hostname (with www treated as equivalent), and not across origins.

After an unexpected close, PsychoVault offers normal-window tabs from its most recent five-second snapshot. Private windows are never included. Sleeping tabs reload when selected and can lose unsaved form data. Battery Saver may sleep tabs sooner than your chosen idle time.

If the updater fails while Proton is connected, disconnect Proton and retry. Future updater-downloaded installers are removed only after the new version starts and the tracked file still matches its recorded hash; older untracked setup files remain in Downloads.

## Keyboard

```
Ctrl+T  new tab            Ctrl+K  command palette
Ctrl+W  close tab          Ctrl+L  focus address bar
Ctrl+Shift+T  reopen       Ctrl+F  find in page
Ctrl+Shift+P  private      Ctrl+\  split view
Ctrl+R  reload             F5      reload
Ctrl++/-/0  zoom           Ctrl+1..9  jump to a workspace
Ctrl+[ / ]  back, forward  Ctrl+K  then "lock the password vault"
Ctrl+Shift+H  Performance   Ctrl+Shift+5  snapshot the page to a PNG
F12  panic (hide everything)  F11  bring the tabs back
```

## Build and run from source

```bash
npm install electron@45.1.0 --save-dev
npx electron .
```

Or with a global Electron:

```bash
electron .
```

There is no bundler step. The files in `app/` are the app.

## Build it yourself, on Windows, in about ten minutes

No account, no subscription and no GitHub Actions minutes: everything runs on your own PC.
**[BUILD-WINDOWS.md](BUILD-WINDOWS.md)** is the step-by-step guide with direct download links.

Short version - install [Node.js LTS](https://nodejs.org/en/download), extract this repository to
`C:\PsychoVault`, then double-click **`PsychoVault_easy_build.bat`**. You get
`build\payload\PsychoVault.exe` (a portable browser you can just run), a
`Release-Notes-<version>.txt` with the changes and the SHA-256 of what it built, and, if
[NSIS](https://nsis.sourceforge.io/Download) is installed, `PsychoVault-Setup-1.0.28.exe`.

Feature and extension ideas for what to build next are in [IDEAS.md](IDEAS.md).

## Run from source, without an installer

When a release build is unavailable - or you want to test a change before it ships - the app
runs straight from a checkout. You need [Node.js](https://nodejs.org) 20 or newer; nothing else.

```bash
git clone https://github.com/Psycho-Core/PsychoVault
cd PsychoVault/app
npm install          # fetches Electron 45 for your platform
npm start
```

That is the real browser, not a mock - tabs, blocking, the extension manager and the proxy shim
all behave as they do in the installer.

Two things differ from an installed copy, and both are deliberate:

- **Automatic updates are off.** The updater only runs on a packaged Windows x64 build
  (`app.isPackaged`), so the update banner will not appear here. Everything else works.
- **Your profile is separate.** It lives under `%APPDATA%\Electron` rather than
  `%APPDATA%\PsychoVault`, so running from source cannot disturb the profile your installed
  copy uses.

This is the quickest way to check a fix - such as **Settings - Extensions - Test proxy support** -
without waiting for a build.

## Installing over an old copy, and what Windows says about it

Setup installs into `%LOCALAPPDATA%\PsychoVault` and your profile - bookmarks, history,
settings, `vault.bin` - lives in `%APPDATA%\PsychoVault`. That split is why updating never
touches your data, and it is also what the uninstaller asks about before it deletes.

Installing a new build over a running one used to fail with `Can't write:
...\PsychoVault.exe`, because Windows will not overwrite an executable that is running, and
you had to uninstall first. Setup now tests the old exe for a lock, tells you the app is
running, closes it on OK (Cancel aborts and leaves the existing install intact), and retries
the copy once if something still had a handle. No uninstall step in between.

The other thing you will see is SmartScreen: *"Microsoft Defender SmartScreen prevented an
unrecognised app from starting"*. That is not a defect and I cannot fix it - it is what
Windows says about any executable that is not signed with a code-signing certificate, and a
certificate is an annual fee to a CA that wants my passport. **More info → Run anyway** is
the path. If you want to check what you downloaded instead of trusting the dialog, the
sha256 is in `SHA256SUMS.txt` below.

Anything that goes wrong inside the app ends up in `errors.log` in your profile folder -
listed with its size in **Settings - Your data** (second image above), clearable there too. A
throw in a menu handler is not supposed to put a modal box between you and your tabs, so it
does not any more.

## Checking what you downloaded

`SHA256SUMS.txt` next to the installer holds its sha256. From the folder you extracted
this archive into:

```bash
sha256sum -c SHA256SUMS.txt          # Windows: certutil -hashfile PsychoVault-Setup-1.0.4.exe SHA256
```

No document packed inside the installer carries the hash of the archive containing it,
which is why the sums file is separate and written after the build.


## Automatic updates

On packaged Windows x64 builds, PsychoVault 1.0.4 checks GitHub Releases once after startup. It sends
GitHub a release lookup request, not browsing history or page URLs. If a newer stable release exists,
the app downloads two small metadata files before asking whether you want to proceed:

1. `update-manifest.json` names the version and installer and records its exact byte size and
   SHA-256 hash.
2. `update-manifest.sig` is an Ed25519 signature over the exact manifest bytes. PsychoVault checks
   that signature using `app/update-public-key.pem`, a public key embedded in the installed app.
   Only someone holding the matching private key can create a valid signature.

After the signature and release details pass validation, PsychoVault asks you to continue. It
downloads to a temporary file in your Downloads folder without a Save As prompt,
checks the actual byte count and SHA-256 against the signed manifest, then saves and launches it.
If a file with the same name already exists, it saves with a numbered suffix instead of overwriting it. If
anything is unsigned, altered, truncated, for the wrong platform, or for a non-newer version, it is
not run. A checksum alone detects changed bytes but does not identify who approved them; the
signature authenticates the expected checksum and release metadata. Offline checks fail quietly so
they do not interrupt browsing.

### One-time repository setup for maintainers

The Ed25519 **public** key lives in `app/update-public-key.pem` and ships with the browser. Its
matching **private** key stays out of Git. GitHub Actions needs it to sign each release manifest. In
the repository, open **Settings → Secrets and variables → Actions → New repository secret** and set:

- Name: `UPDATE_SIGNING_PRIVATE_KEY_B64`
- Value: one-line base64 of the matching private PEM (`base64 -w0 <private-key.pem>` on Linux; on
  macOS use `base64 <private-key.pem | tr -d '\n'`).

If the secret is absent, CI still builds the installer and skips publishing a GitHub Release. It
tries to retain an unsigned Actions artifact for 14 days for testing; if the repository's Actions
storage quota is full, the build still completes but GitHub cannot offer that artifact for download.
An unsigned artifact is not an automatic update and Windows may still warn about it. If the secret is
present but invalid or does not match the embedded public key, signing fails and no release is
published. Never commit or paste the private key into an issue or chat. GitHub-generated release
notes provide the public change history.

The existing 1.0.3 installer predates this updater, so it cannot update itself. Install 1.0.4 once
from GitHub Releases when available; after that, later signed releases can be checked from inside the
app. The Ed25519 signature authenticates update metadata and the installer hash; it is **not** a
Windows Authenticode signature and does not remove the SmartScreen warning.

## Package for Windows

Pushes that change app code, the installer script, or its build tools to the active Arena branch or
`Master` run `.github/workflows/build-release.yml`; it can also be started manually from Actions.

It also runs on a monthly schedule: **11:00 on the 1st, Adelaide time** (`cron: '0 11 1 * *'` with
`timezone: 'Australia/Adelaide'`). That is deliberately half an hour after the monthly GitHub
Actions minute allowance resets at 00:00 UTC, because a scheduled build that fires before the reset
is refused instead of built. If the version in `app/package.json` is already released, the run
still builds and keeps the artifact but does not touch the release, its signature or
`SHA256SUMS.txt`: bump the version to publish a new one. The
workflow runs the audit and tests, downloads the checksum-pinned Electron 45.1.0 Windows runtime,
patches the PsychoVault icon/version, then builds and verifies the installer. It tries to retain an
unsigned CI artifact for 14 days; a full Actions storage quota may prevent the download, but does not
fail the build. If `UPDATE_SIGNING_PRIVATE_KEY_B64` is configured, later steps sign the update
manifest and publish the installer, checksum, signature, and generated release notes. If not, those
release steps are skipped and no public release is created.

For a local rebuild on Windows there is a one-click script: **`PsychoVault_easy_build.bat`**
(see [BUILD-WINDOWS.md](BUILD-WINDOWS.md)). It reuses the Electron runtime that `npm install`
already placed in `app\node_modules\electron\dist`, so it needs no separate Electron download and
no 7-Zip: it stages the payload, patches the icon/version, copies `app\` into
`resources\app`, and runs NSIS if it is installed. Output is `build\payload\PsychoVault.exe`
(portable) and, with NSIS present, `PsychoVault-Setup-<version>.exe`.

On Linux (or in CI), install NSIS and 7-Zip, then provide `$PAYLOAD` (default `/var/tmp/payload`)
with the unpacked Windows Electron runtime and `PsychoVault.exe`. The `patch-exe` helper sets the
runtime icon/version; the build script stages the app, runs `makensis`, and byte-compares the
packaged app files against source:

```bash
npm ci --prefix tools
VERSION=$(node -p 'require("./app/package.json").version')
node tools/patch-exe.mjs "$PAYLOAD/PsychoVault.exe" assets/psychovault.ico "$VERSION"
bash tools/build-installer.sh
```
The output name and installer version come from `app/package.json`; override with
`PAYLOAD=... OUT=... VERSION=... bash tools/build-installer.sh`.

The installer is per-user (`%LOCALAPPDATA%\PsychoVault`), asks for no administrator
rights, writes no services, and registers a normal uninstall entry.

## Files

```
app/
  main.js            windows, tabs, sessions, blocking, stores, menus, IPC
  download-save.js  paused Save As handoff and unique automatic download names
  translation.js    supported target languages and safe Google Translate URL builder
  update-manifest.js signed-release verification helpers
  auto-updater.js   startup GitHub Releases check, verified download and installer handoff
  update-public-key.pem pinned Ed25519 key used to authenticate update manifests
  preload.js         contextBridge surface, whitelisted channels only
  page-preload.js    bridge for psychovault:// pages only - web pages get nothing
  blocklist.json     262 blocked domains
  ui/
    index.html       chrome markup: rail, tab strip, toolbar, panels
    style.css        navy/cyan design tokens
    renderer.js      tab rendering, omnibox, palette, panels, shortcuts
    newtab.html      new tab page
    settings.html    settings page (19 sections in 10 categories, incl. translation, accessibility, vault, extensions)
  package.json
tools/
  build-installer.sh rebuild the Windows installer from a staged Electron tree, then verify
                       what actually went inside it
  audit.mjs          static self-check: IPC, settings, UI ids, translation, accessibility, syntax
  translation.test.cjs tests URL construction, unsafe inputs, and supported languages
  sign-update-manifest.mjs signs installer metadata for a release
  generate-update-keypair.mjs creates an Ed25519 key pair (private key outside Git)
  update-manifest.test.cjs tests manifest signature, integrity, version, and platform checks
installer.nsi        NSIS script, per-user install
docs/                screenshots of the running app
VERIFICATION.md      test log
```

## Verified

`VERIFICATION.md` lists what I actually ran and what it printed. Two layers:

- **static**, `node tools/audit.mjs` - every channel the renderer calls has a handler in
  the main process, every pushed event is in the preload allow-list, every setting key has
  a control on the settings page, every id the UI reaches for exists, no duplicate ids, no
  unscoped selectors, no partial window-state updates that erase tabs, and no fixed narrow
  width on pinned tabs. It syntax-checks all app scripts including the inline settings pages.
  It exits non-zero on a defect. It found two real bugs while I wrote
  it: a switch with no key behind it and a settings row matching the wrong elements.
- **runtime**, a throwaway harness that drove the real app under Xvfb against local test
  pages: 51 assertions for the browser core, then 101 more for the spell checker, the
  service rail, 38 for tab dragging and the download
  flow against a local HTTP server - caret, pinned boundary, live progress, the save
  dialog's default path, a cancelled download that writes nothing. All in
  flow and 30 for extensions - install, a content script changing a live page, a
  `webRequest` rule cancelling a request, enable/disable/remove, and the real Proton VPN
  `.crx` unpacked and analysed. All in `docs/harness-*.txt`, all passing.

The static layer earns its keep on the small things too: it caught a handler with no bridge
while the VPN panel was being built - `ui/renderer.js calls pv.vpn.getconf() but preload.js
does not bridge it`, the shape of bug that ships as a dead button - and it is what made
removing that whole feature safe: the moment the preload stopped bridging `vpn:*` channels,
it named every leftover call site. Both layers are documented in `VERIFICATION.md`,
including the three real bugs the harness caught (a dead "wake" button, a referrer rule that could not fire on IP hosts,
and an accent colour that outranked the user's stylesheet mod) and the one feature I
**deleted** because measurement said it was a placebo: canvas farbling.

## Opera GX parity

The GX feel is the point, so here is the honest mapping - what landed, what is
deliberately not here, and why.

| Opera GX | PsychoVault | Where |
| --- | --- | --- |
| GX Control - CPU limiter | **yes** - `app.setPriority`, off / balanced / low | Settings - Governor |
| GX Control - RAM limiter | **yes** - idle tabs are unloaded and reload on click; pinned tabs never sleep | Settings - Governor |
| GX Control - network limiter | **yes** - a cap in KB/s on downloads, implemented as a duty cycle on the transfer (Electron gives no bytes-per-second hook). It measurably slows a download; fine-grained values bind less tightly on a fat local link than over a WAN | Settings - Performance |
| GX Control - live stats | **yes** - per-tab process memory and CPU with a Sleep button for each | `Ctrl+Shift+H`, or the speedometer in the rail |
| Tab Islands | **yes** - a tab opened from a page joins that page's island, the group gets a collapse control, the tab you are reading is never hidden | `main.js: island`, `renderer.js: renderTabs` |
| Hot Tabs Killer | **yes** - the same panel, sorted by cost | `main.js: hotTabs()` |
| Snapshot | **yes** - `Ctrl+Shift+5` or the tab menu writes a real PNG, and "Save page as PDF" writes a real PDF, both to Downloads. No annotation editor on top | `main.js: capturePage()` |
| Panic Button | **yes** - `F12` hides every tab and shows the escape page you configured, `F11` brings them back; nothing is written to disk while panicked | Settings - Enhance & personality |
| Battery Saver (+ auto on unplug) | **yes** - saturation drop, tabs sleep after 3 min instead of your setting, and it switches itself on when the power source leaves AC | Settings - Governor |
| RGX Mode (Opera's name; PsychoVault calls the two switches Enhance videos / Enhance images) | **yes** - two switches, contrast/saturation/brightness filters applied to `video` and `img` | Settings - Enhance & personality |
| Mods - colours, sounds, custom CSS | **yes** - six accents, synthesised keyboard sounds (no audio files shipped) and a stylesheet box that repaints the chrome. CSS only, capped in length, never injected into pages | Settings - Appearance, Enhance |
| Mods - shaders | **no** - deliberately. Shader mods are arbitrary code over the compositor, and Opera's mod pipeline had a disclosed vulnerability in July 2026 | - |
| Workspaces | **yes** - chips in the tab row with a visible `x` and a right-click menu, own tab set per workspace, `Ctrl+1..9`. Not capped at five like GX | `main.js: wsSwitch / wsClose / wsMenuItems` |
| Split-Screen | **yes** - two pages in one window, `Ctrl+\` | `main.js: splitId` |
| Sidebar services (Discord, Twitch, messengers) | **yes** - up to 14 entries with their real favicons and brand colours, opened as ordinary tabs rather than embedded webviews, so logins, notifications and video players keep working | Settings - Sidebar services |
| Spell check | **yes, and it works** - underlines, suggestions and your own words, from a checker this app ships rather than one it would have to borrow from a browser it is not | Settings - Spelling |
| VPN | **Proton VPN ships with the browser.** A build of Proton's own extension (GPL-3.0, compiled from `github.com/ProtonVPN/proton-vpn-browser-extension` at a pinned commit) is installed on first run, so there is nothing to download and no store that refuses the install. Sign in with your own Proton account in its window; Chromium then routes traffic through Proton's proxy via `session.setProxy`, with the extension's proxy credentials delivered through `webRequest.onAuthRequired`. No account or credential is bundled. Opera's VPN stays absent - it was never PsychoVault's to ship |
| Music Player / Mini Player + auto-pause | **partly** - global media keys route to the tab holding audio, and anything that starts making sound pauses the music tab after 2.5 s. No floating mini-player window | `main.js: mediaRoute` |
| Browser player (video popout) | **no** - not built. Sites can still take Chromium's own picture-in-picture | - |
| Password manager | **yes** - local encrypted vault, save and fill from the field menu, auto-lock, no sync | Settings - Passwords |
| GX Cleaner | **yes** - delete-on-exit per store plus a "clear this now" row per store, listing real files and sizes | Settings - On exit, Your data |
| Ad blocker with uBlock-style lists | **partly** - bundled 262-domain list, counted per page on the shield. No list importer, so easylist subscriptions are out | `blocklist.json` |
| Fake My History | **no** - it writes fabricated visits to your own history to disguise it from whoever sits at your machine. I use "remember history: off" and a private window for that, which does the useful half | Settings - Privacy |
| Pinboards | **no** - a new-tab widget in disguise; the speed dial tiles are already editable | - |
| Background music | **no** - that is a licensed content deal, not a browser feature | - |
| Opera AI | **no** - no model and no cloud endpoint in a browser that uploads nothing | - |
| GX Corner, cashback, crypto wallet | **no** - content partnerships and affiliate surface | - |
| Razer Chroma | **no** - needs their SDK running on your machine | - |
| Flow (phone messaging) | **no** - needs Opera's accounts backend | - |
| Tor / VPN window | **no** - no anonymising network is bundled. A system-wide VPN covers the app; a per-window tunnel is not something this engine offers | - |
| Mouse gestures | **no** - Electron gives page and window events, not the raw pointer stream a gesture needs | - |
| Tab context menu | **yes** - pin, duplicate, mute, split, sleep, snapshot, container, move to window, copy URL, close others / to the right | `main.js: tabMenuTemplate` |
| Right-click paste in any text field | **yes** - Cut / Copy / Paste / Paste and match style / Select all, wired to the focused page | `main.js: pageMenuTemplate` |
| Instant search in the tab switcher | **yes** - `Ctrl+K` palette searches tabs, bookmarks and history | - |

Every "yes" row is a control on the settings page; nothing is forced on you and nothing
is silently locked. The rule I built to: **if I cannot make it better than GX does it,
I do not ship a worse copy of it** - which is why the shader mods, the AI pane and the
crypto wallet are "no" rows with a reason instead of half-finished buttons.



## What is on your disk

The settings page has a **Your data** panel that lists every file in the profile
folder with its size, straight off the disk, and a clear button for each store
(cache, cookies, site data, history, downloads, saved tabs). It exists because I
would rather show you the state than describe it.

It also answers the password question directly. There is no `Login Data` file,
because PsychoVault does not use Chromium's password provider - nothing ever
prompts you to save a login into a store you cannot inspect. What exists instead is
mine, and it is two files:

- `vault.bin` - the entries, as ciphertext only
- `vault.key.json` - a random 16-byte salt and an encrypted verifier, no password

plus `workspaces.json` for the tab sets. Signing into a site still relies on that
site's cookies, and "Clear cookies" removes them.

## Passwords, and what "safe" means here

Someone asked me to make sure their passwords were 100% safe. I can build the
storage, verify the crypto by running it, and show you the bytes - but "100% safe" is
not a thing a browser can offer, so here is exactly what the vault does and does not
protect against.

How it is stored, in order of preference:

1. **A master password you set.** `scrypt` (N=16384, r=8, p=1) with a per-install
   random 16-byte salt derives a 256-bit key; the entries are sealed with
   `aes-256-gcm`, IV per write, auth tag checked on read. The password itself is never
   written down - only the salt and a small encrypted verifier, so a wrong password
   fails on the tag instead of returning garbage.
2. **The OS keychain**, when `safeStorage` reports it is usable (DPAPI on Windows,
   libsecret on Linux). Then there is no master password to remember, and the key is
   tied to your Windows account.
3. **Nothing at all.** If neither is available, the vault refuses to write and tells
   you why. I would rather have an unusable vault than a plaintext file.

What that buys you: anyone who copies your profile folder - a lost laptop, a cloud
backup, someone else's account on the same box with no keychain access - gets
ciphertext, and a GCM tag that rejects any edit to it. The decrypted copy lives in
memory only, is wiped on auto-lock (5 minutes by default), and copying a password to
the clipboard starts its own one-minute lock and clears the clipboard after.

What it does not buy you: a keylogger, malware running as you, a browser process I
have compromised by accident, or a weak master password - scrypt slows down guessing,
it does not launder `hunter2`. If someone has your unlocked session, they have your
passwords, which is true of every manager including the ones you pay for.

Two things I chose not to do: no sync (nothing to breach, no server of mine), and no
autofill of a password into a page whose host does not match the entry's site. The
save is opt-in from the right-click menu on the field, never a nag bar.

## A real page: wowhead.com

I ran the actual build against Wowhead's homepage (no staging, no mock page). The
site is ad-heavy, so it is a decent stress test.

The shield counter in the address bar is **per page** - that load cancelled 151
tracker and ad requests on its own. It resets when you navigate somewhere new, so
the number you see is the number for the page you are looking at, not a session total.

Wowhead does not take kindly to that. A few seconds later it put up its
anti-adblock wall, with the option to pay $0.75 a month. That is the site's own
script, not the browser refusing to render. Two things worth saying plainly: the
wall appears seconds after load, so seeing the homepage clean is a matter of
timing; and the page itself was never blocked from loading - news, feeds, video and
database all render, and the only casualty is the ad slots. If a site insists, flip
Shields off for it from the settings page and it behaves like an ordinary browser
again.

## Proton VPN, and how it got here

Proton publishes the extension's source under GPL-3.0, which permits modification and
redistribution subject to its conditions. This build comes from Proton's source, not the Chrome
Web Store package. The licence and corresponding-source requirements apply to distributions.
`app/builtin/proton-vpn/` is that source built with `npm ci --legacy-peer-deps && npm run build` at
commit `71d7d86f164a2ee8e50ce5a389c9e5cbe0fd6883`, copied into your profile on first launch, and
loaded like any other unpacked extension. Version, commit and licence are recorded in
`app/builtin/README.md` and in `THIRD-PARTY-NOTICES.md`.

The reason it could not work before is one missing API. Electron implements a subset of Chrome's
extension APIs and `chrome.proxy` is not in it - and that single call is the whole mechanism by which
a browser VPN sends traffic through its tunnel. PsychoVault now supplies that API itself, in
`app/ext-shim.js`, registered as both a frame and a service-worker preload script so it is present in
the extension's popup and in its Manifest V3 background worker:

| Chrome API | Backed by |
| --- | --- |
| `chrome.proxy.settings.set / get / clear` | `session.setProxy()` on every session, translated in `app/ext-proxy.js` |
| `chrome.proxy.settings.onChange`, `onProxyError` | emitted from what the network stack actually did |
| `chrome.privacy.network.webRTCIPHandlingPolicy` | `webContents.setWebRTCIPHandlingPolicy()` |
| `chrome.webRequest.onAuthRequired` | Electron's own implementation, which does cover proxy auth |

Nothing is stubbed: a configuration that cannot be applied is reported to the extension as an error
rather than silently leaving traffic direct, and `ext-proxy.js` is unit-tested on exactly that point
(`tools/ext-proxy.test.cjs`). Removing the extension is permanent - PsychoVault records it and does
not reinstall on the next launch - and **Settings - Extensions - Restore** brings it back.

## What is not here yet

I'd rather write the gap down than pretend. Not built: a Tor-routed window, the local
model that was supposed to rewrite summaries offline, mouse gestures, a phone-pairing
Flow equivalent, and any form of fingerprint farbling. I had a canvas-noise switch in
this build and I removed it: Electron runs my code in a page only after that page's own
scripts have already read the canvas, so the probe value came back unchanged in a
measured test. A patch you can detect is a stronger signal than no patch, and I am not
shipping a placebo to fill a row in a feature table (headers and cookie policy only). The vault has no import from another manager yet, and no password
health checks (reuse, weak, breached lists).
Widevine is not in Electron, so DRM video (Netflix, Spotify web) will not play - that is
a licence problem, not a code problem, and I'm not going to fake it.

## Licence

My application code is licensed under the [MIT licence](LICENSE), copyright 2026 Psycho-Core.
You can use, change and share it under those terms, including keeping the copyright and permission
notice with copies or substantial portions of the code.

**No warranty:** PsychoVault is provided as-is, without warranties or guarantees. The full warranty
and liability terms are in [LICENSE](LICENSE).

Bundled third-party software has its own licences. In particular, the Proton VPN extension is
GPL-3.0, not MIT. See [third-party notices](THIRD-PARTY-NOTICES.md) and the
[extension licence](app/builtin/proton-vpn/LICENSE).

Workspaces can be turned off in Settings. Saved groups stay in the profile; when turned back on, the current tabs become a new group. Bookmarks, History and Downloads are in the menu. If a recovery notification mentions errors.log, open Settings > About > Open error log. Review it for private paths and URLs before sharing it.

The About page has a copyable troubleshooting report. It includes runtime versions and a few on/off states, not logs, URLs, paths or credentials. Dark Reader is bundled as an extension and can be managed under Settings > Extensions. It needs access to website content to change how pages look.
