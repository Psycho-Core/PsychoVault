# runtime/ - my own Electron

Nothing here yet. This directory is created by P1 of `docs/Psycho_MASTER-PLAN.md` and holds the privileged side of the app:

- `broker/` - window host, tab model, navigation, sessions, permissions, downloads, update service, dialogs, tray, menus.
- `ipc/` - typed channel definitions, schemas, capability handles, the framed message bus.
- `renderer/` - the sandboxed renderer process entry point that hosts my engine for one site instance.
- `preload/` - the curated `window.psycho` bridge exposed to UI and internal pages.
- `services/` - network, storage, vault, extension host and proxy service wrappers around `platform/`.
- `tests/` - integration harnesses: IPC round-trip, process supervision, navigation, crash recovery.

What exists now (P1 in progress):

- `ipc/Psycho_bus.*` (S021, done): the framed length-prefixed message bus with
  the 21 legacy channel groups plus runtime channels, capability masks per
  endpoint, per-channel schema validation with a refusal convention, bounded
  inbound queues (byte and message back-pressure) and structured errors.
- `broker/Psycho_broker.*` (S020, in-process model done): role registry with
  capability masks per role, the service table, the routing pump with structured
  refusals, and the built-in lifecycle service (ping/pong heartbeat that S022's
  supervision will reuse). Process envelopes with job objects are S022 and are
  honestly not here yet - today a renderer is an in-process endpoint whose mask
  already blocks privileged channels, which is capability control, not OS
  isolation.
- `tests/Psycho_runtime_tests.cpp` (S042 started): 37 runtime checks covering
  framing, partial frames, oversize frames, schema rejections, capability
  denial both directions, both back-pressure budgets, lifecycle round-trip,
  structured refusals and two-renderer isolation. They run inside pv-tests.

Next in P1: S022 (real process envelopes: launch, job object, restricted token,
heartbeat, crash attribution), S023 (single-instance model), S024-S027
(window/tab surfaces on real windows).
