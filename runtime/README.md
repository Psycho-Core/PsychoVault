# runtime/ - my own my own runtime

P1 runtime components now exist here. The shipping shell uses the runtime singleton lifecycle and now launches a broker-owned pool of two isolated renderer processes, probes each through `RendererProcessClient`, and checks/replaces dead workers from its UI timer. Ordinary browser tabs still use the legacy in-process `Session` path; the P1 exit gate remains open until tab routing and empirical Windows process/security checks are completed.

- `broker/` - window host, tab model, navigation, sessions, permissions, downloads, update service, dialogs, tray, menus.
- `ipc/` - typed channel definitions, schemas, capability handles, the framed message bus.
- `renderer/` - engine-owning worker and private renderer-child entry point compiled into `PsychoVault.exe`; Windows build and live-shell launch remain unverified, and ordinary page routing remains unwired.
- `preload/` - the curated `window.psycho` bridge exposed to UI and internal pages.
- `services/` - network, storage, vault, extension host and proxy service wrappers around `platform/`.
- `tests/` - integration harnesses: IPC round-trip, process supervision, navigation, crash recovery.

What exists now (P1 in progress):

**Integration status:** S035-S040 remain tested runtime contract/service slices,
not claims that every listed platform service is finished. S041's validated
parser and S023's singleton lifecycle are wired into the Windows shell. The
shell now starts two sandboxed `PsychoVault.exe --pv-renderer-child` processes through `RendererProcessFarm`,
obtains an actual engine frame from each over process IPC before showing the
window, and checks the child handles on its UI timer to replace exited workers
(up to three replacements per slot). Page tabs still use the legacy in-process
`Session` path, so ordinary navigation is not yet routed through the farm. The
Windows-only tests now exercise two real child processes, report measured IPC
round-trips, kill/recover a worker, and probe profile-file/socket denial, but
those tests and the Windows executable have not been built or exercised in this
Linux run.
English is the only UI language shipped. The P1 exit gate remains outstanding.

- `ipc/Psycho_bus.*` (S021, done): the framed length-prefixed message bus with
  the 21 legacy channel groups plus runtime channels, capability masks per
  endpoint, per-channel and method/channel schema binding, direction-tagged
  responses, structured error replies that cannot bypass request fields, strict
  unknown-field refusal, bounded inbound queues (byte and message back-pressure)
  and structured errors.
- `broker/Psycho_broker.*` (S020, in-process model done): role registry with
  capability masks per role, the service table, the routing pump with structured
  refusals, and the built-in lifecycle service. `Psycho_renderer_farm.*` now
  owns two real Windows child handles/transports, routes navigation/fetch IPC,
  and replaces exited workers under a three-restart budget. The shell performs
  startup frame probes through the farm. Ordinary production tabs still use
  in-process sessions, and the farm's Windows launch and restart behavior is
  unverified here.
- `renderer/Psycho_renderer_worker.*` is an engine-owning worker over
  `ProcessTransport`: it accepts typed navigation requests, runs a real `Host`
  and engine page, and requires an injected broker fetcher (no socket API in the
  worker). A fragmented in-memory end-to-end test covers broker fetch IPC. The
  Windows shell now launches the standalone child and routes its startup probes
  through `RendererProcessClient`; ordinary tabs are not yet routed to it.
- `tests/Psycho_runtime_tests.cpp` (S021-S041 unit contracts): schema and
  wire validation, capability/back-pressure laws, process supervision,
  navigation/session history, permission decisions, and each runtime contract
  are exercised in `pv-tests`. On Windows the same target additionally starts
  two real same-image renderer children, measures navigations, probes file and
  socket access from the AppContainer, terminates a child, and verifies recovery.
- `tests/Psycho_runtime_integration_tests.cpp` (S042): cross-component checks
  route renderer IPC through the broker into tab/navigation history, then
  connect process death to crash-page/replacement flow and permission decisions
  to notification delivery. Platform/process seams are faked; this is not the
  real-process P1 exit gate.
- `broker/Psycho_renderer_harness.*` and
  `tests/Psycho_renderer_harness_tests.cpp` (S043): every broker renderer starts
  quarantined. Before admission, only the lifecycle echo probe is routable;
  `RendererHarness` reports timed echo, navigation, and crash/recovery checks,
  then admits the endpoint only if all three succeed. Requalification closes
  the gate first, and `invalidate()` closes it after a reported renderer
  failure. Tests exercise a real in-process `WebContents`, `Supervisor`, and
  `CrashService` path, including failed and throwing probes. The process seam is
  still fake: the shell does not currently create renderers through this broker,
  and no production caller wires the qualification adapters or crash
  invalidation. S043 therefore does not prove OS isolation or complete the P1
  exit gate (two real renderers, real replacement, measured production IPC,
  profile-file denial, and arbitrary-socket denial remain unproven). S043
  added 57 checks; S023 fail-closed seam tests added 4 more afterward. The
  S021's direction/schema hardening added 7 checks; S022 fragmented process
  transport and engine-worker round trip add 11; S025 resource/overflow hardening
  added 5. The current suite passes 806 engine checks and 2604 total.

