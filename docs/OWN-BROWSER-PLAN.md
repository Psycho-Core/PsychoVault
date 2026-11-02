# My own-browser plan

I want PsychoVault to be my own browser, not another browser with my name on it. That means my own browser shell, profile model, extension engine, vault, settings, privacy controls and update path. Chromium may remain the open-source rendering/network component, and Proton's extension remains Proton's GPL code, but neither should define PsychoVault's product boundary.

## Non-negotiable gates

1. **Ownership:** no Titanium/Vanadium browser shell or GeckoView/Firefox extension host as the product foundation. Third-party components are named, licensed and replaceable.
2. **Proton:** the real Proton extension must sign in, configure a proxy, authenticate to the exact proxy endpoint, route HTTP/HTTPS traffic, fail closed on proxy failure, and pass DNS/WebRTC checks before I call the browser VPN-protected.
3. **Security:** extension code runs isolated from browser chrome and websites; no Node.js in extension/web contexts; default-deny permissions; signed/verified updates; no remote extension code; vault encryption and profile isolation preserved.
4. **Privacy:** no PsychoVault telemetry by default; private browsing does not persist extension/profile data; proxy and WebRTC state is honest and testable.
5. **Performance:** measure cold start, warm start, idle RAM, 10-tab RAM, tab sleep/wake, page load and extension overhead on the same machine and pages. Do not claim "fast" or "low RAM" from architecture alone.
6. **Migration:** keep the working Windows app until the replacement passes feature, data-migration, installer, updater and Proton gates. Never delete profiles, workspaces or vault data.
7. **Build:** GitHub Actions hosted builds only, one build at a time, with release notes on success and failure.

## Slice order

1. Extension-engine policy core: manifest, permissions, dispatcher. **Started.**
2. Isolated extension host: worker/popup/options/content contexts, package identity, install/remove/enable, persistent profile-scoped storage.
3. Proton path: proxy controller, exact-endpoint auth, fail-closed routing, WebRTC policy, real sign-in and leak tests.
4. Browser shell replacement: tabs, navigation, downloads, settings, vault, workspaces, blocker and Performance panel on the owned host.
5. Performance/security hardening: measured RAM/startup budgets, tab discarding, process reuse, sandbox review, fuzzing and update/rollback tests.
6. Android: reuse the owned extension engine and browser services on a Chromium content embedding, not another browser's shell.

## Current evidence

The first slice adds source and unit tests only. No Windows integration, Android compile, device test, Proton connection or leak test has passed for this new engine.
