// PsychoVault original source. My singleton app model: one running instance per
// profile, a second launch forwards its arguments and exits, quitting saves the
// session exactly once. P1/S023 of docs/Psycho_MASTER-PLAN.md.
//
// Same honesty boundary as S022: the decision rules here are portable and fully
// tested on Linux; the lock and the forward pipe themselves are OS objects, so
// the Windows implementation lives in Psycho_instance_win32.cpp and is proven
// on the first authorized manual Windows build.
#pragma once
#include "../../engine/Psycho_base.hpp"
#include <functional>
#include <optional>
#include <string>
#include <vector>

namespace pv::rt {

// What a launch carries into the singleton decision. URLs and flags are already
// validated by S041's command line; here they are only cargo.
struct LaunchRequest {
  std::vector<std::string> args;
};

class AppInstance {
public:
  enum class Start {
    // I am the primary instance: I hold the lock and answer forwarded launches.
    Primary,
    // Another me exists: my arguments went to it, and I exit now, cleanly.
    ForwardedAndExit,
    // The lock could not even be evaluated. Structured error, never a guess.
    LockFailed,
  };

  // Everything OS-shaped is an injected seam. The Windows build binds these to
  // named objects (mutex + pipe); tests bind them to a fake world.
  struct Seams {
    // Returns: true = I took the lock (primary). false = it is held (forward +
    // exit). Structured error = lock evaluation failed.
    std::function<std::optional<bool>(std::string &error)> acquire_lock;
    std::function<void()> release_lock;
    // Deliver my launch request to the running primary. Structured error if it
    // cannot be delivered (pipe broken, primary dying, etc).
    std::function<bool(const LaunchRequest &, std::string &error)> forward;
    // Save my session exactly once on the way out.
    std::function<bool(std::string &error)> save_session;
    // The primary receives forwarded launches through this.
    std::function<void(const LaunchRequest &)> on_forwarded;
  };

  explicit AppInstance(Seams seams) : seams_(std::move(seams)) {}

  Start start(const LaunchRequest &request, std::string &error);
  // Calls from the primary's inbox land here.
  void receive_forwarded(const LaunchRequest &request);
  // Graceful quit: session saved first, lock released after, once for one.
  bool quit(std::string &error);
  bool running() const { return state_ == State::Running; }
  bool session_saved() const { return session_saved_; }

private:
  enum class State { NotStarted, Running, Quitting, Stopped };
  Seams seams_;
  State state_ = State::NotStarted;
  bool session_saved_ = false;
};

// The Windows halves of the seams (declared for every platform, defined only in
// the Windows build). windows_instance_seams owns its OS state internally and
// tears it down through release_lock inside quit().
AppInstance::Seams windows_instance_seams(
    const std::string &profile_key,
    std::function<void(const LaunchRequest &)> on_forwarded = {},
    std::function<bool(std::string &)> save_session = {});
bool windows_forward_launch(const std::string &profile_key, const LaunchRequest &request,
                            std::string &error);

} // namespace pv::rt