- `process/Psycho_process.*` (S022, supervision core done): the supervisor with
  injected clock - spawn, heartbeat windows, hang detection, kill for cause,
  bounded restarts with a spent-budget retirement, every death attributed with
  a reason. 17 original checks, 860 total at S022. `Psycho_process_win32.cpp`
  is the Windows launch envelope: suspended creation, kill-on-close job,
  restricted token, explicit stdio handle-inheritance allowlist, and pipe
  endpoints. `Psycho_process_transport.*` applies S021 framing, direction,
  capabilities, and schemas across those byte streams; fragmented-stream tests
  measure its round trip. Launch requests a per-user AppContainer identity with
  no network capability; the production renderer farm now calls that launcher.
  Windows-only acceptance code in `Psycho_runtime_tests.cpp` attempts a read of a
  real profile fixture and connects to a real local listening socket from the
  child AppContainer, recording measured navigation IPC timings and replacing a
  terminated process. It is implemented but unrun here; empirical denial and
  restart results are therefore not yet available.

- `app/Psycho_instance.*` (S023, decision core done): the singleton model as an
  auditable state machine - primary takes the lock, a second launch forwards
  its arguments and exits, quitting saves the session exactly once and hands
  the lock back; a failed session save refuses the quit and keeps the app
  running. 11 new checks, 871 total. `Psycho_instance_win32.cpp` is the real
  Windows side: named mutex per profile, bounded named-pipe forwarding on the
  S021 wire shape, partial-I/O handling, startup-race retries, and a listener
  thread with a clean quit path. `shell/Psycho_win_main.cpp` now calls this
  lifecycle before creating the Browser, dispatches forwarded URLs to the
  primary UI thread, blocks close on failed persistence, and releases the lock
  during graceful shutdown. Portable contracts pass; Windows compilation and
  real multi-process behavior remain unverified here.

- `window/Psycho_window.*` (S024, rules done): my BrowserWindow equivalent's
  decisions - creation with clamping into monitor work areas, cross-monitor
  moves (destination monitor resolved first, then clamped), min/max/fullscreen
  with restore geometry kept, single-owner focus with deterministic fallover,
  per-window DPI scale and taskbar identity (`PsychoCore.PsychoVault.Browser`).
  27 new checks, 898 total. `Psycho_window_win32.cpp` contains the Windows
  adapter, but it is not production-wired or verified: `WM_DPICHANGED` currently
  applies the OS suggestion directly, and `Fullscreen` still aliases maximize.
  Do not count the platform mapping as complete; fullscreen style work must also
  integrate with S025 presentation.

- `surface/Psycho_surface.*` (S025, rules done): the presentation contract my
  whole runtime obeys - my engine paints bytes, my broker's registry holds the
  only writable copy, my compositor intersects before it copies so writing
  outside the destination is structurally impossible. Shape-lying frames are
  refused; allocations are capped per-surface, in aggregate, and by count; resize
  clears instead of smearing stale memory; placement clipping uses overflow-safe
  arithmetic; paint order is list order. The original checks covered engine
  Canvas pixels and clipping; new P1 hardening tests exercise the budgets and
  extreme signed placements. `Psycho_surface_win32.cpp` presents the target
  through a top-down DIB section - Windows-only behavior remains unverified here.

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

- `services/Psycho_services.*` (S031, rules done): the broker's service law,
  with every OS touchpoint behind a seam that refuses "…unavailable" until the
  real Windows piece exists. Dialogs: message/web vs picker/shell origin law,
  one modal per window (proven by seam re-entry), lying pickers caught.
  Clipboard: 1 MiB writes, writer on record, reads privileged. Notifications:
  permission then mute then title, the newest 50 remembered, no more. Tray:
  exactly one, tooltips bounded, events route only while alive. Menus: install
  whole or not at all - ids unique menu-wide, accelerators canonicalised
  (modifiers sort, key uppercases) and sibling clashes refused, separators own
  no id, radio exclusivity enforced through dispatch AND programmatic checks,
  256 items is the ceiling. Context menus: providers per kind, the offered set
  is the only legal answer, tokens bounded at 16 - and provider output obeys
  the same menu law (my own test caught the validator skipping dup-checks on
  that path; it checks now, unconditionally). 81 new checks (1943 total).

