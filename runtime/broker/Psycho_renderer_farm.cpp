// PsychoVault original source. Real process ownership stays on the broker side.
#include "Psycho_renderer_farm.hpp"

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
  std::lock_guard<std::mutex> lock(mutex_);
  if (count < 2 || count > 32) {
    error = {"renderer.farm.count", "renderer farm requires between two and 32 workers"};
    return false;
  }
  executable_ = renderer_executable;
  stopped_ = false;
  renderers_.resize(count);
  for (Hosted &renderer : renderers_) {
    if (!spawn(renderer, executable_, error)) {
      for (Hosted &started : renderers_) stop(started);
      renderers_.clear();
      executable_.clear();
      stopped_ = true;
      return false;
    }
  }
  error = Error{};
  return true;
}

bool RendererProcessFarm::navigate(size_t renderer, const std::string &url,
                                   const Host::Fetcher &fetcher, Envelope &reply,
                                   Error &error) {
  std::lock_guard<std::mutex> lock(mutex_);
  if (renderer >= renderers_.size() || !renderers_[renderer].client) {
    error = {"renderer.farm.missing", "renderer process is not running"};
    return false;
  }
  Hosted &hosted = renderers_[renderer];
  if (hosted.client->navigate(url, fetcher, reply, error)) return true;
  if (error.code.rfind("process.pipe.", 0) != 0 &&
      error.code.rfind("process.read.", 0) != 0 &&
      error.code != "process.closed" &&
      error.code != "renderer.protocol.unexpected")
    return false;

  const Error navigation_error = error;
  if (hosted.restarts >= 3) {
    stop(hosted);
    hosted.retired = true;
    error = {"renderer.farm.retired", "renderer " + std::to_string(renderer) +
                                          " exhausted its restart budget after an IPC failure"};
    return false;
  }
  const size_t attempts = hosted.restarts + 1;
  stop(hosted);
  hosted.restarts = attempts;
  Error replacement_error;
  if (!spawn(hosted, executable_, replacement_error)) {
    hosted.retired = true;
    error = {"renderer.farm.restart", "renderer " + std::to_string(renderer) +
                                          " failed during IPC recovery: " +
                                          replacement_error.message};
    return false;
  }
  error = navigation_error;
  return false;
}

bool RendererProcessFarm::probe_isolation(size_t renderer,
                                           const std::string &path,
                                           const std::string &host,
                                           uint16_t port, Envelope &reply,
                                           Error &error) {
  std::lock_guard<std::mutex> lock(mutex_);
  if (renderer >= renderers_.size() || !renderers_[renderer].client) {
    error = {"renderer.farm.missing", "renderer process is not running"};
    return false;
  }
  return renderers_[renderer].client->probe_isolation(path, host, port, reply, error);
}

bool RendererProcessFarm::monitor(Error &error) {
  std::lock_guard<std::mutex> lock(mutex_);
  for (size_t i = 0; i < renderers_.size(); ++i) {
    Hosted &renderer = renderers_[i];
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
    if (!spawn(renderer, executable_, error)) {
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
  std::lock_guard<std::mutex> lock(mutex_);
  return renderer < renderers_.size() ? renderers_[renderer].restarts : 0;
}

unsigned long RendererProcessFarm::process_id(size_t renderer) const {
  std::lock_guard<std::mutex> lock(mutex_);
  return renderer < renderers_.size() ? renderers_[renderer].process.pid : 0;
}

void RendererProcessFarm::shutdown() {
  std::lock_guard<std::mutex> lock(mutex_);
  if (stopped_ && renderers_.empty()) return;
  stopped_ = true;
  for (Hosted &renderer : renderers_) stop(renderer);
  renderers_.clear();
  executable_.clear();
}

} // namespace pv::rt
