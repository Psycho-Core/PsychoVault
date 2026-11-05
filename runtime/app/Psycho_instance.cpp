// PsychoVault original source. The singleton's decision rules. No OS objects
// live here, which is why every rule is testable on this machine.
#include "Psycho_instance.hpp"

namespace pv::rt {

AppInstance::Start AppInstance::start(const LaunchRequest &request, std::string &error) {
  if (state_ == State::Running) {
    error.clear();
    return Start::Primary; // Starting the running instance twice is a no-op, not a second me.
  }
  std::optional<bool> acquired = seams_.acquire_lock(error);
  if (!acquired)
    return Start::LockFailed; // error is filled by the seam
  if (*acquired) {
    state_ = State::Running;
    error.clear();
    return Start::Primary;
  }
  // Second instance: hand my arguments to the primary and leave. Delivery is
  // synchronous: if the primary cannot take them I fail the handoff and the
  // caller hears why, because dropping a launch silently is worse.
  if (!seams_.forward(request, error))
    return Start::LockFailed;
  error.clear();
  return Start::ForwardedAndExit;
}

void AppInstance::receive_forwarded(const LaunchRequest &request) {
  if (state_ != State::Running)
    return;
  if (seams_.on_forwarded)
    seams_.on_forwarded(request);
}

bool AppInstance::quit(std::string &error) {
  if (state_ != State::Running)
    return true; // quitting a stopped instance is already true
  state_ = State::Quitting;
  // Session first: if the save fails, I stop quitting and keep the instance.
  if (!session_saved_ && seams_.save_session) {
    if (!seams_.save_session(error)) {
      state_ = State::Running;
      return false;
    }
    session_saved_ = true;
  }
  state_ = State::Stopped;
  if (seams_.release_lock)
    seams_.release_lock();
  error.clear();
  return true;
}

} // namespace pv::rt

// The portable side of the platform seam lives with the Windows implementation
// in Psycho_instance_win32.cpp; that file answers "unsupported" off-Windows so
// tests here never mistake a stub for a lock.