- `keys/Psycho_keys.*` (S032, rules done): one broker-side shortcut registry,
  so a key means the same thing on every surface. It shares ONE canonical
  accelerator language with my menu service (labels and behaviour cannot
  drift even by case). Bind law refuses unknown key tokens, bare printable
  characters (typing is not a shortcut) and same-scope duplicates. Scopes do
  the honest work: app bindings fire everywhere, editable bindings only while
  a field owns focus - and an editable binding deliberately shadows an app
  binding on the same chord, proven both directions. Resolution is a lookup
  the broker runs first: a claimed chord never reaches page code; an
  unclaimed key always propagates. Unbinding a command wipes every scope.
  33 new checks (1976 total).

- `ime/Psycho_ime.*` (S033, rules done): my IME and dead-key law, end to end
  for the editor surfaces I own - page fields plug the same service in at
  S062. *One composition per surface:* a dead key's pending accent and a
  platform IME's preedit are the same state, so beginning either over the
  other refuses. *Platform text is suspect:* preedit must be strictly valid
  UTF-8 (overlongs, surrogates and truncations refuse), bounded at 4096
  bytes, and the caret must sit on a character boundary inside it - refused
  updates never touch the honest state. *Commit is the only path to text:*
  cancel leaves the field exactly as it was, an empty commit delivers
  nothing, and every committed string crosses the seam exactly once before
  the state dies. *The candidate window follows the caret and stays in the
  window:* hung at the caret's baseline, clamped off the edges, flipping
  above the caret line when the bottom would clip - and with no composition
  in flight, asking where it goes refuses. *A dead key is a pending accent,
  not text:* accent+space gives the accent, a combining letter gives the
  precomposed letter (the full audit table, both cases - five accent tokens,
  acute through diaeresis, into the extended Latin range), anything else
  gives accent plus letter, the
  same accent twice gives the accent, and cancelling drops it honestly. All
  platform touchpoints - preedit sink, commit delivery, caret and window
  rects - sit behind seams that refuse by name until the authorized Windows
  build wires them. 71 new checks (2047 total).

- `focus/Psycho_focus.*` (S034, rules done): my focus and activation law.
  *One answer at every level:* at most one active window, one focused tab
  per window, one focused element per tab - dead nodes answer "nothing" or
  refuse by name, never a guess. *Blur before focus, never twice:* every
  transition announces blur strictly before focus, and re-asking for what
  already holds focus is a silent no-op. *Modality captures its own window:*
  while a dialog is modal, tab switches and element focus inside refuse -
  other windows live free; this is the same fact S031's one-modal-per-window
  dialog service keeps, wired together at the M1 slice. *The dead cannot
  hold focus:* closing the focused tab moves focus to the tab after it in
  open order, else the one before; closing the active window activates the
  most recently opened remaining window, else the app honestly holds no
  focus - and death is not an event, a dying page hears no blur. *Visibility
  and throttling:* a tab is ACTIVE in the active unoccluded window, VISIBLE
  as the focused tab of a see-able window, HIDDEN otherwise - background
  tabs, occluded windows, unknown ids - with timer floors 0 / 250 / 1000 ms
  falling out (policy constants; the tiers are law). 68 new checks (2115
  total).

- `downloads/Psycho_downloads.*` (S035, rules done): my broker download
  service - the law in front of `platform/Psycho_download.*`'s transport,
  which the M1 slice adapts through the seams. *One bounded history:* ids
  monotonic, never reused, every attempt recorded - even a transport refusal
  leaves a Failed record; at most 32 in flight, the newest 200 kept (the cap
  split proves the evicted are always terminal). *A narrow state machine:*
  running <-> paused, then exactly one of complete/cancelled/failed;
  terminals never move, by any verb; save-as chooses the target only while
  bytes can still move. *Pause needs a yes from the wire:* the response's
  latest word on ranges is the word that counts; resume re-enters naming the
  byte already held. *Every byte accounted:* no regress, no overrun, a known
  total never shrinks; a completion short of the total is honestly recorded
  as Failed("download.truncated") without pretending the event didn't
  happen. *Open and reveal touch only what finished:* wrong state, no chosen
  target, and vanished files all refuse by name. Unknown size is legal
  (total 0), the save-as suggestion is sanitized from the URL and never
  trusts it (".." and "(1)" become "download" and "report_1_.pdf"). 105 new
  checks (2220 total).

