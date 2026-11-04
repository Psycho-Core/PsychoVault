#include "Psycho_network.hpp"
#include "Psycho_transport.hpp"
#include <filesystem>
#include <fstream>
#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <shlobj.h>
#include <winhttp.h>
#endif

namespace pv {
namespace {
std::string host_matches_list(const std::vector<std::string> &list, const std::string &host) {
  auto lower_host = lower(host);
  for (const auto &entry : list) {
    auto rule = lower(trim(entry));
    if (rule.empty()) continue;
    if (rule.front() == '.') {
      if (lower_host.size() >= rule.size() &&
          lower_host.compare(lower_host.size() - rule.size(), rule.size(), rule) == 0)
        return rule;
      if (lower_host == rule.substr(1)) return rule;
      continue;
    }
    if (rule == lower_host) return rule;
    if (lower_host.size() > rule.size() &&
        lower_host.compare(lower_host.size() - rule.size() - 1, rule.size() + 1, "." + rule) == 0)
      return rule;
  }
  return {};
}
} // namespace

bool policy_blocks(const NetworkPolicy &policy, const Url &url, std::string &reason) {
  if (!url.network()) {
    reason = "I only fetch http and https URLs";
    return true;
  }
  if (policy.block_ads) {
    auto exception = host_matches_list(policy.allowed_hosts, url.host);
    if (!exception.empty()) return false;
    auto blocked = host_matches_list(policy.blocked_hosts, url.host);
    if (!blocked.empty()) {
      reason = "blocked by my ad blocker: " + blocked;
      return true;
    }
  }
  return false;
}

std::string safe_download_name(const Url &url, const std::string &content_disposition) {
  std::string name;
  auto header = lower(content_disposition);
  auto position = header.find("filename=");
  if (position != std::string::npos) {
    auto value = trim(content_disposition.substr(position + 9));
    auto semicolon = value.find(';');
    if (semicolon != std::string::npos) value = trim(value.substr(0, semicolon));
    if (value.size() >= 2 && value.front() == '"' && value.back() == '"')
      value = value.substr(1, value.size() - 2);
    name = value;
  }
  if (name.empty()) {
    auto path = url.path;
    auto slash = path.find_last_of('/');
    name = slash == std::string::npos ? path : path.substr(slash + 1);
  }
  if (name.empty()) name = "download";
  // I strip anything that could escape the download directory or name an
  // alternate data stream, and I refuse reserved Windows device names.
  std::string clean;
  for (char c : name) {
    if (c == '/' || c == '\\' || c == ':' || c == '\0') clean += '_';
    else if (static_cast<unsigned char>(c) < 32) clean += '_';
    else if (c == '<' || c == '>' || c == '"' || c == '|' || c == '?' || c == '*') clean += '_';
    else clean += c;
  }
  // I collapse dot runs so no ".." survives anywhere in the name, even though
  // removing the separators already made traversal impossible.
  std::string collapsed;
  for (char c : clean) {
    if (c == '.' && !collapsed.empty() && collapsed.back() == '.') continue;
    collapsed += c;
  }
  clean = collapsed;
  while (!clean.empty() && (clean.back() == '.' || clean.back() == ' ')) clean.pop_back();
  while (!clean.empty() && clean.front() == '.') clean.erase(0, 1);
  if (clean.empty()) clean = "download";
  if (clean.size() > 120) {
    auto dot = clean.find_last_of('.');
    auto extension = dot != std::string::npos && clean.size() - dot <= 12 ? clean.substr(dot) : "";
    clean = clean.substr(0, 120 - extension.size()) + extension;
  }
  static const std::set<std::string> reserved = {
      "con", "prn", "aux", "nul", "com1", "com2", "com3", "com4", "com5", "com6", "com7",
      "com8", "com9", "lpt1", "lpt2", "lpt3", "lpt4", "lpt5", "lpt6", "lpt7", "lpt8", "lpt9"};
  auto stem = lower(clean.substr(0, clean.find('.')));
  if (reserved.count(stem)) clean = "_" + clean;
  return clean;
}

std::string unique_download_path(const std::string &directory, const std::string &name) {
  namespace fs = std::filesystem;
  auto candidate = fs::path(directory) / name;
  if (!fs::exists(candidate)) return candidate.string();
  auto dot = name.find_last_of('.');
  auto stem = dot == std::string::npos ? name : name.substr(0, dot);
  auto extension = dot == std::string::npos ? std::string() : name.substr(dot);
  for (int index = 1; index < 10000; ++index) {
    candidate = fs::path(directory) / (stem + " (" + std::to_string(index) + ")" + extension);
    if (!fs::exists(candidate)) return candidate.string();
  }
  return (fs::path(directory) / (stem + "." + std::to_string(now_ms()) + extension)).string();
}

std::string default_download_directory() {
#ifdef _WIN32
  wchar_t *path = nullptr;
  if (SUCCEEDED(SHGetKnownFolderPath(FOLDERID_Downloads, 0, nullptr, &path)) && path) {
    int size = WideCharToMultiByte(CP_UTF8, 0, path, -1, nullptr, 0, nullptr, nullptr);
    std::string out(size ? size - 1 : 0, '\0');
    if (size) WideCharToMultiByte(CP_UTF8, 0, path, -1, out.data(), size, nullptr, nullptr);
    CoTaskMemFree(path);
    if (!out.empty()) return out;
  }
  if (path) CoTaskMemFree(path);
#endif
  const char *home = std::getenv("HOME");
  const char *profile = std::getenv("USERPROFILE");
  std::string base = home ? home : profile ? profile : ".";
  return (std::filesystem::path(base) / "Downloads").string();
}


// My own transport is not a Windows feature. The routing decision and the policy it
// reads belong to both builds, so they sit outside the WinHTTP section and the
// headless build reaches the same tunnel the windowed one does.
namespace {
// Credentials for the proxy that was actually routed to. My own VPN panel records
// them on the server they belong to, and an extension hands them over for the proxy it
// named, so a login is never sent to a proxy it was not entered for.
bool proxy_credentials(const NetworkPolicy &policy, const ProxyServer &routed,
                       std::string &user, std::string &password) {
  if (routed.empty())
    return false;
  if (policy.proxy.has_auth() && lower(policy.proxy.single.host) == lower(routed.host)) {
    user = policy.proxy.user;
    password = policy.proxy.password;
    return true;
  }
  if (policy.has_proxy_auth && !policy.proxy_user.empty()) {
    user = policy.proxy_user;
    password = policy.proxy_pass;
    return true;
  }
  return false;
}

// True when this request has to go through my own transport. A false answer with an
// empty server means the policy asked for a direct request; a false answer when the
// proxy is active but unusable comes back as a fail-closed error instead.
bool tunnel_route(const Url &url, const NetworkPolicy &policy, ProxyServer &server,
                  std::string &reason) {
  if (!policy.proxy.active())
    return false;
  if (!policy.proxy.route(url, server, reason))
    return true; // fail-closed: the caller reads the reason and sends nothing
  if (server.empty())
    return false; // bypassed, so a direct request is what policy asked for
  return server.scheme == "socks5" || server.scheme == "https";
}

TunnelConfig tunnel_config(const NetworkPolicy &policy, const ProxyServer &server, const Url &url) {
  TunnelConfig config;
  config.proxy = server;
  config.has_auth = proxy_credentials(policy, server, config.user, config.pass);
  config.user_agent = policy.user_agent;
  config.accept_language = policy.accept_language;
  config.send_dnt = policy.send_dnt;
  config.strip_referrer = policy.strip_referrer;
  config.fail_closed = true;
  if (policy.cookies) {
    bool cross_site =
        !policy.top_level_host.empty() && lower(policy.top_level_host) != lower(url.host);
    config.cookie_header = policy.cookies->header_for(url, double(now_ms()) / 1000.0, cross_site,
                                                      policy.block_third_party_cookies);
  }
  return config;
}
} // namespace

#ifdef _WIN32
namespace {
std::wstring wide(const std::string &text) {
  if (text.empty()) return {};
  int size = MultiByteToWideChar(CP_UTF8, 0, text.c_str(), int(text.size()), nullptr, 0);
  std::wstring out(size, 0);
  MultiByteToWideChar(CP_UTF8, 0, text.c_str(), int(text.size()), out.data(), size);
  return out;
}
std::string narrow(const std::wstring &text) {
  if (text.empty()) return {};
  int size = WideCharToMultiByte(CP_UTF8, 0, text.c_str(), int(text.size()), nullptr, 0, nullptr,
                                 nullptr);
  std::string out(size, 0);
  WideCharToMultiByte(CP_UTF8, 0, text.c_str(), int(text.size()), out.data(), size, nullptr, nullptr);
  return out;
}

// I open a WinHTTP session honouring the resolved proxy. I refuse schemes I
// cannot actually tunnel rather than silently falling back to a direct socket.
struct ProxyAttempt {
  bool ok = false;
  std::string error;
  bool used_proxy = false;
  ProxyServer server;
};
ProxyAttempt apply_proxy(HINTERNET session, const Url &url, const NetworkPolicy &policy) {
  ProxyAttempt attempt;
  if (!policy.proxy.active()) return {true, {}, false};
  ProxyServer server;
  std::string reason;
  if (!policy.proxy.route(url, server, reason)) {
    attempt.error = reason;
    return attempt;
  }
  if (server.empty()) return {true, {}, false}; // bypassed or explicit DIRECT
  if (server.scheme != "http") {
    // A SOCKS5 or HTTPS proxy never reaches WinHTTP: my own transport takes it
    // before this point. Reaching here means the scheme is one I do not route, so I
    // refuse instead of leaking direct.
    attempt.error = "I have no transport for a " + server.scheme +
                    " proxy; refusing the request rather than connecting directly";
    return attempt;
  }
  WINHTTP_PROXY_INFO info{};
  info.dwAccessType = WINHTTP_ACCESS_TYPE_NAMED_PROXY;
  auto proxy_text = wide(server.authority());
  std::wstring bypass_text;
  for (const auto &entry : policy.proxy.bypass) {
    if (!bypass_text.empty()) bypass_text += L";";
    bypass_text += wide(entry);
  }
  info.lpszProxy = proxy_text.data();
  info.lpszProxyBypass = bypass_text.empty() ? nullptr : bypass_text.data();
  if (!WinHttpSetOption(session, WINHTTP_OPTION_PROXY, &info, sizeof(info))) {
    attempt.error = "WinHTTP refused my proxy configuration (error " +
                    std::to_string(GetLastError()) + ")";
    return attempt;
  }
  attempt.ok = true;
  attempt.used_proxy = true;
  attempt.server = server;
  return attempt;
}

double now_seconds() {
  return double(now_ms()) / 1000.0;
}
std::wstring request_headers(const Url &url, const NetworkPolicy &policy,
                            const RequestOptions &request, const ProxyServer &routed) {
  std::wstring headers = L"Accept: */*\r\nAccept-Encoding: identity\r\n";
  headers += L"User-Agent: " + wide(policy.user_agent) + L"\r\n";
  if (!policy.accept_language.empty())
    headers += L"Accept-Language: " + wide(policy.accept_language) + L"\r\n";
  if (policy.send_dnt) headers += L"DNT: 1\r\n";
  if (policy.strip_referrer) headers += L"Referer:\r\n";
  else if (!url.origin().empty())
    headers += L"Referer: " + wide(url.origin() + "/") + L"\r\n";
  if (policy.cookies) {
    bool cross_site =
        !policy.top_level_host.empty() && lower(policy.top_level_host) != lower(url.host);
    auto jar = policy.cookies->header_for(url, now_seconds(), cross_site,
                                          policy.block_third_party_cookies);
    if (!jar.empty())
      headers += L"Cookie: " + wide(jar) + L"\r\n";
  }
  if (!request.body.empty()) {
    headers += L"Content-Type: " +
               wide(request.content_type.empty() ? std::string("application/x-www-form-urlencoded")
                                                 : request.content_type) +
               L"\r\n";
    headers += L"Content-Length: " + std::to_wstring(request.body.size()) + L"\r\n";
  }
  std::string login_user, login_password;
  if (proxy_credentials(policy, routed, login_user, login_password)) {
    // I only ever send proxy credentials to the proxy, never to the origin.
    auto raw = login_user + ":" + login_password;
    Bytes bytes(raw.begin(), raw.end());
    headers += L"Proxy-Authorization: Basic " + wide(base64_encode(bytes)) + L"\r\n";
  }
  return headers;
}
} // namespace


// A scheme WinHTTP cannot tunnel through has to be handed to my own transport. This
// question only exists for the Windows path, because the headless build has no
// WinHTTP at all and my tunnel carries every proxy scheme it accepts.
bool routed_scheme_needs_tunnel(const ProxyServer &server) {
  return server.scheme == "socks5" || server.scheme == "https";
}

Resource fetch_url(const Url &initial, const NetworkPolicy &policy, int timeout_ms,
                   size_t max_bytes) {
  return fetch_url(initial, policy, RequestOptions{}, timeout_ms, max_bytes);
}
Resource fetch_url(const Url &initial, const NetworkPolicy &policy, const RequestOptions &options,
                   int timeout_ms, size_t max_bytes) {
  Resource out;
  out.url = initial.href();
  // Zero means I never got a response, which is what a fetch promise rejects
  // on. A response with an error status still counts as a response.
  out.status = 0;
  std::string blocked;
  if (policy_blocks(policy, initial, blocked)) {
    out.ok = false;
    out.error = blocked;
    return out;
  }
  ProxyServer routed;
  std::string route_reason;
  if (tunnel_route(initial, policy, routed, route_reason)) {
    if (routed.empty()) {
      // Fail-closed: policy named no usable proxy for this URL, so I neither tunnel
      // nor connect directly.
      out.ok = false;
      out.error = route_reason.empty() ? "fail-closed: no proxy is configured for that URL"
                                       : route_reason;
      return out;
    }
    return tunneled_fetch(initial, tunnel_config(policy, routed, initial), options.method,
                          options.body, options.content_type, timeout_ms, max_bytes);
  }
  Url url = initial;
  HINTERNET session = WinHttpOpen(L"PsychoVault/0.2", WINHTTP_ACCESS_TYPE_NO_PROXY,
                                  WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, 0);
  if (!session) {
    out.ok = false;
    out.error = "WinHTTP session failed";
    return out;
  }
  WinHttpSetTimeouts(session, timeout_ms, timeout_ms, timeout_ms, timeout_ms);
  DWORD redirect = WINHTTP_OPTION_REDIRECT_POLICY_NEVER;
  WinHttpSetOption(session, WINHTTP_OPTION_REDIRECT_POLICY, &redirect, sizeof(redirect));
  DWORD disabled = WINHTTP_DISABLE_COOKIES | WINHTTP_DISABLE_AUTHENTICATION |
                   WINHTTP_DISABLE_KEEP_ALIVE;
  WinHttpSetOption(session, WINHTTP_OPTION_DISABLE_FEATURE, &disabled, sizeof(disabled));
  DWORD protocols = WINHTTP_FLAG_SECURE_PROTOCOL_TLS1_2 | WINHTTP_FLAG_SECURE_PROTOCOL_TLS1_3;
  WinHttpSetOption(session, WINHTTP_OPTION_SECURE_PROTOCOLS, &protocols, sizeof(protocols));

  for (int redirects = 0; redirects <= 5; ++redirects) {
    if (!url.network()) {
      out.ok = false;
      out.error = "blocked non-HTTP URL";
      break;
    }
    if (policy_blocks(policy, url, blocked)) {
      out.ok = false;
      out.error = blocked;
      break;
    }
    // The route is decided once, here. A refusal is final, and a scheme my own
    // transport carries is handed straight to it, redirects included, rather than
    // letting WinHTTP take a second, differently behaving swing at it.
    ProxyServer routed_server;
    std::string routed_reason;
    if (policy.proxy.active()) {
      if (!policy.proxy.route(url, routed_server, routed_reason)) {
        out.ok = false;
        out.error = routed_reason.empty() ? "fail-closed: no proxy is configured for that URL"
                                          : routed_reason;
        break;
      }
      if (routed_scheme_needs_tunnel(routed_server)) {
        out = tunneled_fetch(url, tunnel_config(policy, routed_server, initial), options.method,
                             options.body, options.content_type, timeout_ms, max_bytes);
        return out;
      }
    }
    auto proxy = apply_proxy(session, url, policy);
    if (!proxy.ok) {
      out.ok = false;
      out.error = proxy.error;
      break;
    }
    HINTERNET connect = WinHttpConnect(session, wide(url.host).c_str(), url.effective_port(), 0);
    if (!connect) {
      out.ok = false;
      out.error = "connect failed (error " + std::to_string(GetLastError()) + ")";
      break;
    }
    auto path = wide(url.path + (url.query.empty() ? "" : "?" + url.query));
    HINTERNET request = WinHttpOpenRequest(connect, wide(options.method).c_str(), path.c_str(),
                                           nullptr,
                                           WINHTTP_NO_REFERER, WINHTTP_DEFAULT_ACCEPT_TYPES,
                                           url.scheme == "https" ? WINHTTP_FLAG_SECURE : 0);
    if (!request) {
      WinHttpCloseHandle(connect);
      out.ok = false;
      out.error = "request failed";
      break;
    }
    auto headers = request_headers(url, policy, options, proxy.server);
    BOOL sent = WinHttpSendRequest(request, headers.c_str(), DWORD(headers.size()),
                                   options.body.empty() ? WINHTTP_NO_REQUEST_DATA
                                                        : LPVOID(options.body.data()),
                                   DWORD(options.body.size()), DWORD(options.body.size()), 0) &&
                WinHttpReceiveResponse(request, nullptr);
    if (!sent) {
      out.ok = false;
      out.error = "network request failed (error " + std::to_string(GetLastError()) + ")";
      WinHttpCloseHandle(request);
      WinHttpCloseHandle(connect);
      break;
    }
    DWORD status = 0, size = sizeof(status);
    WinHttpQueryHeaders(request, WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER,
                        WINHTTP_HEADER_NAME_BY_INDEX, &status, &size, WINHTTP_NO_HEADER_INDEX);
    out.status = int(status);
    if (policy.cookies) {
      for (DWORD index = 0;; ++index) {
        DWORD cookie_size = 0;
        if (WinHttpQueryHeaders(request, WINHTTP_QUERY_SET_COOKIE, WINHTTP_HEADER_NAME_BY_INDEX,
                                WINHTTP_NO_OUTPUT_BUFFER, &cookie_size, &index) ||
            GetLastError() != ERROR_INSUFFICIENT_BUFFER)
          break;
        std::wstring buffer(cookie_size / sizeof(wchar_t) + 1, L'\0');
        if (!WinHttpQueryHeaders(request, WINHTTP_QUERY_SET_COOKIE, WINHTTP_HEADER_NAME_BY_INDEX,
                                 buffer.data(), &cookie_size, &index))
          break;
        buffer.resize(cookie_size / sizeof(wchar_t));
        auto header = narrow(buffer);
        if (header.empty())
          break;
        policy.cookies->set_from_header(url, header, now_seconds());
      }
    }
    if (status == 407) {
      out.ok = false;
      std::string login_user, login_password;
      out.error = proxy_credentials(policy, proxy.server, login_user, login_password)
                      ? "the proxy rejected my credentials (HTTP 407)"
                      : "the proxy requires authentication (HTTP 407)";
      WinHttpCloseHandle(request);
      WinHttpCloseHandle(connect);
      break;
    }
    wchar_t mime[256];
    size = sizeof(mime);
    if (WinHttpQueryHeaders(request, WINHTTP_QUERY_CONTENT_TYPE, WINHTTP_HEADER_NAME_BY_INDEX, mime,
                            &size, WINHTTP_NO_HEADER_INDEX))
      out.mime = narrow(mime);
    if (status >= 300 && status < 400) {
      wchar_t location[4096];
      size = sizeof(location);
      if (WinHttpQueryHeaders(request, WINHTTP_QUERY_LOCATION, WINHTTP_HEADER_NAME_BY_INDEX,
                              location, &size, WINHTTP_NO_HEADER_INDEX)) {
        auto next = resolve_url(url, narrow(location));
        WinHttpCloseHandle(request);
        WinHttpCloseHandle(connect);
        if (!next || !next->network()) {
          out.ok = false;
          out.error = "blocked redirect";
          break;
        }
        url = *next;
        out.url = url.href();
        continue;
      }
      out.ok = false;
      out.error = "redirect without location";
      WinHttpCloseHandle(request);
      WinHttpCloseHandle(connect);
      break;
    }
    Bytes body;
    while (true) {
      DWORD available = 0;
      if (!WinHttpQueryDataAvailable(request, &available)) {
        out.ok = false;
        out.error = "read failed";
        break;
      }
      if (!available) break;
      if (body.size() + available > max_bytes) {
        out.ok = false;
        out.error = "response size limit";
        break;
      }
      auto old = body.size();
      body.resize(old + available);
      DWORD got = 0;
      if (!WinHttpReadData(request, body.data() + old, available, &got)) {
        out.ok = false;
        out.error = "read failed";
        break;
      }
      body.resize(old + got);
      if (!got) break;
    }
    WinHttpCloseHandle(request);
    WinHttpCloseHandle(connect);
    if (out.error.empty()) {
      out.bytes = std::move(body);
      out.text = legacy_decode(out.bytes);
      if (status >= 400) {
        out.ok = false;
        out.error = "HTTP " + std::to_string(status);
      } else {
        out.ok = true;
      }
    }
    break;
  }
  WinHttpCloseHandle(session);
  return out;
}

DownloadOutcome download_to_file(const Url &initial, const std::string &destination,
                                 const NetworkPolicy &policy,
                                 const std::function<bool()> &cancel,
                                 const std::function<void(const DownloadProgress &)> &progress,
                                 int timeout_ms) {
  DownloadOutcome outcome;
  outcome.final_url = initial.href();
  std::string blocked;
  if (policy_blocks(policy, initial, blocked)) {
    outcome.error = blocked;
    return outcome;
  }
  namespace fs = std::filesystem;
  std::error_code ec;
  fs::create_directories(fs::path(destination).parent_path(), ec);
  auto partial = destination + ".psychovault-part";
  std::ofstream file(partial, std::ios::binary | std::ios::trunc);
  if (!file) {
    outcome.error = "I cannot create the download file";
    return outcome;
  }
  // A download is a request like any other, so it goes through the tunnel when the
  // policy names one. Without this it was the one path that ignored my VPN.
  ProxyServer routed;
  std::string routed_reason;
  if (tunnel_route(initial, policy, routed, routed_reason)) {
    Resource fetched = routed.empty()
                           ? Resource{}
                           : tunneled_fetch(initial, tunnel_config(policy, routed, initial), "GET",
                                            "", "", timeout_ms, uint64_t(1) << 30);
    if (routed.empty()) {
      fetched.ok = false;
      fetched.error = routed_reason.empty() ? "fail-closed: no proxy is configured for that URL"
                                            : routed_reason;
    }
    file.close();
    if (!fetched.ok) {
      outcome.error = fetched.error;
      std::error_code ignored;
      fs::remove(partial, ignored);
      return outcome;
    }
    file.open(partial, std::ios::binary | std::ios::trunc);
    file.write(fetched.text.data(), std::streamsize(fetched.text.size()));
    file.close();
    if (!file) {
      outcome.error = "I could not finish writing the download";
      std::error_code ignored;
      fs::remove(partial, ignored);
      return outcome;
    }
    std::error_code rename_error;
    fs::rename(partial, destination, rename_error);
    if (rename_error) {
      outcome.error = "I could not put the download in place";
      std::error_code ignored;
      fs::remove(partial, ignored);
      return outcome;
    }
    outcome.ok = true;
    outcome.status = fetched.status;
    outcome.final_url = fetched.url;
    outcome.mime = fetched.mime;
    outcome.received = fetched.bytes.size();
    outcome.total = fetched.bytes.size();
    if (progress)
      progress(DownloadProgress{outcome.received, outcome.total});
    return outcome;
  }
  Url url = initial;
  HINTERNET session = WinHttpOpen(L"PsychoVault/0.2", WINHTTP_ACCESS_TYPE_NO_PROXY,
                                  WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, 0);
  if (!session) {
    outcome.error = "WinHTTP session failed";
    file.close();
    fs::remove(partial, ec);
    return outcome;
  }
  WinHttpSetTimeouts(session, timeout_ms, timeout_ms, timeout_ms, timeout_ms);
  DWORD redirect = WINHTTP_OPTION_REDIRECT_POLICY_NEVER;
  WinHttpSetOption(session, WINHTTP_OPTION_REDIRECT_POLICY, &redirect, sizeof(redirect));
  DWORD disabled = WINHTTP_DISABLE_COOKIES | WINHTTP_DISABLE_AUTHENTICATION;
  WinHttpSetOption(session, WINHTTP_OPTION_DISABLE_FEATURE, &disabled, sizeof(disabled));
  DWORD protocols = WINHTTP_FLAG_SECURE_PROTOCOL_TLS1_2 | WINHTTP_FLAG_SECURE_PROTOCOL_TLS1_3;
  WinHttpSetOption(session, WINHTTP_OPTION_SECURE_PROTOCOLS, &protocols, sizeof(protocols));

  for (int redirects = 0; redirects <= 5; ++redirects) {
    if (cancel && cancel()) {
      outcome.cancelled = true;
      outcome.error = "cancelled";
      break;
    }
    auto proxy = apply_proxy(session, url, policy);
    if (!proxy.ok) {
      outcome.error = proxy.error;
      break;
    }
    HINTERNET connect = WinHttpConnect(session, wide(url.host).c_str(), url.effective_port(), 0);
    if (!connect) {
      outcome.error = "connect failed";
      break;
    }
    auto path = wide(url.path + (url.query.empty() ? "" : "?" + url.query));
    HINTERNET request = WinHttpOpenRequest(connect, L"GET", path.c_str(), nullptr,
                                           WINHTTP_NO_REFERER, WINHTTP_DEFAULT_ACCEPT_TYPES,
                                           url.scheme == "https" ? WINHTTP_FLAG_SECURE : 0);
    if (!request) {
      WinHttpCloseHandle(connect);
      outcome.error = "request failed";
      break;
    }
    // A download is a plain GET, so it carries the same cookies as a page load.
    auto headers = request_headers(url, policy, RequestOptions{}, proxy.server);
    BOOL sent = WinHttpSendRequest(request, headers.c_str(), DWORD(headers.size()),
                                   WINHTTP_NO_REQUEST_DATA, 0, 0, 0) &&
                WinHttpReceiveResponse(request, nullptr);
    if (!sent) {
      outcome.error = "network request failed (error " + std::to_string(GetLastError()) + ")";
      WinHttpCloseHandle(request);
      WinHttpCloseHandle(connect);
      break;
    }
    DWORD status = 0, size = sizeof(status);
    WinHttpQueryHeaders(request, WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER,
                        WINHTTP_HEADER_NAME_BY_INDEX, &status, &size, WINHTTP_NO_HEADER_INDEX);
    outcome.status = int(status);
    if (status >= 300 && status < 400) {
      wchar_t location[4096];
      size = sizeof(location);
      bool have_location = WinHttpQueryHeaders(request, WINHTTP_QUERY_LOCATION,
                                              WINHTTP_HEADER_NAME_BY_INDEX, location, &size,
                                              WINHTTP_NO_HEADER_INDEX) != FALSE;
      auto next = have_location ? resolve_url(url, narrow(location)) : std::nullopt;
      WinHttpCloseHandle(request);
      WinHttpCloseHandle(connect);
      if (!next || !next->network()) {
        outcome.error = "blocked redirect";
        break;
      }
      url = *next;
      outcome.final_url = url.href();
      continue;
    }
    if (status >= 400) {
      outcome.error = status == 407 ? "the proxy requires authentication (HTTP 407)"
                                    : "HTTP " + std::to_string(status);
      WinHttpCloseHandle(request);
      WinHttpCloseHandle(connect);
      break;
    }
    wchar_t buffer[512];
    size = sizeof(buffer);
    if (WinHttpQueryHeaders(request, WINHTTP_QUERY_CONTENT_TYPE, WINHTTP_HEADER_NAME_BY_INDEX,
                            buffer, &size, WINHTTP_NO_HEADER_INDEX))
      outcome.mime = narrow(buffer);
    size = sizeof(buffer);
    if (WinHttpQueryHeaders(request, WINHTTP_QUERY_CONTENT_LENGTH, WINHTTP_HEADER_NAME_BY_INDEX,
                            buffer, &size, WINHTTP_NO_HEADER_INDEX)) {
      try { outcome.total = std::stoull(narrow(buffer)); } catch (...) { outcome.total = 0; }
    }
    // I cap a single download at 8 GiB so a hostile length cannot fill the disk.
    constexpr uint64_t limit = uint64_t(8) * 1024 * 1024 * 1024;
    std::vector<char> chunk(65536);
    bool failed = false;
    while (true) {
      if (cancel && cancel()) {
        outcome.cancelled = true;
        outcome.error = "cancelled";
        failed = true;
        break;
      }
      DWORD available = 0;
      if (!WinHttpQueryDataAvailable(request, &available)) {
        outcome.error = "read failed";
        failed = true;
        break;
      }
      if (!available) break;
      DWORD want = DWORD(std::min<size_t>(available, chunk.size()));
      DWORD got = 0;
      if (!WinHttpReadData(request, chunk.data(), want, &got)) {
        outcome.error = "read failed";
        failed = true;
        break;
      }
      if (!got) break;
      if (outcome.received + got > limit) {
        outcome.error = "the download exceeds my 8 GiB limit";
        failed = true;
        break;
      }
      file.write(chunk.data(), std::streamsize(got));
      if (!file) {
        outcome.error = "I cannot write to the download file";
        failed = true;
        break;
      }
      outcome.received += got;
      if (progress) {
        DownloadProgress state;
        state.received = outcome.received;
        state.total = outcome.total;
        progress(state);
      }
    }
    WinHttpCloseHandle(request);
    WinHttpCloseHandle(connect);
    if (!failed) {
      file.flush();
      bool write_ok = bool(file);
      file.close();
      if (!write_ok) {
        outcome.error = "I cannot flush the download file";
        break;
      }
      fs::rename(partial, destination, ec);
      if (ec) {
        outcome.error = "I cannot move the finished download into place";
        break;
      }
      outcome.ok = true;
      if (progress) {
        DownloadProgress state;
        state.received = outcome.received;
        state.total = outcome.total ? outcome.total : outcome.received;
        state.done = true;
        progress(state);
      }
    }
    break;
  }
  WinHttpCloseHandle(session);
  if (file.is_open()) file.close();
  if (!outcome.ok) {
    std::error_code ignored;
    fs::remove(partial, ignored);
  }
  return outcome;
}

#else

Resource fetch_url(const Url &url, const NetworkPolicy &policy, int, size_t) {
  return fetch_url(url, policy, RequestOptions{}, 0, 0);
}
// This build has no WinHTTP, but it does have my own socket, SOCKS5, CONNECT and TLS
// path, so a proxied request is a real request here rather than a refusal. A direct
// request still has no transport, and I say that instead of pretending.
Resource fetch_url(const Url &url, const NetworkPolicy &policy, const RequestOptions &options,
                   int timeout_ms, size_t max_bytes) {
  Resource r;
  r.url = url.href();
  r.status = 0;
  std::string reason;
  if (policy_blocks(policy, url, reason)) {
    r.ok = false;
    r.error = reason;
    return r;
  }
  ProxyServer routed;
  if (tunnel_route(url, policy, routed, reason)) {
    if (routed.empty()) {
      r.ok = false;
      r.error = reason.empty() ? "fail-closed: no proxy is configured for that URL" : reason;
      return r;
    }
    return tunneled_fetch(url, tunnel_config(policy, routed, url), options.method, options.body,
                          options.content_type, timeout_ms, max_bytes);
  }
  r.ok = false;
  r.error = "this build has no direct network transport; point me at a proxy and I will tunnel";
  return r;
}

DownloadOutcome download_to_file(const Url &url, const std::string &, const NetworkPolicy &,
                                 const std::function<bool()> &,
                                 const std::function<void(const DownloadProgress &)> &, int) {
  DownloadOutcome outcome;
  outcome.final_url = url.href();
  outcome.error = "broker downloads are implemented on Windows";
  return outcome;
}
#endif

Resource fetch_url(const Url &url, int timeout_ms, size_t max_bytes) {
  return fetch_url(url, NetworkPolicy{}, timeout_ms, max_bytes);
}
} // namespace pv
