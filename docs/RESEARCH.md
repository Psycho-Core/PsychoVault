# My research for the owned PsychoVault browser

I did this research before writing the build plan. I wanted to know what it actually takes to replace the old application with my own browser, keep my interface and features, and make Proton VPN work inside it. I am not treating a specification, a manifest, or a successful compile as proof that the browser works.

Research date: 2 November 2026.

## 1. What I checked

I checked four kinds of evidence:

1. My old application, read-only from Git commit `503a596bd89b5aaac979d4f6f86f8465e24c192d`.
2. The saved legacy screenshots from commit `3bda2030de69c30aa2923d01c52daf9204ca0dd1`.
3. Primary web-platform specifications and conformance suites.
4. The pinned Proton VPN extension source and the platform APIs I would need on Windows.

I did not restore the old Electron application, Android source, bundled extensions, or old workflows into the new tree. I used them only as reference evidence.

## 2. What my old application actually contained

I extracted a machine-readable inventory into `docs/reference/legacy-inventory.json`. The important counts are:

| Item | Count | What it means |
| --- | ---: | --- |
| Settings categories | 10 | General, Privacy & security, Downloads, Extensions, Performance, Appearance, Spelling, Passwords, Accessibility & language, About |
| Interactive settings controls | 125 | 109 controls with IDs, 10 category buttons, 6 data-clear buttons |
| Default settings keys | 59 | These need behaviour and migration rules, not just visible widgets |
| Main-process IPC handlers | 105 | These are the old privileged browser-service surface |
| Preload namespaces | 21 | These grouped the chrome UI's access to browser services |
| Saved screenshots | 23 | New tab/home, browser window, and all ten settings categories |

The settings screens are long. The screenshot archive proves the visible reference states, but it does not prove that every below-the-fold control was captured. The source inventory is the completeness backstop. I still want full-page captures before I sign off visual parity.

The old chrome UI included tabs, workspaces, private windows, split view, command palette, omnibox suggestions, bookmarks, history, downloads, find, zoom, translation, sidebar services, Performance, ad blocking, Proton, spelling, vault, updates, diagnostics and recovery. The old data stores included `settings.json`, `bookmarks.json`, `history.json`, `downloads.json`, `session.json`, `workspaces.json`, `vault.bin`, `vault.key.json`, `vault-migration.json`, `crash-recovery.json`, extension storage and icon caches.

The old vault had two real encryption paths: an OS-keychain path and a master-password path using scrypt with `N=16384`, `r=8`, `p=1` and AES-256-GCM. Encrypted backups used the same scrypt parameters. I cannot migrate that data by inventing a new format and hoping for the best.

## 3. Web-platform findings

I want my own engine, so I have to implement the platform contracts myself. The main references I am using are:

| Area | Primary reference | Pinned revision |
| --- | --- | --- |
| HTML | https://github.com/whatwg/html | `404b3eb14a7a936dddefb2d78e224dbeb01d2358` |
| DOM | https://github.com/whatwg/dom | `3c53f93a3e1fb11ef5bd38029b7a9c97baaf0177` |
| URL | https://github.com/whatwg/url | `845bafca22fb0c5a42c4bf384a9bdea525a2f10d` |
| Fetch | https://github.com/whatwg/fetch | `59afe349913d56898c947576216f1c2579308be7` |
| Encoding | https://github.com/whatwg/encoding | `be17ac5c49881336538acfc7b40dba3c8009144b` |
| Streams | https://github.com/whatwg/streams | `312b67e396a8511a2a984eb90ce4b2e00283c366` |
| Web IDL | https://github.com/whatwg/webidl | `296213a1d9c8293850295c6e77d3b13dce8681a9` |
| Notifications | https://github.com/whatwg/notifications | `4cf42508117bbe1162a6bcfd583d407533afaebd` |
| XMLHttpRequest | https://github.com/whatwg/xhr | `b2c6fce86bd5c5dbce907caa1b3fe19b4845a4ab` |
| ECMAScript | https://github.com/tc39/ecma262 | `4ad14cf8ec06c18e3ee8b20b0fa0683424b8b29a` |
| Test262 | https://github.com/tc39/test262 | `91c70759363c46557b694792bf34a2551a7af5ba` |
| CSS drafts | https://github.com/w3c/csswg-drafts | `fc3afa7b2f8ae4efe724da7f88f42b00499ce94c` |
| IndexedDB | https://github.com/w3c/IndexedDB | `f9f83f196f8501429282ff1747ebcbe35d41381a` |
| Service Workers | https://github.com/w3c/ServiceWorker | `57dfd24943fee6ba795cea49d277dfb0754e4517` |
| WebRTC | https://github.com/w3c/webrtc-pc | `bf62a51b8df85f1e66e88379c0fdb57f1b4a6a47` |
| Web Platform Tests | https://github.com/web-platform-tests/wpt | `0e5338a1826ea666cce5a49ae67570bbb3f35d4e` |