- `permissions/Psycho_permissions.*` (S036, rules done): my permission
  service. *The legacy defaults are the floor:* the seven kinds of the
  legacy inventory's `defaultSettings.denyPerms` - geolocation, media,
  clipboard-read, midi, sync-xhr, wake-lock, notifications - are exactly the
  known kinds and each defaults to Deny; nothing defaults to Allow; unknown
  kinds refuse `perm.kind`. *Honest origins:* decisions key on normalized
  scheme://host[:port] - uppercase, paths, trailing slashes, bad ports and
  non-http(s) schemes all refuse `perm.origin`; internal pages hold no site
  permissions at all. *The prompt is the only path:* one visible prompt per
  (origin, kind) - re-asking coalesces to the live token instead of stacking
  dialogs; 16 pending is the ceiling; "ask" is not an answer; a remembered
  answer with no profile seam refuses and STAYS pending for the retry
  without remember. *Persistence is exactly what the profile heard:* saves
  and drops cross their seams only for remembered rows; the profile loads
  once, merges last-wins, drops malformed rows. *Private mode is a build
  without the seams:* law 5's proof constructs exactly S037's session store
  and prompts through it. The floor reaches consumers: S031's notification
  law fed by `decide()` keeps the denied denied and the granted heard. 73
  new checks (2293 total).

- `private/Psycho_private.*` (S037, private-session policy): each
  `PrivateSession` owns independent cookie and local-storage maps, plus its
  own `PermissionService` built with no profile load/save/drop seams. State is
  site-scoped and remains in memory only; private permission answers cannot
  be remembered. A separate `RendererPolicy::Private` gives internal shell
  renderers private-tab controls but no profile settings or history facade.
  Private extensions start disabled and can be enabled only through the
  explicit user-switch API; worker launchers must consult `may_run_extensions()`.
  There is no profile path or persistence interface on the private-session
  type. 28 new checks (2321 total).

- `diagnostics/Psycho_diagnostics.*` (S038, diagnostics contract):
  `psychovault://nettest` reports DNS, TCP, TLS, HTTP, proxy and DNS-leak
  checks in that fixed named order. A failed or throwing probe stays attached
  to its stage and never suppresses the later checks; an unwired adapter is a
  visible `diagnostics.unavailable` result, not a green guess. The
  `psychovault://version` contract returns broker-supplied runtime, engine,
  profile, flags and hash facts verbatim, or a named refusal if no provider is
  wired - I never invent a version or hash. Both calls are schema-registered
  and broker-service-backed, exposed only to the diagnostics renderer bridge;
  web and shell pages still receive no diagnostics facade. 29 new checks
  (2350 total).

- `crash/Psycho_crash.*` (S039, crash recovery contract): renderer crashes
  and S022-attributed hangs create bounded, timestamped records; durable
  persistence is attempted before retry, while the dedicated
  `psychovault://crash` page is still shown if writing fails. The crash page has
  its own minimal renderer policy. A retry recreates the tab at its last URL,
  records each state change, and stops after three attempts per tab until the
  replacement is reported stable. Crash history is bounded to 100 records.
  Storage, crash-page navigation and tab recreation are explicit adapters and
  still require the broker's production wiring. 36 new checks (2386 total).

- `logging/Psycho_logging.*` (S040, structured log contract): one JSONL file
  per validated service token, with writes serialized across threads and
  redacted before disk. Sensitive field names,
  URL userinfo and secret query parameters, and inline Basic/Bearer or
  `token=`-style values are masked. The active log defaults to 1 MiB with
  three numbered backups (at most ten); rotation is per service. Support export
  packages only regular logger-owned log files and rotations in a store-only
  ZIP with a manifest, capped at 8 MiB. The logger and export are tested, but
  the runtime services still need production callers. 29 new checks
  (2415 total).

- `commandline/Psycho_commandline.*` (S041): a portable argv parser wired to
  the Windows shell. It accepts up to 64 ordered positional/repeated `--url`
  values, `--profile`, `--private`, `--diagnostics`, and validated English
  `--language` tags, plus the existing smoke/capture harness flags. URLs must be
  HTTP(S), `about:blank`, or registered `psychovault://` pages; controls,
  credentials, whitespace, unregistered schemes/pages, unknown options,
  duplicate singleton options, and missing/oversized values refuse with named
  errors. `--diagnostics` opens the built-in nettest page. The update-only verb
  is separate, validates its PID and can only replace/relaunch this executable;
  ordinary argv is never assembled into a process command. Non-English UI
  languages refuse because no such language pack ships yet. 28 new checks
  (2443 total).

- `tests/Psycho_runtime_integration_tests.cpp` (S042, complete): IPC schema
  rejection and correlated replies, two-renderer isolation, WebContents commits
  with back/forward session history, supervisor kill plus durable crash-page
  and tab-replacement flow, and deny-by-default permission decisions feeding
  notification delivery. 72 new cross-component checks (2515 total). These
  exercise in-process contracts and injected fake platform seams only; they do
  not prove a real OS renderer process, profile-file denial, or socket sandbox.

P1 remains in progress. The next required integration is a real renderer
process connected to the broker's framed IPC, followed by the OS-level crash,
replacement, profile-file denial, and arbitrary-socket denial proofs in the P1
exit gate.
