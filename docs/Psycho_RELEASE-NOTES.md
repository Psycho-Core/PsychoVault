# PsychoVault {{VERSION}} - Windows x64 development preview

I build my own browser engine. This is a development preview I can test, not a finished browser.

## What I added in this build

- **A media pipeline of my own.** I wrote the container readers, the decoder path and the player. WAV files with 8, 16, 24 or 32 bit integer PCM or 32 bit float play, and AVI files carrying MJPEG video and PCM audio play as a picture with sound. The MJPEG frames go through my own JPEG decoder, the audio runs through my own mixer so two elements can play at once without cutting each other off, and the picture is scaled into whatever box my layout gave the element. `<audio>` and `<video>` both work, with `play`, `pause`, `currentTime`, `duration`, `volume`, `playbackRate`, `muted`, `loop` and `canPlayType`, and the `loadedmetadata`, `canplay`, `play`, `playing`, `pause`, `seeking`, `timeupdate` and `ended` events.
- **A real player under the picture.** My shell opens one WASAPI shared-mode stream the first time a page plays something, 48 kHz stereo 16 bit with the device's own sample-rate conversion, and asks the page thread for exactly the frames it needs. When nothing is playing it fills silence rather than looping whatever was in the buffer last.
- **Media Source Extensions.** `MediaSource`, `addSourceBuffer`, `appendBuffer`, `buffered`, `duration`, `endOfStream` and `URL.createObjectURL` work on a real path: a page appends bytes, my reader accepts a partial file, the playable range grows with each append, and an element that started playing before the data arrived begins as soon as enough media exists. `SourceBuffer.remove` refuses and says why, because I cannot cut a decoded segment in half and I am not going to pretend I did.
- **MP4 reading without pretending.** I walk the box tree, report the duration, the tracks and the codec names, and refuse playback with the codec named. A file that says H.264 or AAC is told it is H.264 or AAC, not shown as a black box.
- **A VPN transport I wrote myself.** My own socket layer, my own SOCKS5 client, my own HTTP `CONNECT` request and my own HTTP framing on top of them. A hostname goes to the proxy unresolved, which is the whole point of a tunnel, and the proxy resolves it. A redirect stays inside the same tunnel and reconnects there instead of leaking to a direct request.
- **TLS that stacks.** Windows SChannel does the certificate work, with a real chain build and a hostname policy check against the name I asked for, and it refuses an untrusted, expired, partial or revoked chain and refuses renegotiation. Because my TLS wraps a stream rather than a socket, I can run TLS to the proxy first and the origin's TLS inside that tunnel, which is the path an HTTPS proxy on port 4443 needs.
- **Fail-closed routing.** If my tunnel cannot be built, the request fails with a readable reason. There is no path from a failed tunnel to a direct socket. An empty configuration, a proxy I cannot dial and a scheme I do not carry are all refusals.
- **Proxy logins that stay with the proxy.** A username and password are saved against the server they were typed for and are only ever sent to that server. An extension that supplies credentials for its own proxy gets its own record, and neither set travels to a proxy it was not entered for.
- **A VPN panel I can actually use.** The VPN page now shows the server, the scheme, the username and a masked password, a toggle for refusing requests when the tunnel fails, and a button that turns the tunnel off. I type `host:port` in the same address bar I use for everything else, and my settings remember it.

## What my audit of step 7 found

I shipped step 7, then went back through it line by line instead of trusting a green build. Fifteen things were wrong, and the ones that mattered were not typos.

- **A download bypassed my proxy.** The worst of them. A page load and its subresources went through my tunnel, and a download opened its own session with no reference to the VPN. It now takes the same route decision as everything else, and a fail-closed policy refuses the download with the reason.
- **A cookie and a referer followed a redirect to another host.** The cookie header went out on every hop, and the referer named the page I was fetching rather than the page that sent me.
- **A 303 kept its POST body,** and so did a 301 or 302 after a form reply. A 303 now becomes a GET with no body, 301 and 302 turn a POST into a GET, and 307 and 308 keep both.
- **Framing that believed the wrong things.** A chunk size I could not read was treated as the end of the body, a body shorter than its Content-Length was returned as complete, two disagreeing lengths were accepted, and a HEAD reply was read as if it carried a body.
- **A zero-byte successful read could spin my framing forever.** Found while reading the TLS layer, and fixed in both places.
- **Revocation was claimed but never checked,** and the subject accessor returned the name I asked for instead of the subject the certificate carries.
- **A blocking connect ignored my timeout,** so a proxy that dropped packets could hold a tab for over a minute.
- **My own transport was a Windows-only feature in practice,** because the headless build refused everything before it. It now tunnels through the same code, which is how these defects got tests at all.

