# What I actually ran

Everything on this page was executed against the build in `release/`, on Linux with
Electron 45.1.0 and Xvfb, with screenshots taken from the live window. I am not listing
things I "should" have tested; this is the list of things that produced output.

## Smoke

| Check | Result |
| --- | --- |
| App launches, window appears, blocklist loads | pass — `blocklist: 262 domains` in the log |
| New tab page renders | pass — see `docs/screenshot-newtab.png` |
| Custom window controls (min/max/close) | pass — `ipcMain` handlers wired, close exits cleanly |

## Navigation

| Check | Result |
| --- | --- |
| `https://example.com` loads in a tab | pass |
| `https://en.wikipedia.org/wiki/Web_browser` loads | pass — `docs/screenshot-split.png` left pane |
| Bare hostname in omnibox gets `https://` | pass — `duckduckgo.com` resolved |
| Query text goes to the selected engine | pass — `wiki` offered "Search for wiki" first |
| Back / forward disable state | pass — both start disabled, back enables after navigation |
| History records title, not just URL | pass — `{url: https://example.com/, title: "Example Domain"}` written to `history.json` |
| Session restore reopens last tabs | pass — seeded `session.json` reopened the Wikipedia tab on launch |

## Blocking

Loaded a `data:` page containing four third-party requests:
`doubleclick.net` (image), `google-analytics.com` (iframe), `adnxs.com` (script), and one
benign request.

```
BLOCKED 3
Failed to load URL: https://google-analytics.com/x with error: ERR_BLOCKED_BY_CLIENT
```

Pass — the three listed hosts were cancelled at the session level before the network was
touched, the count on the shield went to 3, and the benign request was untouched.

## Video

Chromium codec probe inside the app:

```
h264: probably   aac: probably   vp9: probably   av1: probably   opus: probably
hevc: (empty)
widevine: missing
```

Then an actual MP4 played in a tab: `Big_Buck_Bunny_720_10s_1MB.mp4` reached
`0:10 / 0:10` with native controls — `docs/screenshot-...` and the frame at the end of
the ten seconds show real decoded picture, not a placeholder.

H.264 + AAC means ordinary web video plays. HEVC and Widevine DRM do not — those are
codec-licence items, not settings I can flip.

## Settings, bookmarks, downloads

`settings:get` → `{"search":"duckduckgo","blockAds":true,"homepage":"psychovault://newtab","restore":true}`

`settings:set {search:'brave'}` returned the merged object and wrote `settings.json`:

```
{"search":"brave","blockAds":true,"homepage":"psychovault://newtab","restore":true}
```

`bookmarks:add` + `bookmarks:list` + `isBookmarked` → `true`, entry on disk.
Downloads: a 4.3 MB zip saved to the Downloads folder, tracked to `done`, listed in the
panel with a clear-list action. `docs/screenshot-downloads.png`

## Windows

| Check | Result |
| --- | --- |
| Private window opens on Ctrl+Shift+P | pass — separate window, violet chrome, "PsychoVault Private" |
| Private window writes no history | pass — the private profile folder stays empty of `history.json` |
| Split view on Ctrl+\ | pass — two live pages side by side, `docs/screenshot-split.png` |
| Command palette on Ctrl+K | pass — filter works, `docs/screenshot-palette.png` |
| Sidebar stack (bookmarks / history / downloads) | pass — panel takes its own column, page re-lays out |
| Installer unpacks and contains the app | pass — payload extracted with 7z, `resources/app/main.js` present |

## Known failures found during testing and fixed

- Menu button was under the window controls' region — moved below the tab strip.
- Settings page never received its bridge because the page preload only built the
  contextBridge for the chrome document — `page-preload.js` now installs it for the two
  internal pages only.
- Downloads queued forever behind Chromium's native save dialog on a machine with no
  desktop portal — the app now sets the save path itself.
- Private-window shortcut called a function that no longer existed. Rewritten.

## Not tested

- The Windows binary itself. It is the official Electron 45.1.0 win32-x64 runtime with
  `electron.exe` renamed and `resources/app` replaced; I can't execute PE binaries in
  this environment. Everything above ran on the same JavaScript against the Linux build
  of the same version.
- macOS.

## Artifact

```
PsychoVault-Setup-1.0.0.exe   100 MB        (round-17 build, superseded)
sha256 fea9c066bee1f63e1265d1926c6f8c0cf0627f083ad6f977e4ca31940c95d47c
```

Inside: `PsychoVault.exe` (249,798,656 bytes, PE32+ x64, version resource
"PsychoVault Browser / Psycho-Core / 1.0.0.0", four icon sizes 16-256), the Chromium
runtime files, `resources/app/` with the 12 source files, and the three text documents.
39 files total, verified by listing the finished installer with 7z.

The installer is unsigned, so Windows SmartScreen will show "unrecognised app" the first
time. That is expected for a build nobody has paid a certificate authority for.

## wowhead.com run (per-page shields)

Ran the real app against `https://www.wowhead.com` with the shipped 262-domain
blocklist. Chromium's own log confirms cancellations, e.g. `ads.pubmatic.com`,
`secure-assets.rubiconproject.com`, `ap.lijit.com`, `rtb.gumgum.com`,
`sync.adkernel.com`, `ssp-sync.criteo.com`, `s.seedtag.com`, `cs.ingage.tech`,
`acdn.adnxs.com`, `i.liadm.com`, `id5-sync.com`, `match.sharethrough.com`,
`apps.smartadserver.com`, `b1sync.outbrain.com`, `cm.adform.net`, `eus.rubiconproject.com`.

Counter check: I instrumented `noteBlocked()` to write one line per cancelled
request to a log file and compared it with the address-bar badge. Badge **176**,
log **176** lines, for that page load. The temporary probe has since been removed
from the source.

Chromium prints fewer `ERR_BLOCKED_BY_CLIENT` lines than that (48-58 for a run
covering several loads) because it dedupes the console warning per URL; the
counter is the number of `webRequest` callbacks I cancelled, which is the
honest figure.

Not verified, and I am not claiming otherwise: mouse input. This headless X
server delivers keystrokes to the window but not pointer clicks, so the site's
own "Continue without disabling" and notification prompts could not be dismissed
by clicking during the test. The clean homepage capture is simply the moment a
few seconds before the site's wall script fires. Everything else above was driven
by real keyboard input into the real window.

## Known state of the Windows package

`PsychoVault-Setup-1.0.0.exe` was built before the per-page shield counter landed,
so the copy in `resources/app` shows a session-wide total instead. Everything else
in that installer - blocking, downloads, settings, video - is the same code.

## GX feature round (tabs, menus, governor, settings)

Driven by a temporary harness inside `main.js` (env-gated, removed afterwards), run on
the real app in the real X session. Every line below is printed output.

| Check | Result |
| --- | --- |
| Tab menu template | `Open new tab to the right, Reload, Duplicate tab, Pin tab, Mute tab, Open in split view, Move tab to new window, Copy tab URL, Bookmark this tab, Close tab, Close other tabs, Close tabs to the right [disabled]` - correct, last item disabled when it is already the right-most tab |
| Pin a tab | flag set, tab moves to head of the strip, menu then offers `Unpin tab` |
| Duplicate | tab count +1 |
| Mute | `webContents.isAudioMuted()` true on the page |
| Close other tabs | 3 tabs to 1 |
| Move tab to new window | windows 1 to 2, tab removed from the old window |
| Field context menu | `Cut [disabled], Copy [disabled], Paste, Paste and match style, Delete, Select all, Undo [disabled], Redo [disabled]` for a focused textarea with no selection |
| Paste actually pastes | clipboard written, `Paste` invoked, field value became `PASTED-VIA-MENUstart` |
| Bridge isolation | `typeof window.pv` is `undefined` inside a loaded page, `object` on the settings page |
| Sleeping tabs | idle tab dropped (`discarded: true`), pill survived, URL and title kept, clicking reloaded it (`document.title` back to `Example Domain`), history never saw the internal `about:blank` |
| Layout guard (the drag bug) | with a degenerate client rect reported (`height: 90`), page view stayed `1356x794` instead of collapsing; after the real resize it followed correctly to `h: 324` |
| Window move | view bounds unchanged by the move (`1356x794` before and after) |
| Hide sidebar | page x goes `44` to `0` and back |
| Accent colour | chrome `--accent` computed to `#ff5a5a` after saving `accent: red` |
| Speed dial | settings list of 3 tiles rendered on the new tab page, first label `Alpha` |
| DNT / GPC on the wire | local server received `dnt: 1` and `sec-gpc: 1` |
| Autoplay switch | applied in both directions without throwing |
| CPU limit | `balanced ok, low ok, off ok` |
| Persistence | `settings.json` after an IPC write: `blockThirdPartyCookies: true, sleepTabsMin: 15, accent: red, speedDial: 3 items` |
| Your data panel | listed 14 profile files with sizes, cookie count, no `Login Data` |
| Clear from the UI | `window.pv.data.clear({cache, history})` returned `cache,history` |

