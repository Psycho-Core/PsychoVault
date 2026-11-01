# Build PsychoVault on Windows - the easy way

**Nothing in this guide costs money.** No account, no subscription, no GitHub Actions minutes.
It all runs on your own PC. Budget about 10 minutes and ~1.5 GB of free disk space.

There are three levels. Pick the one you want - you can always do more later.

| Level | What you get | Time | Needs |
|---|---|---|---|
| **A. Just run it** | The real browser, straight from the source folder | 3 min | Node.js |
| **B. Portable exe** | `build\payload\PsychoVault.exe` - a rebranded, double-clickable browser | 5 min | Node.js |
| **C. Full installer** | `PsychoVault-Setup-<version>.exe` - shortcuts, uninstall entry | 8 min | Node.js + NSIS |

The script does levels A→C automatically. If NSIS is missing it simply stops after level B and
tells you.

---

## Step 0 - the only two downloads you need

| What | Required? | Direct link | Alternative (one command in Terminal) |
|---|---|---|---|
| **Node.js LTS** (runs everything) | **Yes** | <https://nodejs.org/en/download> - pick **Windows Installer (.msi)**, 64-bit | `winget install OpenJS.NodeJS.LTS` |
| **NSIS 3** (makes the Setup .exe) | Only for level C | <https://nsis.sourceforge.io/Download> - the big **Download NSIS** button | `winget install NSIS.NSIS` |

Optional, only if you'd rather clone than download a ZIP:

| What | Direct link |
|---|---|
| Git for Windows | <https://git-scm.com/download/win> |
| GitHub CLI (needed for GitHub release operations) | <https://cli.github.com/> → `winget install GitHub.cli` |
| 7-Zip (for looking inside a built .exe) | <https://7-zip.org/download.html> |

Electron 45.1.0 is **not** something you download by hand - the build fetches it automatically
(~120 MB, once). Its own page, if you ever want it: <https://github.com/electron/electron/releases/tag/v45.1.0>.

---

## Step 1 - install Node.js

Run the `.msi`, click Next through all of it. Leave the "Tools for Native Modules" checkbox
**unticked** - this build does not need it.

Verify: press `Win+R`, type `cmd`, Enter, then type:

```
node -v
```

You should see something like `v24.x.x`. If it says *"not recognized"*, close the window, open a
new one (Windows only picks up new PATH entries in new windows), or reboot.

---

## Step 2 - get the code onto your PC

**Easiest (no Git):** while signed in to GitHub, open

<https://github.com/Psycho-Core/PsychoVault> → green **Code** button → **Download ZIP**

then right-click the downloaded `PsychoVault-Master.zip` → **Extract All** → extract it to
`D:\Blakes_SHIT\#Psycho-core_REPOS\PsychoVault` (avoid Desktop and OneDrive: long paths and
OneDrive sync both cause trouble).

Direct ZIP link, if you are already signed in:
<https://github.com/Psycho-Core/PsychoVault/archive/refs/heads/Master.zip>

**Or with Git:**

```
gh auth login
gh repo clone Psycho-Core/PsychoVault "D:\Blakes_SHIT\#Psycho-core_REPOS\PsychoVault"
```

