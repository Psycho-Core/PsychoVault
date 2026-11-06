// PsychoVault original source. Broker-side synchronous client for one real
// renderer process. It services nested network requests while a navigation is
// in flight, keeping sockets and profile-backed policy in the broker.
#pragma once
#include "../process/Psycho_process_transport.hpp"
#include "../../platform/Psycho_host.hpp"

namespace pv::rt {

class RendererProcessClient {
public:
  explicit RendererProcessClient(ProcessTransport &transport) : transport_(transport) {}

  // Sends one navigation, services all renderer network requests through the
  // injected privileged fetcher, and returns the correlated rendered-frame reply.
  bool navigate(const std::string &url, const Host::Fetcher &fetcher,
                Envelope &reply, Error &error);
  bool input(const Json &command, const Host::Fetcher &fetcher,
             Envelope &reply, Error &error);
  bool shutdown(Error &error);
  bool probe_isolation(const std::string &path, const std::string &host,
                       uint16_t port, Envelope &reply, Error &error);

private:
  bool exchange(const std::string &method, Json payload,
                const Host::Fetcher &fetcher, Envelope &reply, Error &error);

  ProcessTransport &transport_;
  uint32_t next_request_id_ = 1;
};

} // namespace pv::rt
