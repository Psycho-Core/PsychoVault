// PsychoVault original source. One engine-owning renderer worker behind the
// typed process transport. Its fetcher is injected by the broker so this process
// never opens profile files or sockets itself.
#pragma once
#include "../process/Psycho_process_transport.hpp"
#include "../../platform/Psycho_host.hpp"
#include <chrono>
#include <string>

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

  ProcessTransport &transport_;
  Host::Fetcher broker_fetcher_;
  std::chrono::steady_clock::time_point started_;
  bool stopping_ = false;
};

} // namespace pv::rt
