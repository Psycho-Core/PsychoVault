// PsychoVault original source. Real process ownership stays on the broker side.
#include "Psycho_renderer_farm.hpp"
#include <utility>

namespace pv::rt {

RendererProcessFarm::~RendererProcessFarm() { shutdown(); }

bool RendererProcessFarm::spawn(Hosted &renderer, const std::string &executable,
                               Error &error) {
  renderer.process = WinChildHandle{};
  const std::string command_line = "\"" + executable + "\" --pv-renderer-child";
  std::string spawn_error;
  if (!spawn_child_platform("renderer", command_line, renderer.process, spawn_error)) {
    error = {"process.spawn", spawn_error};
    return false;
  }
  renderer.transport = windows_child_transport(renderer.process, Capability::all(),
                                               schemas_, true, error, Limits{}, 30000);
  if (!renderer.transport) {
    std::string ignored;
    kill_child_platform(renderer.process, "transport setup failed", ignored);
    renderer.process = WinChildHandle{};
    return false;
  }
  renderer.client = std::make_unique<RendererProcessClient>(*renderer.transport);
  return true;
}

void RendererProcessFarm::stop(Hosted &renderer) {
  if (renderer.process.process) {
    std::string ignored;
    kill_child_platform(renderer.process, "broker shutdown", ignored);
  }
  renderer.client.reset();
  renderer.transport.reset();
  renderer.process = WinChildHandle{};
}

bool RendererProcessFarm::start(const std::string &renderer_executable, size_t count,
                                Error &error) {
  shutdown();
  if (count < 2 || count > 32) {
    error = {"renderer.farm.count", "renderer farm requires between two and 32 workers"};
    return false;
  }
  std::lock_guard<std::mutex> lock(mutex_);
  executable_ = renderer_executable;
  stopped_ = false;
  renderers_.reserve(count);
  for (size_t i = 0; i < count; ++i) {
    auto renderer = std::make_shared<Hosted>();
    std::lock_guard<std::mutex> io_lock(renderer->io_mutex);
    if (!spawn(*renderer, executable_, error)) {
      for (const auto &started : renderers_) {
        std::lock_guard<std::mutex> started_lock(started->io_mutex);
        stop(*started);
      }
      renderers_.clear();
      executable_.clear();
      stopped_ = true;
      return false;
    }
    renderers_.push_back(std::move(renderer));
  }
  error = Error{};
  return true;
}

bool RendererProcessFarm::acquire(size_t &renderer, Error &error) {
  std::vector<std::shared_ptr<Hosted>> candidates;
  std::string executable;
  {
    std::lock_guard<std::mutex> lock(mutex_);
    if (stopped_ || executable_.empty()) {
      error = {"renderer.farm.stopped", "renderer farm is not running"};
      return false;
    }
    candidates = renderers_;
    executable = executable_;
  }
  for (size_t i = 0; i < candidates.size(); ++i) {
    Hosted &candidate = *candidates[i];
    std::lock_guard<std::mutex> io_lock(candidate.io_mutex);
    {
      std::lock_guard<std::mutex> lock(mutex_);
      if (stopped_) {
        error = {"renderer.farm.stopped", "renderer farm is not running"};
        return false;
      }
    }
    if (candidate.leased || candidate.retired) continue;
    candidate.leased = true;
    if (candidate.process.process &&
        !child_process_running_platform(candidate.process))
      stop(candidate);
    if (!candidate.process.process && !spawn(candidate, executable, error)) {
      candidate.retired = true;
      candidate.leased = false;
      return false;
    }
    {
      std::lock_guard<std::mutex> lock(mutex_);
      if (stopped_) {
        stop(candidate);
        candidate.leased = false;
        error = {"renderer.farm.stopped", "renderer farm stopped during allocation"};
        return false;
      }
    }
    renderer = i;
    error = Error{};
    return true;
  }

  auto candidate = std::make_shared<Hosted>();
  candidate->leased = true;
  size_t index = 0;
  {
    std::lock_guard<std::mutex> lock(mutex_);
    if (stopped_ || executable_.empty()) {
      error = {"renderer.farm.stopped", "renderer farm is not running"};
      return false;
    }
    if (renderers_.size() >= 64) {
      error = {"renderer.farm.limit", "renderer process limit reached"};
      return false;
    }
    index = renderers_.size();
    renderers_.push_back(candidate);
    executable = executable_;
  }
  std::lock_guard<std::mutex> io_lock(candidate->io_mutex);
  {
    std::lock_guard<std::mutex> lock(mutex_);
    if (stopped_) {
      candidate->leased = false;
      error = {"renderer.farm.stopped", "renderer farm stopped during allocation"};
      return false;
    }
  }
  if (!spawn(*candidate, executable, error)) {
    candidate->retired = true;
    candidate->leased = false;
    return false;
  }
  renderer = index;
  error = Error{};
  return true;
}

void RendererProcessFarm::release(size_t renderer) {
  std::shared_ptr<Hosted> released;
  {
    std::lock_guard<std::mutex> lock(mutex_);
    if (renderer >= renderers_.size()) return;
    released = renderers_[renderer];
  }
  std::lock_guard<std::mutex> io_lock(released->io_mutex);
  if (!released->leased) return;
  stop(*released);
  released->leased = false;
  released->retired = false;
  released->restarts = 0;
}

bool RendererProcessFarm::navigate(size_t renderer, const std::string &url,
                                   const Host::Fetcher &fetcher, Envelope &reply,
                                   Error &error, Json cookie_state,
                                   Json storage_state, Host::Poster poster,
                                   RendererProcessClient::BridgeInvoker bridge) {
  std::shared_ptr<Hosted> hosted;
  std::string executable;
  {
    std::lock_guard<std::mutex> lock(mutex_);
    if (renderer >= renderers_.size()) {
      error = {"renderer.farm.missing", "renderer process is not running"};
      return false;
    }
    hosted = renderers_[renderer];
    executable = executable_;
  }
  std::lock_guard<std::mutex> io_lock(hosted->io_mutex);
  if (!hosted->client) {
    error = {"renderer.farm.missing", "renderer process is not running"};
    return false;
  }
  if (hosted->client->navigate(url, fetcher, reply, error, std::move(cookie_state),
                               std::move(storage_state), std::move(poster),
                               std::move(bridge)))
    return true;
  if (error.code.rfind("process.pipe.", 0) != 0 &&
      error.code.rfind("process.read.", 0) != 0 &&
      error.code != "process.closed" &&
      error.code != "renderer.protocol.unexpected")
    return false;

  const Error navigation_error = error;
  if (hosted->restarts >= 3) {
    stop(*hosted);
    hosted->retired = true;
    error = {"renderer.farm.retired", "renderer " + std::to_string(renderer) +
                                          " exhausted its restart budget after an IPC failure"};
    return false;
  }
  const size_t attempts = hosted->restarts + 1;
  stop(*hosted);
  hosted->restarts = attempts;
  bool is_stopped = false;
  {
    std::lock_guard<std::mutex> lock(mutex_);
    is_stopped = stopped_;
  }
  Error replacement_error;
  if (is_stopped || !spawn(*hosted, executable, replacement_error)) {
    hosted->retired = true;
    error = {"renderer.farm.restart", "renderer " + std::to_string(renderer) +
                                          " failed during IPC recovery: " +
                                          replacement_error.message};
    return false;
  }
  error = navigation_error;
  return false;
}

bool RendererProcessFarm::input(size_t renderer, const Json &command,
                                const Host::Fetcher &fetcher, Envelope &reply,
                                Error &error, Json cookie_state,
                                Json storage_state, Host::Poster poster,
                                RendererProcessClient::BridgeInvoker bridge) {
  std::shared_ptr<Hosted> hosted;
  {
    std::lock_guard<std::mutex> lock(mutex_);
    if (renderer >= renderers_.size()) {
      error = {"renderer.farm.missing", "renderer process is not running"};
      return false;
    }
    hosted = renderers_[renderer];
  }
  std::lock_guard<std::mutex> io_lock(hosted->io_mutex);
  if (!hosted->client) {
    error = {"renderer.farm.missing", "renderer process is not running"};
    return false;
  }
  return hosted->client->input(command, fetcher, reply, error,
                               std::move(cookie_state), std::move(storage_state),
                               std::move(poster), std::move(bridge));
}

bool RendererProcessFarm::probe_isolation(size_t renderer,
                                           const std::string &path,
                                           const std::string &host,
                                           uint16_t port, Envelope &reply,
                                           Error &error) {
  std::shared_ptr<Hosted> hosted;
  {
    std::lock_guard<std::mutex> lock(mutex_);
    if (renderer >= renderers_.size()) {
      error = {"renderer.farm.missing", "renderer process is not running"};
      return false;
    }
    hosted = renderers_[renderer];
  }
  std::lock_guard<std::mutex> io_lock(hosted->io_mutex);
  if (!hosted->client) {
    error = {"renderer.farm.missing", "renderer process is not running"};
    return false;
  }
  return hosted->client->probe_isolation(path, host, port, reply, error);
}

bool RendererProcessFarm::monitor(Error &error) {
  std::vector<std::shared_ptr<Hosted>> renderers;
  std::string executable;
  {
    std::lock_guard<std::mutex> lock(mutex_);
    if (stopped_) {
      error = {"renderer.farm.stopped", "renderer farm is not running"};
      return false;
    }
    renderers = renderers_;
    executable = executable_;
  }
  for (size_t i = 0; i < renderers.size(); ++i) {
    Hosted &renderer = *renderers[i];
    std::unique_lock<std::mutex> io_lock(renderer.io_mutex, std::try_to_lock);
    if (!io_lock.owns_lock()) continue;
    if (renderer.retired) {
      error = {"renderer.farm.retired", "renderer " + std::to_string(i) +
                                                " is retired after a failed replacement"};
      return false;
    }
    if (!renderer.process.process || child_process_running_platform(renderer.process))
      continue;
    if (renderer.restarts >= 3) {
      stop(renderer);
      renderer.retired = true;
      error = {"renderer.farm.retired", "renderer " + std::to_string(i) +
                                                " exited after its restart budget was spent"};
      return false;
    }
    const size_t attempts = renderer.restarts + 1;
    stop(renderer);
    renderer.restarts = attempts;
    if (!spawn(renderer, executable, error)) {
      renderer.retired = true;
      error = {"renderer.farm.restart", "renderer " + std::to_string(i) +
                                                " replacement failed: " + error.message};
      return false;
    }
  }
  error = Error{};
  return true;
}

size_t RendererProcessFarm::size() const {
  std::lock_guard<std::mutex> lock(mutex_);
  return renderers_.size();
}

size_t RendererProcessFarm::restarts(size_t renderer) const {
  std::shared_ptr<Hosted> hosted;
  {
    std::lock_guard<std::mutex> lock(mutex_);
    if (renderer >= renderers_.size()) return 0;
    hosted = renderers_[renderer];
  }
  std::lock_guard<std::mutex> io_lock(hosted->io_mutex);
  return hosted->restarts;
}

unsigned long RendererProcessFarm::process_id(size_t renderer) const {
  std::shared_ptr<Hosted> hosted;
  {
    std::lock_guard<std::mutex> lock(mutex_);
    if (renderer >= renderers_.size()) return 0;
    hosted = renderers_[renderer];
  }
  std::lock_guard<std::mutex> io_lock(hosted->io_mutex);
  return hosted->process.pid;
}

void RendererProcessFarm::shutdown() {
  std::vector<std::shared_ptr<Hosted>> renderers;
  {
    std::lock_guard<std::mutex> lock(mutex_);
    if (stopped_ && renderers_.empty()) return;
    stopped_ = true;
    renderers.swap(renderers_);
    executable_.clear();
  }
  for (const auto &renderer : renderers) {
    std::lock_guard<std::mutex> io_lock(renderer->io_mutex);
    stop(*renderer);
    renderer->leased = false;
  }
}

} // namespace pv::rt
