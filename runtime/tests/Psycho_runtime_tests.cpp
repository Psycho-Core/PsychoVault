#include "../app/Psycho_instance.hpp"
#include "../tabs/Psycho_tabs.hpp"
#include "../content/Psycho_content.hpp"
#include "../surface/Psycho_surface.hpp"
#include "../window/Psycho_window.hpp"
#include "../ipc/Psycho_bus.hpp"
#include "../broker/Psycho_broker.hpp"
#include "../process/Psycho_process.hpp"
#include <iostream>

using namespace pv;
using namespace pv::rt;

namespace {

int rt_passed = 0;

void rt_check(bool condition, const char *text, int line) {
  if (!(condition)) {
    std::cerr << "FAIL runtime test line " << line << ": " << text << "\n";
    throw std::runtime_error("runtime check failed");
  }
  ++rt_passed;
}
#define RT_CHECK(x) rt_check(bool(x), #x, __LINE__)

Envelope nav_go(const std::string &url) {
  Envelope env;
  env.channel = Channel::Nav;
  env.method = "nav:go";
  env.request_id = 1;
  Json payload = Json::make_object();
  payload.set("url", Json(url));
  env.payload = payload;
  return env;
}

} // namespace

// Called from tests/Psycho_test_main.cpp. Throws on the first failure so the
// outer harness reports the same way it does for engine checks.
int run_runtime_bus_tests(int &shared) {
  // Channel identity: my wire numbers are stable because the wire remembers.
  RT_CHECK(channel_name(Channel::Nav) == "nav");
  RT_CHECK(channel_name(Channel::Proton) == "proton");
  RT_CHECK(static_cast<int>(Channel::Tab) == 1);
  RT_CHECK(static_cast<int>(Channel::Lifecycle) == 31);
  RT_CHECK(static_cast<int>(Channel::Translate) == 21); // 21 legacy groups, then internal channels

  // Stock schemas: typing is the contract.
  {
    Schemas s = Schemas::stock();
    Error e;
    Json empty = Json::make_object();
    RT_CHECK(!s.validate("nav:go", empty, e) && e.code == "schema.missing");
    Json wrong = Json::make_object();
    wrong.set("url", Json(3.0));
    RT_CHECK(!s.validate("nav:go", wrong, e) && e.code == "schema.type");
    Json good = Json::make_object();
    good.set("url", Json("https://example.com/"));
    RT_CHECK(s.validate("nav:go", good, e));
    RT_CHECK(!s.validate("extensions:bogus", good, e) && e.code == "schema.unknown-method");
  }

  // Framing: full round trip and split delivery.
  {
    Envelope env = nav_go("https://example.com/");
    FrameStream stream;
    std::string frame = stream.encode(env);
    std::vector<Envelope> got;
    Error e;
    stream.feed(frame, got, e);
    RT_CHECK(!e && got.size() == 1);
    RT_CHECK(got[0].method == "nav:go" && got[0].channel == Channel::Nav && got[0].request_id == 1);
    RT_CHECK(got[0].payload.str("url") == "https://example.com/");

    FrameStream split;
    std::vector<Envelope> partial;
    split.feed(frame.substr(0, 3), partial, e);
    RT_CHECK(!e && partial.empty() && split.buffered() == 3);
    split.feed(frame.substr(3), partial, e);
    RT_CHECK(!e && partial.size() == 1 && partial[0].method == "nav:go");

    std::string two = frame + stream.encode(nav_go("https://example.org/"));
    std::vector<Envelope> both;
    FrameStream batch;
    batch.feed(two, both, e);
    RT_CHECK(!e && both.size() == 2 && both[1].payload.str("url") == "https://example.org/");

    // A frame claiming an impossible length is refused, not trusted.
    std::string evil(4, '\0');
    uint32_t too_big = 64u << 20;
    for (int i = 0; i < 4; ++i)
      evil[size_t(i)] = char((too_big >> (8 * i)) & 0xff);
    FrameStream guard;
    std::vector<Envelope> none;
    guard.feed(evil, none, e);
    RT_CHECK(e.code == "wire.oversize" && none.empty());
  }

  // Link behaviour: capabilities and validation in both directions.
  {
    Schemas s = Schemas::stock();
    Link link(capabilities_for(Role::Renderer), "renderer-a", Capability::all(), "broker");
    link.set_schemas(&s);
    Error e;
    // A renderer has no vault capability. That refusal is authored, not ambient.
    Envelope vault_call;
    vault_call.channel = Channel::Vault;
    vault_call.method = "vault:status";
    vault_call.request_id = 9;
    RT_CHECK(!link.a().send(vault_call, e) && e.code == "capability.denied");
    RT_CHECK(!link.b().recv(vault_call, e)); // nothing arrived to leak

    Envelope ok = nav_go("https://example.com/");
    RT_CHECK(link.a().send(ok, e));
    Envelope arrived;
    RT_CHECK(link.b().recv(arrived, e) && !e);
    RT_CHECK(arrived.method == "nav:go");

    // The receive rule: even a capable peer cannot drop a privileged message
    // into a box whose mask lacks the channel.
    Link tight(Capability::all(), "all", [](Channel c) {
      Capability cap;
      cap.allow = uint64_t(1) << static_cast<uint16_t>(c);
      return cap;
    }(Channel::Nav), "nav-only");
    tight.set_schemas(&s);
    Envelope diag;
    diag.channel = Channel::Diagnostics;
    diag.method = "diagnostics:stage";
    diag.request_id = 4;
    Json dp = Json::make_object();
    dp.set("stage", Json("dns"));
    dp.set("ok", Json(true));
    dp.set("result", Json("fine"));
    diag.payload = dp;
    RT_CHECK(tight.a().send(diag, e));
    Envelope leak;
    RT_CHECK(!tight.b().recv(leak, e) && e.code == "capability.denied");
    Error none;
    RT_CHECK(!tight.b().recv(leak, none) && !none);
  }

  // Back-pressure: both budgets bite, with structured errors.
  {
    Schemas s = Schemas::stock();
    Link link(Capability::all(), "left", Capability::all(), "right");
    link.set_schemas(&s);
    Limits small;
    small.max_queued_bytes = 200;
    small.max_queued_messages = 64;
    link.set_limits(small);
    Error e;
    bool crowded = false;
    for (int i = 0; i < 64; ++i)
      if (!link.a().send(nav_go("https://example.com/" + std::to_string(i)), e))
        crowded = true;
    RT_CHECK(crowded && e.code == "bus.full");

    Link few(Capability::all(), "a", Capability::all(), "b");
    few.set_schemas(&s);
    Limits one;
    one.max_queued_messages = 1;
    few.set_limits(one);
    RT_CHECK(few.a().send(nav_go("https://a.example/"), e));
    RT_CHECK(!few.a().send(nav_go("https://b.example/"), e) && e.code == "bus.full");
  }

  // The broker: routing, lifecycle round-trip, refusal, per-renderer isolation.
  {
    Broker broker;
    int a = broker.spawn_renderer("renderer-1");
    int b = broker.spawn_renderer("renderer-2");

    // Heartbeat: ping becomes pong with the request id preserved.
    Error e;
    Envelope ping;
    ping.channel = Channel::Lifecycle;
    ping.method = "lifecycle:ping";
    ping.request_id = 7;
    ping.payload = Json::make_object();
    RT_CHECK(broker.renderer_side(a).send(ping, e));
    RT_CHECK(broker.pump() == 1);
    Envelope pong;
    RT_CHECK(broker.renderer_side(a).recv(pong, e) && !e);
    RT_CHECK(pong.method == "lifecycle:pong" && pong.request_id == 7);
    RT_CHECK(pong.payload.num("uptime_ms", -1) >= 0);

    // A request to a channel with no service gets a structured refusal with the
    // same id - my caller surfaces a reason instead of hanging.
    Envelope ask = nav_go("https://example.com/");
    ask.request_id = 44;
    RT_CHECK(broker.renderer_side(a).send(ask, e));
    broker.pump();
    Envelope refuse;
    RT_CHECK(broker.renderer_side(a).recv(refuse, e) && !e);
    RT_CHECK(refuse.request_id == 44 &&
             refuse.payload.str("error").find("no service") != std::string::npos);

    // Isolation: renderer two never hears what renderer one asked.
    RT_CHECK(broker.renderer_side(b).pending() == 0);
    RT_CHECK(!broker.log().empty());
  }

  // S022 supervision: the state machine is portable, the world is faked, and
  // every transition is checked: live, hung, restarted, budget spent.
  {
    Clock::time_point now = Clock::now();
    SupervisionPolicy policy;
    policy.heartbeat_timeout = std::chrono::milliseconds(1000);
    policy.max_restarts = 2;
    int launched = 0, killed = 0;
    std::vector<std::string> kill_reasons;
    Supervisor sup(
        policy,
        [&](ChildRecord &record, std::string &error) {
          (void)record;
          (void)error;
          ++launched;
          return true;
        },
        [&](const ChildRecord &record, const std::string &reason, std::string &error) {
          (void)record;
          (void)error;
          ++killed;
          kill_reasons.push_back(reason);
          return true;
        },
        [&] { return now; });
    std::string error;
    int id = sup.spawn("renderer-1", error);
    RT_CHECK(id > 0 && sup.has(id));
    sup.heartbeat(id);
    RT_CHECK(sup.records().front().state == ChildState::Live);

    // Inside the heartbeat window: nothing happens.
    now += std::chrono::milliseconds(500);
    RT_CHECK(sup.sweep().empty());

    // Past the window: hung, killed for cause, restarted - and attributed.
    now += std::chrono::milliseconds(600);
    auto changed = sup.sweep();
    RT_CHECK(changed.size() == 1 && changed[0].state == ChildState::Hung);
    const ChildRecord *rec = &sup.records().front();
    RT_CHECK(rec->state == ChildState::Starting && rec->restarts == 1);
    RT_CHECK(rec->attribution.find("hung") != std::string::npos);
    RT_CHECK(launched == 2 && killed == 1);

    // The restarted child heartbeats and is Live again.
    sup.heartbeat(id);
    RT_CHECK(sup.records().front().state == ChildState::Live);

    // Hang again: restart two of two.
    now += std::chrono::milliseconds(1100);
    sup.sweep();
    rec = &sup.records().front();
    RT_CHECK(rec->state == ChildState::Starting && rec->restarts == 2);

    // Hang a third time with the budget spent: retirement, not a retry loop.
    now += std::chrono::milliseconds(1100);
    sup.sweep();
    rec = &sup.records().front();
    RT_CHECK(rec->state == ChildState::Retired);
    RT_CHECK(rec->attribution.find("restart budget spent") != std::string::npos);
    RT_CHECK(launched == 3); // launch + two restarts, no third attempt

    // Operator kill: attribution records who and why.
    int other = sup.spawn("renderer-2", error);
    sup.heartbeat(other);
    std::string kill_error;
    RT_CHECK(sup.request_kill(other, "user closed the tab", kill_error));
    RT_CHECK(sup.records()[size_t(other - 1)].attribution == "killed: user closed the tab");

    // Unknown ids are no-ops, never crashes.
    sup.heartbeat(999);
    RT_CHECK(!sup.request_kill(999, "ghost", error));

    // The platform seam answers honestly here: no process table on Linux.
    WinChildHandle fake;
    std::string platform_error;
    RT_CHECK(!spawn_child_platform("probe", "pv-renderer.exe", fake, platform_error));
    RT_CHECK(platform_error == "process.spawn.unsupported");
  }

  // S023 the singleton: primary takes the lock, a second launch forwards and
  // exits, quitting saves the session exactly once and hands the lock back.
  {
    bool lock_held = false, released = false;
    int saves = 0;
    std::vector<std::string> delivered;
    auto make = [&](std::optional<bool> forced) {
      AppInstance::Seams seams;
      seams.acquire_lock = [&, forced](std::string &error) -> std::optional<bool> {
        (void)error;
        if (forced)
          return forced;
        if (lock_held)
          return false;
        lock_held = true;
        return true;
      };
      seams.release_lock = [&] {
        lock_held = false;
        released = true;
      };
      seams.forward = [&](const LaunchRequest &request, std::string &error) {
        (void)error;
        delivered.insert(delivered.end(), request.args.begin(), request.args.end());
        return true;
      };
      seams.save_session = [&](std::string &error) {
        (void)error;
        ++saves;
        return true;
      };
      seams.on_forwarded = [&](const LaunchRequest &request) {
        delivered.insert(delivered.end(), request.args.begin(), request.args.end());
      };
      return seams;
    };

    // First launch is primary.
    LaunchRequest first{{"--private", "https://example.com/"}};
    AppInstance a(make(std::nullopt));
    std::string error;
    RT_CHECK(a.start(first, error) == AppInstance::Start::Primary && a.running());

    // Second launch: arguments go to the primary, and I exit.
    LaunchRequest second{{"https://example.org/"}};
    AppInstance b(make(std::nullopt));
    RT_CHECK(b.start(second, error) == AppInstance::Start::ForwardedAndExit);
    RT_CHECK(delivered.size() == 1 && delivered[0] == "https://example.org/");

    // The primary's inbox receives the forwarded request.
    a.receive_forwarded(second);
    RT_CHECK(delivered.size() == 2 && delivered.back() == "https://example.org/");

    // Start on a running instance is a no-op primary, not a second process.
    RT_CHECK(a.start(first, error) == AppInstance::Start::Primary);

    // Graceful quit: one save, lock released, quitting again is already true.
    RT_CHECK(a.quit(error) && saves == 1 && released);
    RT_CHECK(a.quit(error) && saves == 1);

    // After the quit, the next start is primary again.
    AppInstance c(make(std::nullopt));
    RT_CHECK(c.start(first, error) == AppInstance::Start::Primary);
    c.quit(error);

    // A lock that cannot even be evaluated is a structured failure, not a guess.
    AppInstance::Seams broken = make(std::nullopt);
    broken.acquire_lock = [](std::string &error) -> std::optional<bool> {
      error = "mutex denied";
      return std::nullopt;
    };
    AppInstance d(broken);
    RT_CHECK(d.start(first, error) == AppInstance::Start::LockFailed && error == "mutex denied");

    // Quit refusing rollback: a failed save keeps the instance running.
    AppInstance::Seams stubborn = make(std::nullopt);
    stubborn.save_session = [](std::string &error) {
      error = "disk full";
      return false;
    };
    AppInstance e(stubborn);
    RT_CHECK(e.start(first, error) == AppInstance::Start::Primary);
    RT_CHECK(!e.quit(error) && error == "disk full" && e.running());
  }

  // S024 the window model: geometry clamps, focus rules, state restore and
  // taskbar identity - every rule decided here, applied through a fake seam.
  {
    std::vector<WindowRecord> applied;
    WindowRules::Seams seams;
    seams.monitors = [] {
      return std::vector<WindowRules::Monitor>{{"primary", {0, 0, 1920, 1040}},
                                               {"side", {1920, 0, 1600, 900}}};
    };
    seams.apply = [&](const WindowRecord &record) { applied.push_back(record); };
    WindowRules rules(seams);
    std::string error;

    // Creation clamps into the monitor's work area and carries my identity.
    int one = rules.create("PsychoVault", {100, 100, 800, 600}, error);
    const WindowRecord *rec = rules.find(one);
    RT_CHECK(rec && rec->bounds == (WinRect{100, 100, 800, 600}) &&
             rec->monitor_id == "primary" && !rec->taskbar_id.empty());
    RT_CHECK(rules.taskbar_identity() == "PsychoCore.PsychoVault.Browser");

    // A window asked past the edge is pulled fully into the work area.
    RT_CHECK(rules.move_resize(one, {1800, 900, 900, 400}));
    rec = rules.find(one);
    RT_CHECK(rec->bounds.x + rec->bounds.w <= 1920 && rec->bounds.y + rec->bounds.h <= 1040);

    // Too small grows to my minimums instead of wallowing.
    RT_CHECK(rules.move_resize(one, {50, 50, 10, 10}));
    rec = rules.find(one);
    RT_CHECK(rec->bounds.w == 320 && rec->bounds.h == 200 && rec->state == WindowState::Normal);

    // Maximise remembers where to come back to; restore returns exactly.
    WinRect before = rec->bounds;
    RT_CHECK(rules.maximize(one));
    rec = rules.find(one);
    RT_CHECK(rec->state == WindowState::Maximized && rec->bounds == (WinRect{0, 0, 1920, 1040}));
    // Geometry keys are refused while maximised: geometry belongs to the state.
    RT_CHECK(!rules.move_resize(one, {10, 10, 500, 500}));
    RT_CHECK(rules.restore(one));
    rec = rules.find(one);
    RT_CHECK(rec->state == WindowState::Normal && rec->bounds == before);

    // Fullscreen takes the monitor's whole work area, restore brings me back.
    RT_CHECK(rules.fullscreen(one));
    rec = rules.find(one);
    RT_CHECK(rec->state == WindowState::Fullscreen && rec->bounds.w == 1920);
    RT_CHECK(rules.restore(one));
    RT_CHECK(rules.find(one)->bounds == before);

    // A window pushed onto the second monitor adopts its identity.
    RT_CHECK(rules.move_resize(one, {2000, 100, 700, 500}));
    rec = rules.find(one);
    RT_CHECK(rec->monitor_id == "side" && rec->bounds.x + rec->bounds.w <= 3520);

    // Focus is single-owner; focus on a minimised window is refused.
    int two = rules.create("Second", {200, 200, 640, 480}, error);
    RT_CHECK(rules.focus(one));
    RT_CHECK(rules.find(one)->focused && !rules.find(two)->focused);
    RT_CHECK(rules.minimize(two));
    RT_CHECK(!rules.focus(two));

    // Destroying the focused window falls focus to the survivor deterministically.
    RT_CHECK(rules.focus(two) == false);
    RT_CHECK(rules.restore(two) && rules.focus(two));
    rules.destroy(two);
    RT_CHECK(rules.find(two) == nullptr && rules.find(one)->focused);

    // DPI changes land as scale on the record; nonsense scales are refused.
    rules.dpi_changed(one, 1.5);
    RT_CHECK(rules.find(one)->dpi_scale == 1.5);
    rules.dpi_changed(one, 0);
    RT_CHECK(rules.find(one)->dpi_scale == 1.5);

    // Every accepted mutation reached the platform seam in order.
    RT_CHECK(applied.size() >= 10 && applied.back().id == one);
  }

  // S025 surface presentation: registry ownership rules, present contracts,
  // compositor clipping and paint order - with real engine paint as payload.
  {
    SurfaceRegistry registry;
    Error error;
    auto px_at = [](const Surface &s, int x, int y) {
      size_t at = (size_t(y) * size_t(s.width) + size_t(x)) * 4;
      return uint32_t(s.pixels[at]) | uint32_t(s.pixels[at + 1]) << 8 |
             uint32_t(s.pixels[at + 2]) << 16 | uint32_t(s.pixels[at + 3]) << 24;
    };

    // Creation and the shape rule: a lying frame count is a refusal.
    int first = registry.create(20, 20, "renderer-1", error);
    RT_CHECK(first > 0 && registry.find(first)->owner == "renderer-1");
    RT_CHECK(registry.find(first)->generation == 0);
    RT_CHECK(!registry.present(first, Bytes(100, 0xaa), 20, 20, error) &&
             error.code == "surface.shape");
    Bytes full(20 * 20 * 4, 0xff);
    RT_CHECK(registry.present(first, full, 20, 20, error) && !error);
    RT_CHECK(registry.find(first)->generation == 1);

    // Real engine paint as the payload: my Canvas draws, my registry adopts it.
    Canvas painted(20, 20, 0xff102030);
    painted.fill(Rect{4, 4, 8, 8}, 0xff3355ee);
    RT_CHECK(registry.present(first, painted.pixels, 20, 20, error));
    RT_CHECK(registry.find(first)->generation == 2);

    // Compositing: the painted pixel arrives at the same place in the target.
    Surface target;
    target.width = 40;
    target.height = 40;
    target.pixels.assign(40 * 40 * 4, 0);
    Compositor compositor;
    Error composite_error;
    RT_CHECK(compositor.composite(registry, {{first, 0, 0}}, target, composite_error));
    uint32_t engine_color = px_at(target, 6, 6);
    uint32_t canvas_background = px_at(target, 2, 2);
    uint32_t untouched_target = px_at(target, 39, 39);
    RT_CHECK(engine_color == 0xff3355ee);
    RT_CHECK(canvas_background == 0xff102030); // my engine's fill colour, copied
    RT_CHECK(untouched_target == 0);           // outside the source stays the target's

    // Paint order: later entries draw over earlier ones on overlap.
    int second = registry.create(20, 20, "renderer-2", error);
    Canvas top_coat(20, 20, 0xff010203);
    top_coat.fill(Rect{4, 4, 8, 8}, 0xff00ff00);
    RT_CHECK(registry.present(second, top_coat.pixels, 20, 20, error));
    RT_CHECK(compositor.composite(registry, {{first, 0, 0}, {second, 0, 0}}, target,
                                  composite_error));
    RT_CHECK(px_at(target, 6, 6) == 0xff00ff00);
    RT_CHECK(compositor.composite(registry, {{second, 0, 0}, {first, 0, 0}}, target,
                                  composite_error));
    RT_CHECK(px_at(target, 6, 6) == 0xff3355ee);

    // Clipping: an off-edge placement writes nothing outside the destination.
    Surface small;
    small.width = 10;
    small.height = 10;
    small.pixels.assign(10 * 10 * 4, 0);
    RT_CHECK(compositor.composite(registry, {{second, -15, 6}}, small, composite_error));
    // Source columns 15-19 land in target columns 0-4: that sliver paints, the
    // rest of the target is untouched - proven both ways.
    RT_CHECK(px_at(small, 2, 8) == 0xff010203);
    RT_CHECK(px_at(small, 4, 9) == 0xff010203);
    RT_CHECK(px_at(small, 5, 9) == 0); // one pixel past the intersection: mine, not his
    RT_CHECK(px_at(small, 9, 5) == 0);

    // Missing surfaces and malformed targets are structured refusals.
    RT_CHECK(!compositor.composite(registry, {{999, 0, 0}}, target, composite_error) &&
             composite_error.code == "composite.missing");
    Surface malformed;
    malformed.width = 5;
    malformed.height = 5;
    malformed.pixels.assign(10, 0);
    RT_CHECK(!compositor.composite(registry, {{first, 0, 0}}, malformed, composite_error) &&
             composite_error.code == "composite.target");

    // Registry lifecycle: resize clears, destroy removes.
    RT_CHECK(registry.resize(first, 4, 4, error));
    RT_CHECK(registry.find(first)->width == 4 && registry.find(first)->pixels[3] == 0);
    RT_CHECK(registry.destroy(first) && registry.find(first) == nullptr);
  }


  // S026 tab model: ordered tab lists per window, the pin-prefix law, groups
  // that never split windows or families, discard/revive accounting, moves at
  // every scope, and a bounded LIFO recently-closed stack.
  {
    TabModel tabs;
    Error e;
    RT_CHECK(tabs.open_tab(7, "ghost", -1, e) == -1 && e.code == "tabs.window-missing");
    RT_CHECK(tabs.attach_window(1, e) && tabs.attach_window(2, e));
    RT_CHECK(!tabs.attach_window(1, e) && e.code == "tabs.window-exists");

    // Insertion indices: front, end, middle; the first tab is the active one.
    RT_CHECK(tabs.open_tab(1, "alpha", -1, e) == 1);   // alpha  -> [1]
    RT_CHECK(tabs.open_tab(1, "beta", 0, e) == 2);     // beta   -> [2,1]
    RT_CHECK(tabs.open_tab(1, "gamma", -1, e) == 3);   // gamma  -> [2,1,3]
    RT_CHECK(tabs.open_tab(1, "delta", 1, e) == 4);    // delta  -> [2,4,1,3]
    RT_CHECK(tabs.order(1) == std::vector<int>({2, 4, 1, 3}));
    RT_CHECK(tabs.active_in(1)->id == 1);

    // The pin law: pins are a prefix. Pinning moves to the pin-block end.
    RT_CHECK(tabs.set_pinned(4, true, e)); // [4,2,1,3]
    RT_CHECK(tabs.set_pinned(1, true, e)); // [4,1,2,3]
    RT_CHECK(tabs.order(1) == std::vector<int>({4, 1, 2, 3}));
    // A misplaced index never refuses - it clamps into the legal family region
    // on BOTH sides of the boundary.
    RT_CHECK(tabs.move_within_window(1, 3, 0, e));   // unpinned to 0 clamps to 2: [4,1,3,2]
    RT_CHECK(tabs.order(1) == std::vector<int>({4, 1, 3, 2}));
    RT_CHECK(tabs.move_within_window(1, 4, 3, e));   // pinned to 3 clamps to 1: [1,4,3,2]
    RT_CHECK(tabs.order(1) == std::vector<int>({1, 4, 3, 2}));
    // Unpinning lands at the start of the unpinned region - after the one
    // surviving pin (delta), never before it. Re-unpin is a no-op.
    RT_CHECK(tabs.set_pinned(1, false, e) && tabs.set_pinned(1, false, e));
    RT_CHECK(tabs.order(1) == std::vector<int>({4, 1, 3, 2}) && !tabs.find(1)->pinned);
    RT_CHECK(!tabs.move_within_window(1, 999, 0, e) && e.code == "tabs.missing");
    RT_CHECK(!tabs.move_within_window(9, 1, 0, e) && e.code == "tabs.window-missing");
    RT_CHECK(!tabs.move_within_window(2, 4, 0, e) && e.code == "tabs.wrong-window");

    // Mute, and the discard constitution: the active tab refuses to sleep;
    // activating a sleeper revives it and the generation counter tells.
    RT_CHECK(!tabs.discard(1, e) && e.code == "tab.discard-active");
    RT_CHECK(tabs.discard(2, e) && tabs.find(2)->state == TabState::Discarded);
    RT_CHECK(tabs.activate(2, e));
    RT_CHECK(tabs.find(2)->state == TabState::Live && tabs.find(2)->generation == 1);
    RT_CHECK(tabs.set_muted(2, true, e) && tabs.find(2)->muted);

    // Closing the active last tab hands activity to the new last one.
    RT_CHECK(tabs.close_tab(2, e)); // [1,4,3], active beta was last
    RT_CHECK(tabs.active_in(1)->id == 3);
    RT_CHECK(tabs.closed().size() == 1 && tabs.closed().back().label == "beta");

    // Groups: never straddle the pin boundary, never span windows, refuse to
    // double up, and their members become contiguous in relative order.
    RT_CHECK(tabs.set_pinned(4, true, e)); // [4,1,3]
    RT_CHECK(tabs.group_tabs({4, 3}, "mix", "red", e) == -1 && e.code == "group.pin-split");
    int work = tabs.group_tabs({3, 1}, "Work", "teal", e);
    RT_CHECK(work > 0);
    RT_CHECK(tabs.order(1) == std::vector<int>({4, 1, 3}) &&
             tabs.find(1)->group_id == work && tabs.find(3)->group_id == work);
    RT_CHECK(tabs.find_group(work)->name == "Work" &&
             tabs.find_group(work)->window_id == 1);
    RT_CHECK(tabs.group_tabs({1}, "again", "red", e) == -1 && e.code == "group.duplicate");
    RT_CHECK(tabs.open_tab(2, "echo", -1, e) == 5);
    // (alpha would refuse as a duplicate first; echo + delta proves the
    // window law with both members otherwise legal.)
    RT_CHECK(tabs.group_tabs({5, 4}, "split", "red", e) == -1 &&
             e.code == "group.window-split");
    RT_CHECK(tabs.set_group(work, "Deep Work", "blue", e) &&
             tabs.find_group(work)->name == "Deep Work");

    // A group travels as one block. The window's active tab was inside the
    // block - the successor takes over without any sleeper waking.
    RT_CHECK(tabs.move_group_to_window(work, 2, 0, e)); // w1 [4], w2 [1,3,5]
    RT_CHECK(tabs.active_in(1)->id == 4);
    RT_CHECK(tabs.order(2) == std::vector<int>({1, 3, 5}));
    RT_CHECK(tabs.find_group(work)->window_id == 2);
    RT_CHECK(!tabs.move_to_window(1, 1, -1, e) && e.code == "tabs.grouped-move");

    // One ungrouped tab crosses windows; its old window repairs its active tab.
    RT_CHECK(tabs.move_to_window(5, 1, -1, e)); // w1 [4,5], w2 [1,3]
    RT_CHECK(tabs.order(1) == std::vector<int>({4, 5}));
    RT_CHECK(tabs.active_in(2)->id == 3);

    // Window merge: pins and unpins keep their families, order is preserved,
    // the source ends up empty and that is the only state that detaches.
    RT_CHECK(tabs.attach_window(3, e));
    RT_CHECK(tabs.open_tab(3, "kilo", -1, e) == 6);
    RT_CHECK(tabs.set_pinned(6, true, e)); // w3 [6(pinned)]
    RT_CHECK(tabs.move_window_to_window(1, 3, -1, e));
    RT_CHECK(tabs.order(3) == std::vector<int>({6, 4, 5}));
    RT_CHECK(tabs.order(1).empty() && tabs.active_in(1) == nullptr);
    RT_CHECK(tabs.find(4)->window_id == 3);
    RT_CHECK(!tabs.move_window_to_window(1, 1, -1, e) && e.code == "tabs.same-window");
    RT_CHECK(tabs.detach_window(1, e));
    RT_CHECK(!tabs.detach_window(3, e) && e.code == "tabs.window-busy");

    // Closing one of two group members keeps the group; the last member's
    // close evaporates it (an empty group is not a thing).
    RT_CHECK(tabs.close_tab(1, e)); // alpha leaves group w2 [3]
    RT_CHECK(tabs.find_group(work) != nullptr);
    RT_CHECK(tabs.close_tab(3, e)); // gamma: last member, group evaporates
    RT_CHECK(tabs.find_group(work) == nullptr && tabs.active_in(2) == nullptr);
    RT_CHECK(tabs.closed().size() == 3); // beta, alpha, gamma

    // LIFO restore: gamma comes back first, its dead group restored from the
    // snapshot under a FRESH id (history ids are never resurrected).
    int gamma2 = tabs.reopen_last(e);
    RT_CHECK(gamma2 == 7);
    RT_CHECK(tabs.active_in(2)->id == 7 && tabs.find(7)->generation == 0);
    RT_CHECK(tabs.find(7)->group_id != -1 && tabs.find(7)->group_id != work);
    RT_CHECK(tabs.find_group(tabs.find(7)->group_id)->name == "Deep Work");
    int alpha2 = tabs.reopen_last(e);
    RT_CHECK(alpha2 == 8 && tabs.order(2) == std::vector<int>({8, 7}));
    RT_CHECK(tabs.closed().size() == 1); // beta stays: its window is gone
    RT_CHECK(tabs.reopen_last(e) == -1 && e.code == "tabs.closed-window-missing");
    RT_CHECK(tabs.closed().size() == 1); // refusal never drops memory

    // A discarded tab that reopens wakes and the generation tells the story.
    RT_CHECK(!tabs.discard(8, e) && e.code == "tab.discard-active");
    RT_CHECK(tabs.activate(7, e));
    RT_CHECK(tabs.discard(8, e));
    RT_CHECK(tabs.close_tab(8, e));
    int alpha3 = tabs.reopen_last(e);
    RT_CHECK(tabs.find(alpha3)->state == TabState::Live &&
             tabs.find(alpha3)->generation == 1);
    RT_CHECK(tabs.active_in(2)->id == alpha3);
  }

  // The closed stack is bounded: memory of what I closed is capped at 25.
  {
    TabModel bound;
    Error e;
    bound.attach_window(9, e);
    for (int i = 0; i < 30; ++i) {
      int id = bound.open_tab(9, "t" + std::to_string(i), -1, e);
      RT_CHECK(bound.close_tab(id, e));
    }
    RT_CHECK(bound.closed().size() == 25);
    RT_CHECK(bound.closed().front().tab_id == 6); // oldest five fell off
  }


  // S027 WebContents equivalent: navigation controller with committed session
  // history, canonical URL policy, bounded audit memory and Tab-channel
  // notifications that all validate against the stock registry.
  {
    std::vector<Envelope> told;
    auto recorder = [&told](const Envelope &env) { told.push_back(env); };
    WebContents wc(11, 41, {recorder});
    const Schemas schemas = Schemas::stock();
    Error e;

    // Refusals change nothing: no state, no history, no envelopes.
    RT_CHECK(wc.go("   ", e) == -1 && e.code == "nav.empty");
    RT_CHECK(wc.go("ftp://files.example/x", e) == -1 && e.code == "nav.scheme");
    RT_CHECK(wc.go("https:///missing", e) == -1 && e.code == "nav.host");
    RT_CHECK(wc.state() == ContentState::Idle && told.empty());
    RT_CHECK(wc.go_back(e) == -1 && e.code == "nav.bounds");
    RT_CHECK(wc.reload(e) == -1 && e.code == "nav.no-entry");
    RT_CHECK(!wc.set_title("ghost", e) && e.code == "nav.no-entry");
    RT_CHECK(!wc.fail("net.dns", "no pending", e) && e.code == "nav.no-pending");
    RT_CHECK(!wc.commit("ghost", "", e) && e.code == "nav.no-pending");

    // First navigation: whitespace trimmed, scheme and host canonicalised.
    int n1 = wc.go("  HTTPS://Example.COM/Path?q=1  ", e);
    RT_CHECK(n1 == 1);
    RT_CHECK(wc.state() == ContentState::Loading);
    RT_CHECK(wc.pending() && wc.pending()->url == "https://example.com/Path?q=1");
    RT_CHECK(told.size() == 2 && told[0].method == "tab:loading" &&
             told[1].method == "tab:state");
    RT_CHECK(told[0].payload.find("kind")->string == "new" &&
             told[0].payload.find("contents")->number == 11);
    RT_CHECK(wc.commit("Example", "fav:a", e));
    RT_CHECK(wc.history().size() == 1 && wc.index() == 0);
    RT_CHECK(wc.current()->title == "Example" && wc.current()->favicon == "fav:a" &&
             wc.current()->navigation == 1);
    // Commit fan-out order: url, then title, then favicon, then state idle.
    RT_CHECK(told.size() == 6 && told[2].method == "tab:url" &&
             told[3].method == "tab:title" && told[4].method == "tab:favicon" &&
             told[5].method == "tab:state");
    RT_CHECK(told[2].payload.find("entry")->number == 1);
    // Every notification I have ever sent validates against the stock schemas.
    for (const Envelope &env : told) {
      RT_CHECK(env.channel == Channel::Tab && env.request_id == 0);
      RT_CHECK(schemas.validate(env.method, env.payload, e));
    }

    // A, B committed; back revisits A and refreshes its title in place.
    RT_CHECK(wc.go("https://two.example/", e) == 2);
    RT_CHECK(wc.commit("Two", "", e));
    RT_CHECK(wc.index() == 1 && wc.can_go_back() && !wc.can_go_forward());
    RT_CHECK(wc.go_back(e) == 3);
    RT_CHECK(wc.pending()->kind == NavKind::Back &&
             wc.pending()->url == "https://example.com/Path?q=1");
    RT_CHECK(wc.commit("Example Again", "", e));
    RT_CHECK(wc.history().size() == 2 && wc.index() == 0);
    RT_CHECK(wc.history()[0].id == 1 && wc.history()[0].title == "Example Again");

    // User stop: the pending navigation becomes a corpse in the log, the real
    // (committed) position never moved, and Idle is announced.
    RT_CHECK(wc.go_forward(e) == 4);
    RT_CHECK(wc.stop(e));
    RT_CHECK(wc.log()[3].status == NavStatus::Aborted && wc.index() == 0);
    RT_CHECK(wc.state() == ContentState::Idle && told.back().method == "tab:state");
    RT_CHECK(wc.stop(e)); // idempotent: stopping twice is not an error

    // Forward commit with empty metadata touches nothing.
    RT_CHECK(wc.go_forward(e) == 5);
    RT_CHECK(wc.commit("", "", e));
    RT_CHECK(wc.index() == 1 && wc.history()[1].title == "Two");

    // Reload re-renders the same entry: no new entry, same id, metadata may
    // refresh, the originating navigation stays the one that committed me.
    RT_CHECK(wc.reload(e) == 6);
    RT_CHECK(wc.pending()->kind == NavKind::Reload &&
             wc.pending()->target_index == 1);
    RT_CHECK(wc.commit("Two Reloaded", "fav:two", e));
    RT_CHECK(wc.history().size() == 2 && wc.history()[1].id == 2);
    RT_CHECK(wc.history()[1].title == "Two Reloaded" &&
             wc.history()[1].favicon == "fav:two" && wc.history()[1].navigation == 2);

    // New navigation from a rewound position kills the forward tail AT COMMIT,
    // never before: going back, then somewhere else, severs B from history.
    RT_CHECK(wc.go_back(e) == 7 && wc.commit("", "", e) && wc.index() == 0);
    RT_CHECK(wc.go("https://c.example/", e) == 8 && wc.commit("C", "", e));
    RT_CHECK(wc.history().size() == 2 && wc.index() == 1);
    RT_CHECK(wc.history()[1].url == "https://c.example/" && !wc.can_go_forward());

    // Failure: the state machine says Failed, the log carries the code, and a
    // failed navigation never rewrites the past.
    RT_CHECK(wc.go("https://dead.example/", e) == 9);
    RT_CHECK(wc.fail("net.dns", "the name does not resolve", e));
    RT_CHECK(wc.state() == ContentState::Failed && wc.error_code() == "net.dns");
    RT_CHECK(wc.history().size() == 2 && wc.index() == 1);
    RT_CHECK(wc.log()[8].status == NavStatus::Failed &&
             wc.log()[8].error_code == "net.dns");
    RT_CHECK(told[told.size() - 2].method == "tab:error" &&
             told.back().payload.find("state")->string == "failed");

    // One in flight at a time: starting E aborts D... and loading F while E is
    // pending aborts E. The aborted navigation stays in the audit log.
    RT_CHECK(wc.go("https://e.example/", e) == 10);
    RT_CHECK(wc.go("https://f.example/", e) == 11);
    RT_CHECK(wc.log()[9].status == NavStatus::Aborted);
    RT_CHECK(wc.pending() && wc.pending()->url == "https://f.example/");
    RT_CHECK(wc.commit("F", "", e));
    RT_CHECK(wc.history().size() == 3 && wc.index() == 2 && !wc.can_go_forward());
    RT_CHECK(wc.go_forward(e) == -1 && e.code == "nav.bounds");

    // Live edits: script retitles and reicons the committed entry it lives on.
    RT_CHECK(wc.set_title("F mk2", e) && wc.set_favicon("fav:f2", e));
    RT_CHECK(wc.current()->title == "F mk2" && wc.current()->favicon == "fav:f2");
    RT_CHECK(told[told.size() - 2].method == "tab:title" &&
             told.back().method == "tab:favicon");
  }

  // Bounded memory, both ledgers: history keeps the newest 100 entries and the
  // audit log keeps the newest 256 navigations; nothing grows without limit.
  {
    WebContents deep(12, 42, {});
    Error e;
    for (int i = 0; i < 300; ++i) {
      RT_CHECK(deep.go("https://cap.example/" + std::to_string(i), e) > 0);
      RT_CHECK(deep.commit("cap", "", e));
    }
    RT_CHECK(deep.history().size() == 100);
    RT_CHECK(deep.history().front().url == "https://cap.example/200");
    RT_CHECK(deep.history().back().url == "https://cap.example/299");
    RT_CHECK(deep.log().size() == 256 && deep.log().front().id == 45);
    RT_CHECK(deep.can_go_back() && !deep.can_go_forward());
    RT_CHECK(deep.go_back(e) > 0 && deep.commit("", "", e));
    RT_CHECK(deep.index() == 98 && deep.current()->url == "https://cap.example/298");
  }

  shared += rt_passed;
  return 0;
}