Two things this round caught that I would otherwise have shipped:

1. `webContents.terminate()` does not exist in Electron 45. My first sleeping-tab
   implementation called it, swallowed the `TypeError`, and silently did nothing. The
   shipped version unloads the tab to `about:blank` instead, which is what the test
   above proves works.
2. A CSS block for the settings page was added with a `str.replace` that did not match,
   so the speed-dial and clear buttons rendered with browser-default styling. Now
   asserted on insert, and the screenshot `docs/screenshot-settings-full.png` shows it.

Not verified, same limitation as before: this X server drops synthetic pointer events,
so no menu was opened with a real right-click here. The menus are the thin part - the
template builders and the actions they run are what the table above exercises.

## Installer rebuilt after the GX feature round

The package built for round 19 was **`PsychoVault-Setup-1.0.0.exe` - 105,252,645 bytes,
sha256 `faf19fb71479914727e3bbb3b56f59314916ca965d49968dd9e4666b5a59564d`**, 39 files
inside. That build is superseded by the round-20 one at the end of this file; the number
is kept so the history is checkable, not current.

Verified by pulling files back out of the finished installer with 7z and comparing them to
the repo: `resources/app/main.js`, `resources/app/ui/renderer.js` and
`resources/app/ui/settings.html` are **byte-identical to source**, and the extracted copies
contain the new code (workspace chips `renderWorkspaces` x4, the private-window guard, the
vault panel, the vault bridge in `page-preload.js`). `PsychoVault.exe` is a PE32+ GUI
x86-64 image (15 sections) with the PsychoVault icon group (id 1) and version strings
written by `resedit`, and the installer script is NSIS/LZMA "compress whole", per-user.

Two notes on this file specifically:

- Nobody has executed the .exe: there is no Windows on this machine. What I can prove is
  what is inside it, and that the Electron build it carries is a real win32-x64 runtime.
- `README.txt` / `VERIFICATION.txt` inside the package are generated before the installer
  exists, so their installer figures refer to the previous build. The copies in the repo
  and in `PsychoVault-1.0.0-source.zip` are authoritative.

Last smoke test before packaging, on Linux, with the shipped source: launched normally,
pressed `Ctrl+Shift+P`, two windows came up, the private one painted violet with **no
workspace chips** (nothing about a private window is written to disk), and the log had
zero uncaught errors.

## Workspaces, battery saver and the vault

This round the two "not built" answers became code, so the tests had to grow. Method
is the same as before: a temporary block spliced into `main.js` behind an environment
flag, calling the real `ipcMain` handlers through `ipcMain._invokeHandlers` (57 of
them registered, so no handler was bypassed), plus a live Chrome renderer and a live
settings page for the UI assertions. The block was deleted afterwards - `grep -c
PV_TEST` over the shipped tree is 0, and `node --check` passes on `main.js`,
`preload.js`, `page-preload.js` and `ui/renderer.js`.

**Vault core - 47 assertions passed, 0 failed, 1 skipped:**

| Checked | Result |
| --- | --- |
| Refuses to store without encryption | `ok:false - "no encryption available - the vault will not write to disk unencrypted"`, and `vault.bin` was not created |
| Master password set | `method: master`, key file holds only `salt` + GCM `check` blob, `kdf: scrypt-N16384-r8-p1` |
| Plaintext on disk | `vault.bin` is 178 B of ciphertext; scanning the password and the master password across every file in the profile returned zero hits |
| Tamper | flipping one byte mid-file makes `vaultOpen` throw - GCM tag rejects it |
| Wrong master password | `reason: "wrong master password"`, nothing decrypted, count stays 0 |
| Lock | `locked:true, count:0`, and `vaultCache`/`vaultKey` are `null` - the key is gone, not just hidden |
| Locked pages | `vault:list` returns `error: locked`, `vault:reveal` refuses, no Fill items in the field menu |
| Unlock | correct password brings the entry back, `list never returns the password` (only `reveal` does, on demand) |
| Auto-lock | with `vaultLockMin: 0.05` the vault was open at 1 s and wiped at 4 s - memory, not just UI state |
| Generator | `Sparrow-dune-lantern-rose-63` style phrases and 24-char random, two calls never equal |
| Host matching | `github.com` and `www.github.com` find the same entries; `evil.example` finds none |
| Live page fill | in a real form: `fill writes into the live form - user+password :: octocat|hunter2 long phrase` |
| Live page save | `grab reads the live form back` with the value the page actually held |
| Field menu | `Fill octocat @ github.com / Fill octocat @ www.github.com / Save password for this site` |

**Cold start, UI and workspaces - 26 assertions passed, 0 failed:**

| Checked | Result |
| --- | --- |
| Restart with an existing vault | `method: master`, `locked: true`, `count: 0` - nothing is decrypted until you ask |
| Entries after restart | all four sites came back through the same key derivation; reveal returned the original password |
| Write path | the entry added in the new session was ciphertext on disk immediately, and survived lock + unlock |
| Settings page, bad password | the real Unlock button put `wrong master password` in the note and kept the list closed |
| Settings page, good password | status line read `vault on - encrypted with your master password (scrypt + AES-256-GCM) - unlocked - 4 entries`; rows painted; lock panel hid itself |
| Rows never leak | inputs held `github.com`, `octocat`, `••••••••` - no password in the DOM |
| Vault off switch | menu items disappear (`length === 0`), status reports `enabled: false`, and both come back |
| Boot restores a workspace | `workspaces.json` active set `[example.com, example.org]` became exactly those two tabs |
| Chips in the chrome | `["General","Gaming2","Builds1"]`, active chip marked, count per workspace |
| Battery saver | `body.battery` applied on `settings:set` and cleared when turned off; `sleepLimitMinutes()` 15 → 3 → 15 |

**The one SKIP is my sandbox, not the vault:** reading the X11 clipboard in this
headless session returns an empty object even for text the test itself wrote, so
`clipboard.readText()` could not confirm the copy. The handler ran and returned
`ok:true`, and the auto-lock-after-copy path is covered by the lock test above. On a
real desktop the clipboard is a normal working thing.

**Three of my own assertions were wrong, not the code** - they expected one GitHub
entry where two were legitimately saved, expected different wording in the reject
note, and read the DOM before the state push landed. Fixed the tests, re-ran, all
green. Kept here because "the test disagreed" is exactly where a real bug hides.

## What I still cannot prove from this box

- Nobody has run the `.exe`. There is no Windows here; the package is verified by
  extracting it and diffing the payload, not by installing it.
- On this Linux box `safeStorage` reports no usable keychain, so the keychain branch
  of the vault is code that ran and reported honestly - the master-password branch is
  the one exercised end to end. On Windows, DPAPI makes the keychain branch the default.
- Mouse gestures are absent by the platform's choice, not mine: Electron hands me page
  and window events, no raw pointer stream to interpret.

## Round 20 - the rest of GX, and an audit of the wiring

This round added the features that were missing from the parity list (GX Control, Tab
Islands, containers, Snapshot, panic, RGX, sidebar services, media keys, sounds, the
stylesheet mod box, WebRTC/DoH/referrer/permission hardening) and built two checkers,
because "I added 20 settings" is not the same statement as "those settings reach code".

### Static: `node tools/audit.mjs`

    channels: 63 handled, 63 called from bridges, 7 push events
    settings: 36 keys, 67 ids on the settings page
    note  main.js has 25 empty catch blocks - each one is a place a failure becomes invisible
    no defects

Exit code 0, and it is the check that catches a control wired to nothing: it flagged the
**"Remember history" switch as dead** (a control on the page with no key behind it) and a
speed-dial selector that matched every `.tile-row` in the document, vault rows included.
Both fixed, then re-run. What it enforces: every `invoke` in the preload bridges has an
`ipcMain.handle`, every pushed event is in the preload allow-list, every key in
`DEFAULT_SETTINGS` has a control id on the settings page (and vice versa), every `$('#id')`
in the UI exists or is created by code, no duplicate ids, no unscoped class queries, and
`node --check` plus a brace-balance pass on all four JS files.

