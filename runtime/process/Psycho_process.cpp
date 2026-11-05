// PsychoVault original source. The supervision rules themselves: portable,
// injected-clock deterministic so every path is testable here.
#include "Psycho_process.hpp"

namespace pv::rt {

Supervisor::Supervisor(SupervisionPolicy policy, Launcher launch, Killer kill,
                       std::function<Clock::time_point()> clock)
    : policy_(policy), launch_(std::move(launch)), kill_(std::move(kill)),
      clock_(std::move(clock)) {}

int Supervisor::spawn(const std::string &label, std::string &error) {
  ChildRecord record;
  record.id = next_id_++;
  record.label = label;
  record.started_at = clock_();
  record.last_heartbeat = record.started_at;
  if (!launch_(record, error)) {
    record.state = ChildState::Dead;
    record.attribution = "launch failed: " + error;
    children_.push_back(record);
    index_[record.id] = children_.size() - 1;
    return -1;
  }
  children_.push_back(record);
  index_[record.id] = children_.size() - 1;
  return record.id;
}

void Supervisor::heartbeat(int id) {
  if (!has(id))
    return;
  ChildRecord &record = at(id);
  record.last_heartbeat = clock_();
  if (record.state == ChildState::Starting || record.state == ChildState::Hung)
    record.state = ChildState::Live;
}

std::vector<ChildRecord> Supervisor::sweep() {
  std::vector<ChildRecord> changed;
  auto now = clock_();
  // Copy the ids first: restart() below may grow children_ mid-iteration.
  std::vector<int> ids;
  for (const auto &record : children_)
    ids.push_back(record.id);
  for (int id : ids) {
    if (!has(id))
      continue;
    ChildRecord &record = at(id);
    if (record.state == ChildState::Dead || record.state == ChildState::Retired)
      continue;
    if (now - record.last_heartbeat <= policy_.heartbeat_timeout)
      continue;
    record.state = ChildState::Hung;
    std::string reason = "no heartbeat for " +
                         std::to_string(std::chrono::duration_cast<std::chrono::milliseconds>(
                                            now - record.last_heartbeat)
                                            .count()) +
                         " ms";
    std::string error;
    if (kill_(record, reason, error)) {
      record.attribution = "hung (" + reason + ")";
      changed.push_back(record);
      restart(record, "hang");
    } else {
      record.state = ChildState::Dead;
      record.attribution = "kill failed: " + error;
      changed.push_back(record);
    }
  }
  return changed;
}

void Supervisor::restart(ChildRecord &record, const std::string &reason) {
  if (record.restarts >= policy_.max_restarts) {
    record.state = ChildState::Retired;
    record.attribution += "; restart budget spent after " + std::to_string(record.restarts) +
                          " attempts, last cause: " + reason;
    return;
  }
  record.restarts += 1;
  record.state = ChildState::Starting;
  std::string error;
  record.last_heartbeat = clock_();
  if (!launch_(record, error)) {
    record.state = ChildState::Dead;
    record.attribution += "; relaunch failed: " + error;
  }
}

bool Supervisor::request_kill(int id, const std::string &reason, std::string &error) {
  if (!has(id)) {
    error = "no child " + std::to_string(id);
    return false;
  }
  ChildRecord &record = at(id);
  if (record.state == ChildState::Dead || record.state == ChildState::Retired)
    return true;
  if (!kill_(record, reason, error))
    return false;
  record.state = ChildState::Dead;
  record.attribution = "killed: " + reason;
  return true;
}

#ifndef _WIN32
// The portable half of the platform seam: on this machine there is no process
// table for me to write into, so both hooks answer honestly.
bool spawn_child_platform(const std::string &label, const std::string &command_line,
                          WinChildHandle &out, std::string &error) {
  (void)label;
  (void)command_line;
  (void)out;
  error = "process.spawn.unsupported";
  return false;
}

bool kill_child_platform(const WinChildHandle &handle, const std::string &reason,
                         std::string &error) {
  (void)handle;
  (void)reason;
  error = "process.kill.unsupported";
  return false;
}
#endif

} // namespace pv::rt
