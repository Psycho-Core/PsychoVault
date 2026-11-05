# installer/ - my setup program

Nothing here yet. This directory is created by P10 of `docs/Psycho_MASTER-PLAN.md` and holds my own Windows setup and uninstall programs, built with the same toolchain as the browser.

Planned content:

- `Psycho_Setup.cpp` - welcome, licence, directory choice, progress, finish; per-user install by default with an optional machine-wide mode; cancel-safe.
- `Psycho_Uninstall.cpp` - remove files, shortcuts and registrations, keep the profile unless asked, no orphans.
- `Psycho_payload.cpp` - the payload format: manifest with per-file hashes, version stamping, upgrade-safe replacement, rollback.
- `Psycho_registry.cpp` - Apps & Features entry, file associations, optional default-browser registration, protocol handling.
- `README.txt` - the text the installer shows and installs.

The previous portable single-file exe model is retired. The artefact I install from from now on is `PsychoVault-Setup-<version>.exe`.
