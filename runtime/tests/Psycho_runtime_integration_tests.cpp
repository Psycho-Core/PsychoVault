// PsychoVault original source. Cross-component runtime tests: the seams remain
// fake, but the bus, broker, tabs, navigation, process state, crash policy, and
// permission decisions are exercised together. P1/S042.
#include "../broker/Psycho_broker.hpp"
#include "../broker/Psycho_renderer_harness.hpp"
#include "../content/Psycho_content.hpp"
#include "../crash/Psycho_crash.hpp"
#include "../internal/Psycho_internal.hpp"
#include "../permissions/Psycho_permissions.hpp"
#include "../process/Psycho_process.hpp"
#include "../services/Psycho_services.hpp"
#include "../tabs/Psycho_tabs.hpp"
#include <iostream>
#include <memory>
#include <stdexcept>
#include <utility>
#include <vector>

using namespace pv;
using namespace pv::rt;

namespace {

void integration_check(bool condition, const char *expression, int line,
                       int &checks) {
  if (!condition) {
    std::cerr << "FAIL runtime integration test line " << line << ": "
              << expression << "\n";
    throw std::runtime_error("runtime integration check failed");
  }
  ++checks;
}
#define INTEGRATION_CHECK(x) integration_check(bool(x), #x, __LINE__, checks)

class CommitNavigationService final : public Service {
public:
  explicit CommitNavigationService(WebContents &contents) : contents_(contents) {}
  std::string name() const override { return "navigation-test-adapter"; }

  void handle(const Envelope &request, Envelope &response, Error &error) override {
    if (request.method != "nav:go") {
      error = {"test.nav-method", "integration adapter only accepts nav:go"};
      return;
    }
    const std::string url = request.payload.str("url");
    if (contents_.go(url, error) < 0) return;
    if (!contents_.commit("Integration page", "", error)) return;
    response.channel = Channel::Nav;
    response.method = request.method;
    Json payload = Json::make_object();
    payload.set("url", Json(contents_.current()->url));
    response.payload = std::move(payload);
  }

private:
  WebContents &contents_;
};

} // namespace