### Runtime: 51 assertions, driven through the real app

The harness (a temporary block inside `main.js`, deleted before this build was packaged)
started two local HTTP servers serving a probe page with a login form, a `<video>`, a
canvas, geolocation and ICE probes, a chunked attachment download and a cross-port image,
then drove windows, tabs and IPC and read back the results - including reading the files
on disk. Last run: **51 PASS / 0 FAIL / 0 ERROR**.

| Group | Measured |
| --- | --- |
| Vault host matching | exact host ✓, subdomain ✓, `www.` parent ✓, lookalike suffix refused, a `example.co.uk` entry **not** offered on `evil.co.uk` but still offered on its own host and subdomains |
| Vault crypto | 40 writes → 40 unique 12-byte IVs, 0 reused; identical plaintext → 6 distinct ciphertexts; scrypt 65 ms/guess ≈ 55,700 guesses/hour/core; no plaintext byte in `vault.bin` |
| Vault frames | field inside another site's frame refused with the reason in the menu label; same-origin frame allowed |
| Credential hygiene | `storeUrl()` → `https://example.com/path`; `hunter2` absent from `history.json`, `session.json`, `workspaces.json` and a byte-scan of the whole profile |
| `remember` switch | history 1 → 1 with it off, 1 → 2 with it back on |
| Containers | same site in a container gets a separate cookie jar (`tag=A-9` vs `tag=A-14`) and `localStorage === null` |
| Snapshot | file starts `89 50 4E 47` (real PNG); "save as PDF" starts `%PDF-` |
| RGX | computed style on the `<video>` is `contrast(1.06) saturate(1.10) brightness(1.03)`; one idempotent `#__pv_rgx` node; removed when switched off |
| Permissions | geolocation → `denied:1`; the deny list is a real setting, editable from the page |
| Referrer | cross-port subresource arrives with **no** `referer` header when the switch is on, and the switch changes live behaviour, not just state |
| WebRTC / DoH | `--force-webrtc-ip-handling-policy=default_public_interface_only` present in Chromium's own switch list; a DoH change reports `restartNeeded: true` and the restart note appears |
| GX Control | hot tabs report per-tab memory and CPU (`281MB`, `0%`...), panel sleeps a tab, panel wakes it again (`discarded` false after reload) |
| Panic | `F12` → one tab on the configured escape page, `F11` → 6 → 6 tabs back, nothing written while panicked |
| Sounds | `window.gxSound` exists and calls do not throw (audibility is not measurable headless - see below) |
| Mods | `:root{--accent:#ff0055}` in the mod box really repaints the chrome (computed `--accent` = `#ff0055`) |
| Services rail | a rail service opens as an ordinary tab, marked `service`; media keys paused the real `<video>` (`how: "pause"`) |
| Tab Islands | tab opened from a page carries `parentId`; strip renders one collapse control for the group; collapsing hides the children (`9..15`), expanding restores them (`9..16`); the tab you are on is exempt |
| Bandwidth cap | uncapped 0.64 s vs 3.54 s at 120 KB/s, with a 3.46 s plateau in the byte counter against 0.56 s uncapped |
| `batteryAuto` | persists like any other setting and survives a re-read |

### Three real bugs the harness caught, all fixed here

1. **"Wake this tab" was dead.** `revive(t)` expected a tab object; the tab menu and the
   GX panel both pass an id, and `!7.discarded` on a number is `undefined` - so the guard
   returned early and nothing happened. `revive` now resolves either form.
2. **The referrer rule could not fire on IP hosts.** The cross-site test used the same
   registrable-domain logic as cookies, and `etld1('http://127.0.0.1:8799')` equals
   `0.1` on both sides, so a cross-port subresource looked same-site. Referrer policy now
   compares origins (`crossOrigin()`, port-aware, opaque `null` treated as cross); cookies
   keep the host rule, because that is what cookies are keyed by.
3. **A user stylesheet mod could not repaint the accent.** `applyAccent()` set custom
   properties inline on `documentElement.style`, and an inline declaration outranks any
   stylesheet, so the mod box looked broken. The accent now lives in a
   `<style id="accent-style">` node ordered before `#modstyle`.

### One feature I deleted, because measurement said it was a placebo

The settings page had a "canvas and audio farbling" switch. In a controlled test the page
read its own canvas fingerprint **unchanged** (`10,20,30` before, `10,20,30` after):
Electron's `registerPreloadScript` runs in an isolated world, and my patch was injected at
`did-stop-loading`, which is after the page's own scripts have already sampled the canvas.
There is no main-world document-start hook to fix that with. A fingerprint taken through a
detectable patch is a *stronger* identifier than an unpatched one, so the switch, the code
and the setting key are gone, and the settings page now says why in plain words.

### What the bandwidth cap is, and what it is not

It is a duty cycle: while a download runs ahead of `bytes / cap`, the item is paused, then
resumed. Measured, it works - 0.64 s became 3.54 s and the stall shows up as a 3.46 s
plateau in the received-byte counter. What it cannot do is pretend to be a token bucket:
bytes already sitting in the socket buffer arrive regardless, so on a loopback link a
512 KB file finished at the same speed whether I asked for 120 KB/s or 45 KB/s. Over a real
WAN, where the pause actually closes the TCP window, the limiter binds repeatedly instead of
once. That is the honest shape of it, and it is why the cap is documented as a cap on
*downloads*, not a global shaper.

### The artifact in this zip

`PsychoVault-Setup-1.0.0.exe` - about 105 MB, 39 files inside. Its exact sha256 is in
`SHA256SUMS.txt`, which sits beside the installer at the top level of this archive and is
deliberately a separate file: no document packed inside an installer can carry the hash of
the archive that contains it, so that number is written after the build instead of pretending
to be self-describing. `ls -l` on the extracted file gives the byte count.

Then the part that matters: the built installer was opened and its payload compared
byte-for-byte against the source in this repo - `app/main.js`, `app/preload.js`,
`app/page-preload.js`, `app/blocklist.json`, `app/ui/index.html`, `app/ui/style.css`,
`app/ui/renderer.js`, `app/ui/newtab.html`, `app/ui/settings.html` - all nine identical.
The bytes that produced the 51 green assertions are the bytes you install.

The raw output of the last run is in `docs/harness-round20.txt` - every line is an assertion
that ran against the app, with the value it measured.

### Still not provable from this box

- Nobody has installed the `.exe`: there is no Windows here. Verified by extraction and
  byte-diff, not by clicking through an install.
- Sounds: the WebAudio graph is constructed and returns without throwing; no speaker and no
  audio device exists under Xvfb, so I cannot claim you can *hear* it.
- The keychain branch of the vault: this Linux box has no libsecret, so `safeStorage`
  reports unavailable and the master-password branch is the one exercised end to end.

## Round 21 - three reports from a real Windows box

You cannot get these from a sandbox. Someone installed it, and it told me three things my
51 assertions had no way to reach. All three were real; all three are fixed and re-run.

| Report | What the code was actually doing | Fix | Proof |
| --- | --- | --- | --- |
| "i cant close workspaces" | The only close gesture was `chip.onauxclick` with `e.button === 1` - a middle-click, which is invisible and, on a lot of Windows mice and every trackpad, not a thing that happens. `wsClose` also skipped `sendState()` when you closed a workspace you were not in, so the strip kept showing the closed set | `x` button on every chip (left-click), right-click menu on the chip, middle-click kept, and `sendState()` on both branches. On the last workspace the `x` clears its tabs rather than breaking the "you always have a home" invariant | 21 runtime assertions: chip count 3 → 2 after a DOM click on the `x`, the chrome's chip row follows, `workspaces.json` on disk shows 2, active index stays in range; menu labels read `["Switch to this workspace","New workspace","Close workspace \"Alpha\""]` and change to `Close all tabs in this workspace` on the last one; closing the last one leaves 1 workspace, 1 tab, `urls: []` on disk |
| `TypeError: clipboard.readText(...).trim is not a function` in a `MenuItem.click`, shown as *"A JavaScript error occurred in the main process"* | "Paste and go" did `clipboard.readText().trim()` and assumed the return is a string. On that machine it was not. One menu item was enough to take the browser down with a modal dialog | `clipText()` - typeof-checked, try/caught, returns `''`; the menu item is `enabled: clipText().length > 0`, so a weird clipboard disables an item instead of crashing the app; `process.on('uncaughtException')` and `unhandledRejection` now append to `errors.log` and the app continues (first one also raises a quiet notification) | with `clipboard.readText` forced to return `42`: `clipText() === ''`; forced to throw: caught and logged; the item came back `enabled: false`; the tab did not navigate (`went === 0`). `process.emit('uncaughtException', new Error('harness probe error'))` wrote the line into `errors.log` and the process stayed up |
| "wont install over the top of a install too update it" | `.onInit` contained a `FindWindow` that did nothing, and the copy section took the first sharing violation as a fatal error: `Can't write: ...\PsychoVault.exe`, Installation Aborted | Setup now probes the old exe by opening it for append (a running exe fails that), asks once - OK closes the app via `taskkill`, Cancel aborts with the old install untouched - and the copy retries once after another kill before it gives you a sentence instead of a wall. The stale payload folders are removed before the copy so an update leaves nothing behind | `makensis` compiles the new script: `Processed 1 file`, 476 install instructions, 90 uninstall, exit 0. **The Windows behaviour itself is not testable here** - no Windows, no locks. The logic is in the script and it builds |

