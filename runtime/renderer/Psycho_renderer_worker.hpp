// PsychoVault original source. One engine-owning renderer worker behind the
// typed process transport. Its fetcher is injected by the broker so this process
// never opens profile files or sockets itself.
#pragma once
#include "../process/Psycho_process_transport.hpp"
#include "../apphost/Psycho_apphost.hpp"
#include "../../platform/Psycho_host.hpp"
#include <chrono>
#include <memory>
#include <string>
#include <vector>

namespace pv::rt {

// Child-side fetch adapter: requests are routed to a privileged service over the
// same process transport; no renderer-owned socket or profile path is involved.
Host::Fetcher broker_fetcher(ProcessTransport &transport);

class RendererWorker {
public:
  RendererWorker(ProcessTransport &transport, Host::Fetcher broker_fetcher);

  // Handles one broker request. A false return means a transport/protocol error;
  // a navigation failure is instead returned as a structured response.
  bool serve_one(Error &error);
  // Long-running child entry: reads requests until lifecycle:shutdown or EOF.
  int run();

private:
  bool navigate(const Envelope &request, Envelope &response, Error &error);
  bool input(const Envelope &request, Envelope &response, Error &error);
  bool send_images(uint32_t request_id, Error &error);
  bool app_bridge_invoke(Channel channel, const std::string &method,
                         const Json &payload, Json &reply, Error &error);

  struct PendingImage {
    std::string url;
    Image image;
  };
  ProcessTransport &transport_;
  Host::Fetcher broker_fetcher_;
  Host::Poster broker_poster_;
  std::unique_ptr<AppHost> app_host_;
  uint32_t next_bridge_id_ = 0x80000000u;
  std::chrono::steady_clock::time_point started_;
  std::unique_ptr<Host> host_;
  CookieJar cookies_;
  Storage storage_;
  std::vector<PendingImage> pending_images_;
  size_t pending_image_bytes_ = 0;
  bool pending_image_overflow_ = false;
  Json last_frame_;
  std::string navigation_url_;
  std::string url_update_;
  bool stopping_ = false;
};

} // namespace pv::rt
