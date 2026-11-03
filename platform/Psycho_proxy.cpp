#include "Psycho_proxy.hpp"
#include "../engine/Psycho_js.hpp"
#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <winsock2.h>
#include <ws2tcpip.h>
#else
#include <arpa/inet.h>
#include <netdb.h>
#include <sys/socket.h>
#endif

namespace pv {
namespace {
// I deliberately do not use a third-party glob or CIDR library.
bool glob_match(std::string_view pattern, std::string_view text) {
  size_t p = 0, t = 0, star = std::string_view::npos, mark = 0;
  while (t < text.size()) {
    if (p < pattern.size() && (pattern[p] == '?' || pattern[p] == text[t])) {
      ++p;
      ++t;
    } else if (p < pattern.size() && pattern[p] == '*') {
      star = p++;
      mark = t;
    } else if (star != std::string_view::npos) {
      p = star + 1;
      t = ++mark;
    } else {
      return false;
    }
  }
  while (p < pattern.size() && pattern[p] == '*') ++p;
  return p == pattern.size();
}
bool parse_ipv4(const std::string &text, uint32_t &out) {
  unsigned parts[4] = {0, 0, 0, 0};
  int seen = 0;
  size_t i = 0;
  while (i < text.size()) {
    if (seen >= 4) return false;
    unsigned value = 0;
    size_t digits = 0;
    while (i < text.size() && text[i] >= '0' && text[i] <= '9') {
      value = value * 10 + unsigned(text[i] - '0');
      if (value > 255) return false;
      ++digits;
      ++i;
    }
    if (!digits) return false;
    parts[seen++] = value;
    if (i < text.size()) {
      if (text[i] != '.') return false;
      ++i;
      if (i >= text.size()) return false;
    }
  }
  if (seen != 4) return false;
  out = parts[0] << 24 | parts[1] << 16 | parts[2] << 8 | parts[3];
  return true;
}
std::string resolve_a(const std::string &host) {
  uint32_t literal = 0;
  if (parse_ipv4(host, literal)) return host;
  addrinfo hints{};
  hints.ai_family = AF_INET;
  hints.ai_socktype = SOCK_STREAM;
  addrinfo *result = nullptr;
  if (getaddrinfo(host.c_str(), nullptr, &hints, &result) != 0 || !result) return {};
  std::string out;
  for (addrinfo *it = result; it; it = it->ai_next) {
    if (it->ai_family != AF_INET) continue;
    char buffer[INET_ADDRSTRLEN] = {0};
    auto *addr = reinterpret_cast<sockaddr_in *>(it->ai_addr);
#ifdef _WIN32
    if (InetNtopA(AF_INET, &addr->sin_addr, buffer, sizeof(buffer))) out = buffer;
#else
    if (inet_ntop(AF_INET, &addr->sin_addr, buffer, sizeof(buffer))) out = buffer;
#endif
    if (!out.empty()) break;
  }
  freeaddrinfo(result);
  return out;
}
} // namespace

bool ProxyServer::valid() const {
  if (host.empty() || port <= 0 || port > 65535) return false;
  if (scheme != "http" && scheme != "https" && scheme != "socks5") return false;
  for (char c : host) {
    if (!(std::isalnum(static_cast<unsigned char>(c)) || c == '.' || c == '-' || c == ':' ||
          c == '[' || c == ']'))
      return false;
  }
  return true;
}

ProxyConfig ProxyConfig::direct() { return ProxyConfig{}; }

std::string ProxyConfig::mode_name() const {
  switch (mode) {
  case Mode::Direct: return "direct";
  case Mode::FixedServers: return "fixed_servers";
  case Mode::PacScript: return "pac_script";
  case Mode::System: return "system";
  }
  return "direct";
}

bool ProxyConfig::proxy_bypasses(const Url &url) const {
  std::string host = lower(url.host);
  // A VPN must never claim to route loopback traffic it cannot actually proxy.
  if (host == "localhost" || host == "127.0.0.1" || host == "::1" || host == "[::1]") return true;
  for (const auto &entry : bypass) {
    std::string rule = lower(trim(entry));
    if (rule.empty()) continue;
    if (rule == "<local>") {
      if (host.find('.') == std::string::npos) return true;
      continue;
    }
    if (rule.size() > 2 && rule.front() == '.' && rule.back() == '.') {
      // ".example.com." style entries are normalised by stripping the dots.
      rule = rule.substr(1, rule.size() - 2);
    }
    if (!rule.empty() && rule.front() == '.') {
      if (host.size() >= rule.size() && host.compare(host.size() - rule.size(), rule.size(), rule) == 0)
        return true;
      if (host == rule.substr(1)) return true;
      continue;
    }
    if (rule.find('/') != std::string::npos) {
      auto slash = rule.find('/');
      uint32_t ip = 0, network = 0;
      int bits = 0;
      try { bits = std::stoi(rule.substr(slash + 1)); } catch (...) { continue; }
      if (bits < 0 || bits > 32) continue;
      if (parse_ipv4(host, ip) && parse_ipv4(rule.substr(0, slash), network)) {
        uint32_t mask = bits ? uint32_t(0xffffffffu << (32 - bits)) : 0;
        if ((ip & mask) == (network & mask)) return true;
      }
      continue;
    }
    if (rule.find('*') != std::string::npos || rule.find('?') != std::string::npos) {
      if (glob_match(rule, host)) return true;
      continue;
    }
    if (host == rule) return true;
  }
  return false;
}

std::vector<ProxyServer> parse_pac_result(const std::string &value) {
  std::vector<ProxyServer> out;
  for (const auto &chunk : split(value, ';')) {
    auto entry = trim(chunk);
    if (entry.empty()) continue;
    auto space = entry.find_first_of(" \t");
    std::string kind = lower(space == std::string::npos ? entry : entry.substr(0, space));
    std::string rest = space == std::string::npos ? "" : trim(entry.substr(space + 1));
    if (kind == "direct") {
      out.push_back(ProxyServer{}); // an empty server means direct
      continue;
    }
    ProxyServer server;
    if (kind == "proxy") server.scheme = "http";
    else if (kind == "https") server.scheme = "https";
    else if (kind == "socks" || kind == "socks5") server.scheme = "socks5";
    else continue;
    auto colon = rest.rfind(':');
    if (colon == std::string::npos) continue;
    server.host = trim(rest.substr(0, colon));
    try { server.port = std::stoi(trim(rest.substr(colon + 1))); } catch (...) { continue; }
    if (server.valid()) out.push_back(server);
  }
  return out;
}

bool ProxyConfig::route(const Url &url, ProxyServer &out, std::string &reason) const {
  out = ProxyServer{};
  reason.clear();
  if (mode == Mode::Direct) return true;
  if (mode == Mode::System) {
    reason = "system proxy resolution is delegated to the platform";
    return true;
  }
  if (proxy_bypasses(url)) return true;
  if (mode == Mode::FixedServers) {
    const ProxyServer &chosen = !single.empty() ? single
                                : url.scheme == "https" && !https.empty() ? https
                                : url.scheme == "http" && !http.empty() ? http
                                : fallback;
    if (chosen.empty() || !chosen.valid()) {
      if (fail_closed) {
        reason = "fail-closed: no valid proxy is configured for this URL";
        return false;
      }
      return true;
    }
    out = chosen;
    return true;
  }
  // PAC
  if (pac.empty()) {
    if (fail_closed) {
      reason = "fail-closed: the PAC script is empty";
      return false;
    }
    return true;
  }
  auto result = run_pac(pac, url);
  if (!result.ok) {
    if (fail_closed) {
      reason = "fail-closed: PAC evaluation failed: " + result.error;
      return false;
    }
    return true;
  }
  for (const auto &server : result.servers) {
    if (server.empty()) return true; // explicit DIRECT
    if (server.valid()) {
      out = server;
      return true;
    }
  }
  if (fail_closed) {
    reason = "fail-closed: PAC returned no usable proxy";
    return false;
  }
  return true;
}

Json ProxyConfig::to_json() const {
  auto server_json = [](const ProxyServer &s) {
    Json j = Json::make_object();
    j.set("scheme", s.scheme);
    j.set("host", s.host);
    j.set("port", s.port);
    return j;
  };
  Json j = Json::make_object();
  j.set("mode", mode_name());
  j.set("singleProxy", server_json(single));
  j.set("proxyForHttp", server_json(http));
  j.set("proxyForHttps", server_json(https));
  j.set("fallbackProxy", server_json(fallback));
  j.set("pac", pac);
  j.set("failClosed", fail_closed);
  j.set("ownerExtension", owner_extension);
  Json list = Json::make_array();
  for (const auto &b : bypass) list.array.push_back(Json(b));
  j.set("bypassList", std::move(list));
  return j;
}

std::optional<ProxyConfig> ProxyConfig::from_json(const Json &json, std::string &error) {
  if (json.type != Json::Type::Object) {
    error = "proxy settings must be an object";
    return std::nullopt;
  }
  ProxyConfig config;
  auto mode = json.str("mode", "direct");
  if (mode == "direct") config.mode = Mode::Direct;
  else if (mode == "fixed_servers") config.mode = Mode::FixedServers;
  else if (mode == "pac_script") config.mode = Mode::PacScript;
  else if (mode == "system") config.mode = Mode::System;
  else {
    error = "unsupported proxy mode: " + mode;
    return std::nullopt;
  }
  auto read_server = [&](const char *key, ProxyServer &server) -> bool {
    const Json *value = json.find(key);
    if (!value || value->type != Json::Type::Object) return true;
    ProxyServer candidate;
    candidate.scheme = lower(value->str("scheme", "http"));
    candidate.host = trim(value->str("host"));
    candidate.port = int(value->num("port", 0));
    if (candidate.host.empty() && candidate.port == 0) return true;
    if (!candidate.valid()) {
      error = std::string("invalid proxy server in ") + key;
      return false;
    }
    server = candidate;
    return true;
  };
  if (!read_server("singleProxy", config.single) || !read_server("proxyForHttp", config.http) ||
      !read_server("proxyForHttps", config.https) || !read_server("fallbackProxy", config.fallback))
    return std::nullopt;
  config.pac = json.str("pac");
  if (config.pac.size() > 256 * 1024) {
    error = "PAC script exceeds my size limit";
    return std::nullopt;
  }
  config.fail_closed = json.flag("failClosed", false);
  config.owner_extension = json.str("ownerExtension");
  const Json *list = json.find("bypassList");
  if (list && list->type == Json::Type::Array) {
    for (const auto &entry : list->array) {
      if (config.bypass.size() >= 512) break;
      if (entry.type == Json::Type::String) config.bypass.push_back(entry.string);
    }
  }
  if (config.mode == Mode::FixedServers && config.single.empty() && config.http.empty() &&
      config.https.empty() && config.fallback.empty()) {
    error = "fixed_servers mode needs at least one proxy server";
    return std::nullopt;
  }
  if (config.mode == Mode::PacScript && config.pac.empty()) {
    error = "pac_script mode needs a script";
    return std::nullopt;
  }
  return config;
}

PacResult run_pac(const std::string &script, const Url &url, int timeout_instructions) {
  PacResult result;
  if (script.size() > 256 * 1024) {
    result.error = "PAC script exceeds my size limit";
    return result;
  }
  try {
    js::Runtime runtime;
    auto &global = runtime.global();
    auto str_arg = [](js::Values &args, size_t index) -> std::string {
      return index < args.size() ? args[index].to_string() : std::string();
    };
    global.properties["isPlainHostName"] = runtime.make_native([&](js::Values &args) {
      return js::Value(str_arg(args, 0).find('.') == std::string::npos);
    });
    global.properties["dnsDomainIs"] = runtime.make_native([&](js::Values &args) {
      auto host = lower(str_arg(args, 0));
      auto domain = lower(str_arg(args, 1));
      if (host.size() < domain.size()) return js::Value(false);
      return js::Value(host.compare(host.size() - domain.size(), domain.size(), domain) == 0);
    });
    global.properties["localHostOrDomainIs"] = runtime.make_native([&](js::Values &args) {
      auto host = lower(str_arg(args, 0));
      auto full = lower(str_arg(args, 1));
      return js::Value(full == host || (full.size() > host.size() &&
                                       full.compare(0, host.size(), host) == 0 &&
                                       full[host.size()] == '.'));
    });
    global.properties["shExpMatch"] = runtime.make_native([&](js::Values &args) {
      return js::Value(glob_match(str_arg(args, 1), str_arg(args, 0)));
    });
    global.properties["dnsDomainLevels"] = runtime.make_native([&](js::Values &args) {
      auto host = str_arg(args, 0);
      return js::Value(double(std::count(host.begin(), host.end(), '.')));
    });
    global.properties["dnsResolve"] = runtime.make_native([&](js::Values &args) {
      auto resolved = resolve_a(str_arg(args, 0));
      return resolved.empty() ? js::Value::null() : js::Value(resolved);
    });
    global.properties["isResolvable"] = runtime.make_native([&](js::Values &args) {
      return js::Value(!resolve_a(str_arg(args, 0)).empty());
    });
    global.properties["isInNet"] = runtime.make_native([&](js::Values &args) {
      auto address = resolve_a(str_arg(args, 0));
      uint32_t ip = 0, network = 0, mask = 0;
      if (address.empty() || !parse_ipv4(address, ip) ||
          !parse_ipv4(str_arg(args, 1), network) || !parse_ipv4(str_arg(args, 2), mask))
        return js::Value(false);
      return js::Value((ip & mask) == (network & mask));
    });
    global.properties["myIpAddress"] = runtime.make_native([&](js::Values &) {
      // I report loopback rather than enumerating adapters inside a PAC script.
      // A VPN extension supplies its own address through chrome.proxy if needed.
      return js::Value(std::string("127.0.0.1"));
    });
    global.properties["alert"] = runtime.make_native([&](js::Values &) { return js::Value(); });
    runtime.run(script);
    auto fn = global.properties["FindProxyForURL"];
    if (fn.type != js::Value::Type::Object || !fn.object || fn.object->kind != js::Object::Kind::Function) {
      result.error = "the PAC script does not define FindProxyForURL";
      return result;
    }
    // I bound the work a PAC script may do so a hostile script cannot hang routing.
    runtime.reset_fuel();
    (void)timeout_instructions;
    auto value = runtime.call(fn, {js::Value(url.href()), js::Value(url.host)});
    result.raw = value.to_string();
    result.servers = parse_pac_result(result.raw);
    result.ok = true;
  } catch (js::JsError &e) {
    result.error = e.value.to_string();
  } catch (const std::exception &e) {
    result.error = e.what();
  }
  return result;
}
} // namespace pv
