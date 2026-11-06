// PsychoVault original source. S043 admission tests: a renderer remains
// quarantined until echo, navigation, and crash/recovery probes all pass.
#include "../broker/Psycho_renderer_harness.hpp"
#include "../content/Psycho_content.hpp"
#include "../crash/Psycho_crash.hpp"
#include "../internal/Psycho_internal.hpp"
#include "../process/Psycho_process.hpp"
#include "../tabs/Psycho_tabs.hpp"
#include <iostream>
#include <stdexcept>
#include <utility>
#include <vector>

using namespace pv;
using namespace pv::rt;

namespace {

void harness_check(bool condition, const char *expression, int line,
                   int &checks) {
  if (!condition) {
    std::cerr << "FAIL renderer harness test line " << line << ": "
              << expression << "\n";
    throw std::runtime_error("renderer harness test failed");
  }
  ++checks;
}
#define HARNESS_CHECK(x) harness_check(bool(x), #x, __LINE__, checks)

Envelope harness_nav(const std::string &url, uint32_t id) {
  Envelope request;
  request.channel = Channel::Nav;
  request.method = "nav:go";
  request.request_id = id;
  request.payload = Json::make_object();
  request.payload.set("url", Json(url));
  return request;
}

bool receive_one(Endpoint &endpoint, Envelope &response) {
  Error error;
  return endpoint.recv(response, error) && !error;
}

} // namespace

