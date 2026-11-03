#pragma once
// My proxy configuration and routing policy. A VPN extension drives this through
// the gated chrome.proxy API; it never reaches the network stack directly.
#include "../engine/Psycho_base.hpp"

namespace pv {

struct ProxyServer {
  std::string scheme = "http"; // http | https | socks5
  std::string host;
  int port = 0;
  bool empty() const { return host.empty() || port <= 0; }
  std::string authority() const { return host + ":" + std::to_string(port); }
  bool valid() const;
};

class ProxyConfig {
public:
  enum class Mode { Direct, FixedServers, PacScript, System };
  Mode mode = Mode::Direct;
  ProxyServer single, http, https, fallback;
  std::vector<std::string> bypass;
  std::string pac;
  // When a VPN owns the configuration I refuse to fall back to a direct
  // connection if the chosen proxy is unusable.
  bool fail_closed = false;
  std::string owner_extension;

  static ProxyConfig direct();
  bool proxy_bypasses(const Url &url) const;
  // I return false when routing is refused. `reason` explains a fail-closed
  // refusal so the caller can surface a real error instead of leaking direct.
  bool route(const Url &url, ProxyServer &out, std::string &reason) const;
  std::string mode_name() const;
  Json to_json() const;
  static std::optional<ProxyConfig> from_json(const Json &json, std::string &error);
  bool active() const { return mode != Mode::Direct; }
};

// I evaluate a PAC script with my own interpreter. No OS or network access is
// exposed to the script beyond the standard PAC helper functions.
struct PacResult {
  bool ok = false;
  std::string raw;    // exactly what FindProxyForURL returned
  std::string error;
  std::vector<ProxyServer> servers;
};
PacResult run_pac(const std::string &script, const Url &url, int timeout_instructions = 400000);

// I parse a PAC return value such as "PROXY host:port; DIRECT" into an ordered
// list. An empty list means direct.
std::vector<ProxyServer> parse_pac_result(const std::string &value);
} // namespace pv