What I took from this:

- HTML parsing, DOM mutation, event dispatch, navigation, session history, workers, timers and rendering updates are one connected event-loop model. I cannot bolt a JavaScript engine onto a HTML parser and call that a browser.
- CSS is not one parser. I need syntax, selectors, cascade, inheritance, values, box model, inline layout, flexbox, grid, positioning, overflow, text, fonts, transforms, transitions, animations and compositing.
- JavaScript compatibility needs Test262. Browser compatibility needs WPT. I will pin test-suite revisions and report exact pass/fail/skip counts. I will not turn a small selected subset into a claim of full compatibility.
- Service Workers, IndexedDB, Fetch, Streams, Web Crypto, WebRTC and Notifications are separate subsystems. Proton and modern sites can depend on them indirectly even when my browser UI does not show them.
- Web IDL is useful as a contract description, but I still have to write the binding layer and the behaviour behind it.

## 4. Extension and Proton findings

I inspected Proton VPN browser extension commit `71d7d86f164a2ee8e50ce5a389c9e5cbe0fd6883`, version `1.3.6`. The source is GPL-3.0. I am treating it as an extension exception, not as browser-engine code.

The pinned manifest declares Manifest V3, a background service worker, an action popup, and these permissions:

```text
idle
notifications
privacy
proxy
scripting
storage
tabs
webRequest
webRequestAuthProvider
```

It also declares host permissions for `https://*/`, `http://*/`, `ftp://*/`, `wss://*/` and `ws://*/`, plus `externally_connectable` for `https://account.proton.me/*`.

From the source, I confirmed these behaviours matter:

- The service worker entry imports the browser polyfill and background bundle.
- Proton uses `chrome.proxy.settings.get/set/clear` and `chrome.proxy.settings.onChange`.
- Proton can configure `fixed_servers` and can generate a PAC script for split tunnelling.
- The production proxy scheme is HTTPS. The normal proxy port is `4443`, with a range up to `4443 + 11` when single-port mode is off. Secure Core uses port `443`.
- Proton excludes local networks and its account/API domain from the proxy according to its configuration. I must not describe those exclusions as a leak, but I also must not hide them.
- Proxy authentication uses `webRequest.onAuthRequired` with `asyncBlocking`. The handler checks `requestDetails.isProxy`, checks the challenger host against known proxy hosts, fetches credentials, and cancels rather than looping forever on bad credentials.
- Proton watches `onBeforeRequest`, `onCompleted` and `onErrorOccurred` to keep credentials ready and clear pending authentication state.
- Proton uses `chrome.privacy.network.webRTCIPHandlingPolicy.get/set/clear`.
- Proton uses storage, tabs, windows, scripting, permissions, notifications, idle, runtime messaging and action/browser-action APIs.
- The extension has its own telemetry configuration. I cannot claim the whole product has no network calls while shipping an extension that may make its own calls. I need to disclose extension behaviour separately from my browser's telemetry policy.

The Chrome extension documentation I checked is archived at `GoogleChrome/developer.chrome.com` commit `51dd7dd5d510ed85d86f5a91cb8fde50b62351c7`. It confirms the proxy modes `direct`, `auto_detect`, `pac_script`, `fixed_servers` and `system`, and it confirms that asynchronous `onAuthRequired` credentials require `webRequest` plus `webRequestAuthProvider`. MDN's proxy documentation also confirms that the Firefox and Chrome proxy APIs are not the same design. I am targeting the Chrome-style MV3 surface Proton actually uses, while keeping my engine independent.

What this means for my engine:

- I need a real MV3 service-worker host, not a permanent hidden webpage pretending to be one.
- I need a PAC evaluator sandboxed enough to run `FindProxyForURL` and the helper functions Proton emits, including `isInNet` and `isPlainHostName`.
- I need proxy ownership, level-of-control reporting, change events, error events and rollback.
- I need authentication challenges to carry the exact challenger host, port, scheme, realm and proxy identity. I must not hand proxy credentials to an arbitrary website.
- I need fail-closed routing when Proton says traffic must use the proxy. A failed proxy must not silently become a direct connection.
- I need DNS and WebRTC policy to be enforced by the network and RTC stacks, not by a settings label.
- I need extension storage, popup rendering, content-script injection, message passing, permission prompts and install/remove/enable/disable lifecycle.

## 5. Windows platform findings

I want Windows x64 to be the first complete release. I checked Microsoft documentation hosted in `MicrosoftDocs/win32`:

