// PsychoVault original source. My WebContents equivalent: the per-tab
// navigation controller - session history, back/forward, reload/stop, and
// title/URL/favicon/state notifications. P1/S027 of docs/Psycho_MASTER-PLAN.md.
//
// Boundary discipline as before: this file owns the navigation RULES and the
// audit log, nothing else. No sockets (fetching is the platform layer's job,
// wired later), no renderer endpoints (S039 crash handling consumes this same
// model), no window handles. Output leaves through one seam: Tab-channel
// envelopes that validate against my stock schemas.
#pragma once
#include "../ipc/Psycho_bus.hpp"
#include <functional>
#include <string>
#include <vector>

namespace pv::rt {

enum class ContentState { Idle, Loading, Failed };
enum class NavKind { New, Back, Forward, Reload };
enum class NavStatus { Pending, Committed, Aborted, Failed };

std::string nav_kind_name(NavKind kind); // "new" | "back" | "forward" | "reload"

struct HistoryEntry {
  int id = -1;              // unique per contents, minted at commit
  std::string url;          // canonical form, post-policy
  std::string title;
  std::string favicon;      // icon token; empty until the page supplies one
  uint32_t navigation = 0;  // the navigation that committed me
};

// Every navigation lives in the audit log forever (bounded, below): accepted,
// aborted, committed or failed, nothing vanishes silently.
struct Navigation {
  uint32_t id = 0;
  NavKind kind = NavKind::New;
  NavStatus status = NavStatus::Pending;
  std::string url;
  int target_index = -1;   // where a committed navigation lands in history
  std::string error_code;  // set when status == Failed
};

class WebContents {
public:
  struct Seams {
    // Every accepted state change is announced here as Tab-channel envelopes.
    std::function<void(const Envelope &)> notify = {};
    // URL policy: admit or refuse, canonicalise in passing. Defaults to my
    // http/https-only rules; S028 widens this seam for the internal scheme
    // behind capabilities - the controller itself never widens.
    std::function<bool(const std::string &raw, std::string &canon, Error &error)> admit = {};
  };

  WebContents(int id, int tab_id, Seams seams);

  // Starting a navigation aborts any pending one first - the log keeps the
  // aborted corpse. Refusals (-1) change nothing at all.
  int go(const std::string &url, Error &error);
  int go_back(Error &error);    // "nav.bounds" at the frontier
  int go_forward(Error &error); // "nav.bounds" at the frontier
  int reload(Error &error);     // "nav.no-entry" before the first commit

  // Resolution of the pending navigation, exactly one of these per pending id:
  bool commit(const std::string &title, const std::string &favicon, Error &error);
  bool fail(const std::string &code, const std::string &message, Error &error);
  bool stop(Error &error); // user stop; no pending navigation is a no-op

  // Live edits to the committed entry (script can retitle the page it is on).
  bool set_title(const std::string &title, Error &error);
  bool set_favicon(const std::string &favicon, Error &error);

  int id() const { return id_; }
  int tab_id() const { return tab_id_; }
  ContentState state() const { return state_; }
  const std::string &error_code() const { return error_code_; }
  int index() const { return index_; } // -1: nothing committed yet
  const HistoryEntry *current() const;
  const std::vector<HistoryEntry> &history() const { return history_; }
  const Navigation *pending() const;
  const std::vector<Navigation> &log() const { return log_; }
  bool can_go_back() const { return index_ > 0; }
  bool can_go_forward() const { return index_ >= 0 && index_ + 1 < (int)history_.size(); }

private:
  int start(NavKind kind, std::string url, int target_index);
  void abort_pending(); // nothing to abort is fine
  void tell(const char *method, Json payload) const; // Tab channel, notify-only
  void apply_metadata(HistoryEntry &entry, const std::string &title,
                      const std::string &favicon);

  int id_ = -1;
  int tab_id_ = -1; // the tab I render into, from S026's model
  Seams seams_;
  std::vector<HistoryEntry> history_;
  int index_ = -1;
  int next_entry_id_ = 1;
  std::vector<Navigation> log_;
  int pending_ = -1; // index into log_, -1: no navigation in flight
  uint32_t next_nav_id_ = 1;
  ContentState state_ = ContentState::Idle;
  std::string error_code_; // meaningful while state_ == Failed
  static constexpr size_t kHistoryLimit = 100; // bounded: oldest frontier dies
  static constexpr size_t kLogLimit = 256;     // bounded audit memory
};

// The stock policy: absolute http(s) URLs only, scheme and host lowercased,
// control bytes refused. psychovault:// arrives in S028 through a seam swap.
bool admit_http_url(const std::string &raw, std::string &canon, Error &error);

} // namespace pv::rt