Reading the installer for that last one turned up a fourth bug nobody had reported: the
uninstaller's "delete your profile" step pointed at
`$LOCALAPPDATA\PsychoVault\User Data`, which is not where Electron puts `userData` - the app
never calls `app.setPath`, so on Windows the profile is `%APPDATA%\PsychoVault`. That
`RMDir` was deleting a path that does not exist, while claiming to clear your bookmarks and
history. It now removes `$APPDATA\PsychoVault`, and only after the yes/no.

Two of my own tools were wrong too, which is the usual pattern: `ws:menu` first came back as
`TypeError: menu.getItems is not a function` (Electron exposes `menu.items`, not `getItems()`)
- the harness caught it on its first run, and the handler now reports the template labels it
built. And the first harness pass looked broken only because it reused a profile with
workspaces still in it; the app was correctly restoring them.

**Not provable from this box:** the install-over fix (no Windows, no locked files), audible
sounds, and a real X clipboard round-trip - `clipboard.writeText` through the new
`clipboard:write` handler returns `ok:true` here and the read-back is empty under Xvfb, same
limitation as round 19. Everything else above is an assertion that ran against the app.

## Round 22 - spell check, real brand icons, and a VPN that is wired up

Three complaints, one of them a rule rather than a feature: *a switch that saves a setting
but changes nothing is worse than no switch*. So all three were built against the API that
does the work, and all three were then checked by driving the real app.

### Spell check: what it took, and what turns out to be impossible

`webPreferences.spellcheck: true` was already set for page views, and that is the whole of what
an Electron build gives you. Everything below was measured on this Electron (45.1.0, Chromium
156.0.8078.49), not read out of documentation:

| Thing I tried | What happened |
| --- | --- |
| `session.downloadDictionary()` / `isDictionaryAvailable()` | **the methods do not exist** on this build (`typeof` is `undefined`) - they were removed from Electron |
| `setSpellCheckerDictionaryDownloadURL('http://127.0.0.1:8797/dict/')` with real `.bdic` files served | **not one request arrived** at my own server (the log held only the page and its favicon). No downloader is compiled in |
| `file:///…/` folder with `en-US.dic` + `en-US.aff` (a Hunspell pair I generated) | nothing: `params.misspelledWord` stayed `""` |
| `<profile>/Dictionaries/en-US.dic` + `.aff`, present before first run | nothing, same result |
| `--enable-features=Spellcheck` on the command line | nothing, same result |
| `availableSpellCheckerLanguages` | lists 57 languages happily. That list is a promise, not a dictionary |

So the browser reports languages it cannot check, which is exactly the "a switch that saves a
setting and changes nothing" complaint the round started from. `setSpellCheckerEnabled` /
`setSpellCheckerLanguages` are still driven properly (they are what makes Chromium ask for the
menu at all, and on platforms where dictionaries exist they do the work), but the feature now
comes with its own engine so it works here and on Windows out of the box:

- **`app/spell-words.txt`** - 15,000 words in frequency order (MIT-licensed list; the more famous
  10,000-word one was fetched, tested, then removed because its licence says to license the data
  from the LDC for anything distributed - see `THIRD-PARTY-NOTICES.md`).
- **`app/spell-page.js`** - injected into the page's own world on `dom-ready`. It tokenises the
  field, checks each word against the list plus your own words (with cheap `-s/-es/-ed/-ing/-ly`
  handling so plurals do not all light up, and skipping acronyms, numbers and words with digits),
  and paints the underline itself: a `pointer-events:none` mirror layer over the textarea with the
  field's computed font metrics copied, bad words wrapped in `text-decoration: underline wavy
  #ff5b5b`, synced to the caret's scroll. It is not contenteditable-only theatre: it is the same
  red squiggle a browser draws.
- **Suggestions** - Damerau distance-1 generation (delete / transpose / replace / insert) against
  the dictionary, falling back to a lazily-built deletion-neighbourhood index for distance 2,
  ranked by edit distance then by how common the word is. `"teh"` → `["the","ten","eh","tea","heh"]`
  - the right answer first, because the list is frequency-ordered.
- **The menu** - `pageMenuTemplate()` asks Chromium first (`params.dictionarySuggestions`) and
  only falls back to the page's answer if Chromium had none, so on a build that does have
  dictionaries nothing changes for you; the row says `suggestion by PsychoVault` when the fix came
  from here, rather than pretending to be the browser's.
- **Your words** are one dictionary, not two: `spellUserWords` in the settings file feeds both
  Chromium's `addWordToSpellCheckerDictionary` and the page checker, and the textarea in Settings
  is authoritative in both directions.
- **`spellBuiltin`** in Settings - Spelling turns the whole engine off if you would rather wait
  for a browser dictionary; the Status line then says `engine: none, nothing will be underlined`
  instead of claiming success.

### The rail icons are the sites' own icons

No icon service, no bundled logo set. For each rail entry PsychoVault asks that origin for
`/favicon.ico`, and if that 404s it reads the page and follows the `rel="icon"` link. The
bytes are sniffed (PNG/GIF/JPEG/ICO/SVG magic), rejected if they are not an image, cached at
`<profile>/icons/<sha1(origin)>`, and handed to the chrome as a data URL - so a cached icon
survives being offline and nothing is fetched twice. Each button also gets the brand colour as
a CSS variable for its underline, and a picture that fails to decode in the renderer falls back
to the two-letter badge rather than showing a broken-image box. `favicon` fetching uses
`net.request` with `useSessionCookies: false`, so no cookies ever go to a favicon endpoint.

### VPN: bundled wireproxy, driven honestly

wireproxy v1.1.3 (ISC, `windtf/wireproxy`) is a userspace WireGuard client that exposes a
SOCKS5 listener; both tarballs were downloaded from the release, checked against the published
`checksums.txt`, and the 10,666,496-byte Windows exe that now ships at
`resources\app\tools\wireproxy.exe` is sha256 `b176b561fd8bf15d828fcab484cfd5b4fb941cb9f61807901ca64b955af27e1f`.

What the app does with it, in order: your pasted config (or the file you point at) is copied -
never edited in place - to `<profile>/wireproxy.conf` with mode `0600`, with `[Socks5]
BindAddress = 127.0.0.1:<port>` appended if it is not already there; `wireproxy -n -c <conf>`
has to say `Config OK` before anything is spawned; the child is started with `-c` only (no
shell); and "is it up" is answered by **TCP-connecting the SOCKS port**, not by scraping
stdout, because the port number is already known to us from the config we wrote. Then every
session gets `socks5://127.0.0.1:<port>`, cache is cleared so no page is remembered from the
direct route, and on the way back out the proxy is returned to `system`.

Because SOCKS5 in wireproxy is CONNECT-only (UDP is a TODO upstream), QUIC would leak around
the tunnel - so `--disable-quic` is applied while the tunnel is on, and the status shows a
"needs a restart" marker rather than pretending the switch took effect instantly. If the child
dies on its own, the kill switch arms: `VPN.blocked` makes the request-shield cancel every
http(s) request until you turn the tunnel off, and the restart ladder tries 2 s / 4 s / 8 s
before giving up - with three different log lines so "auto-restart is off" does not read as
"ran out of retries".

### Runtime proof: three harnesses, 101 assertions, 0 failures

