# My verification record

I separate what passed from what failed, what is blocked and what I have not run.

## Passed locally

- My GCC build of the engine, my page host and my test suite, and a full Clang/MinGW cross-build of the Windows browser executable.
- My engine test suite: **312 checks passed**. This covers URL/JSON/UTF-8, DOM, CSS, my JavaScript interpreter, PNG decode, layout, proxy routing, PAC evaluation, extension permissions, download naming, profile persistence, my bundled VPN bridge, and my page host loading a page, fetching its subresources, decoding an image and reporting frames.
- My source-ownership and boundary audit: **121 passed, 0 failed**. This now includes the single-executable promise: no second executable, no resource compiler, no helper process.
- My file-naming audit: **18 passed, 0 failed**.
- My plan audit: **377 passed, 0 failed**.
- Windows cross-compilation of every module with Clang/MinGW targeting `x86_64-windows-gnu`, including a full link of `PsychoVault.exe`, which catches MSVC-class errors before I spend a hosted build.

## Real bugs my own tests found and I fixed

- **My expression parser corrupted state.** I classified every operator ending in `=` as a compound assignment, so `===`, `==`, `!=`, `<=` and `>=` overwrote the value they were comparing. My VPN bridge exposed it: a `configure` message turned into a boolean. I now classify only real compound assignments and added 24 regression checks. This would have broken most real websites.
- **My Chrome-style callback dispatch invoked a details object as a function.** `proxy.settings.clear({})` would throw. I now pick the last function argument.
- **A denied extension API killed the whole worker.** I threw a C++ exception that escaped the interpreter. I now raise a catchable JS `Error`.
- **My event dispatch was blind to `JsError`** because it does not derive from `std::exception`. I now catch it and surface it in the UI.
- **Three of my required gates never ran.** My shell silently ignored `--expect-image`, `--sandbox-test` and `--sandbox-report`, so those checks proved nothing. My shell now parses them and refuses any option it does not understand.
- **My separate renderer could not start inside its own sandbox.** It unpacked correctly and the loader reported the right DLLs, but the contained process never answered. Rather than keep fighting it, I removed the split: my engine now renders inside my one executable, which is what I actually asked for.
- **My download filename sanitiser left `..` runs.** Harmless once separators were gone, but I collapse them now.
- Four stale references after my rename, two of which would have made CI fetch a 404 fixture.

## Verified on hosted Windows, earlier runs

- MSVC build, engine tests, headless PNG render, native home capture and a fetched page with CSS/JS/PNG.

## Not run yet for this version

- The hosted MSVC build and full gate set for this exact commit. I run these before I publish, and the single-file gate now measures the one-executable promise directly.
- Address/undefined-behaviour sanitizers over my proxy, extension and download code.
- A real download over the network on Windows, and a real proxy connection to a live proxy server.
- A full updater install-and-rollback cycle.

## Blocked or not implemented, and I do not claim otherwise

- **Operating-system containment.** My pages render inside my browser process. I removed the separate contained renderer because it never started reliably, so this is a real security gap I have to close again later.
- **YouTube and Netflix.** No media pipeline, no Media Source Extensions, no DRM. Netflix needs a licensed Widevine module, which can only ever be a separate extension.
- **Proton VPN.** Not bundled and not verified. My own VPN bridge is bundled instead and is clearly labelled as mine.
- **TLS-to-proxy and SOCKS5.** I refuse these rather than leak a direct connection. Proton uses TLS-to-proxy, so this is a real gap.
- Modern JavaScript: no modules, promises, async/await or regular expressions.
- Vault, passwords, translation, WebRTC, media, canvas, workers, IndexedDB.
- Full legacy UI parity, visual regression, accessibility audit, performance comparison, Android, signing and installer.

## Where I look for evidence

- Development workflow: https://github.com/Psycho-Core/PsychoVault/actions/workflows/Psycho_owned-engine.yml
- Release workflow: https://github.com/Psycho-Core/PsychoVault/actions/workflows/Psycho_release.yml
- Releases and update manifest: https://github.com/Psycho-Core/PsychoVault/releases
