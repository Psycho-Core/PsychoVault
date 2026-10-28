# tools/

`wireproxy.exe` is a legacy artifact from an earlier VPN prototype. The current PsychoVault
build does **not** launch it and does not provide a built-in WireGuard tunnel. Its presence
in this folder does not mean that a VPN is connected or that browser traffic is protected.

To use Proton VPN with this Electron-based browser, install and connect Proton VPN's official
desktop application. The system-wide tunnel then covers PsychoVault as it does other apps.
The Proton VPN browser extension can be installed for inspection, but Electron does not
implement `chrome.proxy`, which that extension needs to route browser traffic. The Extensions
category in Settings explains this and links to Proton's official desktop download page.

The binary is retained here for provenance only. It is wireproxy v1.1.3 (`windtf/wireproxy`,
ISC); the upstream Windows archive digest and executable digest are recorded in the root
`THIRD-PARTY-NOTICES.md`. PsychoVault does not execute this binary in the current build.
