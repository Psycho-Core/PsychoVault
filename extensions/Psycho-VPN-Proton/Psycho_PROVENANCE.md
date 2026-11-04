# Proton VPN extension - provenance

This folder contains the compiled payload of Proton VPN's browser extension. It
is a separate program under the GNU General Public License v3 (see LICENSE,
their license, shipped verbatim). My browser loads it as an extension at run
time; licenses stay on their own sides: nothing of mine becomes GPL, none of
their code is mixed into my sources.

- Upstream: https://github.com/ProtonVPN/proton-vpn-browser-extension
- Pinned commit: 71d7d86f164a2ee8e50ce5a389c9e5cbe0fd6883
- Payload version: 1.3.6
- Extension identity (from the manifest key): jplgfhpmjnbigmhklmmbgecoobifkmpa
- Build recipe: `npm ci --legacy-peer-deps` then `npm run build`
  (their own webpack production config; the `--legacy-peer-deps` flag only
  resolves a conflict inside their test-only dependency `ts-jest` and does not
  alter the payload).
- Build host here: v22.22.3
  with npm ci against registry.npmjs.org.
- Modifications by me: none to the payload. The manifest key is theirs, so the
  extension answers to account.proton.me exactly as the store build does.

To refresh the payload against a newer upstream commit, edit the pin in
tools/Psycho_vendor_proton.py and run it:

    python tools/Psycho_vendor_proton.py
