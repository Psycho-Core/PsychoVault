#include "../app/Psycho_instance.hpp"
#include "../tabs/Psycho_tabs.hpp"
#include "../content/Psycho_content.hpp"
#include "../internal/Psycho_internal.hpp"
#include "../bridge/Psycho_bridge.hpp"
#include "../apphost/Psycho_apphost.hpp"
#include "../services/Psycho_services.hpp"
#include "../keys/Psycho_keys.hpp"
#include "../../engine/Psycho_js.hpp"
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


  // S028 internal scheme: the registry, canonicalisation, the three rules -
  // registry-only pages, one-origin-per-page, web gets nothing by default -
  // and the widened admission seam for broker-driven navigation.
  {
    InternalRegistry registry = InternalRegistry::stock();
    InternalGate gate(registry);
    const Schemas schemas = Schemas::stock();
    Error e;

    // The channel exists with a stable wire number and the two conversations
    // validate - and no renderer-role mask holds the Internal bit by default.
    RT_CHECK(channel_name(Channel::Internal) == "internal");
    RT_CHECK(static_cast<int>(Channel::Internal) == 22);
    RT_CHECK(!capabilities_for(Role::Renderer).allows(Channel::Internal));
    RT_CHECK(!capabilities_for(Role::Network).allows(Channel::Internal));
    RT_CHECK(capabilities_for(Role::Broker).allows(Channel::Internal));
    RT_CHECK(schemas.validate("internal:list", Json::make_object(), e));
    Json fetch = Json::make_object();
    fetch.set("url", Json("psychovault://newtab/"));
    RT_CHECK(schemas.validate("internal:fetch", fetch, e));
    Json fetchless = Json::make_object();
    RT_CHECK(!schemas.validate("internal:fetch", fetchless, e) &&
             e.code == "schema.missing");

    // The stock registry: my nine pages, fixed address space, all web-sealed.
    const char *names[] = {"newtab",  "settings", "history", "downloads",
                           "extensions", "apps",  "version", "nettest", "crash"};
    RT_CHECK(registry.pages().size() == 9);
    for (const char *name : names) RT_CHECK(registry.find(name) != nullptr);
    RT_CHECK(registry.find("newtab")->category == "shell");
    RT_CHECK(registry.find("nettest")->category == "diagnostics");
    for (const InternalPage &page : registry.pages()) RT_CHECK(!page.web_reachable);

    // Canonicalisation: case folds, a lone root slash vanishes, query and
    // fragment ride along, and every refusal names its law.
    InternalUrl url;
    RT_CHECK(registry.parse("  PSYCHOVAULT://NewTab/  ", url, e));
    RT_CHECK(url.page == "newtab" && url.path.empty() &&
             url.canon == "psychovault://newtab");
    RT_CHECK(registry.parse("psychovault://settings/app.css", url, e) &&
             url.path == "/app.css" &&
             url.canon == "psychovault://settings/app.css");
    RT_CHECK(registry.parse("psychovault://version?full#build", url, e) &&
             url.canon == "psychovault://version?full#build");
    RT_CHECK(!registry.parse("psychovault://", url, e) &&
             e.code == "internal.page-required");
    RT_CHECK(!registry.parse("psychovault://Set Tings", url, e) &&
             e.code == "internal.page-name");
    RT_CHECK(!registry.parse("psychovault://-dash", url, e) &&
             e.code == "internal.page-name");
    RT_CHECK(!registry.parse("psychovault://nope", url, e) &&
             e.code == "internal.unknown-page");
    RT_CHECK(!registry.parse("https://example.com/", url, e) &&
             e.code == "internal.scheme");
    RT_CHECK(!registry.parse("psychovault://settings/../history", url, e) &&
             e.code == "internal.traversal");
    RT_CHECK(!registry.parse("psychovault://settings/..", url, e) &&
             e.code == "internal.traversal");
    RT_CHECK(!registry.parse("psychovault://settings/%2e%2e/", url, e) &&
             e.code == "internal.escape");
    RT_CHECK(!registry.parse("psychovault://settings/a\\b", url, e) &&
             e.code == "internal.escape");

    // The gate: capability first, existence second - a right-less probe learns
    // nothing about what exists. No stock page is web-embeddable even WITH
    // the capability. Web top-level navigation into my scheme: refused always.
    RT_CHECK(registry.parse("psychovault://newtab", url, e));
    RT_CHECK(!gate.web_fetch(url, Capability{}, e) && e.code == "internal.rights");
    RT_CHECK(!gate.web_fetch(url, Capability::all(), e) && e.code == "internal.embed");
    RT_CHECK(!gate.web_navigate(url, e) && e.code == "internal.web-navigate");
    RT_CHECK(gate.broker_navigate(url, e));
    InternalUrl ghost;
    registry.parse("psychovault://newtab", ghost, e);
    ghost.page = "ghost"; // parse never yields this; the gate still defends it
    RT_CHECK(!gate.web_fetch(ghost, Capability::all(), e) &&
             e.code == "internal.unknown-page");
    RT_CHECK(!gate.broker_navigate(ghost, e) && e.code == "internal.unknown-page");

    // The AND-law both ways, proven on a page that opted in: reachable with
    // the capability, still refused without it.
    InternalRegistry custom;
    custom.add({"badge", "shell", "an opted-in embeddable", true});
    InternalGate custom_gate(custom);
    InternalUrl badge;
    RT_CHECK(custom.parse("psychovault://badge/icon.png", badge, e));
    RT_CHECK(custom_gate.web_fetch(badge, Capability::all(), e));
    RT_CHECK(!custom_gate.web_fetch(badge, Capability{}, e) &&
             e.code == "internal.rights");

    // One origin per page: intra-page fetches fine, cross-origin refused, and
    // an origin that is not registered is nobody.
    InternalUrl settings_css, history_page;
    registry.parse("psychovault://settings/app.css", settings_css, e);
    registry.parse("psychovault://history", history_page, e);
    RT_CHECK(gate.internal_fetch("settings", settings_css, e));
    RT_CHECK(!gate.internal_fetch("settings", history_page, e) &&
             e.code == "internal.origin-cross");
    RT_CHECK(!gate.internal_fetch("nope", settings_css, e) &&
             e.code == "internal.origin-unknown");

    // The S027 seam, widened for my broker: my pages admit, my pages only -
    // and the stock web policy keeps refusing the scheme for a web renderer.
    WebContents web(21, 51, {}); // default stock admit: my scheme stays shut
    RT_CHECK(web.go("psychovault://newtab", e) == -1 && e.code == "nav.scheme");
    std::vector<Envelope> told;
    WebContents ui(22, 52, {[&told](const Envelope &env) { told.push_back(env); },
                            [&registry](const std::string &raw, std::string &canon,
                                        Error &err) {
                              return admit_browser_url(raw, registry, canon, err);
                            }});
    RT_CHECK(ui.go("PSYCHOVAULT://NewTab", e) == 1);
    RT_CHECK(ui.pending()->url == "psychovault://newtab");
    RT_CHECK(ui.go("psychovault://nope", e) == -1 &&
             e.code == "internal.unknown-page");
    RT_CHECK(ui.pending() && ui.pending()->url == "psychovault://newtab");
    RT_CHECK(ui.commit("New Tab", "fav:nt", e));
    RT_CHECK(ui.current()->url == "psychovault://newtab" &&
             ui.current()->title == "New Tab");
    RT_CHECK(ui.go("https://web.example/", e) == 2 && ui.commit("Web", "", e));
    RT_CHECK(ui.history().size() == 2 && ui.current()->url == "https://web.example/");
    // The refused nav left the pending one alone, and every envelope validates.
    for (const Envelope &env : told) {
      RT_CHECK(env.channel == Channel::Tab);
      RT_CHECK(schemas.validate(env.method, env.payload, e));
    }
  }


  // S029 preload bridge: freeze semantics in my engine, then the four laws -
  // isolation, curation, freeze, no direct broker access - on window.psycho.
  {
    // The engine law the facade stands on: frozen objects ignore stores.
    js::Runtime engine_world;
    engine_world.run("var ice = {a: 1}; Object.freeze(ice); ice.a = 2; ice.b = 3;");
    RT_CHECK(engine_world.run("ice.a").number == 1);
    RT_CHECK(engine_world.run("typeof ice.b").string == "undefined");
    RT_CHECK(engine_world.run("Object.isFrozen(ice)").boolean);
    RT_CHECK(!engine_world.run("Object.isFrozen({})").boolean);
    RT_CHECK(engine_world.run("Object.isFrozen(7)").boolean);

    // A bridge with a fake broker seam that records and answers.
    int calls = 0;
    std::string key_seen;     // set only when a call carries "key"
    Json last_payload = Json::make_object();
    PreloadBridge::Seams seams;
    seams.invoke = [&calls, &key_seen, &last_payload](Channel channel,
                       const std::string &method, Json payload, Json &reply,
                       Error &error) {
      (void)channel; (void)method; (void)error;
      ++calls;
      last_payload = payload;
      reply = Json::make_object();
      reply.set("pong", Json(true));
      if (const Json *key = payload.find("key")) {
        key_seen = key->string;
        reply.set("value", Json(std::string("dark:") + key->string));
      }
      return true;
    };
    PreloadBridge bridge(seams);
    Error e;

    // Curation law: schema-registered names only, never twice.
    RT_CHECK(bridge.expose(Channel::Settings, "settings:get", e));
    RT_CHECK(bridge.expose(Channel::Lifecycle, "lifecycle:ping", e));
    RT_CHECK(!bridge.expose(Channel::Settings, "settings:get", e) &&
             e.code == "bridge.duplicate");
    RT_CHECK(!bridge.expose(Channel::Vault, "vault:nope", e) &&
             e.code == "bridge.unknown-method");
    Json reply;
    RT_CHECK(!bridge.dispatch("vault:status", Json::make_object(), reply, e) &&
             e.code == "bridge.unexposed");
    RT_CHECK(!bridge.dispatch("settings:get", Json::make_object(), reply, e) &&
             e.code == "schema.missing");

    // Isolation law: the preload world has the dispatcher and nothing else -
    // no window, no psycho facade, no page objects.
    RT_CHECK(bridge.run_preload("var probe = 41 * 2;", e));
    RT_CHECK(bridge.preload_world().run("probe").number == 82);
    RT_CHECK(bridge.preload_world().run("typeof __psycho_call").string == "function");
    // My engine self-binds window in every world by convention - isolation
    // means the preload world's window carries NO facade and NO page objects.
    RT_CHECK(bridge.preload_world().run("typeof window").string == "object");
    RT_CHECK(bridge.preload_world().run("typeof window.psycho").string == "undefined");
    RT_CHECK(bridge.preload_world().run("typeof psycho").string == "undefined");
    RT_CHECK(!bridge.run_preload("throw 7;", e) && e.code == "bridge.preload-script");

    // The facade: window.psycho materialised, frozen at every level, and the
    // page world has NO dispatcher of its own.
    js::Runtime page;
    RT_CHECK(bridge.install(page, e));
    RT_CHECK(page.run("typeof psycho").string == "object");
    RT_CHECK(page.run("typeof psycho.settings.get").string == "function");
    RT_CHECK(page.run("typeof psycho.lifecycle.ping").string == "function");
    RT_CHECK(page.run("typeof psycho.vault").string == "undefined");
    RT_CHECK(page.run("typeof __psycho_call").string == "undefined");
    RT_CHECK(page.run("Object.isFrozen(psycho)").boolean);
    RT_CHECK(page.run("Object.isFrozen(psycho.settings)").boolean);
    RT_CHECK(page.run("Object.isFrozen(psycho.settings.get)").boolean);

    // Calls round-trip: payload arrives as data, reply clones back as values.
    RT_CHECK(page.run("psycho.settings.get({key: 'theme'}).value").string == "dark:theme");
    RT_CHECK(page.run("psycho.lifecycle.ping({}).pong").boolean);
    RT_CHECK(calls == 2 && key_seen == "theme");

    // Freeze law: page code cannot patch, shadow, replace or extend the facade.
    page.run("psycho.settings.get = function() { return 0; }; psycho.settings = 0;"
             "psycho.pwned = 1;");
    RT_CHECK(page.run("psycho.settings.get({key: 'x'}).value").string == "dark:x");
    RT_CHECK(page.run("typeof psycho.settings.get").string == "function");
    RT_CHECK(page.run("typeof psycho.pwned").string == "undefined");
    RT_CHECK(calls == 3);

    // Only data crosses: a function argument cannot ride into the broker seam.
    RT_CHECK(page.run("psycho.lifecycle.ping(function() { return 7; }).pong").boolean);
    RT_CHECK(last_payload.type == Json::Type::Object ||
             last_payload.type == Json::Type::Null); // never a callable

    // Refusals throw in-page as BridgeError with the machine-readable code.
    RT_CHECK(page.run("try { psycho.settings.get({}); 'silent'; } catch (err) { 'caught'; }")
                 .string == "caught");
    // In-page the refusal is an Error named BridgeError carrying the code.
    RT_CHECK(page.run("try { psycho.settings.get({}); } catch (err) { err.name; }")
                 .string == "BridgeError");
    RT_CHECK(page.run("try { psycho.settings.get({}); } catch (err) { err.message; }")
                 .string.find("schema.missing") != std::string::npos);
  }


  // S030 app-layer JS host: curated minimum per origin kind, extension-less
  // privileges, and every failure surfaced to the broker's log as a record.
  {
    InternalRegistry registry = InternalRegistry::stock();
    auto fake_bus = [](Channel, const std::string &method, Json payload, Json &reply,
                       Error &) {
      reply = Json::make_object();
      if (method == "settings:get")
        reply.set("value", Json(std::string("dark:") + payload.find("key")->string));
      if (method == "internal:list") reply.set("pages", Json(9));
      reply.set("ok", Json(true));
      return true;
    };

    // A web origin: no facade, nothing to leak, and failures still log.
    std::vector<Error> web_log;
    AppHost web(70, nullptr, {fake_bus, [&web_log](const Error &e2) {
                                web_log.push_back(e2);
                              }});
    RT_CHECK(web.kind() == PageKind::Web && web.tab_id() == 70);
    RT_CHECK(web.page_world().run("typeof psycho").string == "undefined");
    RT_CHECK(!web.run_ui("throw 'boom';", "web/boot.js"));
    RT_CHECK(web_log.size() == 1 && web_log[0].code == "apphost.script");
    RT_CHECK(web_log[0].message == "web web/boot.js: boom");
    RT_CHECK(web.run_ui("var fine = 1;", "web/fine.js") && web_log.size() == 1);

    // A shell origin: the curated facade exists - and ends where I say it ends.
    std::vector<Error> shell_log;
    AppHost shell(71, registry.find("settings"), {fake_bus, [&shell_log](const Error &e3) {
                                                    shell_log.push_back(e3);
                                                  }});
    RT_CHECK(shell.kind() == PageKind::Shell);
    RT_CHECK(shell.page_world().run("typeof psycho").string == "object");
    RT_CHECK(shell.page_world().run("typeof psycho.settings.get").string == "function");
    RT_CHECK(shell.page_world().run("typeof psycho.internal.list").string == "function");
    // Extension-less means exactly that: these namespaces do not exist here.
    RT_CHECK(shell.page_world().run("typeof psycho.vault").string == "undefined");
    RT_CHECK(shell.page_world().run("typeof psycho.proton").string == "undefined");
    RT_CHECK(shell.page_world().run("typeof psycho.extensions").string == "undefined");
    // The S029 freeze law holds through the host layer.
    RT_CHECK(shell.page_world().run("Object.isFrozen(psycho.settings)").boolean);
    RT_CHECK(shell.page_world().run("psycho.settings.get({key: 'theme'}).value").string ==
             "dark:theme");
    RT_CHECK(shell_log.empty()); // a clean boot logs nothing

    // Bound law: a runaway UI script dies, the broker lives, the log tells.
    RT_CHECK(!shell.run_ui("while (true) {}", "runaway.js"));
    RT_CHECK(shell_log.size() == 1 && shell_log.back().code == "apphost.bound");
    // A plain script error is its own record, naming page and label.
    RT_CHECK(!shell.run_ui("no_such_fn();", "late.js"));
    RT_CHECK(shell_log.size() == 2 && shell_log.back().code == "apphost.script");
    RT_CHECK(shell_log.back().message.find("settings") != std::string::npos &&
             shell_log.back().message.find("late.js") != std::string::npos);

    // A diagnostics origin: read-mostly - internal:list, never settings.
    std::vector<Error> diag_log;
    AppHost diag(72, registry.find("nettest"), {fake_bus, [&diag_log](const Error &e4) {
                                                  diag_log.push_back(e4);
                                                }});
    RT_CHECK(diag.kind() == PageKind::Diagnostics);
    RT_CHECK(diag.page_world().run("typeof psycho.settings").string == "undefined");
    RT_CHECK(diag.page_world().run("psycho.internal.list({}).pages").number == 9);

    // Isolation carries through: a shell page's world shares nothing with
    // another host's world, even though they sat under the same broker fakery.
    RT_CHECK(shell.run_ui("var shared_secret = 7;", "secret.js"));
    RT_CHECK(diag.page_world().run("typeof shared_secret").string == "undefined");
    RT_CHECK(diag_log.empty());
  }


  // S031 broker services: dialog law (origin, single-modal, shape),
  // clipboard, notifications, tray, menu law (uniqueness, canonical
  // accelerators, radio exclusivity) and context-menu offered-set law.
  {
    Error e;

    // ---- dialogs: validation, origin, modality, result shape.
    auto fake_picker = [](const DialogRequest &req) {
      DialogResult r;
      if (req.message == "cancel") return r; // user pressed escape
      r.cancelled = false;
      r.path = req.kind == DialogRequest::Kind::Message ? "" : "C:\\picked.txt";
      r.button = 0;
      return r;
    };
    DialogService dialogs(DialogService::Seams{fake_picker});
    DialogResult result;
    DialogRequest message;
    message.window_id = 3;
    message.message = "hello from a page"; // alert-like: pages may do this one
    RT_CHECK(dialogs.run(message, result, e) && result.button == 0 && !result.cancelled);
    DialogRequest empty_message;
    RT_CHECK(!dialogs.run(empty_message, result, e) && e.code == "dialog.message");
    // File pickers are trusted-chrome only: a page never spawns one.
    DialogRequest open;
    open.kind = DialogRequest::Kind::Open;
    open.window_id = 3;
    open.filters = {"png"};
    RT_CHECK(!dialogs.run(open, result, e) && e.code == "dialog.origin");
    open.from_shell = true;
    RT_CHECK(dialogs.run(open, result, e) && result.path == "C:\\picked.txt");
    DialogRequest bad_filter = open;
    bad_filter.filters = {"jpeg xl"};
    RT_CHECK(!dialogs.run(bad_filter, result, e) && e.code == "dialog.filter");
    DialogRequest save;
    save.kind = DialogRequest::Kind::Save;
    save.from_shell = true;
    save.window_id = 3;
    RT_CHECK(!dialogs.run(save, result, e) && e.code == "dialog.name");
    save.default_name = "report.txt";
    RT_CHECK(dialogs.run(save, result, e) && result.path == "C:\\picked.txt");
    // Cancellation is a first-class answer, not an error.
    DialogRequest cancelme = message;
    cancelme.message = "cancel";
    RT_CHECK(dialogs.run(cancelme, result, e) && result.cancelled);
    // One modal per window, proven by re-entry from inside the seam itself.
    DialogService *self = nullptr;
    bool busy_refused = false;
    DialogService reenter(DialogService::Seams{[&](const DialogRequest &again) {
      DialogResult inner;
      Error inner_error;
      busy_refused = (!self->run(again, inner, inner_error) &&
                      inner_error.code == "dialog.busy");
      return DialogResult{};
    }});
    self = &reenter;
    DialogRequest reentrant = message;
    reentrant.window_id = 9;
    RT_CHECK(reenter.run(reentrant, result, e) && busy_refused);
    RT_CHECK(!reenter.busy(9)); // cleared when the seam returned
    // No platform seam is a refusal, not a silent nothing.
    DialogService headless(DialogService::Seams{});
    RT_CHECK(!headless.run(message, result, e) && e.code == "dialog.unavailable");
    // A lying seam (says not-cancelled, hands no path) is caught.
    DialogService lying(DialogService::Seams{[](const DialogRequest &) {
      DialogResult r;
      r.cancelled = false;
      return r;
    }});
    RT_CHECK(!lying.run(open, result, e) && e.code == "dialog.result");

    // ---- clipboard: bounded writes, privileged reads, writer on record.
    bool applied = false;
    ClipboardService clip(ClipboardService::Seams{
        [&applied](const std::string &) { applied = true; }});
    std::string too_big((size_t(1) << 20) + 1, 'x');
    RT_CHECK(!clip.write_text("shell", too_big, e) && e.code == "clipboard.too-large");
    RT_CHECK(clip.write_text("shell", "hello", e) && applied &&
             clip.generation() == 1 && clip.last_writer() == "shell");
    std::string out;
    RT_CHECK(!clip.read_text("web.example", false, out, e) &&
             e.code == "clipboard.denied");
    RT_CHECK(clip.read_text("shell", true, out, e) && out == "hello");

    // ---- notifications: permission, mute, required title, bounded memory.
    int posted = 0;
    NotificationService notes(
        NotificationService::Seams{[&posted](const Notice &) { ++posted; }});
    RT_CHECK(!notes.notify("web.example", "Hi", "body", false, e) &&
             e.code == "notify.permission");
    RT_CHECK(notes.notify("web.example", "Hi", "body", true, e) && posted == 1);
    RT_CHECK(!notes.notify("shell", "", "body", true, e) && e.code == "notify.title");
    notes.mute("web.example", true);
    RT_CHECK(!notes.notify("web.example", "Hi", "b", true, e) && e.code == "notify.muted");
    notes.mute("web.example", false);
    RT_CHECK(notes.notify("web.example", "Hi", "b", true, e) && posted == 2);
    for (int i = 0; i < 55; ++i) notes.notify("shell", "n", "b", true, e);
    RT_CHECK(notes.recent().size() == 50 && notes.recent().front().id == 8);
    RT_CHECK(posted == 57);

    // ---- tray: exactly one, tooltip bounded, events route while alive.
    std::string tray_got;
    TrayService tray(TrayService::Seams{
        [&tray_got](const std::string &event) { tray_got = event; }});
    RT_CHECK(tray.create("PsychoVault", e));
    RT_CHECK(!tray.create("again", e) && e.code == "tray.exists");
    RT_CHECK(!tray.set_tooltip(std::string(129, 't'), e) && e.code == "tray.too-long");
    RT_CHECK(tray.set_tooltip("PV", e) && tray.tooltip() == "PV");
    tray.event("left");
    RT_CHECK(tray_got == "left");
    tray_got.clear();
    RT_CHECK(tray.destroy(e) && !tray.alive());
    tray.event("left"); // dead tray routes nothing
    RT_CHECK(tray_got.empty());
    RT_CHECK(!tray.destroy(e) && e.code == "tray.missing");
    RT_CHECK(!tray.set_tooltip("x", e) && e.code == "tray.missing");
    RT_CHECK(tray.create("PsychoVault", e)); // rebirth is fine

    // ---- menus: canonical accelerators, uniqueness, radio law, dispatch.
    RT_CHECK(MenuService::canonical_accelerator("shift+f5") == "Shift+F5");
    RT_CHECK(MenuService::canonical_accelerator("shift+Ctrl+p") == "Ctrl+Shift+P");
    // Modifiers sort, the key uppercases whole: canonical IS case-free.
    RT_CHECK(MenuService::canonical_accelerator("ctrl+alt+delete") == "Alt+Ctrl+DELETE");
    RT_CHECK(MenuService::canonical_accelerator("") == "");
    std::string fired;
    MenuService menus(
        MenuService::Seams{[&fired](const std::string &id) { fired = id; }});
    MenuItem file_new{"file.new", "New Tab", "ctrl+t"};
    MenuItem view_small{"view.small", "Small", "", MenuItem::Type::Radio};
    MenuItem view_large{"view.large", "Large", "", MenuItem::Type::Radio};
    MenuItem view_bar{"view.bar", "Toolbar", "ctrl+shift+b", MenuItem::Type::Checkbox};
    MenuItem view_menu{"menu.view", "View", ""};
    view_menu.submenu = {view_small, view_large, view_bar};
    MenuItem sep;
    sep.type = MenuItem::Type::Separator;
    RT_CHECK(menus.install({file_new, sep, view_menu}, e));
    RT_CHECK(menus.find("file.new")->accelerator == "Ctrl+T");
    RT_CHECK(menus.dispatch("file.new", e) && fired == "file.new");
    RT_CHECK(!menus.dispatch("menu.nope", e) && e.code == "menu.missing");
    // Radio: checking one unchecks its siblings.
    RT_CHECK(menus.dispatch("view.small", e) &&
             menus.find("view.small")->checked);
    RT_CHECK(menus.dispatch("view.large", e) && menus.find("view.large")->checked &&
             !menus.find("view.small")->checked);
    // Checkbox toggles through dispatch, and accepts a programmatic check.
    RT_CHECK(menus.dispatch("view.bar", e) && menus.find("view.bar")->checked);
    RT_CHECK(menus.dispatch("view.bar", e) && !menus.find("view.bar")->checked);
    RT_CHECK(menus.set_checked("view.bar", true, e) && menus.find("view.bar")->checked);
    // Disabled commands refuse before any state changes.
    RT_CHECK(menus.set_enabled("view.large", false, e));
    RT_CHECK(!menus.dispatch("view.large", e) && e.code == "menu.disabled");
    // The law at install: duplicate ids and sibling accelerator clashes refuse.
    MenuItem dupe = file_new;
    RT_CHECK(!menus.install({file_new, dupe}, e) && e.code == "menu.duplicate");
    MenuItem clash_a{"a", "A", "Ctrl+N"};
    MenuItem clash_b{"b", "B", "ctrl+n"};
    RT_CHECK(!menus.install({clash_a, clash_b}, e) && e.code == "menu.accelerator");
    MenuItem other_parent{"wrap", "Wrap", ""};
    other_parent.submenu = {clash_b}; // same accel under a different parent is legal
    RT_CHECK(menus.install({clash_a, other_parent}, e));
    std::vector<MenuItem> huge;
    for (int i = 0; i < 300; ++i)
      huge.push_back({"h" + std::to_string(i), "h", ""});
    RT_CHECK(!menus.install(huge, e) && e.code == "menu.too-large");

    // ---- context menus: providers build, the offered set is law.
    std::string ctx_cmd;
    int ctx_contents = -1;
    ContextMenus ctx(ContextMenus::Seams{
        [&ctx_cmd, &ctx_contents](const std::string &id, const ContextInfo &info) {
          ctx_cmd = id;
          ctx_contents = info.contents_id;
        }});
    RT_CHECK(ctx.register_provider("link",
                                   [](const ContextInfo &) {
                                     return std::vector<MenuItem>{
                                         {"link.open", "Open Link", ""},
                                         {"link.copy", "Copy Link", "Ctrl+Shift+C"}};
                                   },
                                   e));
    RT_CHECK(!ctx.register_provider("link", [](const ContextInfo &) {
                                      return std::vector<MenuItem>{};
                                    },
                                    e) &&
             e.code == "context.duplicate-provider");
    ContextInfo info;
    info.kind = "link";
    info.contents_id = 5;
    info.link_url = "https://example.com/";
    uint64_t token = ctx.open(info, e);
    RT_CHECK(token == 1 && ctx.outstanding() == 1);
    RT_CHECK(ctx.choose(token, "link.copy", e) && ctx_cmd == "link.copy" &&
             ctx_contents == 5 && ctx.outstanding() == 0);
    RT_CHECK(!ctx.choose(token, "link.copy", e) && e.code == "context.token");
    token = ctx.open(info, e);
    RT_CHECK(!ctx.choose(token, "menu.nope", e) && e.code == "context.not-offered");
    RT_CHECK(ctx.outstanding() == 1); // a refused choice keeps the menu open
    RT_CHECK(ctx.cancel(token) && !ctx.cancel(token));
    ContextInfo unknown;
    unknown.kind = "mystery";
    RT_CHECK(ctx.open(unknown, e) == 0 && e.code == "context.no-provider");
    // Provider output obeys the same menu law.
    RT_CHECK(ctx.register_provider("bad",
                                   [](const ContextInfo &) {
                                     return std::vector<MenuItem>{{"x", "X", ""},
                                                                  {"x", "X2", ""}};
                                   },
                                   e));
    ContextInfo badinfo;
    badinfo.kind = "bad";
    RT_CHECK(ctx.open(badinfo, e) == 0 && e.code == "menu.duplicate");
    // Outstanding sessions are bounded.
    for (int i = 0; i < 16; ++i) RT_CHECK(ctx.open(info, e) > 0);
    RT_CHECK(ctx.outstanding() == 16);
    RT_CHECK(ctx.open(info, e) == 0 && e.code == "context.too-many");
  }


  // S032 shortcut registry: one canonical language shared with menus, scope
  // law with the editable shadow, and propagation of the unclaimed.
  {
    ShortcutRegistry keys;
    Error e;
    const KeyContext on_page{false};
    const KeyContext in_field{true};

    // Bind law: named keys may stand alone; printable characters may not;
    // unknown tokens and empty targets refuse, by name.
    RT_CHECK(keys.bind(KeyScope::App, "ctrl+t", "tab.new", e));
    RT_CHECK(keys.bind(KeyScope::App, "Shift+Ctrl+P", "palette.open", e));
    RT_CHECK(keys.bind(KeyScope::App, "F5", "nav.reload", e));
    RT_CHECK(!keys.bind(KeyScope::App, "t", "evil", e) && e.code == "keys.bare");
    RT_CHECK(!keys.bind(KeyScope::App, "Ctrl+Nonsense", "evil", e) &&
             e.code == "keys.key");
    RT_CHECK(!keys.bind(KeyScope::App, "Ctrl+Alt+", "evil", e) && e.code == "keys.key");
    RT_CHECK(!keys.bind(KeyScope::App, "Ctrl+T", "tab.other", e) &&
             e.code == "keys.duplicate");
    RT_CHECK(!keys.bind(KeyScope::App, "Ctrl+Q", "", e) && e.code == "keys.command");
    // The same accelerator in the OTHER scope is legal: the shadow law.
    RT_CHECK(keys.bind(KeyScope::Editable, "Ctrl+Shift+P", "edit.inspect", e));

    // My menus and my keys speak ONE canonical language, case-free on both.
    KeyEvent palette{true, false, true, "p"};
    RT_CHECK(ShortcutRegistry::accelerator_for(palette) == "Ctrl+Shift+P");
    RT_CHECK(ShortcutRegistry::accelerator_for(palette) ==
             MenuService::canonical_accelerator("shift+ctrl+p"));

    // Resolution law: app scope fires on a page AND inside an editable field.
    KeyEvent ctrl_t{true, false, false, "t"};
    RT_CHECK(keys.resolve(ctrl_t, on_page) == "tab.new");
    RT_CHECK(keys.resolve(ctrl_t, in_field) == "tab.new");
    RT_CHECK(keys.resolve({true, false, false, "T"}, on_page) == "tab.new"); // case-free
    RT_CHECK(keys.resolve({false, false, false, "F5"}, on_page) == "nav.reload");
    RT_CHECK(keys.resolve({false, false, false, "f5"}, on_page) == "nav.reload");
    // The shadow law, proven both directions on the same chord.
    RT_CHECK(keys.resolve(palette, in_field) == "edit.inspect");
    RT_CHECK(keys.resolve(palette, on_page) == "palette.open");
    // Editable bindings do not fire on a page: the key stays with the content.
    RT_CHECK(keys.bind(KeyScope::Editable, "Ctrl+A", "edit.select-all", e));
    RT_CHECK(keys.resolve({true, false, false, "a"}, in_field) == "edit.select-all");
    RT_CHECK(keys.resolve({true, false, false, "a"}, on_page) == "");
    // The unclaimed keys always propagate - typing belongs to the page.
    RT_CHECK(keys.resolve({false, false, false, "k"}, on_page).empty());
    RT_CHECK(keys.resolve({false, false, false, " "}, in_field).empty());
    RT_CHECK(keys.resolve({true, false, false, "="}, on_page).empty());

    // A command's bindings die together across scopes; unbinding is truth.
    RT_CHECK(keys.bind(KeyScope::Editable, "Ctrl+P", "palette.open", e));
    RT_CHECK(keys.resolve({true, false, false, "p"}, in_field) == "palette.open");
    RT_CHECK(keys.size() == 6); // tab.new, palette.open x2 scopes, reload, inspect, select-all
    RT_CHECK(keys.unbind_command("palette.open"));
    RT_CHECK(keys.size() == 4); // both palette bindings died together
    RT_CHECK(keys.resolve(palette, on_page).empty());
    RT_CHECK(keys.resolve({true, false, false, "p"}, in_field).empty());
    RT_CHECK(keys.resolve(palette, in_field) == "edit.inspect"); // shadow remapped
    RT_CHECK(!keys.unbind_command("palette.open")); // already gone, said so
  }

  shared += rt_passed;
  return 0;
}
