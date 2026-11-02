# PsychoVault extension engine

This is my own extension-engine core for PsychoVault. It is MIT licensed and contains no Titanium, GeckoView, Electron or Chromium extension-host code.

## What it is today

- A Manifest V3 reader that rejects duplicate keys, unsafe resource paths, remote/unsafe extension CSP, malformed match patterns and oversized manifests.
- A default-deny permission policy for the first Proton/Dark Reader API surface.
- A profile-scoped request dispatcher with opaque context tokens, request/result size limits, in-flight limits, timeouts with `AbortSignal`, and JSON-only parameters/results. A timed-out handler keeps its slot until it settles.
- Unit tests for the policy boundary.

## What it is not yet

- It is not a JavaScript sandbox. The embedding browser must still run each extension worker/page in an isolated renderer with no Node.js, no privileged browser bridge and no shared mutable state.
- It is not wired into the Windows app or the Android build.
- It does not yet implement storage, proxy routing, tabs, scripting, webRequest authentication, content-script injection, extension package verification, updates or a service-worker lifecycle.
- It does not prove that Proton VPN connects, routes traffic, authenticates to a proxy or prevents leaks.

I am starting here because a browser that only loads someone else's extension host is not the ownership boundary I want. The next slices must add a real isolated host, persistent profile-scoped storage, a fail-closed proxy controller and package identity/update checks, then prove Proton on a device before replacing the working Windows extension path.

## Test

```sh
node --test extension-engine/test/*.test.cjs
```

These are source/unit tests. They are not an Electron integration test, an Android compile, a device test or a Proton/leak test.
