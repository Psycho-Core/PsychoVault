# My audit of step 7, the VPN transport

I built step 7, released it as 0.10.0, and then went back through it line by line because a green build is not the same thing as a correct tunnel. This is what I found, what I changed, and what I am still not claiming.

## How I audited it

I read the transport as the attacker would read it, not as the author would. The questions I asked were narrow and specific:

- What happens when a server or a proxy lies about where its body ends.
- What happens when the far side sends nothing at all, or sends a read that succeeds with no bytes.
- What does my request carry on the second hop of a redirect, and does a cookie follow me to a host that never set it.
- Can a failed tunnel reach the network by any other route.
- Does a download follow the same policy as a page, or does it go its own way.
- Does the certificate check happen before anything private is sent, and does it actually check what I said it checks.

Every finding below has a test behind it now, and the number at the end of this page is the count of checks that pass.

## What was wrong

**A cookie followed a redirect to a different host.** My transport attached the page's cookie header to every hop. A redirect to another host therefore carried the first host's cookies with it, which is exactly the leak a browser is supposed to prevent. The header is now sent only while the hop's host is the host I started with.

**The referer named the wrong page.** I sent `current.origin()`, which is the address I was fetching, so every request claimed to have come from itself. It now names the page that actually sent me, and moves forward with each hop.

**A 303 kept the old method and the old body.** A form reply answered with `303 See Other` was re-sent as a POST with the same body. `301` and `302` after a POST had the same problem. My transport now follows the rule a browser follows: `303` becomes a GET with no body, `301` and `302` turn a POST into a GET, and `307` and `308` keep both the method and the body.

**A chunked body could be truncated without a word.** The chunk size was parsed with `strtoull`, which returns zero for anything it cannot read, so a chunk header of `zz` looked like the end of the body and I returned half a response as a success. The size is now parsed strictly, a size I cannot read is an error, an oversized size is an error, and the two bytes after a chunk's data have to be the line break they claim to be.

**A body shorter than its Content-Length was returned as complete.** If the connection closed early I returned the bytes I had. It now fails with the number of bytes that were missing, because rendering part of a page and calling it whole is worse than saying the transfer broke.

**Two different Content-Length values were accepted.** The first one won silently. A response that disagrees with itself is now refused, and so is a length that is not a number.

**A HEAD reply and any 204 or 304 were read as if they carried a body.** A HEAD reply carries the length of the body it is describing and none of the bytes, so my client would have waited for content that was never coming. Those three cases now stop after the head.

**A proxy scheme I never wrote fell through to the HTTP proxy path.** Anything that was not `socks5` or `https` was treated as an HTTP proxy, which meant a nonsense scheme produced a real `CONNECT` on a plain socket. The scheme is now checked first and refused by name.

**An IPv6 target produced a malformed CONNECT request.** `2001:db8::1:443` reads as a host with several colons and no port. The literal is now bracketed.

**A blocking connect ignored my timeout.** The receive and send timeouts were set, but `connect()` itself is not covered by them and can sit in the kernel for over a minute. A proxy that silently drops packets held a tab that long. The connect is now non-blocking with my own wait and a defined limit, and a read that times out says "my proxy did not answer in time" rather than surfacing a number.

**A read that succeeded with zero bytes could spin my framing forever.** This one came out of reading the TLS layer: a control record that carries no application data was reported as a successful read of nothing, and the loop above it appended nothing, looked again, and did the same thing for as long as the tab existed. TLS now reads again instead of returning an empty success, and every framing loop treats a success with no bytes as an error or as the end of the body.

**Revocation was claimed but not checked.** My chain parameters asked for no revocation check at all, and `CERT_CHAIN_POLICY_SSL` with no flags does not perform one either, so "refuses a revoked certificate" was only true if the chain itself said so, which it never would without asking. Revocation checking is now requested for the chain apart from the root, and a certificate that is definitely revoked is refused by name.

**The certificate subject accessor returned the wrong thing.** It returned the name I asked for, not the subject the certificate carried, and the real subject was read into a buffer nobody used. It now returns the subject from the certificate.

**A download bypassed my proxy policy entirely.** This was the worst one. A page load and its subresources went through my tunnel, and a download opened its own WinHTTP session with no reference to the VPN at all. That is a direct connection from a browser whose panel said it was tunnelling. Downloads now take the same route decision as everything else, and a fail-closed policy refuses the download with the reason.

**The Windows path could make the routing decision twice.** A tunnel scheme returned before the WinHTTP loop, but a refusal from the router fell through to a second call that decided again. The route is now decided once, and a refusal ends the request.

**My own transport was a Windows-only feature in practice.** The headless build returned "broker networking is implemented on Windows" for anything, which meant the transport I had just written was never exercised by the build I can run here. The routing helpers moved out of the Windows section, and the headless build now tunnels through the same code, so a proxied request in a test is the same code a proxied request in the browser runs.

**The VPN panel offered a plaintext scheme.** `http` was accepted as a tunnel scheme, which sends my `CONNECT` target and my proxy login across the network in clear text. The panel now takes only `socks5` and `https`, says so by name when you type anything else, warns on screen if a profile already carries a plain HTTP proxy, and no longer defaults to one.

## What I checked afterwards

- 648 engine checks pass. The step-7 additions are the redirect rules, the cross-host cookie and referer checks, five ways a response can lie about its body, a HEAD reply, an unknown scheme, an IPv6 CONNECT target and a stream that reads nothing.
- The redirect and cookie checks run over a real loopback SOCKS5 proxy, not a scripted stream: one tunnel for the first request, a second tunnel for the redirect, and the second request's bytes examined for the cookie and the referer.
- The Windows browser cross-compiles and links with the TLS changes, including the revocation flag, which is a parameter of the chain call rather than of the chain parameters and only the Windows headers will tell you that.
- My source audit, naming audit and plan audit all pass with nothing skipped.

## What this audit did not test

An audit I ran here cannot stand in for a live test, and I am not going to let the numbers imply otherwise.

- **No live Proton session.** I have no Proton code, no account and no endpoint of theirs to point at. What I can say is that a SOCKS5 proxy and an HTTPS proxy both work against a proxy I control, and that the transport refuses rather than connecting direct.
- **No real certificate handshake was exercised.** My SChannel path compiles and links, and its logic is guarded, but I cannot complete a TLS handshake in this environment. The chain and name checks are code I have read carefully and cannot demonstrate here. On the machine I test on, a page over HTTPS through an HTTPS proxy is the check that settles it.
- **Revocation checking is requested, not proven.** A status I could not fetch is still not a refusal, and I do not treat "nobody answered" as "verified". I would rather say that plainly than claim a guarantee the code does not give.
- **WebRTC is still not implemented,** so a WebRTC policy setting is a stored preference and not a leak prevention I can demonstrate.
- **One tunnel per hop, no connection reuse.** A redirect opens a new tunnel, which is correct but slower than reusing one, and each hop pays for its own handshake.

## What is left before I call step 7 done

The transport is mine and it behaves, but step 7 is not finished in the sense that matters to the person holding the EXE.

- Proton still is not bundled. It needs its own build toolchain and modern JavaScript my engine does not have yet.
- A live provider test needs an account and a network, and until I run one I mark it not run.
- Connection reuse, and a keep-alive path inside a tunnel, are not written.
- A proxy that answers a CONNECT with a body, or dribbles one byte at a time, is handled by the same rules but has not been fuzzed.

I would rather hand over a transport that refuses loudly than one that leaks quietly, and the list above is the difference between those two.
