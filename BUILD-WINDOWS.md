# Build PsychoVault on Windows — the easy way

**Nothing in this guide costs money.** No account, no subscription, no GitHub Actions minutes.
It all runs on your own PC. Budget about 10 minutes and ~1.5 GB of free disk space.

There are three levels. Pick the one you want — you can always do more later.

| Level | What you get | Time | Needs |
|---|---|---|---|
| **A. Just run it** | The real browser, straight from the source folder | 3 min | Node.js |
| **B. Portable exe** | `build\payload\PsychoVault.exe` — a rebranded, double-clickable browser | 5 min | Node.js |
| **C. Full installer** | `PsychoVault-Setup-1.0.12.exe` — shortcuts, uninstall entry | 8 min | Node.js + NSIS |

The script does levels A→C automatically. If NSIS is missing it simply stops after level B and
tells you.

---

## Step 0 — the only two downloads you need

| What | Required? | Direct link | Alternative (one command in Terminal) |
|---|---|---|---|
| **Node.js LTS** (runs everything) | **Yes** | <https://nodejs.org/en/download> — pick **Windows Installer (.msi)**, 64-bit | `winget install OpenJS.NodeJS.LTS` |
| **NSIS 3** (makes the Setup .exe) | Only for level C | <https://nsis.sourceforge.io/Download> — the big **Download NSIS** button | `winget install NSIS.NSIS` |

Optional, only if you'd rather clone than download a ZIP:

| What | Direct link |
|---|---|
| Git for Windows | <https://git-scm.com/download/win> |
| GitHub CLI (handles the private-repo login for you) | <https://cli.github.com/> → `winget install GitHub.cli` |
| 7-Zip (for looking inside a built .exe) | <https://7-zip.org/download.html> |

Electron 45.1.0 is **not** something you download by hand — the build fetches it automatically
(~120 MB, once). Its own page, if you ever want it: <https://github.com/electron/electron/releases/tag/v45.1.0>.

---

## Step 1 — install Node.js

Run the `.msi`, click Next through all of it. Leave the "Tools for Native Modules" checkbox
**unticked** — this build does not need it.

Verify: press `Win+R`, type `cmd`, Enter, then type:

```
node -v
```

You should see something like `v24.x.x`. If it says *"not recognized"*, close the window, open a
new one (Windows only picks up new PATH entries in new windows), or reboot.

---

## Step 2 — get the code onto your PC

**Easiest (no Git):** while signed in to GitHub, open

<https://github.com/Psycho-Core/PsychoVault> → green **Code** button → **Download ZIP**

then right-click the downloaded `PsychoVault-Master.zip` → **Extract All** → extract to a short
path such as `C:\PsychoVault` (avoid Desktop/OneDrive: long paths and OneDrive sync both cause
trouble).

Direct ZIP link, if you are already signed in:
<https://github.com/Psycho-Core/PsychoVault/archive/refs/heads/Master.zip>

**Or with Git:**

```
gh auth login
gh repo clone Psycho-Core/PsychoVault C:\PsychoVault
```

