// PsychoVault original source. Broker-side synchronous client for one real
// renderer process. It services nested network requests while a navigation is
// in flight, keeping sockets and profile-backed policy in the broker.
#pragma once
#include "../process/Psycho_process_transport.hpp"
#include "../../platform/Psycho_host.hpp"
#include <functional>

namespace pv::rt {

class RendererProcessClient {
public:
  using BridgeInvoker = std::function<bool(Channel, const std::string &, Json,
                                           Json &, Error &)>;
  explicit RendererProcessClient(ProcessTransport &transport) : transport_(transport) {}

  // Sends one navigation, services all renderer network requests through the
  // injected privileged fetcher, and returns the correlated rendered-frame reply.
  // State snapshots are explicit; HttpOnly cookie records are stripped here,
  // while callers remain responsible for origin-scoping visible state.
  bool navigate(const std::string &url, const Host::Fetcher &fetcher,
                Envelope &reply, Error &error,
                Json cookie_state = Json::make_array(),
                Json storage_state = Json::make_object(),
                Host::Poster poster = {}, BridgeInvoker bridge = {},
                MediaEnvironment media_environment = {});
  bool input(const Json &command, const Host::Fetcher &fetcher,
             Envelope &reply, Error &error,
             Json cookie_state = Json::make_array(),
             Json storage_state = Json::make_object(),
             Host::Poster poster = {}, BridgeInvoker bridge = {});
  bool shutdown(Error &error);
  bool probe_isolation(const std::string &path, const std::string &host,
                       uint16_t port, Envelope &reply, Error &error);

private:
  bool exchange(const std::string &method, Json payload,
                const Host::Fetcher &fetcher, Envelope &reply, Error &error,
                const Json &cookie_state, const Json &storage_state,
                const Host::Poster &poster, const BridgeInvoker &bridge);

  ProcessTransport &transport_;
  uint32_t next_request_id_ = 1;
};

} // namespace pv::rt
