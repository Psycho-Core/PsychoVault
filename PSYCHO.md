# PSYCHO.md

This is my project guide for people and coding agents working on PsychoVault. Read it before making changes, then check the current code and tests - this file is a guide, not proof that a feature works.

`PSYCHO.md` is a custom filename. Tools that automatically load `CLAUDE.md` or `AGENTS.md` will not necessarily load this file. Ask the tool to read `PSYCHO.md`, or add an instruction in that tool's own configuration pointing to it.

## What I am building

PsychoVault is my desktop browser application, built with Electron and Chromium. The browser UI and features are in this repository; Electron supplies the rendering engine. The Windows x64 installer is the main release target. Do not describe Android or an APK as available unless it has actually been built and tested.

Keep changes focused on what I asked for. If a request has multiple parts, answer them in order. When I report a bug, use the installed version I give you, read the first error in a transcript, and do not assume a screenshot or generic notification identifies the cause.

## Where things live

- `app/main.js` - windows, tabs, sessions, settings, IPC, extensions and browser services.
- `app/preload.js` and `app/page-preload.js` - the separate, restricted IPC bridges for browser chrome and internal pages. Do not expose privileged APIs to ordinary websites.
- `app/ui/` - browser chrome, internal pages, settings and styles. Electron child views can cover the chrome; check window layout as well as CSS when fixing overlays.
- `app/auto-updater.js`, `app/update-*.js` and `app/update-install.ps1` - signed update checks, downloads, installer handoff, backup and cleanup.
- `installer.nsi` and `tools/gen-filelist.mjs` - NSIS installer and its generated file list.
- `PsychoVault_easy_build.bat` - my double-click Windows build entry point. `tools/build-installer.sh` is the CI/Linux installer path.
- `tools/*.test.cjs` - Node tests. `tools/*-electron-smoke.cjs` and the Windows PowerShell smoke tests exercise Electron and the installer.
- `app/builtin/` - bundled extensions and their provenance. Read its README and `THIRD-PARTY-NOTICES.md` before changing third-party code.
- `release-notes/<version>.txt` - version-specific changes. `VERIFICATION.md` records test evidence; do not treat older entries as proof of a new change.

## How I want changes made

- Prefer my existing features over adding a separate product. Improve the existing vault rather than adding Bitwarden or another password manager.
- Keep Proton VPN as the default bundled browser VPN. A desktop or system-wide VPN is not a substitute. Keep extension installation general, not Proton-only.
- Keep the ad blocker and Proton buttons above Settings, with the ad blocker above Proton. The control panel is called Performance.
- Preserve user profiles, workspaces and vault data. Think through migration, private windows and failure recovery before changing persistence, installation or updates.
- Keep the updater's signature and hash verification. Do not delete untracked installers or user files. Test silent install and rollback on Windows, and distinguish a simulated failure from a live update between released versions.
- Electron supports only part of the Chrome extension API. Do not claim an extension, VPN connection, leak protection or blocker works just because it loads or displays a status. Test the actual behaviour and report the limits. The uBlock Lite probe is informational and is not evidence of support.
- Do not commit credentials, signing keys, user profiles, logs, downloaded runtimes or generated build outputs. Never ask me to paste a token or password into chat. Review diagnostics for private paths, URLs and credentials before sharing them.
- Keep third-party licences, attribution, source provenance and local patches accurate when bundling or updating an extension.

## Build and check

From the repository root, the quick checks are:

```sh
node tools/audit.mjs
node --test tools/*.test.cjs
```

On Windows, I want the normal build to work by double-clicking `PsychoVault_easy_build.bat`. My checkout is at `D:\Blakes_SHIT\#Psycho-core_REPOS\PsychoVault`; do not tell me to move it. Keep release notes as a required build output, including when a build fails. Read `BUILD-WINDOWS.md` and the script for the current prerequisites and behaviour before changing the build.

For installer, updater, extension or UI changes, run the relevant integration tests as well as the quick checks when the environment permits. `.github/workflows/installer-check.yml` shows the Windows installer, update, Electron and Proton checks. A source test is not a substitute for checking a real Windows installer or visually checking a UI change. Say what passed, what failed, and what was not run.

The release workflow is `.github/workflows/build-release.yml`. The version comes from `app/package.json`; an existing release is not overwritten. Signed updates use an Ed25519 manifest and a bundled public key. A public repository does not need a read token to check releases. Never print or commit private signing material.

## Writing and reporting

Write docs and release notes in plain first-person language, like I wrote them. Use ` - ` rather than em dashes. Keep the new-tab picture near the top of the README and retain the notices and no-warranty information. Keep this file and other docs aligned with the actual code when behaviour changes.

Give me evidence, not an assertion. Link the PR and release if they exist; do not say a PR was merged, a release was published, or a test passed until you have checked. If something cannot be completed safely or tested, say so and explain what remains. Prefer free options when suggesting features or tools.