You should end up with a folder that contains `PsychoVault_easy_build.bat`, `installer.nsi`,
`app\`, `tools\` and `assets\`.

---

## Step 3 - run the build script

In that folder, **double-click `PsychoVault_easy_build.bat`**.

(If Windows asks how to open it, choose Command Prompt. If you prefer a terminal:
`cd /d "D:\Blakes_SHIT\#Psycho-core_REPOS\PsychoVault"` then `PsychoVault_easy_build.bat`.)

It prints nine numbered steps and takes 3–6 minutes the first time, mostly downloading
Electron. What it is doing:

1. finds Node.js and reads the three-part version from `app\package.json`
2. `npm install` in `app\` → downloads the Electron 45 Windows runtime
3. copies that runtime to `build\payload\` and renames `electron.exe` → `PsychoVault.exe`
4. `npm ci` in `tools\` → downloads `resedit`, which stamps your icon and version onto the exe
5. copies the browser source into `build\payload\resources\app`, skipping `node_modules` and
   the unused 10 MB `app\tools\wireproxy.exe`
6. checks the payload before packaging anything: every file the browser needs is present and
   non-empty, and the old `default_app.asar` is gone. If that check fails the build stops there
7. if NSIS is installed, builds `PsychoVault-Setup-<version>.exe` in the repo root
8. writes `Release-Notes-<version>.txt` next to the script
9. asks whether to publish the build as a GitHub release, then uploads the exe, its SHA-256 and
   the notes if you say yes

When it finishes you get:

```
build\payload\PsychoVault.exe            <- just run this
PsychoVault-Setup-<version>.exe             <- only if NSIS was installed
Release-Notes-<version>.txt                 <- what changed, and the SHA-256 of both
```

The release notes list the version, the Electron runtime, the SHA-256 of the portable exe and of
the installer (when one was built), the last ten commit subjects if you cloned with Git, how to
run each build, and the unsigned-build warning. If you extracted a ZIP instead of cloning, it says
so instead of inventing a change list.

The script offers to start the browser for you at the end.

### Runtime is incomplete

`app\node_modules\electron\dist` must contain a whole Electron runtime, not just
`electron.exe`. A complete win32-x64 tree has ~17 files plus `locales\`, `resources\` and
`version`:

```
electron.exe  chrome_100_percent.pak  chrome_200_percent.pak  d3dcompiler_47.dll
dxcompiler.dll  electron_wer.dll  ffmpeg.dll  icudtl.dat  resources.pak
snapshot_blob.bin  v8_context_snapshot.bin  vk_swiftshader.dll
vk_swiftshader_icd.json  vulkan-1.dll  version  LICENSE  LICENSES.chromium.html
locales\  resources\
```

If only the exe is there, unpacking stopped early - usually antivirus quarantining the
libraries mid-extract. Skip npm entirely and take the archive directly:

```
cd /d "D:\Blakes_SHIT\#Psycho-core_REPOS\PsychoVault"
curl -L -o "%TEMP%\electron45.zip" https://github.com/electron/electron/releases/download/v45.1.0/electron-v45.1.0-win32-x64.zip
certutil -hashfile "%TEMP%\electron45.zip" SHA256
```

The middle line must be `55eaba17abff7b5bba6c3bbc85068967379024fe742fe9c7fdba9118922ccbe2` - the same
value CI pins. Then:

```
rmdir /s /q "app\node_modules\electron\dist"
mkdir "app\node_modules\electron\dist"
tar -xf "%TEMP%\electron45.zip" -C "app\node_modules\electron\dist"
```

`dir "app\node_modules\electron\dist"` should now show 17 files and 3 folders. If they vanish
again on a second `dir`, antivirus is removing them: restore from **Windows Security -> Virus &
threat protection -> Protection history**, then from an administrator PowerShell:

```
Add-MpPreference -ExclusionPath "D:\Blakes_SHIT\#Psycho-core_REPOS\PsychoVault"
```

### What Windows will say

- **"Windows protected your PC" / SmartScreen** - expected for any unsigned executable. Click
  **More info → Run anyway**. This cannot be fixed without buying a code-signing certificate, and
  it is the same warning the official build would get.
- Some antivirus products quarantine a freshly built unsigned exe. Restore it from quarantine and
  add an exclusion for your build folder if you trust your own build (you should - you just made it).

### Your data

Bookmarks, history, settings and `vault.bin` live in `%APPDATA%\PsychoVault`, never in the install
folder, so rebuilding and reinstalling never touches them.

---

## Level A only: run without building anything

If you just want the browser running and don't care about an exe, after Step 1 and Step 2:

```
cd /d "D:\Blakes_SHIT\#Psycho-core_REPOS\PsychoVault\app"
npm install
npm start
```

That is the real browser - tabs, ad blocking, extension manager, proxy shim all behave exactly as
in the installer. Two differences: automatic updates are off (the updater only runs in a packaged
Windows build) and your profile lives in `%APPDATA%\Electron` instead of `%APPDATA%\PsychoVault`.

This is also the fastest way to check something like **Settings → Extensions → Test proxy support**
without rebuilding.

---

## Making the update banner work in your own build

The in-app updater reads releases over the ordinary public GitHub API, so on this repository it
needs no read token - install a build and use **Settings - About - Check now**. Updates still need a published release with a valid signed manifest. You only need what follows if you point the updater at a **private** fork or keep
the repository private again; then it needs a read-only token, and a local build has to supply
its own.

1. Open <https://github.com/settings/tokens?type=beta> → **Generate new token**
2. Name: `psychovault-updater-local`. Expiration: as long as you like.
3. Repository access: **Only select repositories** → `Psycho-Core/PsychoVault`
4. Permissions → Repository permissions → **Contents: Read-only**. Nothing else.
5. Generate, copy the `github_pat_...` value.
6. In `app\` (that is `D:\Blakes_SHIT\#Psycho-core_REPOS\PsychoVault\app\`), create a file
   named `update-token.json` containing exactly:

```json
{"token":"github_pat_PASTE_YOURS_HERE"}
```

It is git-ignored, but can still be force-added. Never commit it. A token in app/ is included in the installer and can be extracted by anyone receiving it. Re-run the build script and the banner
can then see new versions.

The file is `app\update-token.json` - one line, inside the `app` folder, named exactly that.
Not `update-token.json.txt`: Explorer hides the extension, so turn on "File name extensions"
in View if you are unsure.

If you skip this on a public repository, nothing is missing: the check already works.

---

## Publishing the build as a release

Step 9 of `PsychoVault_easy_build.bat` asks whether to post the build to GitHub. Answer `y` and it
creates (or updates) release `v<version>` with:

- the installer, `PsychoVault-Setup-<version>.exe`
- `update-manifest.json` and `update-manifest.sig`, if a signing key is present
- the release notes from `release-notes/<version>.txt`, plus the size and SHA-256 of what it built

It needs a token with **Contents: read and write** on this repository - the read-only token in
`app\update-token.json` is not enough, and must not be reused. Put the write token in
`publish-token.json` at the repo root:

```json
{"token":"github_pat_PASTE_YOURS_HERE"}
```

That file is git-ignored. Keep it out of `app\`, because everything in `app\` is copied into the
build and shipped to users. `GITHUB_TOKEN` in the environment works too, and takes priority.

It is a file of exactly one line, in the repo folder next to `PsychoVault_easy_build.bat`:

```
D:\Blakes_SHIT\#Psycho-core_REPOS\PsychoVault\publish-token.json
```

`publish-token.example.json` is in the repo as a copy of that shape. From a command prompt:

```
cd /d "D:\Blakes_SHIT\#Psycho-core_REPOS\PsychoVault"
copy publish-token.example.json publish-token.json
notepad publish-token.json
```

then replace the placeholder between the quotes with your `github_pat_...` value and save. There is
no line to edit in any script: the build reads that file, and says so when the token is missing.

Publishing is never automatic: it asks every time, `N` is the default answer, and a failed publish
never fails a build that already produced an installer. To skip the question, set `PV_PUBLISH=y`
or `PV_PUBLISH=n` before running the script.

### Signing, so the in-app updater accepts the release

The updater only trusts a release whose `update-manifest.json` is signed with the key matching
`app\update-public-key.pem`. Put the private key next to the script as `update-private-key.pem`
(it is git-ignored), and step 9 signs the manifest before uploading. Without it the release is
still published, but the banner will not offer it - the release body says which of the two
happened.

### Checking the token before a build

```
node tools\publish-release.mjs --check
```

contacts GitHub and prints which token was found, whether it can see the repository, and
whether it is allowed to **write**. Publishing always needs a token, public repository or not. A token that can only read behaves exactly like a missing one
at publish time, so this is worth ten seconds before a twenty-minute build:

```
Token source: publish-token.json (fine-grained token, 93 characters)
Repository:  Psycho-Core/PsychoVault (public)
Read access: yes
Write access: yes - publishing will work
Releases:    readable
```

The token's value is never printed, not even part of it.

### Checking the release before it goes public

```
node tools\publish-release.mjs --dry-run
```

prints the exact release body and the assets it would upload, without contacting GitHub. Add
`--draft` to create a draft instead of a published release.

---

## Troubleshooting

| Symptom | Fix |
|---|---|
| `node` is not recognized | New terminal window, or reboot so PATH refreshes |
| `npm install` hangs or `ECONNRESET` | Corporate proxy/antivirus. Try `npm install --registry=https://registry.npmjs.org/` or turn off the AV web shield for 10 minutes |
| `npm finished but there is no Electron runtime` / `up to date in 634ms` | The package folder exists but its download never finished, so npm thinks it is installed. Run the downloader itself: `cd app` then `node node_modules\electron\install.js`. The build script now does this automatically, then `npm rebuild electron`, then removes the folder and reinstalls |
| `install.js` prints nothing and returns | That is what success looks like: it only prints on failure. Check for the file: `dir app\node_modules\electron\dist\electron.exe` |
| `dist\version` exists but `electron.exe` does not | The runtime was unpacked and the exe then deleted - in practice antivirus quarantine. Re-extract with `rmdir /s /q app\node_modules\electron\dist` then `del app\node_modules\electron\path.txt` then `node node_modules\electron\install.js` from `app`. If the exe disappears again, restore it from Windows Security → Virus & threat protection → Protection history, or run `Add-MpPreference -ExclusionPath "D:\Blakes_SHIT\#Psycho-core_REPOS\PsychoVault"` from an administrator PowerShell |
| The downloader fails too | Almost always `npm config get ignore-scripts` printing `true` (scripts that fetch the runtime are skipped), or the network blocking GitHub's release servers. Check with `curl -I https://github.com/electron/electron/releases/download/v45.1.0/electron-v45.1.0-win32-x64.zip` - it should print `HTTP/2 302`. If it does not: `set ELECTRON_MIRROR=https://npmmirror.com/mirrors/electron/` then `node node_modules\electron\install.js` |
| Build fails while copying | PsychoVault is running and holding the exe - close it, delete `build\`, run again |
| `makensis` not found | Optional. Install NSIS (<https://nsis.sourceforge.io/Download>), reopen the terminal, re-run |
| Setup says *"Can't write ...PsychoVault.exe"* | The browser is open. Setup now offers to close it - accept |
| Long-path / `ENAMETOOLONG` errors | The build stops at step 6 with the file it could not copy. Keep the repo out of Desktop and OneDrive, and enable long paths: `reg add HKLM\SYSTEM\CurrentControlSet\Control\FileSystem /v LongPathsEnabled /t REG_DWORD /d 1 /f` from an administrator prompt |
| Everything fails and you want to start clean | Delete `app\node_modules`, `tools\node_modules`, `build\` and re-run |

---

## For later: rebuilding after a code change

Once the steps above have run once, a rebuild is fast - it reuses the Electron runtime:

```
PsychoVault_easy_build.bat
```

It re-stages `app\` and re-runs NSIS in well under a minute. Bump `version` in `app\package.json`
first if you want the installer to look like a new version.

The Linux/CI equivalent (`tools/build-installer.sh`) still exists and is what the GitHub workflow
runs; the `.bat` does the same work using Windows tools.