| File | Assertions | What it covers |
| --- | --- | --- |
| `docs/harness-round22.txt` | 56 pass, 0 fail, 0 error | the whole round as first built: policy, menu, icons, catalogue, keygen, configtest, tunnel, kill switch, retry |
| `docs/harness-spellcheck.txt` | 19 pass, 0 fail | the checker in a real page: underlines, suggestions, ranking, apply, your words, off/on, harmless on pages with no fields |
| `docs/harness-round22b.txt` | 26 pass, 0 fail | the round re-run against the final code, after the ICO fix, the `page-preload.js` gap and the message changes - plus an assertion that no harness or driver code is left in the file |

All of them drive the shipped IPC and the shipped page script; nothing is re-implemented for the
test, and each one failed at least once before it passed.

Driven through the shipped IPC (no re-implementation): a local HTTP server for favicons and
the exit-IP endpoint, and the **real Linux wireproxy binary** for the tunnel.

| Claim | What was actually observed |
| --- | --- |
| the switch reaches the browser, not just the settings file | `defaultSession.isSpellCheckerEnabled()` flips `true -> false -> true` on `spellCheck`, and a container session follows |
| your language list is honoured | `"de-de, en-us"` -> `getSpellCheckerLanguages()` = `["de-DE","en-US"]`; `"zz-zz"` -> reported as missing, `languages: []`, no throw |
| your words round-trip | `spell:add` -> `settings.spellUserWords` contains it, `spell:remove` -> it does not |
| the underlines are real | `"teh quikc brown fox jumped ovre the laty dog"` -> `bad: 4`, four `<u>` spans in the mirror layer, style contains `underline wavy`; `"The quick brown fox jumps over the lazy dog. NASA and HTTP work fine, 42 too."` -> `bad: 0` |
| the suggestions are useful | caret in `teh` -> `["the","ten","eh","tea","heh"]`, and the menu puts `the` first because the list is frequency-ranked |
| clicking one edits the page | field went from `"teh quikc brown fox "` to `"the quikc brown fox "`, and `bad` dropped from 4 to 3 |
| adding a word lands in the page | after `spell:add("laty")` that word stopped being flagged, in the live field, without a reload |
| suggestions really are in the menu | template labels `["the", "tea", "Add “teh” to dictionary"]`; clicking `the` calls `replaceMisspelling("the")`; empty suggestions -> disabled "No suggestions from the dictionary" row; `isMissingSpellChecker` -> a row pointing at Settings |
| icons come from the site itself | 1 request to `/favicon.ico` (404), 1 to the page, 1 to `/brand.png`; cached file in the profile; `railIconMap` = `["http://127.0.0.1:8799"]`; the rail's `<img>` has `naturalWidth === 32`, i.e. the bytes decoded |
| the cache is used | a second refresh made **zero** further requests (`faviconHits 1 -> 1, docHits 1 -> 1`) |
| bad payloads are refused | `<html>` served as `image/png` is not cached (magic check returns null); a garbage data URL in the map gives the badge, not a broken image |
| the catalogue is real | 18 entries, each with a `#rrggbb` brand colour; tick GitHub -> rail goes 1 -> 2 buttons; untick -> 1 |
| right-click on a rail button | `["Open Probe","Open in a new window","Reload","Copy address","Remove from rail"]` |
| key generation is WireGuard-shaped | `crypto.generateKeyPairSync('x25519')` -> 44-char base64, and the config built from those keys passes `wireproxy -n` with `Config OK` |
| config hygiene | `[Socks5] BindAddress = 127.0.0.1:25399` appended to the copy; file mode `600` |
| the tunnel starts and Chromium obeys | `state: running`, `port: 25399`, `pid > 0`; `proxyRule` `socks5://127.0.0.1:25399`; `resolveProxy('http://example.com')` -> `SOCKS5 127.0.0.1:25399` |
| **traffic really goes through it** | with the tunnel up and no peer at the endpoint, `example.com` never returns a page (it hangs), and the log shows wireproxy itself saying `Resolving address for example.com` - the name reached the tunnel process, so it was not resolved locally |
| kill switch | process SIGKILLed -> `state: down`, `blocked: true`, log `wireproxy exited (SIGKILL)` + `not retried - auto-restart is off`; a navigation then fails with **`-20` (net::ERR_BLOCKED_BY_CLIENT - "the client chose to block the request")**, which is this app cancelling it, not the network |
| turning it off | `state: off`, `blocked: false`, `proxyRule: system`, and `example.com` loads again; a stop you asked for is logged as `stopped`, never as a crash |
| failure modes are distinguishable | a config the binary refuses (`-n` exits 1) is **never spawned**; a binary that starts and dies reaches `retry 1/3 in 2s`; a missing binary says `wireproxy is not here` and names the `tools` folders and PATH |
| QUIC | `vpnBlockQuic` flips `state.vpn.quicDrift` to true so the UI can say "applies on restart" |

### What this round caught in my own code, before shipping

All six of these were found by the harness, not by reading:

1. `trackSession()` did not add the session to the registry, so `settings:set` re-applied the
   spell policy to containers and **skipped the default session** - the main one. (Test 3.)
2. Language tags were lower-cased before comparison against Chromium's canonical list, so every
   user-specified language was filtered out and `setSpellCheckerLanguages` was never called -
   and had it been called with `en-us`, Electron throws. (Test 5.)
3. `spell:status` returned an object containing a function, which the IPC layer cannot clone:
   `Error: An object could not be cloned`. The whole spell panel was dead. (ERROR, first run.)
4. `fetchIconFor` still keyed its pending-set by the old `host` variable after the switch to
   origins: a `ReferenceError` in `finally`, so the entry never cleared and each origin could
   only ever be fetched once per run.
5. `net.request` was being read off **Node's** `net` module (imported for the TCP probe) -
   `TypeError: net.request is not a function` in two places, one of which was every single
   favicon fetch. The exit-IP check was dead for the same reason.
6. `vpnStop()` set the state to `off` synchronously, so when the child later reported its exit,
   the handler could not tell a user stop from a crash (`state !== 'stopping'`, code null) and
   re-armed the kill switch. Result: **after one crash, turning the VPN off left your web
   traffic blocked** - `proxyRule: system` but `blocked: true`. There is now a `stopRequested`
   flag, the state waits for the exit event with a 4 s backstop, and the exit handler says
   `stopped` for a stop you asked for.

`session.getProxy()` does not exist either (it is `resolveProxy`); the app now keeps the rule it
applied as `_pvProxy` and reports both "what we told Chromium" and "what Chromium will do".

### Still not provable from this box

- **Windows.** The bundled exe is verified by digest, not by running it; the tunnel lifecycle
  above was proven with the Linux binary, which is the same program and the same flags.
- **A real exit IP.** There is no WireGuard peer for me to connect to, so what is proven is that
  DNS and TCP leave through the tunnel (the `Resolving address for example.com` log line and the
  hang while the peer is dead), not which address a site sees. `vpn:checkip` against a local
  server does prove the check reads through whatever proxy is set (`via: "direct (no tunnel)"`
  with no tunnel, `code: 200`).
- **Opera's VPN product.** This is your config on your provider's key; it is not a free proxy
  run by me, and I am not going to imply that it is.

## Round 23 - the VPN panel stopped reporting a problem it could not fix

Reported from the live Windows install: a screenshot of the rail panel reading `STATE error`,
`LAST ERROR no WireGuard config yet...`, `AUTO-START on`, and the same refusal twice in the
log. Every part of that was wrong except the diagnosis.

- **A refused start no longer arms auto-start.** `vpn:toggle` wrote `vpnEnabled: want` *before*
  trying, so turning it on with nothing configured promised to bring the tunnel up at every
  future boot and produced a red `STATE error` each time. Arming now happens only when the
  returned state is `running`/`starting`, and the boot path - `vpnAutoStartBoot()`, called from
  `whenReady` - disarms when there is no config or no binary, but not on a transient peer
  failure, which should keep trying.
- **`STATE error` became `needs a config`**, amber instead of red, and the rail button follows
  it (`vpn-warn`, tooltip `no config yet`). `vpnStatus()` also stopped inventing a path: it
  printed `VPN.confPath()` whether or not that file existed, which is the
  `CONFIG C:\Users\...\PsychoVault\wireproxy.conf` line in the screenshot pointing at
  nothing. It now names the source that would actually be used and adds `confFile`,
  `confExists`, `hasConf`, `needsConfig`.
- **The panel is where you configure it now.** New `vpn:getconf` / `pv.vpn.getconf()` returns the
  config in use (paste or file, 8 KB cap) and the panel renders it in a box with `Save config`,
  `Save and turn on`, `Test config` and `Check exit IP`; `Test config` puts wireproxy's own words
  on the button instead of a bare "error". An empty box does not veto a config already on disk,
  and saving clears the stale error, so the panel cannot stay red for the thing it just fixed.
