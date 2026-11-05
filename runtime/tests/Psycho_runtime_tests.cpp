#include "../app/Psycho_instance.hpp"
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

  shared += rt_passed;
  return 0;
}
