# My Windows build

I double-click **`PsychoVault.exe`**. That is the whole install. There is nothing to extract, no folder to keep together and no commands to run.

If Windows SmartScreen warns me, that is because my build is not code-signed yet. I can check the file against `SHA256SUMS.txt` on my release page before I choose to run it.

## What happens the first time I run it

My browser creates its profile folder and starts. My engine, my page host and my shell are all compiled into that one executable, so nothing is unpacked and no second program starts behind me.

I do not need administrator rights and I do not install a service.

## What works

- **Tabs.** Each tab keeps its own page on its own worker thread, so switching tabs does not reload the page. Stop, reload, back, forward and restoring my last tabs all work.
- **Pages.** My own HTML, CSS, JavaScript, layout and painting code renders what I fetch. PNG images decode and paint.
- **Downloads.** `Ctrl+J` opens my downloads. Files stream to disk with progress and cancel, and my list survives a restart.
- **Settings.** All ten categories are there, and the controls change real saved behaviour - search engine, home page, ad blocking, Do Not Track, referrer stripping, download folder, accent colour and more.
- **Extensions.** I can add an unpacked extension. Permissions are denied unless the extension declares them.
- **VPN.** My bundled VPN bridge drives my real proxy code, including PAC scripts and fail-closed routing that refuses to connect directly when my proxy is unusable.

## What does not work yet, and I am not going to pretend otherwise

- **Pages render inside my browser process.** I dropped the separate contained process so one file renders on its own without fighting the loader. That means a hostile page is not yet fenced off by the operating system. Restoring that boundary is a later step, and until it is done I should not treat this build as hardened.
- **YouTube and Netflix do not play.** I have no video pipeline and no DRM. Netflix needs a licensed Widevine module, which can only ever be a separate extension - never part of my engine.
- **Proton VPN is not bundled.** The VPN bridge included here is my own code and needs a proxy I point it at. I have not verified Proton sign-in or routing. I earlier said their extension source was unreachable; it is not - it is at `ProtonVPN/proton-vpn-browser-extension` under GPL-3.0, and it drives an HTTPS proxy on port 4443, which is the scheme my VPN panel accepts.
- **My VPN panel keys the tunnel.** My transport speaks SOCKS5 and HTTPS proxies, with TLS to the proxy stacked on the same TLS it uses for the origin. It refuses rather than connecting direct when the proxy is unusable.
- **Most big modern sites will not work yet.** My JavaScript engine has classes, destructuring, template literals, spread, Map and Set, promises, async/await, iterators and my own regular-expression engine, plus the pieces my Test262 run added in step 9 - but no modules, typed arrays or generators, and my CSS and layout coverage is still small. Panels that talk about my vault are not finished either.
- **My vault and password manager are not built.** I show an honest status rather than a password box that saves nothing.
- No WebRTC, canvas, translation or spell-check dictionary yet.

## Where my data lives

My profile is in `%LOCALAPPDATA%\PsychoVault\Owned`. A private window writes no history and no session tabs. If a settings file is ever damaged I keep it as a `.corrupt-*` backup instead of overwriting it. I should still back up anything I care about before testing.

## No warranty

I provide this preview as-is, without warranty. I decide where I run it and what I put into it.
