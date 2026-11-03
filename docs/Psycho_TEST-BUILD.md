# My Windows build

I double-click **`PsychoVault.exe`**. That is the whole install. There is nothing to extract, no folder to keep together and no commands to run.

If Windows SmartScreen warns me, that is because my build is not code-signed yet. I can check the file against `SHA256SUMS.txt` on my release page before I choose to run it.

## What happens the first time I run it

My browser unpacks its own renderer into my Windows user folder and checks it against the hash built into the executable before it runs it. I do that once per version. Every page still runs in a separate contained process, because that separation is what stops a hostile website reaching my files.

I do not need administrator rights and I do not install a service.

## What works

- **Tabs.** Each tab keeps its own contained renderer, so switching tabs does not reload the page. Stop, reload, back, forward and restoring my last tabs all work.
- **Pages.** My own HTML, CSS, JavaScript, layout and painting code renders what I fetch. PNG images decode and paint.
- **Downloads.** `Ctrl+J` opens my downloads. Files stream to disk with progress and cancel, and my list survives a restart.
- **Settings.** All ten categories are there, and the controls change real saved behaviour - search engine, home page, ad blocking, Do Not Track, referrer stripping, download folder, accent colour and more.
- **Extensions.** I can add an unpacked extension. Permissions are denied unless the extension declares them.
- **VPN.** My bundled VPN bridge drives my real proxy code, including PAC scripts and fail-closed routing that refuses to connect directly when my proxy is unusable.
- **Containment.** My renderer runs in a Windows AppContainer on a private desktop, with no access to my files and no direct network sockets.

## What does not work yet, and I am not going to pretend otherwise

- **YouTube and Netflix do not play.** I have no video pipeline and no DRM. Netflix needs a licensed Widevine module, which can only ever be a separate extension - never part of my engine.
- **Proton VPN is not bundled.** The VPN bridge included here is my own code and needs a proxy I point it at. I have not verified Proton sign-in or routing.
- **My VPN only tunnels HTTP proxies so far.** I refuse TLS-to-proxy and SOCKS5 rather than quietly connecting directly.
- **Most big modern sites will not work yet.** My JavaScript engine has no classes, modules, promises, async/await or regular expressions.
- **My vault and password manager are not built.** I show an honest status rather than a password box that saves nothing.
- No WebRTC, canvas, translation or spell-check dictionary yet.

## Where my data lives

My profile is in `%LOCALAPPDATA%\PsychoVault\Owned`. A private window writes no history and no session tabs. If a settings file is ever damaged I keep it as a `.corrupt-*` backup instead of overwriting it. I should still back up anything I care about before testing.

## No warranty

I provide this preview as-is, without warranty. I decide where I run it and what I put into it.
