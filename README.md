# PsychoVault

I am building my own browser engine and native Windows shell. I want my old interface and features back, with Proton VPN working as a bundled extension, without using another browser's engine underneath.

This is an early development build. It can parse and render a limited subset of HTML/CSS/JavaScript, but it is not a finished replacement for my old browser.

## Where I am keeping the work

- [PSYCHO.md](PSYCHO.md) - my project guide and non-negotiable requirements.
- [docs/PLAN.md](docs/PLAN.md) - my architecture, gates and delivery sequence.
- [docs/IMPLEMENTATION-STATUS.md](docs/IMPLEMENTATION-STATUS.md) - what works, what does not, and what I have actually tested.
- [docs/FEATURE-INVENTORY.md](docs/FEATURE-INVENTORY.md) - the old features and settings I am keeping myself accountable to.
- [docs/PLAN-AUDIT.md](docs/PLAN-AUDIT.md) - my planning audit.

## Builds

I use GitHub Actions for Windows builds. `PsychoVault_easy_build.bat` delegates to the hosted build instead of asking me to compile locally. Development artifacts are not signed releases.

## What I am not claiming

I am not claiming Proton, privacy, performance, full web compatibility or Android support until I have test evidence. I am not using legacy screenshots as proof that the new engine is finished.

## Licence and notices

I will keep my own code, bundled extensions, third-party notices and no-warranty information separate and accurate. Proton VPN remains Proton's extension, not my browser engine.
