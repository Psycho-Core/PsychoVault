#include "network.hpp"
#ifdef _WIN32
#define NOMINMAX
#include <windows.h>
#include <winhttp.h>
#endif
namespace pv {
#ifdef _WIN32
static std::wstring wide(const std::string &s) {
  if (s.empty())
    return {};
  int n = MultiByteToWideChar(CP_UTF8, 0, s.c_str(), int(s.size()), nullptr, 0);
  std::wstring w(n, 0);
  MultiByteToWideChar(CP_UTF8, 0, s.c_str(), int(s.size()), w.data(), n);
  return w;
}
Resource fetch_url(const Url &initial, int timeout_ms, size_t max_bytes) {
  Resource out;
  out.url = initial.href();
  Url url = initial;
  HINTERNET session =
      WinHttpOpen(L"PsychoVault/0.1 (owned engine; development)",
                  WINHTTP_ACCESS_TYPE_NO_PROXY, WINHTTP_NO_PROXY_NAME,
                  WINHTTP_NO_PROXY_BYPASS, 0);
  if (!session) {
    out.ok = false;
    out.error = "WinHTTP session failed";
    return out;
  }
  WinHttpSetTimeouts(session, timeout_ms, timeout_ms, timeout_ms, timeout_ms);
  DWORD redirect = WINHTTP_OPTION_REDIRECT_POLICY_NEVER;
  WinHttpSetOption(session, WINHTTP_OPTION_REDIRECT_POLICY, &redirect,
                   sizeof(redirect));
  DWORD disabled = WINHTTP_DISABLE_COOKIES | WINHTTP_DISABLE_AUTHENTICATION;
  WinHttpSetOption(session, WINHTTP_OPTION_DISABLE_FEATURE, &disabled,
                   sizeof(disabled));
  for (int redirects = 0; redirects <= 5; ++redirects) {
    if (!url.network()) {
      out.ok = false;
      out.error = "blocked non-HTTP URL";
      break;
    }
    HINTERNET connect = WinHttpConnect(session, wide(url.host).c_str(),
                                       url.effective_port(), 0);
    if (!connect) {
      out.ok = false;
      out.error = "connect failed";
      break;
    }
    std::wstring path =
        wide(url.path + (url.query.empty() ? "" : "?" + url.query));
    HINTERNET request =
        WinHttpOpenRequest(connect, L"GET", path.c_str(), nullptr,
                           WINHTTP_NO_REFERER, WINHTTP_DEFAULT_ACCEPT_TYPES,
                           url.scheme == "https" ? WINHTTP_FLAG_SECURE : 0);
    if (!request) {
      WinHttpCloseHandle(connect);
      out.ok = false;
      out.error = "request failed";
      break;
    }
    DWORD protocols = WINHTTP_FLAG_SECURE_PROTOCOL_TLS1_2 |
                      WINHTTP_FLAG_SECURE_PROTOCOL_TLS1_3;
    WinHttpSetOption(session, WINHTTP_OPTION_SECURE_PROTOCOLS, &protocols,
                     sizeof(protocols));
    const wchar_t *headers =
        L"Accept: "
        L"text/html,application/xhtml+xml,text/css,application/"
        L"javascript,image/png,image/*;q=0.8,*/*;q=0.5\r\nAccept-Encoding: "
        L"identity\r\n";
    BOOL sent = WinHttpSendRequest(request, headers, DWORD(wcslen(headers)),
                                   WINHTTP_NO_REQUEST_DATA, 0, 0, 0) &&
                WinHttpReceiveResponse(request, nullptr);
    if (!sent) {
      out.ok = false;
      out.error = "network request failed: " + std::to_string(GetLastError());
      WinHttpCloseHandle(request);
      WinHttpCloseHandle(connect);
      break;
    }
    DWORD status = 0, size = sizeof(status);
    WinHttpQueryHeaders(
        request, WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER,
        WINHTTP_HEADER_NAME_BY_INDEX, &status, &size, WINHTTP_NO_HEADER_INDEX);
    out.status = int(status);
    wchar_t mime[256];
    size = sizeof(mime);
    if (WinHttpQueryHeaders(request, WINHTTP_QUERY_CONTENT_TYPE,
                            WINHTTP_HEADER_NAME_BY_INDEX, mime, &size,
                            WINHTTP_NO_HEADER_INDEX)) {
      int n = WideCharToMultiByte(CP_UTF8, 0, mime, -1, nullptr, 0, nullptr,
                                  nullptr);
      std::string m(n ? n - 1 : 0, 0);
      if (n)
        WideCharToMultiByte(CP_UTF8, 0, mime, -1, m.data(), n, nullptr,
                            nullptr);
      out.mime = m;
    }
    if (status >= 300 && status < 400) {
      wchar_t location[4096];
      size = sizeof(location);
      if (WinHttpQueryHeaders(request, WINHTTP_QUERY_LOCATION,
                              WINHTTP_HEADER_NAME_BY_INDEX, location, &size,
                              WINHTTP_NO_HEADER_INDEX)) {
        int n = WideCharToMultiByte(CP_UTF8, 0, location, -1, nullptr, 0,
                                    nullptr, nullptr);
        std::string loc(n ? n - 1 : 0, 0);
        if (n)
          WideCharToMultiByte(CP_UTF8, 0, location, -1, loc.data(), n, nullptr,
                              nullptr);
        auto next = resolve_url(url, loc);
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
      if (!available)
        break;
      if (body.size() + available > max_bytes) {
        out.ok = false;
        out.error = "response size limit";
        break;
      }
      size_t old = body.size();
      body.resize(old + available);
      DWORD got = 0;
      if (!WinHttpReadData(request, body.data() + old, available, &got)) {
        out.ok = false;
        out.error = "read failed";
        break;
      }
      body.resize(old + got);
      if (!got)
        break;
    }
    WinHttpCloseHandle(request);
    WinHttpCloseHandle(connect);
    if (out.error.empty() ||
        out.error == "read failed" && status >= 200 && status < 300) {
      out.bytes = std::move(body);
      out.text = legacy_decode(out.bytes);
      if (status >= 400) {
        out.ok = false;
        out.error = "HTTP " + std::to_string(status);
      } else
        out.ok = out.error.empty();
    }
    break;
  }
  WinHttpCloseHandle(session);
  return out;
}
#else
Resource fetch_url(const Url &url, int, size_t) {
  Resource r;
  r.url = url.href();
  r.ok = false;
  r.error = "broker networking is implemented on Windows; headless CLI uses "
            "supplied resources";
  return r;
}
#endif
} // namespace pv
