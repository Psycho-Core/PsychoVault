// PsychoVault original source. The S039 renderer failure/recovery laws.
#include "Psycho_crash.hpp"
#include <chrono>
#include <exception>
#include <utility>

namespace pv::rt {

Error CrashService::refuse(const char *code, const std::string &message) {
  Error error;
  error.code = code;
  error.message = message;
  return error;
}

std::deque<CrashRecord>::iterator CrashService::find(int id) {
  for (auto it = records_.begin(); it != records_.end(); ++it)
    if (it->id == id) return it;
  return records_.end();
}

bool CrashService::persist(CrashRecord &record, Error &error) {
  if (!seams_.persist_record) {
    error = refuse("crash.persist-unavailable",
                   "no durable crash-record writer is wired");
    return false;
  }
  std::string why;
  try {
    if (!seams_.persist_record(record, why)) {
      error = refuse("crash.persist-failed",
                     why.empty() ? "the crash record was not saved" : why);
      return false;
    }
  } catch (const std::exception &exception) {
    error = refuse("crash.persist-failed", exception.what());
    return false;
  } catch (...) {
    error = refuse("crash.persist-failed",
                   "crash-record writer failed with a non-standard exception");
    return false;
  }
  persisted_ids_.insert(record.id);
  return true;
}

bool CrashService::renderer_failed(int renderer_id, int tab_id,
                                   RendererFailureKind kind,
                                   const std::string &url,
                                   const std::string &reason,
                                   CrashRecord &out, Error &error) {
  error = Error{};
  out = CrashRecord{};
  if (renderer_id <= 0 || tab_id <= 0) {
    error = refuse("crash.identity", "renderer and tab ids must be positive");
    return false;
  }
  if (kind != RendererFailureKind::Crash && kind != RendererFailureKind::Hang) {
    error = refuse("crash.kind", "unknown renderer failure kind");
    return false;
  }
  CrashRecord record;
  record.id = next_id_++;
  record.renderer_id = renderer_id;
  record.tab_id = tab_id;
  record.kind = kind;
  record.url = url;
  record.reason = reason;
  if (seams_.now_ms) {
    try {
      record.timestamp_ms = seams_.now_ms();
    } catch (...) {
      // A timestamp is useful but not a reason to lose the crash itself.
      record.timestamp_ms = 0;
    }
  } else {
    record.timestamp_ms = std::chrono::duration_cast<std::chrono::milliseconds>(
                              std::chrono::system_clock::now().time_since_epoch())
                              .count();
  }
  records_.push_back(record);
  while (records_.size() > kHistoryLimit) {
    persisted_ids_.erase(records_.front().id);
    records_.pop_front();
  }
  auto it = find(record.id);
  bool saved = persist(*it, error);

  Error page_error;
  bool shown = false;
  if (!seams_.show_crash_page) {
    page_error = refuse("crash.page-unavailable",
                        "no adapter is wired for psychovault://crash");
  } else {
    std::string why;
    try {
      shown = seams_.show_crash_page(tab_id, *it, why);
      if (!shown)
        page_error = refuse("crash.page-failed",
                            why.empty() ? "the crash page could not be shown" : why);
    } catch (const std::exception &exception) {
      page_error = refuse("crash.page-failed", exception.what());
    } catch (...) {
      page_error = refuse("crash.page-failed",
                          "crash-page adapter failed with a non-standard exception");
    }
  }
  out = *it;
  if (saved && shown) return true;
  if (saved && page_error) error = std::move(page_error);
  return false;
}

bool CrashService::restart(int record_id, Error &error) {
  error = Error{};
  auto it = find(record_id);
  if (it == records_.end()) {
    error = refuse("crash.record-unknown", "no crash record " +
                                               std::to_string(record_id));
    return false;
  }
  if (it->state != CrashRecordState::CrashPage &&
      it->state != CrashRecordState::RestartFailed) {
    error = refuse("crash.state", "this crash record is not awaiting a restart");
    return false;
  }
  Error persist_error;
  // A previous transient write failure may be retried here, but a restart is
  // never attempted until the crash itself has durable evidence.
  if (!persisted_ids_.count(it->id) && !persist(*it, persist_error)) {
    error = std::move(persist_error);
    return false;
  }
  if (restart_counts_[it->tab_id] >= kRestartLimit) {
    it->state = CrashRecordState::Retired;
    if (!persist(*it, persist_error))
      error = refuse("crash.restart-limit",
                     "restart budget spent; retirement record failed: " +
                         persist_error.message);
    else
      error = refuse("crash.restart-limit",
                     "this tab exhausted its bounded restart budget");
    return false;
  }
  if (!seams_.restart_tab) {
    error = refuse("crash.restart-unavailable", "no tab-restart adapter is wired");
    return false;
  }
  const CrashRecordState previous_state = it->state;
  ++restart_counts_[it->tab_id];
  it->restart_attempts = restart_counts_[it->tab_id];
  it->state = CrashRecordState::Restarting;
  if (!persist(*it, persist_error)) {
    --restart_counts_[it->tab_id];
    --it->restart_attempts;
    it->state = previous_state;
    error = std::move(persist_error);
    return false;
  }

  std::string why;
  bool restarted = false;
  try {
    restarted = seams_.restart_tab(it->tab_id, it->url, why);
  } catch (const std::exception &exception) {
    why = exception.what();
  } catch (...) {
    why = "tab-restart adapter failed with a non-standard exception";
  }
  if (restarted) {
    it->state = CrashRecordState::Restarted;
    if (!persist(*it, persist_error)) {
      error = std::move(persist_error);
      return false; // the tab restarted; the record update did not persist
    }
    return true;
  }

  it->state = CrashRecordState::RestartFailed;
  const std::string failure = why.empty() ? "tab restart failed" : why;
  Error restart_error = refuse("crash.restart-failed", failure);
  if (!persist(*it, persist_error))
    restart_error.message += "; record update failed: " + persist_error.message;
  error = std::move(restart_error);
  return false;
}

void CrashService::renderer_stable(int tab_id) {
  restart_counts_.erase(tab_id);
}

const CrashRecord *CrashService::record(int id) const {
  for (const CrashRecord &record : records_)
    if (record.id == id) return &record;
  return nullptr;
}

int CrashService::restarts_for_tab(int tab_id) const {
  auto it = restart_counts_.find(tab_id);
  return it == restart_counts_.end() ? 0 : it->second;
}

} // namespace pv::rt
