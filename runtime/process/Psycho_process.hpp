// PsychoVault original source. My process supervision core - launch, heartbeat,
// hang detection, kill, bounded restart, crash attribution. P1/S022 of
// docs/Psycho_MASTER-PLAN.md.
//
// Honesty boundary, as in the rest of the runtime:
//  - This header plus Psycho_process.cpp are portable: the supervision rules a
//    broker applies to its children. They are tested here on Linux.
//  - Psycho_process_win32.cpp is the Windows half: CreateProcess with a job
//    object that kills the tree on close, a restricted token plus AppContainer
//    identity (no network capability), allowlisted stdio pipes, and a resume.
//    It only compiles on Windows and is proven on
//    the first authorized manual Windows build; until then the sandbox pid in a
//    ChildRecord is evidence of intent, not of a running process.
#pragma once
#include "../../engine/Psycho_base.hpp"
#include <chrono>
#include <cstdint>
#include <functional>
#include <map>
#include <string>
#include <vector>

namespace pv::rt {

struct Error;
using Clock = std::chrono::steady_clock;

enum class ChildState { Starting, Live, Hung, Dead, Retired };

struct ChildRecord {
  int id = -1;
  std::string label;
  ChildState state = ChildState::Starting;
  Clock::time_point started_at;
  Clock::time_point last_heartbeat;
  int restarts = 0;
  // Why this child is not Live anymore, in one sentence. A crash I cannot
  // explain is the bug I cannot fix, so attribution is a required field.
  std::string attribution;
};

struct SupervisionPolicy {
  // A child gets this many restarts before I retire it: a process that will not
  // stay alive is a report, not a retry loop.
  int max_restarts = 3;
  std::chrono::milliseconds heartbeat_timeout{10000};
  // Restart backoff grows linearly: 1x, 2x, 3x of this.
  std::chrono::milliseconds backoff{250};
};

class Supervisor {
public:
  // launch: the platform hook that turns a ChildRecord into a running child.
  // kill: the platform hook that stops one. Both answer their own error.
  using Launcher = std::function<bool(ChildRecord &record, std::string &error)>;
  using Killer = std::function<bool(const ChildRecord &record, const std::string &reason,
                                    std::string &error)>;

  Supervisor(SupervisionPolicy policy, Launcher launch, Killer kill,
             std::function<Clock::time_point()> clock);

  // S022 semantics: a spawn is synchronous; the child is Live only once it
  // answers its first heartbeat.
  int spawn(const std::string &label, std::string &error);
  void heartbeat(int id);
  // The sweep is the supervisor's clock tick: it finds hung children, kills
  // them, attributes the reason and restarts within the policy budget.
  std::vector<ChildRecord> sweep();
  bool request_kill(int id, const std::string &reason, std::string &error);
  const std::vector<ChildRecord> &records() const { return children_; }
  bool has(int id) const { return index_.count(id) != 0; }

private:
  ChildRecord &at(int id) { return children_[index_.at(id)]; }
  void restart(ChildRecord &record, const std::string &reason);

  SupervisionPolicy policy_;
  Launcher launch_;
  Killer kill_;
  std::function<Clock::time_point()> clock_;
  std::vector<ChildRecord> children_;
  std::map<int, size_t> index_;
  int next_id_ = 1;
};

// The platform halves. Psycho_process_win32.cpp implements these with real
// CreateProcess/job-object calls when compiling for Windows; everywhere else
// they answer a structured "unsupported" so tests never mistake a stub for a
// process.
struct WinChildHandle {
  intptr_t process = 0;  // HANDLE, kept opaque here.
  intptr_t job = 0;
  intptr_t to_child = 0; // write end of my pipe to the child
  intptr_t from_child = 0; // read end of the child's pipe to me
  unsigned long pid = 0;
};
bool spawn_child_platform(const std::string &label, const std::string &command_line,
                          WinChildHandle &out, std::string &error);
bool kill_child_platform(const WinChildHandle &handle, const std::string &reason,
                         std::string &error);
bool child_process_running_platform(const WinChildHandle &handle);
bool process_pipe_read(intptr_t pipe, std::string &bytes, Error &error);
bool process_pipe_read_timeout(intptr_t pipe, std::string &bytes, Error &error,
                               uint32_t timeout_ms);
bool process_pipe_write(intptr_t pipe, const std::string &bytes, Error &error);

} // namespace pv::rt