You should end up with a folder that contains `PsychoVault_easy_build.bat`, `installer.nsi`,
`app\`, `tools\` and `assets\`.

---

## Step 3 — run the build script

In that folder, **double-click `PsychoVault_easy_build.bat`**.

(If Windows asks how to open it, choose Command Prompt. If you prefer a terminal:
`cd /d C:\PsychoVault` then `PsychoVault_easy_build.bat`.)

It prints six numbered steps and takes 3–6 minutes the first time, mostly downloading Electron.
What it is doing:

1. finds Node.js and reads the version from `app\package.json`
2. `npm install` in `app\` → downloads the Electron 45 Windows runtime
3. copies that runtime to `build\payload\` and renames `electron.exe` → `PsychoVault.exe`
4. `npm ci` in `tools\` → downloads `resedit`, which stamps your icon and version onto the exe
5. copies the browser source into `build\payload\resources\app` (excluding `node_modules`)
6. if NSIS is installed, builds `PsychoVault-Setup-1.0.12.exe` in the repo root

When it finishes you get:

```
C:\PsychoVault\build\payload\PsychoVault.exe     <- just run this
C:\PsychoVault\PsychoVault-Setup-1.0.12.exe      <- only if NSIS was installed
```

The script offers to start the browser for you at the end.

### What Windows will say

- **"Windows protected your PC" / SmartScreen** — expected for any unsigned executable. Click
  **More info → Run anyway**. This cannot be fixed without buying a code-signing certificate, and
  it is the same warning the official build would get.
- Some antivirus products quarantine a freshly built unsigned exe. Restore it from quarantine and
  add an exclusion for your build folder if you trust your own build (you should — you just made it).

### Your data

Bookmarks, history, settings and `vault.bin` live in `%APPDATA%\PsychoVault`, never in the install
folder, so rebuilding and reinstalling never touches them.

---

## Level A only: run without building anything

If you just want the browser running and don't care about an exe, after Step 1 and Step 2:

```
cd /d C:\PsychoVault\app
npm install
npm start
```

That is the real browser — tabs, ad blocking, extension manager, proxy shim all behave exactly as
in the installer. Two differences: automatic updates are off (the updater only runs in a packaged
Windows build) and your profile lives in `%APPDATA%\Electron` instead of `%APPDATA%\PsychoVault`.

This is also the fastest way to check something like **Settings → Extensions → Test proxy support**
without rebuilding.

---

## Making the update banner work in your own build

The in-app updater reads releases from a **private** repository, so it needs a read-only token.
The official build bakes one in from a GitHub secret; for a local build you supply your own.

1. Open <https://github.com/settings/tokens?type=beta> → **Generate new token**
2. Name: `psychovault-updater-local`. Expiration: as long as you like.
3. Repository access: **Only select repositories** → `Psycho-Core/PsychoVault`
4. Permissions → Repository permissions → **Contents: Read-only**. Nothing else.
5. Generate, copy the `github_pat_...` value.
6. In `C:\PsychoVault\app\`, create a file named `update-token.json` containing exactly:

```json
{"token":"github_pat_PASTE_YOURS_HERE"}
```

It is already git-ignored, so it can never be committed. Re-run the build script and the banner
can then see new versions.

If you skip this, the browser itself works fine — only the update check is unavailable.

---

## Troubleshooting

| Symptom | Fix |
|---|---|
| `node` is not recognized | New terminal window, or reboot so PATH refreshes |
| `npm install` hangs or `ECONNRESET` | Corporate proxy/antivirus. Try `npm install --registry=https://registry.npmjs.org/` or turn off the AV web shield for 10 minutes |
| `no Electron runtime at ...\node_modules\electron\dist` | `cd app` then `npm install --force` |
| Build fails while copying | PsychoVault is running and holding the exe — close it, delete `build\`, run again |
| `makensis` not found | Optional. Install NSIS (<https://nsis.sourceforge.io/Download>), reopen the terminal, re-run |
| Setup says *"Can't write ...PsychoVault.exe"* | The browser is open. Setup now offers to close it — accept |
| Long-path / `ENAMETOOLONG` errors | Move the folder to `C:\PsychoVault` |
| Everything fails and you want to start clean | Delete `app\node_modules`, `tools\node_modules`, `build\` and re-run |

---

## For later: rebuilding after a code change

Once the steps above have run once, a rebuild is fast — it reuses the Electron runtime:

```
PsychoVault_easy_build.bat
```

It re-stages `app\` and re-runs NSIS in well under a minute. Bump `version` in `app\package.json`
first if you want the installer to look like a new version.

The Linux/CI equivalent (`tools/build-installer.sh`) still exists and is what the GitHub workflow
runs; the `.bat` does the same work using Windows tools.