int run_renderer_harness_tests(int &shared) {
  int checks = 0;
  Broker broker;
  const int renderer = broker.spawn_renderer("qualification-candidate");
  const int other = broker.spawn_renderer("not-yet-qualified");
  Error error;

  HARNESS_CHECK(renderer >= 0 && other >= 0 && broker.has_renderer(renderer));
  HARNESS_CHECK(!broker.renderer_qualified(renderer) &&
                !broker.renderer_qualified(other));

  // Before admission, ordinary UI traffic is refused by Broker::pump even
  // though the typed endpoint can serialize it. The refusal is correlated.
  HARNESS_CHECK(broker.renderer_side(renderer).send(
      harness_nav("https://blocked.example/", 9), error));
  HARNESS_CHECK(broker.pump() == 1);
  Envelope refused;
  HARNESS_CHECK(receive_one(broker.renderer_side(renderer), refused));
  HARNESS_CHECK(refused.request_id == 9 &&
                refused.payload.str("error").find("broker.renderer-unqualified") !=
                    std::string::npos);

  // A pre-existing request cannot have its refusal consumed by the harness.
  HARNESS_CHECK(broker.renderer_side(other).send(
      harness_nav("https://pending.example/", 10), error));
  RendererHarness::Seams pending_seams;
  pending_seams.navigation = [](int, std::string &, Error &) { return true; };
  pending_seams.crash_recover = [](int, std::string &, Error &) { return true; };
  RendererHarness pending_harness(broker, std::move(pending_seams));
  RendererQualification pending_report;
  HARNESS_CHECK(!pending_harness.qualify(other, pending_report, error) &&
                error.code == "renderer.harness.echo");
  HARNESS_CHECK(pending_report.probes.size() == 1 &&
                !pending_report.probes[0].passed &&
                !broker.renderer_qualified(other));
  HARNESS_CHECK(broker.pump() == 1);
  HARNESS_CHECK(receive_one(broker.renderer_side(other), refused) &&
                refused.request_id == 10 &&
                refused.payload.str("error").find("broker.renderer-unqualified") !=
                    std::string::npos);

  bool crash_probe_called = false;
  RendererHarness::Seams failing_seams;
  failing_seams.navigation = [](int, std::string &detail, Error &probe_error) {
    detail = "navigation did not commit";
    probe_error = {"test.navigation", detail};
    return false;
  };
  failing_seams.crash_recover = [&](int, std::string &, Error &) {
    crash_probe_called = true;
    return true;
  };
  RendererHarness failing_harness(broker, std::move(failing_seams));
  RendererQualification failed;
  HARNESS_CHECK(!failing_harness.qualify(renderer, failed, error) &&
                error.code == "renderer.harness.navigation");
  HARNESS_CHECK(!failed.admitted && !broker.renderer_qualified(renderer) &&
                failed.probes.size() == 2 &&
                failed.probes[0].probe == RendererProbe::Echo &&
                failed.probes[0].passed &&
                failed.probes[1].probe == RendererProbe::Navigation &&
                !failed.probes[1].passed && !crash_probe_called);

  // Build a real in-process navigation/session and process/crash fixture for
  // the passing candidate. Only the OS process boundary remains fake.
  TabModel tabs;
  HARNESS_CHECK(tabs.attach_window(3, error));
  const int tab_id = tabs.open_tab(3, "initial", -1, error);
  HARNESS_CHECK(tab_id > 0);
  InternalRegistry registry = InternalRegistry::stock();
  const Schemas schemas = Schemas::stock();
  std::vector<Envelope> notifications;
  WebContents contents(
      81, tab_id,
      {[&](const Envelope &notification) { notifications.push_back(notification); },
       [&](const std::string &raw, std::string &canonical, Error &admit_error) {
         return admit_browser_url(raw, registry, canonical, admit_error);
       }});
  bool nav_committed = false;
  std::string initial_url;
  int killed = 0, replacements = 0;
  std::vector<CrashRecord> persisted;
  std::string process_error;
  SupervisionPolicy policy;
  Supervisor supervisor(
      policy,
      [](ChildRecord &, std::string &) { return true; },
      [&](const ChildRecord &, const std::string &, std::string &) {
        ++killed;
        return true;
      },
      [] { return Clock::time_point{}; });
  const int child = supervisor.spawn("candidate-renderer", process_error);
  HARNESS_CHECK(child > 0 && process_error.empty());
  supervisor.heartbeat(child);
  CrashService crash_service({
      [] { return int64_t(2222); },
      [&](const CrashRecord &record, std::string &) {
        persisted.push_back(record);
        return true;
      },
      [&](int requested_tab, const CrashRecord &, std::string &why) {
        if (requested_tab != tab_id) {
          why = "crash page addressed the wrong tab";
          return false;
        }
        Error nav_error;
        if (contents.go(CrashService::crash_page_url(), nav_error) < 0 ||
            !contents.commit("Crash page", "", nav_error)) {
          why = nav_error.code + ": " + nav_error.message;
          return false;
        }
        return true;
      },
      [&](int requested_tab, const std::string &url, std::string &why) {
        if (requested_tab != tab_id) {
          why = "replacement addressed the wrong tab";
          return false;
        }
        const int replacement = supervisor.spawn("qualified-replacement", why);
        if (replacement <= 0) return false;
        supervisor.heartbeat(replacement);
        ++replacements;
        Error nav_error;
        if (contents.go(url, nav_error) < 0 ||
            !contents.commit("Recovered", "", nav_error)) {
          why = nav_error.code + ": " + nav_error.message;
          return false;
        }
        return true;
      }});

  RendererHarness::Seams passing_seams;
  passing_seams.navigation = [&](int id, std::string &detail, Error &probe_error) {
    if (id != renderer) {
      probe_error = {"test.renderer-id", "wrong renderer was probed"};
      return false;
    }
    initial_url = "https://ready.example/start";
    if (contents.go(initial_url, probe_error) < 0 ||
        !contents.commit("Harness page", "", probe_error)) {
      detail = probe_error.code + ": " + probe_error.message;
      return false;
    }
    nav_committed = contents.current() && contents.current()->url == initial_url;
    detail = nav_committed ? contents.current()->url : "navigation did not commit";
    return nav_committed;
  };
  passing_seams.crash_recover = [&](int id, std::string &detail,
                                    Error &probe_error) {
    if (id != renderer) {
      probe_error = {"test.renderer-id", "wrong renderer was recovered"};
      return false;
    }
    if (!supervisor.request_kill(child, "harness crash probe", process_error)) {
      probe_error = {"test.renderer-kill", process_error};
      return false;
    }
    CrashRecord record;
    if (!crash_service.renderer_failed(child, tab_id, RendererFailureKind::Crash,
                                       initial_url, "harness crash probe", record,
                                       probe_error))
      return false;
    if (!crash_service.restart(record.id, probe_error)) return false;
    detail = "crash page shown and replacement committed " + contents.current()->url;
    return killed == 1 && replacements == 1 &&
           crash_service.record(record.id)->state == CrashRecordState::Restarted &&
           contents.current()->url == initial_url &&
           supervisor.records().back().state == ChildState::Live;
  };
  RendererHarness passing_harness(broker, std::move(passing_seams));
  RendererQualification passed;
  HARNESS_CHECK(passing_harness.qualify(renderer, passed, error) && !error);
  HARNESS_CHECK(passed.passed && passed.admitted && passed.probes.size() == 3 &&
                broker.renderer_qualified(renderer));
  HARNESS_CHECK(passed.probes[0].probe == RendererProbe::Echo &&
                passed.probes[0].passed && passed.probes[0].elapsed_us >= 0 &&
                passed.probes[0].detail.find("nonce echoed") != std::string::npos);
  HARNESS_CHECK(passed.probes[1].probe == RendererProbe::Navigation &&
                passed.probes[1].passed && nav_committed &&
                passed.probes[1].detail == initial_url);
  HARNESS_CHECK(passed.probes[2].probe == RendererProbe::CrashRecovery &&
                passed.probes[2].passed &&
                passed.probes[2].detail.find("replacement committed") !=
                    std::string::npos);
  HARNESS_CHECK(contents.history().size() == 3 &&
                contents.history()[1].url == CrashService::crash_page_url() &&
                contents.current()->url == initial_url && persisted.back().state ==
                    CrashRecordState::Restarted);
  HARNESS_CHECK(notifications.size() >= 12);
  for (const Envelope &notification : notifications)
    HARNESS_CHECK(notification.channel == Channel::Tab &&
                  schemas.validate(notification.method, notification.payload, error));

  // Admission allows ordinary broker routing; a missing UI service still gets
  // its own refusal rather than the quarantine refusal. A crash invalidates it.
  HARNESS_CHECK(broker.renderer_side(renderer).send(
      harness_nav("https://after-harness.example/", 91), error));
  HARNESS_CHECK(broker.pump() == 1);
  Envelope no_service;
  HARNESS_CHECK(receive_one(broker.renderer_side(renderer), no_service));
  HARNESS_CHECK(no_service.request_id == 91 &&
                no_service.payload.str("error").find("broker.renderer-unqualified") ==
                    std::string::npos);
  passing_harness.invalidate(renderer);
  HARNESS_CHECK(!passing_harness.admitted(renderer));
  HARNESS_CHECK(broker.renderer_side(renderer).send(
      harness_nav("https://after-crash.example/", 92), error));
  HARNESS_CHECK(broker.pump() == 1);
  HARNESS_CHECK(receive_one(broker.renderer_side(renderer), refused) &&
                refused.request_id == 92 &&
                refused.payload.str("error").find("broker.renderer-unqualified") !=
                    std::string::npos);
  HARNESS_CHECK(!broker.renderer_qualified(other) &&
                broker.renderer_side(other).pending() == 0);

  // A thrown probe cannot leave stale UI access enabled after requalification.
  RendererHarness::Seams throwing_seams;
  throwing_seams.navigation = [](int, std::string &, Error &) -> bool {
    throw std::runtime_error("navigation probe crashed");
  };
  throwing_seams.crash_recover = [](int, std::string &, Error &) { return true; };
  RendererHarness throwing_harness(broker, std::move(throwing_seams));
  RendererQualification thrown;
  HARNESS_CHECK(!throwing_harness.qualify(renderer, thrown, error) &&
                error.code == "renderer.harness.navigation" &&
                !throwing_harness.admitted(renderer));
  HARNESS_CHECK(thrown.probes.size() == 2 &&
                thrown.probes[1].detail.find("navigation probe crashed") !=
                    std::string::npos);

  RendererHarness::Seams missing_seams;
  missing_seams.navigation = [](int, std::string &detail, Error &) {
    detail = "navigation adapter passed";
    return true;
  };
  RendererHarness missing_harness(broker, std::move(missing_seams));
  RendererQualification missing;
  HARNESS_CHECK(!missing_harness.qualify(other, missing, error) &&
                error.code == "renderer.harness.crash-recover" &&
                !missing_harness.admitted(other));
  HARNESS_CHECK(missing.probes.size() == 3 && missing.probes[0].passed &&
                missing.probes[1].passed && !missing.probes[2].passed &&
                missing.probes[2].detail.find("not wired") != std::string::npos);

  shared += checks;
  return 0;
}