- AppContainer isolation: https://github.com/MicrosoftDocs/win32/blob/docs/desktop-src/SecAuthZ/appcontainer-isolation.md
- Implementing an AppContainer: https://github.com/MicrosoftDocs/win32/blob/docs/desktop-src/SecAuthZ/implementing-an-appcontainer.md
- Schannel: https://github.com/MicrosoftDocs/win32/blob/docs/desktop-src/com/schannel.md
- DirectWrite: https://github.com/MicrosoftDocs/win32/blob/docs/desktop-src/DirectWrite/introducing-directwrite.md
- Direct2D and DirectWrite: https://github.com/MicrosoftDocs/win32/blob/docs/desktop-src/Direct2D/direct2d-and-directwrite.md

What I took from this:

- I can use Windows as the operating system without embedding another browser. Win32, Direct2D, DirectWrite, Direct3D, Schannel, CNG, DPAPI and AppContainer are platform boundaries, not browser engines.
- AppContainer gives me a least-privilege sandbox for renderer and extension processes. I still need a broker, restricted IPC, job objects, handle validation and explicit capability grants.
- Schannel lets me use the OS TLS stack instead of writing my own TLS cryptography. I still own HTTP semantics, connection pooling, proxy tunnelling, certificate policy, HSTS, mixed-content rules and request scheduling.
- DirectWrite and Direct2D give me text and 2D rendering primitives. I still own font selection, shaping decisions, line breaking, layout, paint order, damage tracking and compositing.
- DPAPI can help me read the old OS-keychain vault path. CNG can provide AES-GCM, hashing, random bytes and signature verification. I do not want to invent cryptographic primitives.

I also checked Rust platform support. `x86_64-pc-windows-msvc` is a Tier 1 target with host tools in the Rust platform-support documentation: https://github.com/rust-lang/rust/blob/main/src/doc/rustc/src/platform-support.md. The GitHub Windows 2022 runner image lists Rust, Cargo, Clippy, Rustfmt and NSIS: https://github.com/actions/runner-images/blob/main/images/windows/Windows2022-Readme.md. I am not relying on a remembered runner version; the build plan pins and records the toolchain.

## 6. Performance findings

I cannot honestly promise that a new engine will match Electron before I measure it. Electron gave me a mature rendering engine, JavaScript engine, network stack, compositor and process model. Replacing those means I have to earn the performance result.

I want these measurements on the same Windows hardware, power mode, display scale and network conditions:

- Cold start to interactive chrome.
- Warm start to restored session.
- New tab open and first paint.
- Tab switch latency.
- Address-bar input latency.
- Scrolling and animation frame pacing.
- Idle CPU and memory.
- Memory with 1, 10 and 30 ordinary tabs.
- Memory and wake latency with sleeping tabs.
- Page load for a fixed local corpus and a fixed live-site corpus.
- Extension overhead with Proton installed but disconnected, connected, and failing.
- Renderer crash recovery time.
- Shutdown time and data-flush correctness.

I will compare against the old application where I can run it legally and safely as a reference. I will not ship the old runtime as part of the new browser. If I cannot run the old application on the same machine, I will report absolute numbers and say that the direct comparison was not run.

## 7. Build and release findings

I want GitHub-hosted builds. The Windows runner image already provides the basic Rust and NSIS tooling, but I still need to pin versions, record hashes, cache correctly and fail on the first real error. I want one heavy build at a time. I want release notes on success and failure. I want signed updates, checksums, installer tests, rollback tests and a clean separation between source, build output and user data.

I also know that GitHub-hosted runners are ephemeral. A cache is not a build result. I will key caches by toolchain, source revision and build configuration. I will not reuse a binary built from different source and call it current.

## 8. Options I rejected

| Option | Why I rejected it |
| --- | --- |
| Keep Electron and rename it | It does not satisfy my own-engine requirement. |
| Embed Chromium, Gecko, WebView, Servo, WebKit or another engine | It moves the browser work into someone else's engine. |
| Use Node.js as the extension runtime | It is not a browser engine and it would give extensions the wrong privilege model. |
| Use a proxy popup as VPN proof | It proves UI, not routing, authentication or leak protection. |
| Copy another browser's source and change names | It is not my implementation and creates licence and provenance problems. |
| Ship a partial engine as a finished browser | It would repeat the mistake I am trying to fix. |
| Write my own TLS or block cipher | I want OS-backed cryptography, not homemade cryptography. |

## 9. What remains uncertain

I am not going to hide these:

- Full modern-web compatibility is a large engine project. I need staged WPT/Test262 gates and a fixed site corpus.
- Proton sign-in and live proxy routing need a real account and live network tests. I cannot prove them from source alone.
- Windows sandboxing needs real process-escape and handle-leak tests, not just API calls.
- Visual parity needs full-page reference captures and pixel/interaction tests at multiple sizes and DPI settings.
- Vault migration needs test fixtures for both old encryption paths and interrupted migrations.
- Android needs its own windowing, input, IME, clipboard, notifications, storage and process model work. I will not call it done because the core compiles.

These uncertainties belong in the plan as gates. They are not reasons to fake completion.
