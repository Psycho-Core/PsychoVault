#include "../app/Psycho_instance.hpp"
#include "../tabs/Psycho_tabs.hpp"
#include "../content/Psycho_content.hpp"
#include "../internal/Psycho_internal.hpp"
#include "../bridge/Psycho_bridge.hpp"
#include "../apphost/Psycho_apphost.hpp"
#include "../services/Psycho_services.hpp"
#include "../keys/Psycho_keys.hpp"
#include "../ime/Psycho_ime.hpp"
#include "../focus/Psycho_focus.hpp"
#include "../downloads/Psycho_downloads.hpp"
#include "../permissions/Psycho_permissions.hpp"
#include "../../engine/Psycho_js.hpp"
#include "../surface/Psycho_surface.hpp"
#include "../window/Psycho_window.hpp"
#include "../ipc/Psycho_bus.hpp"
#include "../broker/Psycho_broker.hpp"
#include "../process/Psycho_process.hpp"
#include <algorithm>
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

  // S033 IME and dead keys: one composition state per surface, candidate
  // window placement inside the window, commit as the only path to text,
  // and the dead-key law end to end.
  {
    std::vector<std::pair<int, Composition>> preedits;
    std::vector<std::pair<int, std::string>> commits;
    ImeService::Seams seams;
    seams.on_preedit = [&](int id, const Composition &c) {
      preedits.push_back({id, c});
    };
    seams.on_commit = [&](int id, const std::string &t) {
      commits.push_back({id, t});
    };
    const ImeRect win{0, 0, 800, 600};
    ImeRect caret{100, 100, 2, 20};
    seams.window_rect = [&](int) { return win; };
    seams.caret_rect = [&](int) { return caret; };
    ImeService ime(seams);
    Error e;
    size_t commits_before = 0;

    // A build without the pieces refuses by name, never silently.
    {
      ImeService bare(ImeService::Seams{});
      RT_CHECK(!bare.begin(1, e) && e.code == "ime.unavailable");
    }

    // A platform composition end to end: begin, update, place, commit.
    RT_CHECK(ime.begin(7, e));
    RT_CHECK(ime.composing(7) && !ime.pending_accent(7));
    RT_CHECK(!ime.begin(7, e) && e.code == "ime.busy"); // one per surface
    RT_CHECK(ime.update(7, "\xc3\xa9t", 3, e));         // "ét" (\xc3\xa9 = é)
    const Composition *comp = ime.composition(7);
    RT_CHECK(comp != nullptr && comp->preedit == "\xc3\xa9t" && comp->caret == 3);
    // Caret law: mid-character and past-the-end both refuse by name.
    RT_CHECK(!ime.update(7, "\xc3\xa9t", 1, e) && e.code == "ime.caret");
    RT_CHECK(!ime.update(7, "\xc3\xa9t", 4, e) && e.code == "ime.caret");
    // Text law: malformed UTF-8 and unbounded preedit both refuse...
    RT_CHECK(!ime.update(7, std::string("\x80"), 0, e) && e.code == "ime.text");
    RT_CHECK(!ime.update(7, std::string(5000, 'a'), 5000, e) && e.code == "ime.text");
    // ...and a refused update never touched the honest state.
    comp = ime.composition(7);
    RT_CHECK(comp->preedit == "\xc3\xa9t" && comp->caret == 3);

    // The candidate window follows the caret and stays in the window.
    int cx = -1, cy = -1;
    RT_CHECK(ime.candidate_position(7, cx, cy, e));
    RT_CHECK(cx == 100 && cy == 120); // the caret's baseline, unclamped
    caret = ImeRect{700, 560, 2, 20}; // typing at the window's far corner
    RT_CHECK(ime.candidate_position(7, cx, cy, e));
    RT_CHECK(cx == 540); // 800-260: pulled back off the right edge
    RT_CHECK(cy == 380); // 560-180: flipped above the caret line
    caret = ImeRect{100, 100, 2, 20};

    // Commit: text crosses exactly once, the composition ends, the
    // underline clears; and afterwards everything idle refuses.
    commits_before = commits.size();
    RT_CHECK(ime.commit(7, "\xc3\xa9tudiant", e));
    RT_CHECK(commits.size() == commits_before + 1 && commits.back().first == 7 &&
             commits.back().second == "\xc3\xa9tudiant");
    RT_CHECK(!ime.composing(7) && ime.composition(7) == nullptr);
    RT_CHECK(!preedits.empty() && preedits.back().second.preedit.empty());
    RT_CHECK(!ime.candidate_position(7, cx, cy, e) && e.code == "ime.idle");
    RT_CHECK(!ime.commit(7, "again", e) && e.code == "ime.idle");

    // Cancel: the field is exactly as it was - nothing ever crosses.
    RT_CHECK(ime.begin(7, e) && ime.update(7, "draft", 5, e));
    commits_before = commits.size();
    RT_CHECK(ime.cancel(7, e));
    RT_CHECK(commits.size() == commits_before && !ime.composing(7));
    RT_CHECK(!ime.cancel(7, e) && e.code == "ime.idle");
    // An empty commit is the platform's last-moment "never mind": legal,
    // ends the composition, delivers nothing.
    RT_CHECK(ime.begin(7, e) && ime.update(7, "x", 1, e));
    commits_before = commits.size();
    RT_CHECK(ime.commit(7, "", e));
    RT_CHECK(commits.size() == commits_before && !ime.composing(7));

    // Dead keys: the pending accent IS the one composition state.
    RT_CHECK(ime.dead_key(3, "'", e));
    RT_CHECK(ime.composing(3) && ime.pending_accent(3));
    RT_CHECK(ime.composition(3)->preedit == "'");
    RT_CHECK(!ime.begin(3, e) && e.code == "ime.busy"); // ...so begin refuses
    RT_CHECK(ime.type_char(3, 'e', e));
    RT_CHECK(commits.back().second == "\xc3\xa9" && !ime.composing(3)); // é

    // The combining table, audited: uppercase, and beyond Latin-1.
    RT_CHECK(ime.dead_key(3, "'", e) && ime.type_char(3, 'E', e));
    RT_CHECK(commits.back().second == "\xc3\x89"); // É
    RT_CHECK(ime.dead_key(3, "^", e) && ime.type_char(3, 'g', e));
    RT_CHECK(commits.back().second == "\xc4\x9d"); // ĝ
    RT_CHECK(ime.dead_key(3, "~", e) && ime.type_char(3, 'N', e));
    RT_CHECK(commits.back().second == "\xc3\x91"); // Ñ

    // Law 5's honest fallbacks: no combination, space, the same accent
    // twice, and one accent after another.
    RT_CHECK(ime.dead_key(3, "~", e) && ime.type_char(3, 'b', e));
    RT_CHECK(commits.back().second == "~b"); // accent and letter both arrive
    RT_CHECK(ime.dead_key(3, "`", e) && ime.type_char(3, ' ', e));
    RT_CHECK(commits.back().second == "`"); // space asks for the accent
    RT_CHECK(ime.dead_key(3, "'", e) && ime.dead_key(3, "'", e));
    RT_CHECK(commits.back().second == "'" && !ime.composing(3));
    commits_before = commits.size();
    RT_CHECK(ime.dead_key(3, "'", e) && ime.dead_key(3, "^", e));
    RT_CHECK(commits.size() == commits_before + 1 && commits.back().second == "'");
    RT_CHECK(ime.pending_accent(3) && ime.composition(3)->preedit == "^");
    RT_CHECK(ime.type_char(3, 'e', e) && commits.back().second == "\xc3\xaa"); // ê

    // Dead-key refusals: unknown accent by name, typing with nothing
    // pending, a dead key against a live platform composition.
    RT_CHECK(!ime.dead_key(3, "%", e) && e.code == "ime.accent");
    RT_CHECK(!ime.type_char(3, 'e', e) && e.code == "ime.idle");
    RT_CHECK(ime.begin(4, e));
    RT_CHECK(!ime.dead_key(4, "'", e) && e.code == "ime.busy");
    commits_before = commits.size();
    RT_CHECK(ime.cancel(4, e));
    RT_CHECK(commits.size() == commits_before);
    // Cancelling a pending accent drops it: nothing crosses, state clears.
    RT_CHECK(ime.dead_key(3, "'", e));
    commits_before = commits.size();
    RT_CHECK(ime.cancel(3, e));
    RT_CHECK(commits.size() == commits_before && !ime.composing(3));

    // A dying surface takes its composition with it - death is not an
    // error, and saying so twice is not either.
    RT_CHECK(ime.dead_key(5, "'", e));
    commits_before = commits.size();
    ime.surface_closed(5);
    RT_CHECK(!ime.composing(5) && commits.size() == commits_before);
    ime.surface_closed(5);

    // Two surfaces never share one keystroke's worth of state.
    RT_CHECK(ime.dead_key(8, "'", e));
    RT_CHECK(ime.begin(9, e) && ime.update(9, "ime-", 4, e));
    RT_CHECK(ime.composing(8) && ime.composing(9));
    RT_CHECK(ime.commit(9, "ime-text", e) && !ime.composing(9));
    RT_CHECK(ime.pending_accent(8)); // the neighbor's finish left it alone
    RT_CHECK(ime.type_char(8, 'o', e) && commits.back().second == "\xc3\xb3"); // ó

    // Placement needs its seams: a half-built placement stack refuses.
    {
      ImeService::Seams partial;
      partial.on_preedit = seams.on_preedit;
      partial.on_commit = seams.on_commit;
      ImeService blind(partial);
      RT_CHECK(blind.begin(1, e));
      RT_CHECK(!blind.candidate_position(1, cx, cy, e) &&
               e.code == "ime.unavailable");
      RT_CHECK(blind.cancel(1, e));
    }
  }

  // S034 focus and activation: one answer at every level, blur before focus
  // and never twice, modal capture, the dead cannot hold focus, visibility
  // and throttling fall out of focus.
  {
    std::vector<std::string> events;
    FocusService::Seams fseams;
    fseams.on_event = [&](const FocusEvent &fe) {
      std::string s = fe.op == FocusEvent::Op::Blur ? "blur" : "focus";
      s += fe.level == FocusEvent::Level::Window  ? ":window:"
           : fe.level == FocusEvent::Level::Tab   ? ":tab:"
                                                  : ":element:";
      s += fe.level == FocusEvent::Level::Element ? fe.element
                                                  : std::to_string(fe.id);
      events.push_back(s);
    };
    FocusService focus(fseams);
    Error e;
    int ms = -1;
    size_t n = 0;

    // Law 1: operations on nodes I never registered refuse by name; the
    // visibility query's honest answer for a dead node is Hidden.
    RT_CHECK(!focus.focus_window(7, e) && e.code == "focus.unknown");
    RT_CHECK(!focus.tab_opened(7, 1, false, e) && e.code == "focus.unknown");
    RT_CHECK(!focus.focus_tab(7, 1, e) && e.code == "focus.unknown");
    RT_CHECK(!focus.focus_element(1, "a", e) && e.code == "focus.unknown");
    RT_CHECK(!focus.blur_element(1, e) && e.code == "focus.unknown");
    RT_CHECK(!focus.modal_open(7, e) && e.code == "focus.unknown");
    RT_CHECK(!focus.timer_floor(1, ms, e) && e.code == "focus.unknown");
    RT_CHECK(focus.visibility_of(1) == PageVisibility::Hidden);
    RT_CHECK(!focus.window_closed(7, e) && e.code == "focus.unknown");

    // Windows and tabs: opening activates, registering twice refuses.
    RT_CHECK(focus.window_opened(1, e) && focus.active_window() == 1);
    RT_CHECK(events.size() == 1 && events[0] == "focus:window:1");
    RT_CHECK(!focus.window_opened(1, e) && e.code == "focus.duplicate");
    RT_CHECK(focus.tab_opened(1, 10, true, e) && focus.focused_tab(1) == 10);
    RT_CHECK(events.back() == "focus:tab:10");
    RT_CHECK(focus.visibility_of(10) == PageVisibility::Active);
    RT_CHECK(focus.timer_floor(10, ms, e) && ms == FocusService::kActiveFloorMs);
    RT_CHECK(!focus.has_page_focus(10)); // no element holds focus yet
    // A background tab opens quietly: no events, focus stays put.
    RT_CHECK(focus.tab_opened(1, 11, false, e) && focus.focused_tab(1) == 10 &&
             events.size() == 2);
    RT_CHECK(focus.visibility_of(11) == PageVisibility::Hidden);
    RT_CHECK(focus.timer_floor(11, ms, e) && ms == FocusService::kHiddenFloorMs);
    // Switching tabs: blur strictly before focus.
    RT_CHECK(focus.focus_tab(1, 11, e) && focus.focused_tab(1) == 11);
    RT_CHECK(events.size() == 4 && events[2] == "blur:tab:10" &&
             events[3] == "focus:tab:11");
    RT_CHECK(focus.visibility_of(11) == PageVisibility::Active &&
             focus.visibility_of(10) == PageVisibility::Hidden);
    // Law 2's silence: re-focusing what holds focus emits nothing.
    n = events.size();
    RT_CHECK(focus.focus_tab(1, 11, e) && events.size() == n);
    RT_CHECK(!focus.focus_tab(1, 99, e) && e.code == "focus.unknown");

    // Page focus: element transitions blur-then-focus as well.
    RT_CHECK(focus.focus_element(11, "addr", e) &&
             events.back() == "focus:element:addr");
    RT_CHECK(focus.focused_element(11) == "addr" && focus.has_page_focus(11));
    n = events.size();
    RT_CHECK(focus.focus_element(11, "addr", e) && events.size() == n);
    RT_CHECK(focus.focus_element(11, "q", e) && events.size() >= n + 2 &&
             events[n] == "blur:element:addr" && events[n + 1] == "focus:element:q");
    RT_CHECK(focus.blur_element(11, e) && events.back() == "blur:element:q" &&
             focus.focused_element(11).empty());
    n = events.size();
    RT_CHECK(focus.blur_element(11, e) && events.size() == n);
    // Page focus flows through window and tab: a background tab's document
    // cannot take it, and an element needs a name.
    RT_CHECK(!focus.focus_element(10, "x", e) && e.code == "focus.inactive");
    RT_CHECK(!focus.focus_element(11, "", e) && e.code == "focus.arg");

    // A second window activates; the first's focused tab drops to Visible,
    // and occlusion drops it the rest of the way.
    n = events.size();
    RT_CHECK(focus.window_opened(2, e) && focus.active_window() == 2);
    RT_CHECK(events[n] == "blur:window:1" && events[n + 1] == "focus:window:2");
    RT_CHECK(focus.visibility_of(11) == PageVisibility::Visible);
    RT_CHECK(focus.timer_floor(11, ms, e) && ms == FocusService::kVisibleFloorMs);
    RT_CHECK(focus.set_occluded(1, true, e) &&
             focus.visibility_of(11) == PageVisibility::Hidden);
    RT_CHECK(focus.timer_floor(11, ms, e) && ms == FocusService::kHiddenFloorMs);
    RT_CHECK(focus.set_occluded(1, false, e) &&
             focus.visibility_of(11) == PageVisibility::Visible);
    RT_CHECK(!focus.set_occluded(9, true, e) && e.code == "focus.unknown");
    // Page focus in the document sense tracks the whole chain.
    RT_CHECK(focus.tab_opened(2, 20, true, e) &&
             focus.focus_element(20, "find", e));
    RT_CHECK(focus.has_page_focus(20) && !focus.has_page_focus(11));

    // Law 3: a modal dialog captures its own window - tab switches and
    // element focus refuse; other windows live free.
    RT_CHECK(focus.modal_open(2, e) && focus.is_modal(2));
    RT_CHECK(focus.tab_opened(2, 21, false, e) && focus.focused_tab(2) == 20);
    RT_CHECK(!focus.focus_tab(2, 21, e) && e.code == "focus.modal");
    RT_CHECK(!focus.focus_element(20, "blocked", e) && e.code == "focus.modal");
    RT_CHECK(focus.focus_window(1, e) && focus.active_window() == 1);
    RT_CHECK(focus.focus_window(2, e) && focus.active_window() == 2);
    RT_CHECK(!focus.modal_open(2, e) && e.code == "focus.modal"); // twice
    RT_CHECK(focus.modal_close(2, e) && !focus.is_modal(2));
    RT_CHECK(!focus.modal_close(2, e) && e.code == "focus.modal"); // unclosing
    RT_CHECK(focus.focus_tab(2, 21, e) && focus.focused_tab(2) == 21);

    // Law 4: closing the focused tab moves focus to the declared fallback -
    // the tab after it in open order, else the one before - and death is
    // not an event: the dying tab's element hears no blur.
    RT_CHECK(focus.focus_element(21, "last", e));
    n = events.size();
    RT_CHECK(focus.tab_closed(2, 21, e) && focus.focused_tab(2) == 20);
    RT_CHECK(events[n] == "blur:tab:21" && events[n + 1] == "focus:tab:20");
    RT_CHECK(std::find(events.begin(), events.end(), "blur:element:last") ==
             events.end());
    RT_CHECK(focus.focused_element(21).empty());
    RT_CHECK(!focus.focus_tab(2, 21, e) && e.code == "focus.unknown");
    RT_CHECK(!focus.timer_floor(21, ms, e) && e.code == "focus.unknown");
    // The last tab closing leaves the window focused on nothing - legal.
    RT_CHECK(focus.tab_closed(2, 20, e) && focus.focused_tab(2) == -1);
    RT_CHECK(events.back() == "blur:tab:20");

    // Closing the active window activates the most recently opened
    // remaining window; closing the last window leaves the app unfocused,
    // which is legal - and matters again when a new window arrives.
    n = events.size();
    RT_CHECK(focus.window_closed(2, e) && focus.active_window() == 1);
    RT_CHECK(events[n] == "blur:window:2" && events[n + 1] == "focus:window:1");
    RT_CHECK(focus.visibility_of(11) == PageVisibility::Active &&
             focus.visibility_of(10) == PageVisibility::Hidden);
    n = events.size();
    RT_CHECK(focus.window_closed(1, e) && focus.active_window() == -1);
    RT_CHECK(events.size() == n + 1 && events[n] == "blur:window:1");
    RT_CHECK(focus.window_opened(3, e) && focus.active_window() == 3 &&
             events.back() == "focus:window:3");
  }

  // S035 download service: one bounded history, a narrow state machine,
  // pause only where the wire said yes, every byte accounted, open/reveal
  // only for what honestly finished.
  {
    struct FakeTransport {
      int next_handle = 100;
      int refuse_next = 0;
      std::vector<int> begun, paused, cancelled;
      std::vector<std::pair<int, uint64_t>> resumed;
      std::vector<std::string> opened, revealed;
      std::set<std::string> existing;
    } tr;
    DownloadService::Seams dseams;
    dseams.begin_download = [&](const DownloadRecord &r) {
      if (tr.refuse_next > 0) {
        --tr.refuse_next;
        return 0;
      }
      tr.begun.push_back(r.id);
      return tr.next_handle++;
    };
    dseams.pause_download = [&](int h) { tr.paused.push_back(h); };
    dseams.resume_download = [&](int h, uint64_t from) {
      tr.resumed.push_back({h, from});
    };
    dseams.cancel_download = [&](int h) { tr.cancelled.push_back(h); };
    dseams.open_file = [&](const std::string &p) { tr.opened.push_back(p); };
    dseams.reveal_file = [&](const std::string &p) { tr.revealed.push_back(p); };
    dseams.file_exists = [&](const std::string &p) {
      return tr.existing.count(p) != 0;
    };
    int changes = 0;
    dseams.on_change = [&](const DownloadRecord &) { ++changes; };
    DownloadService dl(dseams);
    Error e;

    // A build with no transport refuses by name; the URL law names itself.
    {
      DownloadService bare(DownloadService::Seams{});
      RT_CHECK(bare.start("https://h/f.zip", "", e) == 0 &&
               e.code == "download.unavailable");
    }
    RT_CHECK(dl.start("", "", e) == 0 && e.code == "download.url");
    RT_CHECK(dl.start("ftp://h/f", "", e) == 0 && e.code == "download.url");
    // The save-as suggestion never trusts the wire.
    RT_CHECK(DownloadService::sanitize_filename("https://h/dir/report(1).pdf?dl=1") ==
             "report_1_.pdf");
    RT_CHECK(DownloadService::sanitize_filename("https://h/") == "download");
    RT_CHECK(DownloadService::sanitize_filename("https://h/..") == "download");
    RT_CHECK(DownloadService::sanitize_filename("https://h/a/../../b.zip") == "b.zip");

    // Happy path: begin rides the seam, progress and pause/resume obey.
    const int d1 = dl.start("https://h/app.zip", "", e);
    RT_CHECK(d1 == 1 && tr.begun.size() == 1 && tr.begun[0] == 1);
    RT_CHECK(dl.record(d1) != nullptr &&
             dl.record(d1)->state == DownloadState::Running &&
             dl.record(d1)->suggested_name == "app.zip");
    RT_CHECK(dl.progress(d1, 500, 1000, true, e));
    RT_CHECK(dl.record(d1)->received == 500 && dl.record(d1)->total == 1000 &&
             dl.record(d1)->resumable);
    RT_CHECK(!dl.progress(999, 1, 1, true, e) && e.code == "download.unknown");
    // Law 4: no regress, no overrun, no shrinking known total - and a
    // refused report never touches the honest numbers.
    RT_CHECK(!dl.progress(d1, 400, 1000, true, e) && e.code == "download.regress");
    RT_CHECK(!dl.progress(d1, 1001, 1000, true, e) && e.code == "download.overrun");
    RT_CHECK(!dl.progress(d1, 500, 900, true, e) && e.code == "download.regress");
    RT_CHECK(dl.record(d1)->received == 500 && dl.record(d1)->total == 1000);
    // Pause/resume where supported: handle and resume byte position named.
    RT_CHECK(dl.pause(d1, e) && dl.record(d1)->state == DownloadState::Paused);
    RT_CHECK(tr.paused.size() == 1 && tr.paused[0] == 100);
    RT_CHECK(!dl.pause(d1, e) && e.code == "download.state"); // already paused
    RT_CHECK(dl.resume(d1, e) && dl.record(d1)->state == DownloadState::Running);
    RT_CHECK(tr.resumed.size() == 1 && tr.resumed[0].first == 100 &&
             tr.resumed[0].second == 500);
    RT_CHECK(!dl.resume(d1, e) && e.code == "download.state"); // not paused
    // d1's story is told: cancel it. (Active in flight from here: only d5.)
    RT_CHECK(dl.cancel(d1, e) && dl.record(d1)->state == DownloadState::Cancelled);

    // The wire's no is law, and its latest word is the word that counts.
    const int d2 = dl.start("https://h/stream.mp4", "", e);
    RT_CHECK(d2 == 2 && dl.progress(d2, 10, 100, false, e));
    RT_CHECK(!dl.pause(d2, e) && e.code == "download.cannot-pause");
    const int d3 = dl.start("https://h/big.iso", "", e);
    RT_CHECK(d3 == 3 && dl.progress(d3, 50, 100, true, e) && dl.pause(d3, e));
    RT_CHECK(dl.resume(d3, e) && dl.progress(d3, 50, 100, false, e));
    RT_CHECK(!dl.pause(d3, e) && e.code == "download.cannot-pause");
    // Cancel is terminal: nothing moves a finished record, by any verb.
    RT_CHECK(dl.cancel(d2, e) && dl.record(d2)->state == DownloadState::Cancelled);
    RT_CHECK(!dl.cancel(d2, e) && e.code == "download.finished");
    RT_CHECK(!dl.pause(d2, e) && e.code == "download.finished");
    RT_CHECK(!dl.progress(d2, 20, 100, false, e) && e.code == "download.finished");
    // A completion short of the known total is a failed download, told
    // honestly - and the completion event itself is accepted (it happened).
    RT_CHECK(dl.complete(d3, e));
    RT_CHECK(dl.record(d3)->state == DownloadState::Failed &&
             dl.record(d3)->error == "download.truncated");
    RT_CHECK(!dl.open(d3, e) && e.code == "download.not-complete");

    // Save-as chooses before bytes finish; open/reveal touch only what
    // finished, exists, and was actually targeted.
    const int d4 = dl.start("https://h/movie.mkv", "", e);
    RT_CHECK(d4 == 4);
    RT_CHECK(!dl.set_target(d4, "", e) && e.code == "download.target");
    RT_CHECK(!dl.open(d4, e) && e.code == "download.not-complete");
    RT_CHECK(dl.set_target(d4, "/downloads/movie.mkv", e));
    RT_CHECK(dl.progress(d4, 2048, 2048, false, e) && dl.complete(d4, e));
    RT_CHECK(dl.record(d4)->state == DownloadState::Complete);
    RT_CHECK(!dl.open(d4, e) && e.code == "download.missing"); // disk disagrees
    tr.existing.insert("/downloads/movie.mkv");
    RT_CHECK(dl.open(d4, e) && tr.opened.size() == 1 &&
             tr.opened[0] == "/downloads/movie.mkv");
    RT_CHECK(dl.reveal(d4, e) && tr.revealed.size() == 1 &&
             tr.revealed[0] == "/downloads/movie.mkv");
    RT_CHECK(!dl.set_target(d4, "/elsewhere", e) && e.code == "download.finished");
    // Unknown size is legal: total 0 means the wire has not said.
    const int d5 = dl.start("https://h/blob", "", e);
    RT_CHECK(d5 == 5 && dl.progress(d5, 700, 0, false, e));
    RT_CHECK(dl.progress(d5, 700, 900, true, e) && dl.record(d5)->total == 900);
    const int changes_before = changes;
    RT_CHECK(dl.progress(d5, 800, 900, true, e) && changes == changes_before + 1);
    // Completion with no target finishes fine; opening it has nothing to
    // open and says so by name.
    const int d6 = dl.start("https://h/note.txt", "", e);
    RT_CHECK(d6 == 6 && dl.progress(d6, 5, 5, false, e) && dl.complete(d6, e));
    RT_CHECK(!dl.open(d6, e) && e.code == "download.target");
    // Failure is a first-class terminal state with its reason on record.
    const int d7 = dl.start("https://h/patch.diff", "", e);
    RT_CHECK(d7 == 7 && dl.fail(d7, "socket reset", e));
    RT_CHECK(dl.record(d7)->state == DownloadState::Failed &&
             dl.record(d7)->error == "socket reset");
    // Law 1: even a transport refusal is a recorded attempt, id consumed.
    tr.refuse_next = 1;
    RT_CHECK(dl.start("https://h/nope.bin", "", e) == 0 &&
             e.code == "download.transport");
    RT_CHECK(dl.record(8) != nullptr &&
             dl.record(8)->state == DownloadState::Failed &&
             dl.record(8)->error == "download.transport");
    RT_CHECK(dl.active_count() == 1); // only d5 still runs

    // The caps: at most 32 in flight, the newest 200 in history.
    std::vector<int> batch_ids;
    for (int i = 0; i < 31; ++i) {
      const int id = dl.start("https://h/cap.bin", "", e);
      RT_CHECK(id > 0);
      batch_ids.push_back(id);
    }
    RT_CHECK(dl.active_count() == 32);
    RT_CHECK(dl.start("https://h/one-more.bin", "", e) == 0 &&
             e.code == "download.too-many");
    int cleanup_failures = 0;
    if (!dl.cancel(d5, e)) ++cleanup_failures;
    for (int id : batch_ids)
      if (!dl.cancel(id, e)) ++cleanup_failures;
    RT_CHECK(cleanup_failures == 0 && dl.active_count() == 0);
    // Run 205 complete downloads through: ids 40..244 (1..39 are history).
    int loop_failures = 0;
    for (int i = 0; i < 205; ++i) {
      const int id = dl.start("https://h/bulk.bin", "", e);
      if (id <= 0 || !dl.progress(id, 1, 1, true, e) || !dl.complete(id, e))
        ++loop_failures;
    }
    RT_CHECK(loop_failures == 0);
    RT_CHECK(dl.history().size() == DownloadService::kHistoryLimit);
    RT_CHECK(dl.history().front().id == 45); // 244 attempts, newest 200 kept
    RT_CHECK(dl.history().back().id == 244);
    RT_CHECK(dl.record(1) == nullptr); // evicted, none dangling

    // History hygiene: erasing an in-flight record refuses; clearing keeps
    // what still moves.
    const int d_alive = dl.start("https://h/live.dat", "", e);
    RT_CHECK(d_alive == 245 && dl.active_count() == 1);
    RT_CHECK(!dl.remove_record(d_alive, e) && e.code == "download.state");
    const int oldest = dl.history().front().id;
    RT_CHECK(dl.remove_record(oldest, e) && dl.record(oldest) == nullptr);
    dl.clear_history();
    RT_CHECK(dl.history().size() == 1 && dl.history().front().id == d_alive);
    // Every verb names the unknown.
    RT_CHECK(!dl.resume(999, e) && e.code == "download.unknown");
    RT_CHECK(!dl.complete(999, e) && e.code == "download.unknown");
    RT_CHECK(!dl.fail(999, "x", e) && e.code == "download.unknown");
    RT_CHECK(!dl.open(999, e) && e.code == "download.unknown");
    RT_CHECK(!dl.set_target(999, "/x", e) && e.code == "download.unknown");
    RT_CHECK(!dl.remove_record(999, e) && e.code == "download.unknown");
    RT_CHECK(std::string(download_state_name(DownloadState::Paused)) == "paused");
  }

  // S036 permission service: the legacy deny list is the floor, prompts are
  // the only path from default to answer, and persistence is exactly what
  // the profile heard.
  {
    std::vector<PermissionGrant> profile_rows;
    profile_rows.push_back({"https://a.example", "geolocation", true});
    profile_rows.push_back({"https://a.example", "media", false});
    profile_rows.push_back({"https://a.example", "geolocation", false}); // last wins
    profile_rows.push_back({"not-an-origin", "midi", true});  // malformed row
    profile_rows.push_back({"https://b.example", "bluetooth", true}); // unknown kind
    std::vector<PermissionGrant> saved;
    std::vector<std::pair<std::string, std::string>> dropped;
    std::vector<std::string> shown;
    PermissionService::Seams pseams;
    pseams.load = [&]() { return profile_rows; };
    pseams.save = [&](const PermissionGrant &g) { saved.push_back(g); };
    pseams.drop = [&](const std::string &o, const std::string &k) {
      dropped.push_back({o, k});
    };
    pseams.show_prompt = [&](const std::string &o, const std::string &k) {
      shown.push_back(o + "|" + k);
    };
    PermissionService perms(pseams);
    Error e;
    Decision d = Decision::Deny;

    // The profile loads once: last-wins merge, malformed and unknown-kind
    // rows dropped, not trusted.
    RT_CHECK(perms.load_profile(e) && perms.stored() == 2);
    RT_CHECK(perms.decide("https://a.example", "geolocation", d, e) &&
             d == Decision::Deny); // the block row landed last
    RT_CHECK(perms.decide("https://a.example", "media", d, e) &&
             d == Decision::Deny);
    RT_CHECK(!perms.load_profile(e) && e.code == "perm.state");

    // Law 1: the legacy seven default to Deny - all of them, nowhere Allow.
    RT_CHECK(PermissionService::known_kinds().size() == 7);
    for (const std::string &kind : PermissionService::known_kinds()) {
      RT_CHECK(perms.decide("https://never.example", kind, d, e) &&
               d == Decision::Deny);
    }
    RT_CHECK(!perms.decide("https://never.example", "bluetooth", d, e) &&
             e.code == "perm.kind");

    // Law 2: origins are normalized scheme://host[:port], or refused.
    RT_CHECK(PermissionService::valid_origin("https://example.com"));
    RT_CHECK(PermissionService::valid_origin("http://a.b:8080"));
    RT_CHECK(!perms.decide("https://EXAMPLE.com", "midi", d, e) &&
             e.code == "perm.origin"); // uppercase is not normalized
    RT_CHECK(!perms.decide("https://example.com/", "midi", d, e) &&
             e.code == "perm.origin"); // trailing slash is a path
    RT_CHECK(!perms.decide("https://example.com/x", "midi", d, e) &&
             e.code == "perm.origin");
    RT_CHECK(!perms.decide("example.com", "midi", d, e) && e.code == "perm.origin");
    RT_CHECK(!PermissionService::valid_origin("https://example.com:0"));
    RT_CHECK(!PermissionService::valid_origin("psychovault://newtab"));

    // The verbs: grant and block remember honestly; reset returns to the
    // floor and tells the profile exactly what died.
    RT_CHECK(perms.grant("https://c.example", "notifications", e));
    RT_CHECK(perms.decide("https://c.example", "notifications", d, e) &&
             d == Decision::Allow);
    const size_t saved_before_block = saved.size();
    RT_CHECK(perms.block("https://c.example", "notifications", e) &&
             saved.size() == saved_before_block + 1 && !saved.back().allow);
    RT_CHECK(perms.reset("https://c.example", "notifications", e) &&
             dropped.size() == 1 &&
             dropped[0] == std::make_pair(std::string("https://c.example"),
                                          std::string("notifications")));
    RT_CHECK(perms.decide("https://c.example", "notifications", d, e) &&
             d == Decision::Deny); // back to the floor
    const size_t dropped_before = dropped.size();
    RT_CHECK(perms.reset("https://c.example", "notifications", e) &&
             dropped.size() == dropped_before); // resetting nothing is quiet

    // reset_origin clears every kind the origin held, and names its count.
    const size_t stored_before_site = perms.stored();
    RT_CHECK(perms.grant("https://c.example", "media", e) &&
             perms.grant("https://c.example", "wake-lock", e));
    RT_CHECK(perms.reset_origin("https://c.example", e) == 2 &&
             perms.stored() == stored_before_site);
    RT_CHECK(perms.reset_origin("HTTPS://BAD", e) == 0 && e.code == "perm.origin");

    // Law 3: prompts coalesce per (origin, kind), stay bounded, and
    // persistence of an answer is exactly the remember flag.
    const uint64_t t1 = perms.ask("https://d.example", "geolocation", e);
    RT_CHECK(t1 > 0 && shown.size() == 1 && perms.pending() == 1);
    const uint64_t t1_again = perms.ask("https://d.example", "geolocation", e);
    RT_CHECK(t1_again == t1 && shown.size() == 1); // no dialog stacking
    const uint64_t t2 = perms.ask("https://d.example", "media", e);
    RT_CHECK(t2 > 0 && t2 != t1 && shown.size() == 2 && perms.pending() == 2);
    RT_CHECK(!perms.answer(t1, Decision::Ask, false, e) &&
             e.code == "perm.decision" && perms.pending() == 2);
    const size_t saved_before_answer = saved.size();
    RT_CHECK(perms.answer(t1, Decision::Allow, true, e)); // remember
    RT_CHECK(saved.size() == saved_before_answer + 1 && saved.back().allow &&
             saved.back().origin == "https://d.example");
    RT_CHECK(perms.decide("https://d.example", "geolocation", d, e) &&
             d == Decision::Allow && perms.pending() == 1);
    RT_CHECK(!perms.answer(t1, Decision::Deny, false, e) &&
             e.code == "perm.unknown"); // a dead token stays dead
    RT_CHECK(perms.answer(t2, Decision::Deny, false, e)); // session answer
    RT_CHECK(saved.size() == saved_before_answer + 1);      // nothing crossed
    RT_CHECK(perms.decide("https://d.example", "media", d, e) &&
             d == Decision::Deny);
    // A dead tab owes no record: dropping the prompt is not an answer.
    const uint64_t t3 = perms.ask("https://f.example", "midi", e);
    RT_CHECK(t3 > 0 && perms.drop_prompt(t3) && !perms.drop_prompt(t3));
    RT_CHECK(!perms.answer(t3, Decision::Allow, false, e) &&
             e.code == "perm.unknown");
    // The pending bound refuses by name.
    for (int i = 0; i < 16; ++i) {
      RT_CHECK(perms.ask("https://p" + std::to_string(i) + ".example", "midi",
                         e) > 0);
    }
    RT_CHECK(perms.pending() == PermissionService::kPendingLimit);
    RT_CHECK(perms.ask("https://p16.example", "midi", e) == 0 &&
             e.code == "perm.too-many");
    // A build that cannot show prompts says so instead of hanging.
    {
      PermissionService::Seams nop;
      nop.save = pseams.save;
      nop.load = pseams.load;
      nop.drop = pseams.drop;
      PermissionService voiceless(nop);
      RT_CHECK(voiceless.ask("https://g.example", "midi", e) == 0 &&
               e.code == "perm.unavailable");
    }

    // Law 5: a seamles-profile build is the session store S037 will build.
    {
      PermissionService::Seams sess;
      sess.show_prompt = pseams.show_prompt;
      PermissionService session(sess);
      RT_CHECK(!session.persistence_enabled());
      RT_CHECK(session.load_profile(e)); // nothing to load, honestly so
      // Remembered answers impossible, session answers fine, both named.
      RT_CHECK(!session.grant("https://h.example", "midi", e) &&
               e.code == "perm.unavailable");
      const uint64_t ts = session.ask("https://h.example", "midi", e);
      RT_CHECK(ts > 0 && session.answer(ts, Decision::Allow, false, e));
      RT_CHECK(session.decide("https://h.example", "midi", d, e) &&
               d == Decision::Allow && session.stored() == 1);
      const uint64_t ts2 = session.ask("https://h.example", "wake-lock", e);
      RT_CHECK(ts2 > 0);
      RT_CHECK(!session.answer(ts2, Decision::Deny, true, e) &&
               e.code == "perm.unavailable" && session.pending() == 1);
      RT_CHECK(session.answer(ts2, Decision::Deny, false, e) &&
               session.pending() == 0);
    }

    // The floor reaches the services that consume it: S031's notification
    // law, fed by decide(), keeps the denied denied and the granted heard.
    {
      std::vector<std::string> posted;
      NotificationService notes(
          NotificationService::Seams{[&](const Notice &n) {
            posted.push_back(n.title);
          }});
      auto allows = [&](const std::string &origin, const std::string &kind) {
        Decision dd = Decision::Deny;
        Error ee;
        return perms.decide(origin, kind, dd, ee) && dd == Decision::Allow;
      };
      RT_CHECK(perms.grant("https://n.example", "notifications", e));
      RT_CHECK(notes.notify("https://n.example", "hello", "",
                            allows("https://n.example", "notifications"), e) &&
               posted.size() == 1);
      RT_CHECK(!notes.notify("https://d.example", "hello", "",
                             allows("https://d.example", "notifications"), e) &&
               e.code == "notify.permission");
    }
  }

  shared += rt_passed;
  return 0;
}
