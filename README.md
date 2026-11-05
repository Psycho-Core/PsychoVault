# PsychoVault

I am building my own browser: my own app runtime, my own rendering engine, my own interface and my own installer, with Proton VPN arriving as an extension beside it - never another browser's engine underneath.

The old app I made earlier ran on Electron. I want that experience back - the same dark interface, the same tabs, workspaces, settings, vault, blocker, extensions and VPN - on code written for this project. The way to get there is to build my own equivalent of Electron first (a runtime that hosts my UI as web pages and my engine as the thing that draws them), and then grow the engine until it renders the real web.

## Where the work is

- [PSYCHO.md](PSYCHO.md) - my project guide and the requirements I hold myself to.
- [docs/Psycho_MASTER-PLAN.md](docs/Psycho_MASTER-PLAN.md) - the rebuild: milestones, and the numbered steps from an empty runtime to an installed release.
- [docs/Psycho_ARCHITECTURE.md](docs/Psycho_ARCHITECTURE.md) - how my runtime, engine, UI, extension host and installer fit together.
- [docs/Psycho_REFERENCE.md](docs/Psycho_REFERENCE.md) - how I use the old app and its screenshots as a contract, not as code to copy.
- [docs/Psycho_FEATURE-INVENTORY.md](docs/Psycho_FEATURE-INVENTORY.md) - every old feature and setting I must bring back.
- [docs/Psycho_CONFORMANCE.md](docs/Psycho_CONFORMANCE.md) - my Test262 measurement, run by my own runner.
- [docs/Psycho_BUILD-AND-RELEASE.md](docs/Psycho_BUILD-AND-RELEASE.md) - how I build, test and ship, including why my GitHub Actions builds are parked until the runtime milestone is real.

## Where it stands

This is a rebuild in progress. The engine, networking, profile, vault and extension groundwork exist and are tested at the unit level. What I have not built yet is the part that makes it feel like my old browser: the runtime that hosts my interface, the interface itself rendered by my own engine, and the installer. That is exactly where the plan starts, and it is why there is no new release to try yet.

## How it will ship

As a real Windows installer - `PsychoVault-Setup-<version>.exe` - that installs, registers, makes shortcuts, appears in Apps & Features and uninstalls. Not a portable exe.

## What I am not claiming

I am not claiming Proton, web compatibility, privacy, performance or Android support until I have test evidence. There is no current release: the last published builds were the earlier preview line and they do not represent what this plan builds. Proton VPN is Proton's own extension, GPL-3.0, bundled and sandboxed as an extension - never merged into my engine.

## Licence and notices

My engine, runtime, interface and installer are my own work. Extensions are the one place third-party code is allowed, always under their own licences and notices. See [NOTICE.txt](NOTICE.txt).
