# Bundled extensions

## Proton VPN: Fast & Secure 1.3.6 — `proton-vpn/`

This folder is a build of Proton AG's own browser extension, **not** a copy taken from the
Chrome Web Store. It is here because Proton publishes the extension under GPL-3.0, which
permits redistribution, and because a VPN that has to be downloaded from a store that
PsychoVault cannot install from is a VPN most users will never turn on.

| | |
| --- | --- |
| Upstream | https://github.com/ProtonVPN/proton-vpn-browser-extension |
| Pinned commit | `71d7d86f164a2ee8e50ce5a389c9e5cbe0fd6883` (2026-10-09) |
| Version | 1.3.6, Manifest V3 |
| Built with | `npm ci --legacy-peer-deps && npm run build`, Node 22, no source changes |
| Licence | GPL-3.0 — full text in `proton-vpn/LICENSE` |

The build is reproducible from that commit with the two commands above. `npm ci` needs
`--legacy-peer-deps` because the upstream lockfile has a `ts-jest`/`@babel/core` peer
conflict; nothing else about the build is changed.

PsychoVault loads this folder with Electron's `session.loadExtension()` after copying it into
the profile, exactly as it loads any other unpacked extension. **The source in the repository
and the licence travel with it.** If you want the exact sources that produced this build:

    git clone https://github.com/ProtonVPN/proton-vpn-browser-extension
    cd proton-vpn-browser-extension
    git checkout 71d7d86f164a2ee8e50ce5a389c9e5cbe0fd6883

That satisfies GPL-3.0's source requirement without PsychoVault having to mirror a copy that
could drift from upstream. Each release states this version and commit in `VERIFICATION.md`.

### What it does here, and what it is honest about

The extension sends browser traffic through Proton's proxy. It needs two Chrome APIs that
Electron does not implement — `chrome.proxy` and `chrome.privacy`. PsychoVault supplies both
itself (`app/ext-shim.js`), backed by `session.setProxy()` and Chromium's WebRTC policy
setter. **You still need your own Proton account**: no credentials, no subscription, and no
server list are bundled, and the extension talks to Proton's API directly the way it does in
Chrome.

Removing the extension is allowed and permanent — PsychoVault records the removal and does
not put it back on the next launch. **Settings → Extensions → Restore** brings it back.

### Not covered by PsychoVault's MIT licence

`proton-vpn/` is GPL-3.0 and is the property of Proton AG. PsychoVault's own source under
`app/` remains MIT. The two are aggregated, not linked: the extension is a separate program
that PsychoVault loads at runtime, and PsychoVault works without it.
