// PsychoVault original source. Broker-owned pool of isolated engine processes.
// The broker retains process handles, provides transport, and replaces a dead
// worker under a bounded restart policy.
#pragma once
#include "Psycho_renderer_process.hpp"
#include <memory>
#include <mutex>
#include <vector>

namespace pv::rt {

class RendererProcessFarm {
public:
  RendererProcessFarm() : schemas_(Schemas::stock()) {}
  ~RendererProcessFarm();
  RendererProcessFarm(const RendererProcessFarm &) = delete;
  RendererProcessFarm &operator=(const RendererProcessFarm &) = delete;

  bool start(const std::string &renderer_executable, size_t count, Error &error);
  // A tab owns one dedicated child until release; released processes are stopped
  // so a later tab can never inherit the previous page's renderer memory.
  bool acquire(size_t &renderer, Error &error);
  void release(size_t renderer);
  bool navigate(size_t renderer, const std::string &url, const Host::Fetcher &fetcher,
                Envelope &reply, Error &error,
                Json cookie_state = Json::make_array(),
                Json storage_state = Json::make_object(),
                Host::Poster poster = {},
                RendererProcessClient::BridgeInvoker bridge = {},
                MediaEnvironment media_environment = {});
  bool input(size_t renderer, const Json &command, const Host::Fetcher &fetcher,
             Envelope &reply, Error &error,
             Json cookie_state = Json::make_array(),
             Json storage_state = Json::make_object(),
             Host::Poster poster = {},
             RendererProcessClient::BridgeInvoker bridge = {});
  bool probe_isolation(size_t renderer, const std::string &path,
                       const std::string &host, uint16_t port,
                       Envelope &reply, Error &error);
  // Replaces unexpectedly exited workers; after three replacements it fails
  // closed and reports retirement instead of looping forever.
  bool monitor(Error &error);
  void shutdown();
  size_t size() const;
  size_t restarts(size_t renderer) const;
  unsigned long process_id(size_t renderer) const;

private:
  struct Hosted {
    std::mutex io_mutex;
    WinChildHandle process;
    std::unique_ptr<ProcessTransport> transport;
    std::unique_ptr<RendererProcessClient> client;
    size_t restarts = 0;
    bool leased = false;
    bool retired = false;
  };
  bool spawn(Hosted &renderer, const std::string &executable, Error &error);
  void stop(Hosted &renderer);

  Schemas schemas_;
  mutable std::mutex mutex_;
  std::vector<std::shared_ptr<Hosted>> renderers_;
  std::string executable_;
  bool stopped_ = false;
};

} // namespace pv::rt