int run_runtime_integration_tests(int &shared) {
  int checks = 0;
  Error error;

  // One renderer sends a typed navigation request through the real in-process
  // broker. Its notifications come back through the Tab schema; the second
  // renderer stays isolated. TabModel and WebContents retain matching identity.
  Broker broker;
  const int renderer = broker.spawn_renderer("integration-renderer");
  const int observer = broker.spawn_renderer("unrelated-renderer");
  TabModel tabs;
  INTEGRATION_CHECK(tabs.attach_window(1, error));
  const int tab_id = tabs.open_tab(1, "start", -1, error);
  INTEGRATION_CHECK(tab_id > 0 && tabs.active_in(1)->id == tab_id);

  InternalRegistry registry = InternalRegistry::stock();
  std::vector<Error> notification_errors;
  WebContents contents(
      700, tab_id,
      {[&](const Envelope &notification) {
         Error send_error;
         if (!broker.broker_side(renderer).send(notification, send_error))
           notification_errors.push_back(std::move(send_error));
       },
       [&](const std::string &raw, std::string &canonical, Error &admit_error) {
         return admit_browser_url(raw, registry, canonical, admit_error);
       }});
  broker.add_service(Channel::Nav,
                     std::make_unique<CommitNavigationService>(contents));
  RendererHarness::Seams admission_probes;
  admission_probes.navigation = [&](int id, std::string &detail,
                                    Error &probe_error) {
    if (id != renderer) {
      probe_error = {"test.navigation-renderer", "wrong renderer"};
      return false;
    }
    return admit_browser_url("https://harness.example/", registry, detail,
                             probe_error);
  };
  admission_probes.crash_recover = [](int, std::string &detail, Error &) {
    detail = "S042 crash adapter is exercised below";
    return true;
  };
  RendererHarness admission_harness(broker, std::move(admission_probes));
  RendererQualification qualification;
  INTEGRATION_CHECK(admission_harness.qualify(renderer, qualification, error) &&
                    qualification.admitted && qualification.probes.size() == 3);
  INTEGRATION_CHECK(!admission_harness.admitted(observer));

  auto send_navigation = [&](const std::string &url, uint32_t request_id,
                             std::vector<Envelope> &received) {
    Envelope request;
    request.channel = Channel::Nav;
    request.method = "nav:go";
    request.request_id = request_id;
    Json payload = Json::make_object();
    payload.set("url", Json(url));
    request.payload = std::move(payload);
    Error send_error;
    if (!broker.renderer_side(renderer).send(request, send_error)) return false;
    if (broker.pump() != 1) return false;
    for (;;) {
      Envelope response;
      if (!broker.renderer_side(renderer).recv(response, send_error)) break;
      received.push_back(std::move(response));
    }
    return !send_error;
  };

  // A malformed request is stopped by the channel schema before the service
  // runs. A disallowed scheme reaches the service but cannot mutate history.
  Envelope malformed;
  malformed.channel = Channel::Nav;
  malformed.method = "nav:go";
  malformed.request_id = 1;
  malformed.payload = Json::make_object();
  INTEGRATION_CHECK(!broker.renderer_side(renderer).send(malformed, error) &&
                    error.code == "schema.missing");
  std::vector<Envelope> packets;
  INTEGRATION_CHECK(send_navigation("file:///private/profile", 2, packets));
  INTEGRATION_CHECK(packets.size() == 1 && packets[0].request_id == 2 &&
                    packets[0].payload.str("error").find("nav.scheme") !=
                        std::string::npos);
  INTEGRATION_CHECK(contents.history().empty() && contents.state() == ContentState::Idle);

  // Two successful routes each produce a correlated IPC reply and schema-valid
  // Tab-channel events. Back/forward then revisit the same committed entries.
  packets.clear();
  INTEGRATION_CHECK(send_navigation("https://one.example/a", 10, packets));
  INTEGRATION_CHECK(contents.history().size() == 1 && contents.index() == 0 &&
                    packets.back().request_id == 10 &&
                    packets.back().method == "nav:go");
  packets.clear();
  INTEGRATION_CHECK(send_navigation("https://two.example/b", 11, packets));
  INTEGRATION_CHECK(contents.history().size() == 2 && contents.index() == 1 &&
                    contents.current()->url == "https://two.example/b" &&
                    packets.back().request_id == 11);
  INTEGRATION_CHECK(contents.go_back(error) > 0 &&
                    contents.commit("One revisited", "", error));
  INTEGRATION_CHECK(contents.index() == 0 &&
                    contents.current()->url == "https://one.example/a" &&
                    contents.history().size() == 2);
  INTEGRATION_CHECK(contents.go_forward(error) > 0 &&
                    contents.commit("Two revisited", "", error));
  INTEGRATION_CHECK(contents.index() == 1 &&
                    contents.current()->url == "https://two.example/b" &&
                    contents.history().size() == 2);
  INTEGRATION_CHECK(notification_errors.empty());

  // The fake platform seams still exercise the real supervisor state machine.
  // A renderer death is persisted, shown in the registered crash page, then a
  // replacement child is spawned before the tab returns to its last URL.
  Clock::time_point now{};
  int kills = 0;
  SupervisionPolicy policy;
  policy.heartbeat_timeout = std::chrono::milliseconds(100);
  Supervisor supervisor(
      policy,
      [](ChildRecord &, std::string &) { return true; },
      [&](const ChildRecord &, const std::string &, std::string &) {
        ++kills;
        return true;
      },
      [&]() { return now; });
  std::string process_error;
  const int child = supervisor.spawn("renderer-for-tab", process_error);
  INTEGRATION_CHECK(child > 0 && process_error.empty());
  supervisor.heartbeat(child);
  INTEGRATION_CHECK(supervisor.records()[size_t(child - 1)].state == ChildState::Live);
  INTEGRATION_CHECK(supervisor.request_kill(child, "renderer exited", process_error) &&
                    process_error.empty() && kills == 1 &&
                    supervisor.records()[size_t(child - 1)].state == ChildState::Dead);

  std::vector<CrashRecord> persisted;
  int replacements = 0;
  CrashService crashes({
      [&]() { return int64_t(9000); },
      [&](const CrashRecord &record, std::string &) {
        persisted.push_back(record);
        return true;
      },
      [&](int requested_tab, const CrashRecord &, std::string &page_error) {
        if (requested_tab != tab_id) {
          page_error = "wrong tab";
          return false;
        }
        Error navigation_error;
        if (contents.go(CrashService::crash_page_url(), navigation_error) < 0) {
          page_error = navigation_error.code + ": " + navigation_error.message;
          return false;
        }
        if (!contents.commit("Renderer stopped", "", navigation_error)) {
          page_error = navigation_error.code + ": " + navigation_error.message;
          return false;
        }
        return true;
      },
      [&](int requested_tab, const std::string &url, std::string &restart_error) {
        if (requested_tab != tab_id) {
          restart_error = "wrong tab";
          return false;
        }
        const int replacement = supervisor.spawn("replacement-renderer", restart_error);
        if (replacement <= 0) return false;
        supervisor.heartbeat(replacement);
        ++replacements;
        Error navigation_error;
        if (contents.go(url, navigation_error) < 0) {
          restart_error = navigation_error.code + ": " + navigation_error.message;
          return false;
        }
        if (!contents.commit("Recovered page", "", navigation_error)) {
          restart_error = navigation_error.code + ": " + navigation_error.message;
          return false;
        }
        return true;
      }});
  CrashRecord record;
  INTEGRATION_CHECK(crashes.renderer_failed(child, tab_id, RendererFailureKind::Crash,
                                               contents.current()->url,
                                               "renderer exited", record, error));
  INTEGRATION_CHECK(contents.current()->url == CrashService::crash_page_url() &&
                    record.state == CrashRecordState::CrashPage &&
                    persisted.size() == 1);
  INTEGRATION_CHECK(crashes.restart(record.id, error) && replacements == 1);
  INTEGRATION_CHECK(crashes.record(record.id)->state == CrashRecordState::Restarted &&
                    contents.current()->url == "https://two.example/b" &&
                    supervisor.records().back().state == ChildState::Live);
  INTEGRATION_CHECK(contents.history().size() == 4 &&
                    contents.history()[2].url == CrashService::crash_page_url() &&
                    contents.history()[3].url == "https://two.example/b");

  // PermissionService is the policy source consumed by NotificationService:
  // deny-by-default blocks delivery, and only an explicit grant crosses it.
  std::vector<PermissionGrant> permission_writes;
  PermissionService::Seams permission_seams;
  permission_seams.save = [&](const PermissionGrant &grant) {
    permission_writes.push_back(grant);
  };
  PermissionService permissions(std::move(permission_seams));
  std::vector<Notice> posted;
  NotificationService notifications({[&](const Notice &notice) {
    posted.push_back(notice);
  }});
  const std::string origin = "https://two.example";
  Decision decision = Decision::Allow;
  INTEGRATION_CHECK(permissions.decide(origin, "notifications", decision, error) &&
                    decision == Decision::Deny);
  INTEGRATION_CHECK(!notifications.notify(origin, "blocked", "",
                                           decision == Decision::Allow, error) &&
                    error.code == "notify.permission" && posted.empty());
  INTEGRATION_CHECK(permissions.grant(origin, "notifications", error) &&
                    permission_writes.size() == 1);
  INTEGRATION_CHECK(permissions.decide(origin, "notifications", decision, error) &&
                    decision == Decision::Allow);
  INTEGRATION_CHECK(notifications.notify(origin, "allowed", "ok",
                                          decision == Decision::Allow, error) &&
                    posted.size() == 1 && posted[0].title == "allowed");

  // All accumulated IPC, including crash/restart navigation, still obeys the
  // schemas; no packet or state transition leaked to the unrelated renderer.
  for (;;) {
    Envelope notification;
    if (!broker.renderer_side(renderer).recv(notification, error)) break;
    INTEGRATION_CHECK(broker.schemas().validate(notification.method,
                                                 notification.payload, error));
    INTEGRATION_CHECK(notification.channel == Channel::Tab &&
                      notification.request_id == 0);
  }
  INTEGRATION_CHECK(!error && notification_errors.empty());
  INTEGRATION_CHECK(broker.renderer_side(observer).pending() == 0);
  INTEGRATION_CHECK(tabs.active_in(1)->id == tab_id &&
                    contents.tab_id() == tab_id);
  INTEGRATION_CHECK(persisted.back().state == CrashRecordState::Restarted);

  shared += checks;
  return 0;
}