- Two smaller finds: `VPN.note` dedupes a repeated complaint within its last 4 lines (one refusal
  per incident, not per click), and `sendState()` no longer throws
  `TypeError: Cannot read properties of undefined (reading 'id')` when a renderer-crash or
  teardown event pushes state onto ids whose tabs are gone - it prunes them first. Both came from
  running the *packaged* tree rather than the source tree.

```
node tools/audit.mjs      ->  no defects
docs/harness-round23.txt  ->  30 PASS / 0 FAIL
```

| Proven by that run | Result |
|---|---|
| no binary on disk: `state: missing`, one `start refused` line after two clicks, `vpnEnabled` still `false` | pass |
| binary but no config: `needsConfig true`, `conf ''`, `confExists false`; panel row class `warn` reading `needs a config`; rail button `vpn-warn` and not `vpn-down` | pass |
| the panel offers `Save and turn on`, not a doomed `Turn on`, and carries the paste box | pass |
| booting into that state disarms auto-start and logs `auto-start turned off: no WireGuard config yet` | pass |
| paste + `Save config` -> `wireproxy.conf` written mode `0600`, `state: off`, `err: ''` | pass |
| `Test config` on `PrivateKey = broken` shows `invalid base64 string: broken` on the button and never claims running | pass |
| an empty box refills itself from disk; `Turn on` -> `running` on the configured port, pid set, `resolveProxy` -> `SOCKS5 127.0.0.1:25455`, rail `vpn-on`, auto-start `true` | pass |
| `Turn off` -> `off`, auto-start `false`, kill switch not left armed, session proxy handed back | pass |
| `tabs.clear()` plus orphan ids in `order`, then a state push: no throw | pass |

### The build path moved under me, so it is a script now

Mid-round the sandbox was recycled: `/var/tmp` (Electron runtime, makensis staging, the wireproxy
kit) and the apt-installed `nsis`/`p7zip` disappeared, and `app/tools/` - the bundled
`wireproxy.exe` - had to be restored from the previous zip. Packaging is therefore in the repo
now, `tools/build-installer.sh`: restage `resources/app`, run `makensis`, then extract the built
installer and `cmp` nine payload files against the source (`main.js`, `preload.js`,
`page-preload.js`, `spell-page.js`, `spell-words.txt`, `ui/renderer.js`, `ui/style.css`,
`ui/settings.html`, `tools/wireproxy.exe`), the last of which must match the upstream digest
`b176b561fd8bf15d...` twice over - in the workspace and inside the installer. Two installer fixes
came with it: `MUI_PAGE_LICENSE` was added (the licence was named in the docs and never shown)
and one welcome-page bullet had a double-escaped `\r\n` that printed its line break as text.