The full page is in `docs/Psycho_STEP7-AUDIT.md`, including what the audit could not test: no live Proton session, and no real certificate handshake in this environment.

## What I checked before calling this build done

I would rather show a number than a claim, so this is what I ran.

- My engine test suite passed: 648 checks. That includes the media readers, the source buffer path, my own HTTP framing and the whole tunnel.
- My tunnel was tested over a real loopback socket, not a script. A small SOCKS5 proxy runs inside my test, my client speaks to it, the name `example.test` arrives at the proxy rather than being resolved on this machine, and a page comes back through my framing.
- A redirect through that tunnel was tested the same way. The first request got a 302, the second went to the new path inside a fresh tunnel with the right `Host` line, and the page behind it came back.
- My source audit passed: 225 checks. My naming audit passed: 21 checks. My plan audit passed: 377 checks.
- The whole browser, including the new transport and the Windows TLS file, cross-compiled for x64 Windows before I tagged anything, so a missing header or a wrong library name was found here rather than on the runner.

One thing I want to be straight about: while writing the SOCKS5 tests I had three failures in a row that turned out to be my test's byte indices, not my client. I wrote a standalone probe, printed what my client actually put on the wire, and compared it byte for byte. The client was right; my arithmetic in the test was wrong. I fixed the test and left the probe result in the test comments.

Two more things went wrong in ways worth writing down. I had a check that passed on Linux and failed on the Windows runner with numbers that contradicted the failure line, so I printed the real values and the frame itself into the log. The cause was my own test helper being called `near`, which a Windows header defines as an empty macro, so every check whose expectation was zero quietly became a comma expression. It is renamed, and my naming audit now refuses that class of name in my tests. Then I built with address and undefined behaviour sanitizers and they caught a real bug in my JavaScript runtime: when a script threw, I collected garbage while the thrown value was still in the caller's hands, so the error message could read freed memory. I no longer collect on the way out of a throw.

## What I am still not claiming about the tunnel

- **No live Proton session.** I have no Proton code, no account and no endpoint of theirs here, so a working proxy I control is not a working VPN provider.
- **No real certificate handshake was exercised.** My SChannel path compiles, links and reads correctly, but I cannot complete a handshake against a real host in this environment. The HTTPS-through-an-HTTPS-proxy page load on my own machine is the check that settles it.
- **Revocation checking is requested, not proven.** A status I could not fetch is not a refusal, and I am not calling "nobody answered" a verification.
- **One tunnel per hop.** A redirect opens a new tunnel rather than reusing one, so each hop pays for its own handshake.

## What I am not claiming

- **YouTube and Netflix do not play.** My engine has my own media pipeline and my own Media Source Extensions, but no H.264 decoder, no AAC decoder, no VP8, VP9 or Opus, and no WebM. The streams those sites send are refused by name. DRM will only ever arrive as a separately licensed extension, never inside my engine.
- **Proton VPN is not bundled.** I have no Proton code in this build. What is new is my own transport and a panel I can point at a proxy, so a Proton endpoint on 4443 has somewhere to go once its credentials are in hand. A panel with a filled-in server is not proof of a working VPN, and I am not calling it one.
- WebP and progressive JPEG are not decoded. My page says so in its diagnostics rather than showing a blank space without explanation.
- WebRTC is not implemented, so my extension-facing WebRTC policy setting is a stored preference, not a leak prevention I can demonstrate.
- My pages render inside my browser process. There is no operating-system containment for page code yet.
- My vault, password manager, translation, media keys and full legacy settings parity are not finished.
- My ad blocker is host-list based and is not the rule engine I described in my plan.
- This build is unsigned, so Windows SmartScreen may warn me.

## How I run it

I download **PsychoVault.exe** and double-click it. That is the whole install.

My engine and my page host are compiled into that one file, so nothing is unpacked and nothing sits beside it.

## How my updater reads this release

I publish `PsychoVault-update.json` with the version, tag and a SHA-256 for every asset. `PsychoVault.exe` is the update payload, so an update replaces one file. I verify the hash before I offer to install anything.

## No warranty

I provide this preview as-is, without warranty.
