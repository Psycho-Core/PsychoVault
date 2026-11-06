// PsychoVault original source. My renderer crash coordinator: persist the
// failure, present psychovault://crash, and restart the affected tab under a
// bounded retry budget. It consumes S022 supervision events; the broker wires
// its durable profile and renderer/tab adapters. P1/S039 of
// docs/Psycho_MASTER-PLAN.md.
#pragma once
#include "../ipc/Psycho_bus.hpp"
#include <deque>
#include <functional>
#include <map>
#include <set>
#include <string>
#include <utility>

namespace pv::rt {

enum class RendererFailureKind { Crash, Hang };
enum class CrashRecordState { CrashPage, Restarting, Restarted, RestartFailed, Retired };

struct CrashRecord {
  int id = 0;
  int renderer_id = 0;
  int tab_id = 0;
  RendererFailureKind kind = RendererFailureKind::Crash;
  int64_t timestamp_ms = 0;
  std::string url;
  std::string reason;
  int restart_attempts = 0;
  CrashRecordState state = CrashRecordState::CrashPage;
};

class CrashService {
public:
  struct Seams {
    std::function<int64_t()> now_ms;
    // A durable profile writer that upserts by record.id. Restart is refused
    // until the crash record has made it through this seam; the crash page is
    // still shown when initial persistence fails.
    std::function<bool(const CrashRecord &, std::string &error)> persist_record;
    // The adapter navigates this tab to the registered internal crash page.
    std::function<bool(int tab_id, const CrashRecord &, std::string &error)>
        show_crash_page;
    // Recreates the crashed renderer/tab at its last committed URL.
    std::function<bool(int tab_id, const std::string &url,
                       std::string &error)> restart_tab;
  };

  explicit CrashService(Seams seams) : seams_(std::move(seams)) {}

  // Called by the renderer/process monitor for a crash OR a hang attributed by
  // S022. Persists the record, then shows the crash page even if persistence
  // failed so the user is not left with a dead surface.
  bool renderer_failed(int renderer_id, int tab_id, RendererFailureKind kind,
                       const std::string &url, const std::string &reason,
                       CrashRecord &out, Error &error);
  // User/system retry from the crash page. Attempts count against the tab until
  // its replacement is reported stable; budget exhaustion retires the record.
  bool restart(int record_id, Error &error);
  void renderer_stable(int tab_id);

  const CrashRecord *record(int id) const;
  const std::deque<CrashRecord> &records() const { return records_; }
  int restarts_for_tab(int tab_id) const;

  static constexpr const char *crash_page_url() { return "psychovault://crash"; }
  static constexpr int kRestartLimit = 3;
  static constexpr size_t kHistoryLimit = 100;

private:
  std::deque<CrashRecord>::iterator find(int id);
  bool persist(CrashRecord &record, Error &error);
  static Error refuse(const char *code, const std::string &message);

  Seams seams_;
  std::deque<CrashRecord> records_;
  std::set<int> persisted_ids_;
  std::map<int, int> restart_counts_;
  int next_id_ = 1;
};

} // namespace pv::rt
