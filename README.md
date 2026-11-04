# PsychoVault

I am building my own browser engine and a native Windows shell, and I am keeping the documents honest as I go. My goal is my old browser back, with my own code under it and Proton VPN arriving as an extension beside it - never another browser's engine underneath.

This is still a development preview. It renders its own pages, runs its own JavaScript and handles downloads, tabs and settings, but it is not a replacement for a finished browser.

## Where I keep the work

- [PSYCHO.md](PSYCHO.md) - my project guide and the requirements I am holding myself to.
- [docs/Psycho_PLAN.md](docs/Psycho_PLAN.md) - my architecture, gates and the order I am building in.
- [docs/Psycho_IMPLEMENTATION-STATUS.md](docs/Psycho_IMPLEMENTATION-STATUS.md) - what works, what does not, and what I actually tested.
- [docs/Psycho_CONFORMANCE.md](docs/Psycho_CONFORMANCE.md) - my Test262 measurement, run by my own runner.
- [docs/Psycho_FEATURE-INVENTORY.md](docs/Psycho_FEATURE-INVENTORY.md) - the old features and settings I am keeping myself accountable to.
- [docs/Psycho_VERIFICATION.md](docs/Psycho_VERIFICATION.md) - the record of every check behind each claim.

## How I build and ship it

Everything compiles on GitHub Actions, and each tag publishes a portable executable, a ZIP, a checksums file and an update manifest. The work is broken into steps I can measure; the ninth and latest measures my engine against Test262, and it is live at [releases/tag/v0.12.0](https://github.com/Psycho-Core/PsychoVault/releases/tag/v0.12.0).

## What I am not claiming

I am not claiming Proton, full web compatibility, privacy, performance or Android support until I have test evidence of them. Proton VPN is not bundled yet: a live session needs a real account and a reachable endpoint, which this environment does not have - but the gap I recorded earlier about finding their source was my own mistake in a lookup, and their extension is public and GPL-3.0.

## Licence and notices

My engine and shell are my own work. Extensions are the one place third-party code is allowed to arrive, always under its own licence and its own notices. Proton VPN will remain Proton's extension, not part of my browser engine.
