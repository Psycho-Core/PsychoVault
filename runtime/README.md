# runtime/ - my own Electron

Nothing here yet. This directory is created by P1 of `docs/Psycho_MASTER-PLAN.md` and holds the privileged side of the app:

- `broker/` - window host, tab model, navigation, sessions, permissions, downloads, update service, dialogs, tray, menus.
- `ipc/` - typed channel definitions, schemas, capability handles, the framed message bus.
- `renderer/` - the sandboxed renderer process entry point that hosts my engine for one site instance.
- `preload/` - the curated `window.psycho` bridge exposed to UI and internal pages.
- `services/` - network, storage, vault, extension host and proxy service wrappers around `platform/`.
- `tests/` - integration harnesses: IPC round-trip, process supervision, navigation, crash recovery.

The contract for every channel is in `docs/Psycho_ARCHITECTURE.md` section 3. The first code lands at S020-S021. Until then this README is the only file here, and no build target references it.
