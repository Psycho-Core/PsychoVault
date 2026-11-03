# My verification record

I separate what passed from what failed, what is blocked and what I have not run.

## Passed locally

- My GCC build of the engine, broker, renderer and tests.
- My engine test suite: **214 checks passed**. This covers URL/JSON/UTF-8, DOM, CSS, my JavaScript interpreter, PNG decode, layout, proxy routing, PAC evaluation, extension permissions, download naming, profile persistence and my bundled VPN bridge driven end to end.
- My source-ownership and boundary audit: **105 passed, 0 failed**. This now includes release-pipeline gates.
- My file-naming audit: **18 passed, 0 failed**.
- My plan audit: **377 passed, 0 failed**.
- Windows cross-compilation of every new module with Clang/MinGW targeting `x86_64-windows-gnu`, which catches MSVC-class errors before I spend a hosted build.

## Real bugs my own tests found and I fixed

- **My expression parser corrupted state.** I classified every operator ending in `=` as a compound assignment, so `===`, `==`, `!=`, `<=` and `>=` overwrote the value they were comparing. My VPN bridge exposed it: a `configure` message turned into a boolean. I now classify only real compound assignments and added 24 regression checks. This would have broken most real websites.
- **My Chrome-style callback dispatch invoked a details object as a function.** `proxy.settings.clear({})` would throw. I now pick the last function argument.
- **A denied extension API killed the whole worker.** I threw a C++ exception that escaped the interpreter. I now raise a catchable JS `Error`.
- **My event dispatch was blind to `JsError`** because it does not derive from `std::exception`. I now catch it and surface it in the UI.
- **My download filename sanitiser left `..` runs.** Harmless once separators were gone, but I collapse them now.
- Four stale references after my rename, two of which would have made CI fetch a 404 fixture.

## Verified on hosted Windows in earlier runs

- MSVC build, engine tests, headless PNG render, native home capture and a fetched page with CSS/JS/PNG through the sandboxed renderer.
- My required AppContainer gate: token active, broker-owned file unreadable, direct TCP **and** UDP sockets refused.

## Not run yet for this version

- The hosted MSVC build and full gate set for this exact commit. I run these before I publish.
- Address/undefined-behaviour sanitizers over my new proxy, extension and download code.
- A real download over the network on Windows, and a real proxy connection to a live proxy server.
- A full updater install-and-rollback cycle.

## Blocked or not implemented, and I do not claim otherwise

- **YouTube and Netflix.** No media pipeline, no Media Source Extensions, no DRM. Netflix needs a licensed Widevine module, which can only ever be a separate extension.
- **Proton VPN.** Not bundled and not verified. My own VPN bridge is bundled instead and is clearly labelled as mine.
- **TLS-to-proxy and SOCKS5.** I refuse these rather than leak a direct connection. Proton uses TLS-to-proxy, so this is a real gap.
- Modern JavaScript: no classes, modules, promises, async/await or regular expressions.
- Vault, passwords, translation, WebRTC, media, canvas, workers, IndexedDB.
- Full legacy UI parity, visual regression, accessibility audit, performance comparison, Android, signing and installer.

## Where I look for evidence

- Development workflow: https://github.com/Psycho-Core/PsychoVault/actions/workflows/Psycho_owned-engine.yml
- Release workflow: https://github.com/Psycho-Core/PsychoVault/actions/workflows/Psycho_release.yml
- Releases and update manifest: https://github.com/Psycho-Core/PsychoVault/releases
