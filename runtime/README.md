# runtime/ - my own my own runtime

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

- `process/Psycho_process.*` (S022, supervision core done): the supervisor with
  injected clock - spawn, heartbeat windows, hang detection, kill for cause,
  bounded restarts with a spent-budget retirement, every death attributed with
  a reason. 17 new checks, 860 total passing. `Psycho_process_win32.cpp` is the
  real Windows envelope: CreateProcess with suspended launch, a job object that
  kills the whole tree on close, a maximum-privileges-stripped token, and pipes
  the S021 wire runs over. It compiles on Windows only and is proven on the
  first authorized manual Windows build.

- `app/Psycho_instance.*` (S023, decision core done): the singleton model as an
  auditable state machine - primary takes the lock, a second launch forwards
  its arguments and exits, quitting saves the session exactly once and hands
  the lock back; a failed session save refuses the quit and keeps the app
  running. 11 new checks, 871 total. `Psycho_instance_win32.cpp` is the real
  Windows side: named mutex per profile, named pipe forwarding on the S021 wire
  shape, listener thread with a clean quit path - proven on the first
  authorized manual Windows build.

- `window/Psycho_window.*` (S024, rules done): my BrowserWindow equivalent's
  decisions - creation with clamping into monitor work areas, cross-monitor
  moves (destination monitor resolved first, then clamped), min/max/fullscreen
  with restore geometry kept, single-owner focus with deterministic fallover,
  per-window DPI scale and taskbar identity (`PsychoCore.PsychoVault.Browser`).
  27 new checks, 898 total. `Psycho_window_win32.cpp` is the real OS half: my
  own window class, SetWindowPos/ShowWindow application, AppUserModelID, a
  WM_DPICHANGED path that obeys the rules object; proven on the first
  authorized manual Windows build. Fullscreen style-swap polish rides S025's
  presentation work.

- `surface/Psycho_surface.*` (S025, rules done): the presentation contract my
  whole runtime obeys - my engine paints bytes, my broker's registry holds the
  only writable copy, my compositor intersects before it copies so writing
  outside the destination is structurally impossible. Shape-lying frames are
  refused; resize clears instead of smearing stale memory; paint order is list
  order. 26 new checks (924 total), including a real engine Canvas payload
  composited pixel-exact and off-edge clipping proven on both sides of the
  line. `Psycho_surface_win32.cpp` presents the target through a top-down DIB
  section - proven on the first authorized manual Windows build.

- `tabs/Psycho_tabs.*` (S026, rules done): my tab model - ordered tab lists
  per attached window, and the laws are structural, not advisory. Pinned tabs
  are always a prefix of the order, so no index can ever smuggle an unpinned
  tab into the pin block (family-crossing indices clamp, never refuse). A
  group never spans windows, never straddles the pin boundary, holds only
  contiguous members, and evaporates when its last member closes. The active
  tab can never be discarded; waking a sleeper bumps its generation counter,
  which is the honest record of a fresh load. Single tabs, whole groups and
  whole windows move between windows through one transplant primitive. The
  recently-closed stack is bounded to 25 and restores positions, pin state and
  group identity from snapshots - under fresh ids, because history ids are
  never resurrected - and a refused restore leaves memory intact. 107 new
  checks (1031 total).

- `content/Psycho_content.*` (S027, model done): my WebContents equivalent -
  a per-tab navigation controller with committed session history, back and
  forward travel, reload and stop, and every state change announced on the Tab
  channel through envelopes that validate against the stock schema registry.
  The rules with teeth: exactly one navigation in flight (starting another
  aborts the first and the aborted corpse stays in the bounded audit log), a
  committed NEW navigation severs the forward tail at commit and never before
  it, a revisit refreshes metadata but never grows history, a failed
  navigation changes nothing that was already true, and both ledgers are
  bounded (100 history entries, 256 logged navigations). URLs are canonicalised
  by an admit-policy seam that ships http/https-only; the internal scheme
  widens it in S028. 679 new checks (1710 total), including a 300-navigation
  proof that both bounds hold.

- `internal/Psycho_internal.*` (S028, rules done): my internal scheme
  `psychovault://` and its three laws. Pages exist only by registry (nine
  stock entries, address space fixed; web probing can never invent one). Every
  page is its own origin: a settings page may hold its own resources but never
  reaches history's - cross-internal fetches refuse. Web content gets nothing
  by default: fetching an internal origin needs the Internal-channel
  capability AND a page that opted in - both, never either, and no stock page
  opts in - while web top-level navigation into the scheme is refused
  unconditionally. Canonicalisation folds case, strips the lone root slash,
  refuses traversal, escapes and backslash separators outright. New bus
  channel `Internal` (wire id 22, every stock channel's number unchanged) plus
  `internal:list`/`internal:fetch` schemas; `admit_browser_url` widens S027's
  admission seam so broker-driven navigation reaches my pages while the stock
  web policy still refuses them. 86 new checks (1796 total).

- `bridge/Psycho_bridge.*` (S029, laws done): my preload bridge and its four
  laws. *Isolation:* preload code runs in its own js::Runtime world; the only
  thing that crosses between worlds is JSON-shaped data. *Curation:* the facade
  contains exactly the schema-registered bus methods the broker declared;
  undeclared names don't exist and the dispatcher refuses them even from
  preload code. *Freeze:* the installed window.psycho graph is frozen - engine
  law now, `Object.freeze`/`Object.isFrozen` plus the frozen flag honoured at
  the `set_member` choke point, sloppy-mode silent refusal - so page code
  cannot patch, shadow or extend it. *No direct broker access:* the page world
  has no dispatcher, only trampolines through the bridge. Wire names stay
  colon-shaped on the bus (`settings:get`), dot-shaped in-page
  (`psycho.settings.get`); refusals surface in-page as `BridgeError` carrying
  the machine-readable code. 39 new checks (1835 total).

- `apphost/Psycho_apphost.*` (S030, model done): my app-layer JS host turns
  the layers below into per-page policy. Origin kind sets the curated minimum:
  web pages get NO facade; shell pages get the small frozen UI set - and
  deliberately not vault, proton or extensions (extension-less means exactly
  that); diagnostics pages get an even smaller read-mostly set. Every failure
  becomes a broker log record, never a crash across the boundary: page script
  throws land as `apphost.script` records naming page and label, and a runaway
  script's fuel exhaustion lands as `apphost.bound` - the script dies, the
  broker lives. 27 new checks (1862 total), including cross-host isolation
  proven through a shared broker fakery. Renderer-process wiring belongs to
  the M1 integration slice on top of S022's envelope; this step is the honest
  rules layer it will consume.

Next in P1: S031 (broker services - dialogs, clipboard, notifications, tray,
application menu, context-menu plumbing), then S032 keyboard shortcuts and
S033 IME.