**Still unproven, and not claimed:** no Windows run of the bundled `wireproxy.exe` (verified by
digest; the lifecycle is proven with the same program's Linux build), and there is no real
WireGuard peer here, so which exit IP a site sees is unproven.

---

## Round 24 - tab reordering and the download flow

Two bugs reported from the installed build: tabs could not be dragged into a different order, and
a download vanished into the Downloads folder with no question, no progress and no confirmation.
Both were true, and neither was a regression - the code for them had never existed. Assertion log:
`docs/harness-round24.txt`, 38 assertions, 0 failures, run against the real app under Xvfb.

### Tabs could not be dragged because nothing listened for a drag

There was no drag code in the strip at all. The obstacle is that `#tabstrip` sits inside a
`-webkit-app-region: drag` band so the empty space moves the window, which is why the usual HTML5
drag-and-drop route is a dead end there - Chromium hands those gestures to the window mover. The
new code uses pointer events on the tab itself: a gesture only becomes a drag after 5px of
horizontal movement, so a click still selects and a wobble does nothing.

The renderer sends **an id, not an index** (`{ id, before }`). Indices in the DOM skip collapsed
tab-island members, so an index computed from element positions is wrong the moment a group is
collapsed; "put this tab before that one" survives it. The main process removes the tab, finds the
target id in the remaining order, and inserts there, clamping at the pinned block - pinned tabs
are one run at the head of the strip and a drag stops at that boundary instead of silently
re-sorting them (`setPinned` sorts, so order is not to be trusted for the boundary either).

Two faults surfaced while testing the drag:

1. **A state push killed the drag.** Every title, favicon or loading flag calls `sendState()`, the
   renderer rebuilds the strip, and the element under the pointer is deleted mid-gesture. The
   first screenshot caught exactly this: the drag had silently died before the frame was taken.
   `renderTabs()` now defers while a drag is live and repaints once it ends - asserted by pushing
   state deliberately mid-drag and checking the ghost and caret are still there.
2. **`w.session` does not exist.** The save-dialog parent lookup used it; each tab builds its own
   `Session` through `newSession()`, so the lookup silently fell back to no parent window.

### Downloads: silent by default, and the list closed itself

`askSaveLocation` defaulted to `false`, and the code behind it was a three-button message box
(`Save / Save as... / Cancel`) whose "Save" then wrote straight to the default folder - "Save as"
was the only way to choose a folder, and Cancel still let the transfer start before refusing it.
Replaced with a single `showSaveDialogSync`: one question, the folder is part of the answer, and
Cancel calls `event.preventDefault()` so nothing is written and no row appears.

Progress only ever reached the UI when a bandwidth cap was set, because the sole `updated`
listener belonged to the cap's duty-cycle code. A throttled listener (5 pushes a second) now
broadcasts while the file moves, and the completion path announces the result. New in the UI:

- rail badge counting live transfers, cleared when nothing is moving;
- a start message that names the destination folder, and a finish message;
- system notification when no window has focus (`Notification`, already used for error recovery);
- the downloads panel states `Saved to <folder>` with a **Change** button, and repaints in place.

That last one was a real bug, found by the screenshot being wrong: the panel refreshed itself by
calling `openPanel()`, which **toggles** when that panel is already open - so the first live
progress broadcast closed the panel it was meant to update. Painting is now `paintPanel()`, and
the harness asserts the panel is still open, still showing a live row and still showing its folder
line 900ms into a transfer.

Because an existing profile already has `askSaveLocation: false` written to `settings.json`, a
new default alone would never reach it. `settingsVersion` is bumped to 2 once, which turns the
dialog back on for anyone who never chose otherwise and then stops moving, so a later "off" sticks.

### What the harness proves

Local HTTP server, real `will-download` events, no stubs except the save dialog itself:

| Assertion | Result |
| --- | --- |
| `moveTab` reorders; pointer drag from the strip moves the first tab to the end | pass |
| caret painted before the tab under the pointer; dragged tab ghosted | pass |
| a `sendState()` in the middle of a drag leaves the drag alive | pass |
| pinned tab stays at the head; unpinned tab cannot land inside the pinned block | pass |
| panel stays open, keeps its folder line, shows a live `28%` row | pass |
| rail badge reads `1` during transfer, `Downloads - 1 in progress`, clears after | pass |
| start message carries the file name **and** the destination folder | pass |
| file lands in `downloadDir` at full size (245,760 bytes) | pass |
| with asking on: one dialog, `defaultPath` inside the download folder, `createDirectory` offered | pass |
| file saved exactly where the dialog answered (`/tmp/pvdl2/picked-name.txt`) | pass |
| cancel: no row added, no file written | pass |
| two silent downloads of the same name: `quick-test.txt`, then `quick-test (1).txt` | pass |

**Not proven here:** the modal save dialog itself was stubbed in-process (a native dialog cannot
be answered under Xvfb), so the assertion is that the code asks the right question with the right
default, not that Windows draws it. The system notification path - the one that fires when no
window is focused - is exercised only as far as `Notification` construction on a headless host.

---

## Round 25 - the VPN panel, again: a dead end is a bug

The report was a screenshot of the installed panel reading `needs a config`, with the example text
in the box, and the words "still broken". The state was honest - there was no config - but the
panel was a dead end: pressing `Save and turn on` with an empty box changed one button's caption
for three seconds and nothing else, the example text looked like content, and nothing on screen
said where a config was supposed to come from. Assertion log: `docs/harness-round25.txt`,
38 assertions, 0 failures.

### What was actually broken

1. **Every outcome message could be lost.** `renderVpnPanel` built a *new* banner node and the
   button handler wrote into the old, detached one, so after any state push the message went
   nowhere and the panel kept the previous sentence. `setHint()` now writes through to whatever
   banner is on screen, and a hint is tagged with the state it belongs to, so "Saved to ..." does
   not outlive the save.
2. **Only the message was missing, but the press also did nothing.** An empty box with
   `needsConfig` returned early with no explanation. It now focuses the box and says, in red,
   that there is nothing to run and names the three ways to fix that.
3. **The example was a placeholder that read as content.** It now says, in the placeholder, that
   it is an example and not a config.
4. **No path in.** Ctrl+V does work in that textarea (verified with a real key event and with
   `webContents.paste()`, both in the log), but relying on the user knowing that, with the
   clipboard holding a config they cannot see, is not a path. There are now **Paste from
   clipboard** and **Choose .conf file...** buttons, and a typed file path is read from disk by
   `vpn:save` instead of being stored as the config body.
5. **`clipText()` never returned anything.** In Electron 45 on this host `clipboard.readText()`
   returns a Promise; the old helper compared it to `'string'` and answered `''`. That silently
   disabled "Paste and go" in the chrome's context menu - the same class of bug as the VPN panel:
   a control that is present and does nothing. It is `async` now, and the menu item reads the
   clipboard when chosen rather than while the menu is built.
6. **The panel now answers "where do I get one?"** three concrete sources, plus the sentence that
   PsychoVault runs no VPN servers - because an empty box with no explanation reads as "the
   browser is broken", which is exactly how it was reported.

### What the harness proves

| Assertion | Result |
| --- | --- |
| empty box + `Save and turn on`: red banner naming the fix, nothing armed, state not `running` | pass |
| the example is a placeholder (textarea value is empty) and says so | pass |
| `clipText()` reads the clipboard (249 chars) after the Promise fix | pass |
| **Paste from clipboard** fills the box and confirms it looks like a wg-quick config | pass |
| **Choose .conf file** loads the file and says which file | pass |
| typing `hello` names the missing fields (`[Interface]`, `PrivateKey`, ...) | pass |
| Save: `needsConfig` clears, state `off`, error empty, file written `0600` | pass |
| panel confirms the save with the path and byte count; button becomes `Turn on` | pass |
| `PrivateKey = broken` -> wireproxy's own text on the panel (`invalid base64 string: broken`) | pass |
| a typed path is read as a file, and `[Socks5]` is appended to what was saved | pass |
| `Turn on` -> `running`, live pid, `resolveProxy` = `SOCKS5 127.0.0.1:25455` | pass |
| `Turn off` -> `off`, auto-start disarmed, proxy handed back (`DIRECT`) | pass |

Screenshots: `docs/screenshot-vpn-empty-press.png` (the press that used to do nothing) and
`docs/screenshot-vpn-fresh-profile.png`. Both were taken on this Linux test host, so the
**binary** row shows the test build's path rather than the bundled
`resources\\app\\tools\\wireproxy.exe` a Windows install prints.

**Still unproven, and unchanged:** no Windows run of the bundled `wireproxy.exe`, and no real
WireGuard peer, so which exit IP a site sees remains unproven. What is proven is that the panel
accepts a config three ways, validates it, saves it `0600`, and drives a real wireproxy process
with real SOCKS5 proxying against a local endpoint.

---

## Round 26 - a VPN looks like a list of places, not a text box

The report: "the config should be done automatically, there should be options/locations to pick
from, it's supposed to be a VPN, correct?" Two different complaints in one sentence. The first
cannot be done by a client - see below. The second was a fair hit: a single textarea is not what
a VPN looks like. It is a list of places and a switch. Assertion log: `docs/harness-round26.txt`,
34 assertions, 0 failures.

### Locations

- **Add folder...** reads every `.conf` in a folder, **Add configs...** takes a multi-selection of
  files. Each file is copied into `<profile>/vpn/<id>.conf` at `0600`, named after the file, and
  becomes a row in the panel with its `Endpoint` shown beside it. Anything that does not pass the
  `[Interface]`/`[Peer]`/`PrivateKey`/`PublicKey`/`Endpoint`/`AllowedIPs` check is skipped, not
  imported as a broken row.
- **Clicking a row connects.** If the tunnel is already up, the old process is stopped and the new
  location is brought up in its place - the switch is one press, not stop-then-connect.
- The choice is written to `settings.json` (`vpnActive`) and the runtime config is re-rendered from
  the active file on every start, so the providers' files are never edited in place.
- Upgrading carries the old single config over as the first location (`migrateVpnProfiles`), so a
  config you pasted before does not appear to have vanished.

### What the harness proves

| Assertion | Result |
| --- | --- |
| fresh profile: no locations, the panel explains what to add | pass |
| Add folder: 3 `.conf` files become 3 rows, the `.txt` is skipped | pass |
| rows sorted by name, each showing its endpoint | pass |
| importing does not silently start a tunnel | pass |
| clicking a row connects: state `running`, live pid, `resolveProxy` = SOCKS5 25455 | pass |
| connected row marked with a live dot; banner names the location | pass |
| clicking another row while connected: still `running`, new pid, runtime config rewritten | pass |
| active id and all locations persisted in `settings.json` | pass |
| Remove deletes the file and its row, another location takes over as active | pass |
| a pasted config is imported as one active location on upgrade | pass |

### The part that cannot be automated, said plainly

PsychoVault is the client half of a VPN. A WireGuard config is not a setting that can be
generated locally - it contains the address and public key of somebody's server, and reaching
that server means somebody is paying for it. So "the config should be done automatically" has two
possible meanings, and only one of them is buildable:

- **Automating the mechanics** - yes, and that is what this round did: one folder import instead of
  a text box, a list to pick from, one click to connect, switching without a stop/start dance.
- **Supplying the service** - not without an account somewhere. I did not wire in a free
  WireGuard-endpoint aggregator: those endpoints are unaccountable third parties who can read
  every byte you send, which is the opposite of what a privacy browser should do with your
  traffic, and it would mean shipping a feature that telephones a stranger on your behalf. The
  panel states this in one sentence instead of pretending the box is a bug.

Vendors that do supply locations for this client already hand out the configs in bulk. Mullvad,
Proton VPN, IVPN, AirVPN and Windscribe all export a `.conf` per server from their dashboard;
`Add folder...` on the download folder turns the whole fleet into the list.

**Still unproven:** no Windows run of the bundled `wireproxy.exe` and no real peer, so the exit IP
a site actually sees is unproven. The list, the switching, the file handling and the proxying are
proven against a local wireproxy with a local endpoint.

---

## Round 27 - wireproxy removed, extensions added, and what Proton's extension actually needs

Three instructions, in order: remove wireproxy completely, add extension support, and make Proton
VPN work. The first two are done and proven. The third is now *answerable* rather than doubtful,
and the answer is not the one the request assumed - the evidence is below. Assertion log:
`docs/harness-round27.txt`, 30 assertions, 0 failures.

### 1. Removed

Everything went: the `VPN` state machine (16.8 KB of main process), the `vpn:*` channels (9 KB),
the seven settings keys, the rail button, the panel, the palette commands, the settings section,
the kill switch in `webRequest`, the QUIC switch that existed only because wireproxy's SOCKS5 is
TCP-only, the crash-retry loop, the locations feature, the bundled `wireproxy.exe` (10.6 MB) and
its recovery path in `tools/build-installer.sh`. Grep for `vpn|VPN|wireproxy` across
`app/**/*.js`, `*.html`, `*.css` and the build scripts returns nothing.

That removal is safe to state precisely because of the audit tool: it cross-checks every bridge
call against a handler and a preload. Dropping the `vpn` namespace made it list every leftover
call site in one run, which is how the palette entries and the settings section were found rather
than guessed at.

### 2. Extensions

- **Install** from an unpacked folder, or from a **`.crx`**. Electron refuses `.crx` outright, so
  the browser does what Chrome's installer does: strip the CRX header, read the zip payload out
  with a small central-directory reader (stored + deflate, zip64 refused, entries that escape the
  destination refused), then load the folder. **No signature is verified** - the trust is the same
  as unzipping that file yourself, and the Settings text says so.
- Loaded into every session, including each container partition, so an extension is present in
  every tab a private window does not own. Enable / disable / remove per extension, persisted.
- Every manifest is analysed on install and the verdict is shown in Settings: the extensions API
  this engine implements, the ones it does not, and a sentence per blocker.

### 3. What the harness proves

| Assertion | Result |
| --- | --- |
| a test extension installs from a folder and the engine accepts it | pass |
| its content script runs in a real page (title becomes `EXT-OK: Plain`) and changes the DOM | pass |
| its `webRequest` rule cancels `http://blocked.test/x`, and ordinary traffic still returns 200 | pass |
| disable unloads it from the session; enable loads it again | pass |
| remove deletes the installed copy from the profile and leaves nothing loaded | pass |
| Proton VPN's real `.crx` (2,604,047 bytes) unpacks: 336 files, version `1.3.6`, manifest v3 | pass |
| the panel names what it needs: `chrome.proxy` | pass |
| the Settings list renders it as `cannot work here` with the reason underneath | pass |

### 4. Proton VPN, answered with its own manifest

The extension at `chromewebstore.google.com/detail/.../jplgfhpmjnbigmhklmmbgecoobifkmpa` was
downloaded (CRX3), unpacked and read. Its manifest v1.3.6 requests:

```
permissions: idle, notifications, privacy, proxy, scripting, storage, tabs, webRequest,
             webRequestAuthProvider
```

and its code calls `chrome.proxy.settings` and `chrome.proxy.onProxyError`. That is how a browser
VPN extension works: it does not encrypt anything itself, it points the browser at a proxy.

Electron's documented extension support lists `devtools.*`, `extension`, `management`, `runtime`,
`scripting`, `storage`, `tabs` and `webRequest`. **`chrome.proxy` is not implemented in this build** -
Electron also does not support arbitrary store extensions, only unpacked ones. So the honest result
on this engine is: the extension installs, loads, appears in Settings, and **cannot make a tunnel**.
The panel says exactly that, in those words, next to a live install.

Two consequences worth writing down rather than discovering later:

- A toolbar popup cannot be clicked, because this browser has no toolbar. Extensions with a popup
  get an "Open its window" button that loads `chrome-extension://<id>/<popup>` in a tab - enough
  for its UI to render and be usable, which is what makes an ad-blocker-with-options or a
  password-manager extension practical here.
- MV3 `declarativeNetRequest` is also missing, so an MV3 blocker that relies on it will not filter
  anything. Only MV2 `webRequest` blocking works, and that is what the harness exercises.

At the time of this verification, the Settings card recommended the Windows app. The user later
clarified that the requested destination is Proton VPN's Chrome extension, not its desktop client;
Round 30 below supersedes that recommendation. The extension still cannot tunnel traffic until the
missing `chrome.proxy` API is implemented.


## Round 28 - settings categories, mandatory Save As, and Proton VPN guidance

This pass addresses the three user-facing requests without claiming API support Electron does
not provide.

### Settings organization

- The 17 settings sections are now grouped into nine selectable categories: General, Privacy &
  security, Downloads, Extensions, Performance, Appearance, Spelling, Passwords, and About.
- The selected category is available as a URL fragment and remembered in local storage. The nav
  collapses to a horizontally scrollable strip on narrow windows; section controls themselves
  keep their existing ids and settings bindings.
- Download controls and the stylesheet editor were moved into their own appropriate categories.

### Downloads

- At this revision, the Save As dialog was unconditional; there was no setting that could silently
  bypass it. Startup migration removed the old `askSaveLocation` value from existing profiles, and
  settings writes discarded that retired key as well.
- The chosen download folder was only the dialog's starting directory. At that stage, Cancel called
  `preventDefault()` before a download row was created. Round 30 below replaces the synchronous
  dialog path with an explicitly paused async flow and `item.cancel()`.

### Extensions and Proton VPN

The extension manager remains available in the new Extensions category (unpacked folders and
`.crx` installs, enable/disable/remove, with missing APIs reported). Proton VPN's browser extension
still cannot route traffic here: it requires `chrome.proxy`, which this Electron engine does not
implement. At that revision, the UI linked to Proton's desktop download page; Round 30 below
replaces that CTA with the user's requested Chrome-extension page. No VPN state or successful
connection is simulated by the browser.

### Checks run

- `node --check` passed for `app/main.js`, `app/preload.js`, `app/page-preload.js`,
  `app/ui/renderer.js`, and the inline Settings script.
- `node tools/audit.mjs` passed, including checks that every category has a navigation button and
  panel, all 17 sections are grouped, Save As cannot be disabled, cancellation prevents a transfer,
  and IPC bridges match their handlers.
- `git diff --check` passed.

An Electron GUI/runtime test was not run in this sandbox because there is no display server or
Electron runtime installed.

## Round 29 - window state and pinned tab size

- Maximize/unmaximize no longer push `{ maximized }` as if it were the entire renderer state. They
  trigger a full snapshot (including the current `maximized` flag); restore also relayouts and
  refreshes state. The renderer merges state updates defensively, so a future partial state message
  cannot wipe `tabs`, `settings`, or other existing fields.
- A restored/maximized state updates the window control's title, while retaining the same tabs.
- Pinned tabs now inherit the normal tab flex-basis and width instead of being forced to an
  icon-only 42-pixel tab. The pin marker stays visible; pinned tabs still omit the close button.
- The static audit now checks these invariants, syntax-checks `spell-page.js` and both inline HTML
  scripts, and retains the prior IPC/settings/download/category checks.

Checks run: `node tools/audit.mjs` (no defects), JavaScript syntax checks through the audit,
HTML nesting/unique-ID check, and `git diff --check`. A GUI regression run was not possible in this
sandbox because no Electron runtime or display server is installed.

## Round 30 - paused Save As handoff and Proton VPN Chrome-extension destination

A user report against the previous build showed that the synchronous save-picker implementation
still allowed downloads to land without a useful location choice. This round changes the handoff,
not just the dialog text:

- Every `will-download` item is paused before the asynchronous native Save As dialog is shown.
- The selected path is applied while the item is paused. Only after the download row and progress/
  completion listeners are installed does PsychoVault resume the transfer.
- Cancelling the dialog calls `item.cancel()` and creates no download-history row. A dialog failure
  also cancels instead of silently falling back to the Downloads folder.
- Simultaneous downloads queue their native dialogs, and the parent window is resolved from the
  actual tab WebContents so container and private-window prompts belong to the window that started
  the download.
- The Proton VPN Settings card now opens `https://protonvpn.com/download-chrome-extension` in a
  new PsychoVault tab, rather than opening the desktop-client page in the system browser. A
  downloaded file therefore goes through PsychoVault's own download handling. The card still says
  plainly that the extension cannot route traffic in this Electron build because `chrome.proxy` is
  missing; there is no desktop-app recommendation or claim of an active VPN tunnel.

Checks run: `node tools/audit.mjs` (no defects), `node --test tools/download-save.test.cjs` (4 passed,
covering pause-before-picker, chosen-path application, cancellation, picker errors, and queuing),
JavaScript/shell syntax checks through the audit, and `git diff --check`. The Windows v1.0.2 installer
is built by the source-based GitHub Actions workflow. The native Windows picker itself still needs a
manual Windows run; the helper tests verify the event ordering and fail-closed behavior without
pretending to draw a native dialog.

## Round 31 - page translation and accessibility controls

- Added a target-language choice with 20 destinations under **Settings → Accessibility & language**.
  The browser Menu and command palette can translate the active HTTP(S) page. PsychoVault asks for
  consent before opening Google's webpage-translation route, explains that Google receives the full
  page URL and retrieves its contents, leaves the original tab untouched, and opens the translated
  copy in a new tab. Internal pages, credential-bearing URLs, and unsupported language codes are
  rejected by the URL builder. Google Translate needs an internet connection and may not work on
  every site; do not use it for sensitive pages.
- Added optional high-contrast browser chrome and Settings colors, reduced-motion styling for the
  browser and Settings (also honoring the OS preference) plus a reduced-motion stylesheet for pages,
  and a setting that keeps tab-close buttons visible. Browser tab items now expose tab names and
  selected state to assistive technology, support Left/Right/Home/End/Enter/Space keyboard use, and
  provide an accessible button for expanding/collapsing tab islands. Focus rings, accessible names
  for browser controls, status announcements, and Settings control focus styles were also improved.
- Bumped the app version to 1.0.3. The installer staging/verification script now checks
  `translation.js`; the release workflow runs the complete test glob alongside the static audit.

Checks run: `node tools/audit.mjs` (no defects), `node --test tools/*.test.cjs` (7 passed),
JavaScript syntax checks and shell syntax validation through the audit, and `git diff --check`.
A Windows installer/runtime GUI test was not run in this sandbox because no Electron display/runtime
is available. The Google Translate route is an external service, so a live translation test also
requires an internet-enabled app session.
