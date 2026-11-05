// PsychoVault original source. My broker download service: start, progress,
// pause/resume where the wire supports it, cancel, open, reveal, save-as,
// history - the law in front of the transport. P1/S035 of
// docs/Psycho_MASTER-PLAN.md.
//
// Layering: pv::DownloadManager (platform/) is the transport - threads,
// sockets, spool files. This service is the broker surface the UI and pages
// talk to, and it keeps the law no transport may bend; the M1 slice adapts
// the two through the seams below. Unwired seams refuse "...unavailable".
//
// The laws, stated so nobody forgets them:
//   1. EVERY DOWNLOAD LIVES IN ONE HISTORY. Ids are monotonic and never
//      reused, even when the transport refuses before the first byte - the
//      attempt is a record. At most 32 unfinished downloads; the history
//      keeps the newest 200 records (a record over the cap is always
//      terminal, because the active cap is smaller).
//   2. THE STATE MACHINE IS NARROW. running <-> paused, then exactly one
//      terminal state: complete, cancelled or failed. Terminals are
//      terminal. Save-as chooses the target only while bytes can still move;
//      after that the file is where it is.
//   3. PAUSE NEEDS A YES FROM THE WIRE. Pause is legal only while running
//      AND the response's latest word on range support was yes; resume
//      re-enters from a pause, naming the byte we already hold. "Where
//      supported" is enforced here, not hoped for.
//   4. EVERY BYTE IS ACCOUNTED, ONCE. Progress never runs backwards and
//      never passes a known total. A completion that arrives short of the
//      known total is not a refusal - it is a FAILED download, recorded
//      honestly as "download.truncated".
//   5. OPEN AND REVEAL TOUCH ONLY WHAT FINISHED. They refuse running
//      downloads by name, refuse downloads with no chosen target, and refuse
//      files the disk no longer holds.
#pragma once
#include "../ipc/Psycho_bus.hpp"
#include <deque>
#include <functional>
#include <string>

namespace pv::rt {

enum class DownloadState { Running, Paused, Complete, Cancelled, Failed };
const char *download_state_name(DownloadState state);

struct DownloadRecord {
  int id = 0;
  DownloadState state = DownloadState::Running;
  std::string url;
  std::string target;          // final path; "" = save-as not chosen yet
  std::string suggested_name;  // sanitized from the URL at start
  uint64_t received = 0;
  uint64_t total = 0;          // 0 = the wire has not said
  bool resumable = false;      // the response's latest word on ranges
  int transport = -1;          // the transport's handle while live
  std::string error;           // set on Failed
};

class DownloadService {
public:
  struct Seams {
    // The transport. Hand 0 back to say "I refused", anything else is the
    // handle every later call carries. Absent = this build cannot download.
    std::function<int(const DownloadRecord &)> begin_download = {};
    std::function<void(int transport)> pause_download = {};
    std::function<void(int transport, uint64_t resume_from)> resume_download = {};
    std::function<void(int transport)> cancel_download = {};
    // Shell touchpoints, honestly absent until the Windows build wires them.
    std::function<void(const std::string &path)> open_file = {};
    std::function<void(const std::string &path)> reveal_file = {};
    std::function<bool(const std::string &path)> file_exists = {};
    // UI invalidation; optional - the state law never needs a listener.
    std::function<void(const DownloadRecord &)> on_change = {};
  };
  explicit DownloadService(Seams seams) : seams_(std::move(seams)) {}

  // Returns the new id, or 0 with the refusal named. The attempt is always
  // recorded: even a transport refusal leaves a Failed record (law 1).
  int start(const std::string &url, const std::string &target, Error &error);
  // The transport's progress reports. total 0 keeps the size unknown; the
  // resumable flag is the response's latest word (law 3).
  bool progress(int id, uint64_t received, uint64_t total, bool resumable,
                Error &error);
  bool pause(int id, Error &error);
  bool resume(int id, Error &error);
  bool cancel(int id, Error &error);
  // Short-of-total completion becomes Failed(truncated) and still returns
  // true (law 4): the event happened, the record tells the truth.
  bool complete(int id, Error &error);
  bool fail(int id, const std::string &why, Error &error);
  // Save-as (law 2): only while bytes can still move.
  bool set_target(int id, const std::string &path, Error &error);
  bool open(int id, Error &error);
  bool reveal(int id, Error &error);

  // History (law 1). Removing a single record or clearing the history never
  // touches downloads still in flight; trying refuses "download.state".
  const DownloadRecord *record(int id) const;
  const std::deque<DownloadRecord> &history() const { return history_; }
  bool remove_record(int id, Error &error);
  void clear_history();
  size_t active_count() const;

  // The URL's final path segment, query-off, cut to safe characters - the
  // name a save-as dialog may pre-fill without ever trusting the wire.
  static std::string sanitize_filename(const std::string &url);

  static constexpr size_t kActiveLimit = 32;
  static constexpr size_t kHistoryLimit = 200;

private:
  void changed(const DownloadRecord &rec); // fires on_change when wired
  static bool terminal(const DownloadRecord &rec);
  // Bounded history (law 1) makes a scan the honest index: at most 200.
  std::deque<DownloadRecord>::iterator find(int id);
  std::deque<DownloadRecord>::const_iterator find(int id) const;

  Seams seams_;
  std::deque<DownloadRecord> history_; // oldest first
  int next_id_ = 1;
};

} // namespace pv::rt
