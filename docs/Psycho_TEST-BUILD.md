# My Windows test build

This is a development preview of my own browser. I am publishing it so I can test it on my own computer and find real problems. It is not a finished browser and it is not signed.

## How I run it

1. I extract **every** file from `PsychoVault-windows-x64.zip` into its own folder.
2. I keep `PsychoVault.exe` and `pv-renderer.exe` together. My browser is multi-process, so the bare `PsychoVault.exe` on its own will not render pages.
3. I double-click `PsychoVault.exe`.
4. If SmartScreen warns me, I check the hash below before I choose to run an unsigned build.

## How I check the build

I open PowerShell in the extracted folder and run:

```powershell
.\pv-tests.exe
.\PsychoVault.exe --sandbox-test --sandbox-report sandbox-test.json
Get-Content .\sandbox-test.json
Get-FileHash .\PsychoVault.exe -Algorithm SHA256
```

I compare the hashes with `SHA256SUMS.txt`. `pv-tests.exe` runs my engine, proxy, extension, download and profile checks. The sandbox command reports whether my renderer really is contained.

## What works in this build

- **Tabs.** Each tab keeps its own sandboxed renderer, so switching tabs does not reload the page. I have stop, reload, back, forward and active-tab restore.
- **Pages.** I fetch through my broker and render with my own HTML, CSS, JavaScript and layout code. Valid PNG images decode and paint.
- **Downloads.** I stream to disk with progress, cancel, a safe filename and a persistent list. `Ctrl+J` opens my downloads.
- **Settings.** All ten categories are present and the controls I show change real persisted behaviour - search, home page, ad blocking, Do Not Track, referrer stripping, download folder, accent, tab close buttons and more.
- **Extensions.** I load unpacked extensions with manifest validation and default-deny permissions. A denied API raises a catchable error instead of silently working.
- **VPN transport.** My bundled VPN bridge drives my real proxy code. I support fixed proxies and PAC scripts evaluated in my own interpreter, with fail-closed routing.
- **Containment.** My renderer runs in a Windows AppContainer on a private desktop. My required gate proves the token is active, a broker-owned file is unreadable and direct TCP/UDP sockets are refused.

## What does not work, and I am not pretending otherwise

- **YouTube and Netflix do not play.** I have no media pipeline, no Media Source Extensions and no DRM. Netflix specifically needs a licensed Widevine module, which I will only ever load as a separate extension - never inside my engine.
- **Proton VPN is not bundled.** The included "PsychoVault VPN Bridge" is my own code, not Proton. It needs a proxy I point it at. I have not verified any Proton sign-in or routing.
- **My VPN bridge only tunnels HTTP proxies.** I refuse `https` and `socks5` proxies rather than silently connecting directly, because I cannot tunnel those yet. Proton uses TLS-to-proxy, so this is a real gap.
- **Most modern sites will not work.** My JavaScript engine has no classes, modules, promises, async/await or regular expressions.
- **My vault and password manager are not implemented.** I show an honest status instead of a password field that stores nothing.
- **No WebRTC, media, canvas, workers or IndexedDB.** My WebRTC setting records policy; it is not a leak test.
- **Unsigned.** No installer, no code signing, and my updater verifies hashes but I have not tested a full install-and-rollback cycle.

## My profile and my privacy

My normal profile lives in `%LOCALAPPDATA%\PsychoVault\Owned`. A private window does not write history or session tabs. I keep a damaged settings file as a `.corrupt-*` backup instead of overwriting it. I should still back up anything I care about before testing.

## No warranty

I provide this preview as-is, without warranty. I am responsible for deciding where I run it and what I put into it.
