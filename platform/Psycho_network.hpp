#pragma once
#include "../engine/Psycho_base.hpp"
#include "../engine/Psycho_cookies.hpp"
#include "Psycho_proxy.hpp"

namespace pv {

// I keep every request-affecting browser setting in one place so a VPN or a
// privacy toggle changes real behaviour rather than a label.
struct NetworkPolicy {
  ProxyConfig proxy;
  std::string user_agent = "PsychoVault/0.2 (owned engine; development)";
  std::string accept_language = "en-US,en;q=0.9";
  bool send_dnt = false;
  bool strip_referrer = false;
  bool block_third_party_cookies = true;
  bool block_ads = false;
  std::vector<std::string> blocked_hosts;
  std::vector<std::string> allowed_hosts; // per-site shield exceptions
  // Credentials a VPN extension supplied through webRequest.onAuthRequired.
  std::string proxy_user, proxy_pass;
  bool has_proxy_auth = false;
  // I attach the profile's jar here so every request in the browser, including
  // a script's fetch and a form post, reads and writes the same cookies.
  CookieJar *cookies = nullptr;
  // The host of the page making the request, so a third-party cookie can be
  // refused rather than silently sent.
  std::string top_level_host;
};

// What one request carries beyond its URL.
struct RequestOptions {
  std::string method = "GET", body, content_type;
};

// I refuse a request before any socket is opened when policy blocks it.
bool policy_blocks(const NetworkPolicy &policy, const Url &url, std::string &reason);

Resource fetch_url(const Url &url, int timeout_ms = 20000, size_t max_bytes = kMaxDocument);
Resource fetch_url(const Url &url, const NetworkPolicy &policy, int timeout_ms = 20000,
                   size_t max_bytes = kMaxDocument);
Resource fetch_url(const Url &url, const NetworkPolicy &policy, const RequestOptions &request,
                   int timeout_ms = 20000, size_t max_bytes = kMaxDocument);

struct DownloadProgress {
  uint64_t received = 0;
  uint64_t total = 0; // 0 when the server sent no Content-Length
  bool done = false;
};

struct DownloadOutcome {
  bool ok = false;
  std::string error;
  std::string final_url;
  std::string mime;
  uint64_t received = 0;
  uint64_t total = 0;
  int status = 0;
  bool cancelled = false;
};

// I stream a response straight to disk so a large file never sits in memory.
// `cancel` is polled between reads; `progress` runs on the calling worker thread.
DownloadOutcome download_to_file(const Url &url, const std::string &destination,
                                 const NetworkPolicy &policy,
                                 const std::function<bool()> &cancel,
                                 const std::function<void(const DownloadProgress &)> &progress,
                                 int timeout_ms = 30000);

// I turn a URL and a Content-Disposition header into a safe local filename.
std::string safe_download_name(const Url &url, const std::string &content_disposition);
std::string unique_download_path(const std::string &directory, const std::string &name);
std::string default_download_directory();
} // namespace pv
