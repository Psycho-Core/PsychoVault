# My Windows test build

This is a development preview of my own browser. I am giving it to myself and other testers so I can find real problems early. It is not a finished browser and it is not a signed release.

## What I can test here

- I can open the native window, create and switch tabs, and use the address bar.
- I can load HTTP and HTTPS pages through my broker process while the page renderer runs in a Windows AppContainer.
- I can run my JavaScript, CSS, layout and PNG image code on simple pages.
- I can save bookmarks, history, search settings and my last tabs in my local profile.
- I can run `pv-tests.exe` and `pv-sandbox-test.exe` from the extracted folder.

## How I run it

1. I extract every file from the ZIP into its own folder.
2. I keep `PsychoVault.exe` and `pv-renderer.exe` together.
3. I double-click `PsychoVault.exe`.
4. If Windows SmartScreen warns me, I check the source and checksum before choosing to run an unsigned development build.

## How I check the build

I open PowerShell in the extracted folder and run:

```powershell
.\pv-tests.exe
.\pv-sandbox-test.exe
Get-FileHash .\PsychoVault.exe -Algorithm SHA256
```

I compare the hashes with `SHA256SUMS.txt`.

## What I am not claiming yet

I am not claiming full website compatibility, extension support, Proton VPN sign-in or routing, vault migration, Android support, accessibility completion, performance parity, signing or update safety. I will keep those as open work until I have real evidence for each one.

## My profile and my privacy

The normal profile is under `%LOCALAPPDATA%\PsychoVault\Owned`. A private window does not write browsing history or session tabs. I should not put secrets into a development preview, and I should back up anything I care about before testing profile changes.

## No warranty

I provide this preview as-is, without warranty. I am responsible for deciding where I run it and what I put into it.
