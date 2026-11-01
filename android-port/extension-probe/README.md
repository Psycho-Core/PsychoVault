# Android extension compatibility probe

This is a separate, unpacked test extension, not part of the current WebView APK and not a VPN. Its permissions mirror most of the bundled Proton extension's declared permissions. The browser will warn that it can access all HTTP and HTTPS sites because of the host permissions; the probe itself does not read site contents, make network requests, register request listeners or change proxy settings.

On a candidate extension-capable Android browser, load this folder as an unpacked extension if that browser supports it and open its popup. Press Run checks. Results only report whether API functions exist and the proxy control level; proxy values, credentials and URLs are not reported. Nothing here proves that an API actually behaves correctly, that Proton can sign in, or that traffic is routed or protected. A host could expose non-functional stubs. Follow-up tests must exercise an independent extension, authentication, actual proxy requests and failure modes, then Proton with consent, and separately check DNS and WebRTC. The probe does not test `externally_connectable`, service-worker lifecycle, popup resizing or permissions behaviour.

The current Android prototype has no extension host and cannot install this probe. Do not interpret a successful probe as a finished port or a VPN connection.
