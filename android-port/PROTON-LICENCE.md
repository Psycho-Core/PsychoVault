# Proton VPN extension licence check

I checked Proton's upstream browser-extension repository at commit `71d7d86f164a2ee8e50ce5a389c9e5cbe0fd6883`, the source commit recorded for the desktop bundle. Its `LICENSE` is GNU GPL version 3, and its SHA-256 matches `app/builtin/proton-vpn/LICENSE` in this repository. Proton's build instructions cover Chrome and Firefox builds. The GPL permits modification and redistribution, including an Android adaptation, provided its conditions are met. The licence does not guarantee technical compatibility with Android.

For a distribution, I need to retain the required copyright and licence notices, provide the complete corresponding source for the shipped GPL-covered work, including any changes and the relevant build/install scripts as required by GPL-3.0, and meet the applicable requirements for installation information on user products. The precise obligations depend on how the extension and browser are combined and conveyed. Simply linking to an upstream commit does not automatically prove compliance for every release. Other bundled components can have additional licences. GPL-3.0 does not grant general rights to Proton's trademarks or override service terms. This is an engineering licence review, not a legal opinion or permission from Proton beyond the licence.

- Upstream licence at the pinned commit: https://github.com/ProtonVPN/proton-vpn-browser-extension/blob/71d7d86f164a2ee8e50ce5a389c9e5cbe0fd6883/LICENSE
- Upstream build instructions at that commit: https://github.com/ProtonVPN/proton-vpn-browser-extension/blob/71d7d86f164a2ee8e50ce5a389c9e5cbe0fd6883/readme.md
- Local licence: `app/builtin/proton-vpn/LICENSE`

This confirms the licence permits a compliant port in principle. It does not mean Proton works on Android yet, and no one has tested Proton sign-in, traffic, DNS or WebRTC on the current Android source. I must not describe the Android browser as VPN-protected until those checks pass.
