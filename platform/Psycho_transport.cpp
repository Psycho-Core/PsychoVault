// PsychoVault original source. My sockets, my SOCKS5 client, my CONNECT request
// and my HTTP framing. Nothing here resolves a hostname locally when a proxy is
// meant to do it, and nothing here quietly connects direct when a tunnel fails.
#include "Psycho_transport.hpp"

#ifdef _WIN32
#define NOMINMAX
#include <winsock2.h>
#include <ws2tcpip.h>
#else
#include <arpa/inet.h>
#include <netdb.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <unistd.h>
#endif

namespace pv {
namespace {
#ifdef _WIN32
bool ensure_winsock() {
  static bool started = false;
  static bool ok = false;
  if (!started) {
    started = true;
    WSADATA data{};
    ok = WSAStartup(MAKEWORD(2, 2), &data) == 0;
  }
  return ok;
}
void close_handle(intptr_t fd) {
  if (fd >= 0)
    closesocket(SOCKET(fd));
}
int last_socket_error() {
  return WSAGetLastError();
}
#else
void close_handle(intptr_t fd) {
  if (fd >= 0)
    ::close(int(fd));
}
int last_socket_error() {
  return errno;
}
#endif

} // namespace

Socket::~Socket() {
  close();
}

bool Socket::open() const {
  return fd_ >= 0;
}

intptr_t Socket::handle() const {
  return fd_;
}

void Socket::close() {
  if (fd_ >= 0) {
    close_handle(fd_);
    fd_ = -1;
  }
}

bool Socket::connect_to(const std::string &host, int port, int timeout_ms, std::string &error) {
#ifdef _WIN32
  if (!ensure_winsock()) {
    error = "WinSock would not start";
    return false;
  }
#endif
  close();
  // A proxy is normally named, and a browser has to be able to reach it, so this
  // is the one place a name is resolved: the address of the proxy itself. The
  // target of the tunnel is never resolved here.
  addrinfo hints{};
  hints.ai_family = AF_UNSPEC;
  hints.ai_socktype = SOCK_STREAM;
  addrinfo *list = nullptr;
  auto service = std::to_string(port);
  if (getaddrinfo(host.c_str(), service.c_str(), &hints, &list) != 0 || !list) {
    error = "I could not look up my proxy " + host;
    return false;
  }
  intptr_t fd = -1;
  for (addrinfo *at = list; at; at = at->ai_next) {
    fd = intptr_t(socket(at->ai_family, at->ai_socktype, at->ai_protocol));
    if (fd < 0)
      continue;
#ifdef _WIN32
    DWORD limit = DWORD(std::max(1, timeout_ms));
    setsockopt(SOCKET(fd), SOL_SOCKET, SO_RCVTIMEO, reinterpret_cast<const char *>(&limit),
               sizeof limit);
    setsockopt(SOCKET(fd), SOL_SOCKET, SO_SNDTIMEO, reinterpret_cast<const char *>(&limit),
               sizeof limit);
    if (connect(SOCKET(fd), at->ai_addr, int(at->ai_addrlen)) == 0)
      break;
#else
    timeval tv{timeout_ms / 1000, (timeout_ms % 1000) * 1000};
    setsockopt(int(fd), SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof tv);
    setsockopt(int(fd), SOL_SOCKET, SO_SNDTIMEO, &tv, sizeof tv);
    if (connect(int(fd), at->ai_addr, at->ai_addrlen) == 0)
      break;
#endif
    close_handle(fd);
    fd = -1;
  }
  freeaddrinfo(list);
  if (fd < 0) {
    error = "I could not reach my proxy at " + host + ":" + service + " (error " +
            std::to_string(last_socket_error()) + ")";
    return false;
  }
  fd_ = fd;
  return true;
}

bool Socket::write_all(const uint8_t *data, size_t size, std::string &error) {
  if (fd_ < 0) {
    error = "this socket is not open";
    return false;
  }
  size_t sent = 0;
  while (sent < size) {
#ifdef _WIN32
    int n = send(SOCKET(fd_), reinterpret_cast<const char *>(data + sent), int(size - sent), 0);
#else
    ssize_t n = ::send(int(fd_), data + sent, size - sent, 0);
#endif
    if (n <= 0) {
      error = "the write to my proxy failed";
      return false;
    }
    sent += size_t(n);
  }
  return true;
}

bool Socket::read_some(uint8_t *out, size_t capacity, size_t &got, std::string &error) {
  got = 0;
  if (fd_ < 0) {
    error = "this socket is not open";
    return false;
  }
#ifdef _WIN32
  int n = recv(SOCKET(fd_), reinterpret_cast<char *>(out), int(capacity), 0);
#else
  ssize_t n = ::recv(int(fd_), out, capacity, 0);
#endif
  if (n < 0) {
    error = "the read from my proxy failed";
    return false;
  }
  got = size_t(n);
  return n > 0;
}

// ---- SOCKS5 --------------------------------------------------------------
namespace {
bool send_all(Stream &stream, const Bytes &bytes, std::string &error) {
  return stream.write_all(bytes.data(), bytes.size(), error);
}
bool read_exact(Stream &stream, uint8_t *out, size_t size, std::string &error) {
  size_t done = 0;
  while (done < size) {
    size_t got = 0;
    if (!stream.read_some(out + done, size - done, got, error)) {
      if (error.empty())
        error = "my proxy closed the connection part way through a handshake";
      return false;
    }
    done += got;
  }
  return true;
}
} // namespace

bool socks5_connect(Stream &stream, const TunnelTarget &target, const std::string &user,
                    const std::string &pass, bool has_auth, std::string &error) {
  Bytes greeting{5, uint8_t(has_auth ? 2 : 1)};
  greeting.push_back(0); // no authentication
  if (has_auth)
    greeting.push_back(2); // username and password
  if (!send_all(stream, greeting, error))
    return false;
  uint8_t reply[2] = {0, 0};
  if (!read_exact(stream, reply, 2, error))
    return false;
  if (reply[0] != 5) {
    error = "that proxy did not answer as SOCKS5";
    return false;
  }
  if (reply[1] == 0xff) {
    error = "my proxy refused every authentication method I offered";
    return false;
  }
  if (reply[1] == 2) {
    if (!has_auth) {
      error = "my proxy wants a username and password I do not have";
      return false;
    }
    if (user.size() > 255 || pass.size() > 255) {
      error = "that proxy username or password is longer than SOCKS5 allows";
      return false;
    }
    Bytes auth{1, uint8_t(user.size())};
    auth.insert(auth.end(), user.begin(), user.end());
    auth.push_back(uint8_t(pass.size()));
    auth.insert(auth.end(), pass.begin(), pass.end());
    if (!send_all(stream, auth, error))
      return false;
    uint8_t status[2] = {0, 0};
    if (!read_exact(stream, status, 2, error))
      return false;
    if (status[1] != 0) {
      error = "my proxy rejected the username and password";
      return false;
    }
  } else if (reply[1] != 0) {
    error = "my proxy asked for an authentication method I do not implement";
    return false;
  }
  // The request carries a name not an address whenever I was given a name, so the
  // proxy performs the lookup rather than my machine.
  Bytes request{5, 1, 0};
  in_addr v4{};
  in6_addr v6{};
  if (inet_pton(AF_INET, target.host.c_str(), &v4) == 1) {
    request.push_back(1);
    auto *raw = reinterpret_cast<const uint8_t *>(&v4);
    request.insert(request.end(), raw, raw + 4);
  } else if (inet_pton(AF_INET6, target.host.c_str(), &v6) == 1) {
    request.push_back(4);
    auto *raw = reinterpret_cast<const uint8_t *>(&v6);
    request.insert(request.end(), raw, raw + 16);
  } else {
    if (target.host.empty() || target.host.size() > 255) {
      error = "that hostname is not one SOCKS5 can carry";
      return false;
    }
    request.push_back(3);
    request.push_back(uint8_t(target.host.size()));
    request.insert(request.end(), target.host.begin(), target.host.end());
  }
  request.push_back(uint8_t((target.port >> 8) & 0xff));
  request.push_back(uint8_t(target.port & 0xff));
  if (!send_all(stream, request, error))
    return false;
  uint8_t head[4] = {0, 0, 0, 0};
  if (!read_exact(stream, head, 4, error))
    return false;
  if (head[0] != 5) {
    error = "that proxy did not answer as SOCKS5";
    return false;
  }
  if (head[1] != 0) {
    const char *why = "the proxy refused the connection";
    switch (head[1]) {
    case 2:
      why = "my proxy would not allow that address";
      break;
    case 3:
      why = "my proxy could not reach the network";
      break;
    case 4:
      why = "my proxy could not resolve that hostname, and my machine did not ask";
      break;
    case 5:
      why = "my proxy refused the connection";
      break;
    case 6:
      why = "that connection timed out at my proxy";
      break;
    case 7:
      why = "my proxy does not support that command";
      break;
    case 8:
      why = "my proxy does not allow that address type";
      break;
    }
    error = std::string(why) + " (SOCKS5 code " + std::to_string(int(head[1])) + ")";
    return false;
  }
  // The bound address is measured before the tunnel is treated as open.
  size_t skip = 0;
  if (head[3] == 1)
    skip = 4;
  else if (head[3] == 4)
    skip = 16;
  else if (head[3] == 3) {
    uint8_t length = 0;
    if (!read_exact(stream, &length, 1, error))
      return false;
    skip = length;
  } else {
    error = "my proxy answered with an address type I do not know";
    return false;
  }
  Bytes tail(skip + 2, 0);
  if (!read_exact(stream, tail.data(), tail.size(), error))
    return false;
  return true;
}

// ---- HTTP CONNECT --------------------------------------------------------
bool http_connect(Stream &stream, const TunnelTarget &target, const std::string &user,
                  const std::string &pass, bool has_auth, std::string &error) {
  std::string authority = target.host + ":" + std::to_string(target.port);
  std::string request = "CONNECT " + authority + " HTTP/1.1\r\nHost: " + authority + "\r\n";
  if (has_auth && !user.empty()) {
    std::string raw = user + ":" + pass;
    Bytes bytes(raw.begin(), raw.end());
    request += "Proxy-Authorization: Basic " + base64_encode(bytes) + "\r\n";
  }
  request += "Proxy-Connection: keep-alive\r\n\r\n";
  if (!stream.write_all(reinterpret_cast<const uint8_t *>(request.data()), request.size(), error))
    return false;
  // I read the response head only; the tunnel starts straight after it.
  std::string head;
  while (head.find("\r\n\r\n") == std::string::npos) {
    if (head.size() > 32768) {
      error = "my proxy sent a CONNECT response I could not finish reading";
      return false;
    }
    uint8_t chunk[2048];
    size_t got = 0;
    if (!stream.read_some(chunk, sizeof chunk, got, error)) {
      if (error.empty())
        error = "my proxy closed the connection during CONNECT";
      else
        error += " during CONNECT";
      return false;
    }
    head.append(reinterpret_cast<const char *>(chunk), got);
  }
  int status = 0;
  auto space = head.find(' ');
  if (space != std::string::npos && head.size() > space + 3)
    status = std::atoi(head.c_str() + space + 1);
  if (status != 200) {
    error = "my proxy refused CONNECT to " + authority + " with status " + std::to_string(status);
    return false;
  }
  if (head.find("Proxy-Authenticate") != std::string::npos && has_auth)
    return true;
  return true;
}

// ---- HTTP exchange -------------------------------------------------------
namespace {
std::string header_value(const HttpResponse &response, const std::string &name) {
  for (const auto &entry : response.headers)
    if (lower(entry.first) == lower(name))
      return entry.second;
  return std::string();
}
bool parse_head(const std::string &head, HttpResponse &response, size_t &consumed) {
  auto end = head.find("\r\n\r\n");
  if (end == std::string::npos)
    return false;
  consumed = end + 4;
  std::istringstream lines(head.substr(0, end));
  std::string line;
  if (!std::getline(lines, line))
    return false;
  if (!line.empty() && line.back() == '\r')
    line.pop_back();
  if (line.rfind("HTTP/", 0) != 0)
    return false;
  auto first = line.find(' ');
  if (first == std::string::npos)
    return false;
  response.status = std::atoi(line.c_str() + first + 1);
  auto second = line.find(' ', first + 1);
  if (second != std::string::npos) {
    response.reason = line.substr(second + 1);
    if (!response.reason.empty() && response.reason.back() == '\r')
      response.reason.pop_back();
  }
  while (std::getline(lines, line)) {
    if (!line.empty() && line.back() == '\r')
      line.pop_back();
    auto colon = line.find(':');
    if (colon == std::string::npos)
      continue;
    response.headers.emplace_back(trim(line.substr(0, colon)), trim(line.substr(colon + 1)));
  }
  std::string transfer = lower(header_value(response, "Transfer-Encoding"));
  response.chunked = transfer.find("chunked") != std::string::npos;
  return true;
}
} // namespace

bool http_exchange(Stream &stream, const HttpRequest &request, HttpResponse &response,
                   std::string &error) {
  std::string head = request.method + " " + request.path + " HTTP/1.1\r\nHost: " + request.host;
  if (request.port > 0 && request.port != 80 && request.port != 443)
    head += ":" + std::to_string(request.port);
  head += "\r\n";
  for (const auto &entry : request.headers)
    head += entry.first + ": " + entry.second + "\r\n";
  if (!request.body.empty())
    head += "Content-Length: " + std::to_string(request.body.size()) + "\r\n";
  head += "Connection: close\r\n\r\n";
  if (!stream.write_all(reinterpret_cast<const uint8_t *>(head.data()), head.size(), error))
    return false;
  if (!request.body.empty() &&
      !stream.write_all(reinterpret_cast<const uint8_t *>(request.body.data()),
                        request.body.size(), error))
    return false;

  std::string buffer;
  size_t consumed = 0;
  while (buffer.find("\r\n\r\n") == std::string::npos) {
    if (buffer.size() > 65536) {
      error = "the response head from that server was larger than I will read";
      return false;
    }
    uint8_t chunk[8192];
    size_t got = 0;
    if (!stream.read_some(chunk, sizeof chunk, got, error)) {
      if (error.empty())
        error = "the server closed before it sent a complete response head";
      return false;
    }
    buffer.append(reinterpret_cast<const char *>(chunk), got);
  }
  if (!parse_head(buffer, response, consumed)) {
    error = "that server did not send an HTTP response I could read";
    return false;
  }
  std::string rest = buffer.substr(consumed);
  auto finish = [&]() {
    response.body = rest;
    response.bytes.assign(rest.begin(), rest.end());
    return true;
  };
  if (response.status == 204 || response.status == 304)
    return finish();
  if (response.chunked) {
    std::string body;
    size_t at = 0;
    auto need = [&](size_t more) -> bool {
      while (rest.size() < at + more) {
        uint8_t chunk[8192];
        size_t got = 0;
        if (!stream.read_some(chunk, sizeof chunk, got, error))
          return false;
        rest.append(reinterpret_cast<const char *>(chunk), got);
      }
      return true;
    };
    while (true) {
      auto line_end = rest.find("\r\n", at);
      while (line_end == std::string::npos) {
        if (!need(1))
          return false;
        if (rest.size() > request.max_bytes * 2 + 65536) {
          error = "that chunked response was larger than I will hold";
          return false;
        }
        line_end = rest.find("\r\n", at);
      }
      std::string size_text = rest.substr(at, line_end - at);
      auto extension = size_text.find(';');
      if (extension != std::string::npos)
        size_text = size_text.substr(0, extension);
      size_t size = size_t(std::strtoull(trim(size_text).c_str(), nullptr, 16));
      at = line_end + 2;
      if (size == 0)
        break;
      if (!need(size + 2))
        return false;
      if (body.size() + size > request.max_bytes) {
        error = "that response was larger than I will hold in memory";
        return false;
      }
      body.append(rest, at, size);
      at += size + 2;
    }
    response.body = std::move(body);
    response.bytes.assign(response.body.begin(), response.body.end());
    return true;
  }
  std::string length_text = header_value(response, "Content-Length");
  if (!length_text.empty()) {
    size_t want = size_t(std::strtoull(length_text.c_str(), nullptr, 10));
    if (want > request.max_bytes) {
      error = "that response was larger than I will hold in memory";
      return false;
    }
    while (rest.size() < want) {
      uint8_t chunk[8192];
      size_t got = 0;
      if (!stream.read_some(chunk, sizeof chunk, got, error)) {
        if (error.empty())
          break; // the body I have is what arrived, which is honest
        return false;
      }
      rest.append(reinterpret_cast<const char *>(chunk), got);
      if (rest.size() > want + 65536) {
        rest.resize(want);
        break;
      }
    }
    rest.resize(std::min(rest.size(), want));
    return finish();
  }
  // No length and no chunks, so the body ends when the connection does.
  while (true) {
    if (rest.size() > request.max_bytes) {
      error = "that response was larger than I will hold in memory";
      return false;
    }
    uint8_t chunk[8192];
    size_t got = 0;
    if (!stream.read_some(chunk, sizeof chunk, got, error)) {
      if (!error.empty())
        return false;
      break;
    }
    rest.append(reinterpret_cast<const char *>(chunk), got);
  }
  return finish();
}

Resource tunneled_fetch(const Url &url, const TunnelConfig &config, const std::string &method,
                        const std::string &body, const std::string &content_type,
                        int timeout_ms, size_t max_bytes) {
  Resource out;
  out.url = url.href();
  out.status = 0;
  if (!config.proxy.valid()) {
    out.ok = false;
    out.error = "fail-closed: my proxy is not a server I can dial";
    return out;
  }
  Url current = url;
  for (int hop = 0; hop <= 5; ++hop) {
    if (current.scheme != "http" && current.scheme != "https") {
      out.ok = false;
      out.error = "I only tunnel http and https";
      return out;
    }
    Socket socket;
    std::string error;
    if (!socket.connect_to(config.proxy.host, config.proxy.port, timeout_ms, error)) {
      out.ok = false;
      out.error = error;
      return out;
    }
    // The stack is mine all the way down: a socket, then the proxy's TLS when the
    // proxy speaks HTTPS, then the origin's TLS inside that tunnel. Nothing is ever
    // sent unencrypted to an https address.
    auto base = std::make_unique<SocketStream>(std::move(socket));
    Stream *top = base.get();
    std::unique_ptr<TlsStream> proxy_tls;
    if (config.proxy.scheme == "https") {
      proxy_tls = TlsStream::open(*top, config.proxy.host, error);
      if (!proxy_tls) {
        out.ok = false;
        out.error = error;
        return out;
      }
      top = proxy_tls.get();
    }
    TunnelTarget target{current.host, int(current.effective_port())};
    bool handshake = false;
    if (config.proxy.scheme == "socks5")
      handshake = socks5_connect(*top, target, config.user, config.pass, config.has_auth, error);
    else
      handshake = http_connect(*top, target, config.user, config.pass, config.has_auth, error);
    if (!handshake) {
      out.ok = false;
      out.error = error;
      return out;
    }
    std::unique_ptr<TlsStream> origin_tls;
    if (current.scheme == "https") {
      // The tunnel is opaque, so the origin's TLS runs inside it and its
      // certificate is checked against the origin's name.
      origin_tls = TlsStream::open(*top, current.host, error);
      if (!origin_tls) {
        out.ok = false;
        out.error = error;
        return out;
      }
      top = origin_tls.get();
    }
    HttpRequest request;
    request.method = method;
    request.path = current.path + (current.query.empty() ? "" : "?" + current.query);
    if (request.path.empty())
      request.path = "/";
    request.host = current.host;
    request.port = int(current.effective_port());
    request.body = body;
    request.max_bytes = max_bytes;
    request.headers.emplace_back("Accept", "*/*");
    request.headers.emplace_back("Accept-Encoding", "identity");
    request.headers.emplace_back("User-Agent", config.user_agent);
    if (!config.accept_language.empty())
      request.headers.emplace_back("Accept-Language", config.accept_language);
    if (config.send_dnt)
      request.headers.emplace_back("DNT", "1");
    if (!config.strip_referrer)
      request.headers.emplace_back("Referer", current.origin() + "/");
    if (!config.cookie_header.empty())
      request.headers.emplace_back("Cookie", config.cookie_header);
    if (!body.empty())
      request.headers.emplace_back("Content-Type",
                                   content_type.empty() ? "application/x-www-form-urlencoded"
                                                        : content_type);
    HttpResponse response;
    if (!http_exchange(*top, request, response, error)) {
      out.ok = false;
      out.error = error;
      return out;
    }
    out.status = response.status;
    out.mime = [&] {
      auto value = lower([&] {
        for (const auto &entry : response.headers)
          if (lower(entry.first) == "content-type")
            return entry.second;
        return std::string();
      }());
      auto cut = value.find(';');
      return cut == std::string::npos ? trim(value) : trim(value.substr(0, cut));
    }();
    out.text = response.body;
    out.bytes = response.bytes;
    out.ok = response.status >= 200 && response.status < 400;
    if (response.status >= 300 && response.status < 400) {
      std::string location;
      for (const auto &entry : response.headers)
        if (lower(entry.first) == "location")
          location = entry.second;
      auto next = location.empty() ? std::nullopt : resolve_url(current, location);
      if (next) {
        current = *next;
        out.url = current.href();
        continue;
      }
    }
    if (response.status >= 400)
      out.error = "the server answered " + std::to_string(response.status);
    return out;
  }
  out.ok = false;
  out.error = "that page redirected more times than I will follow";
  return out;
}
} // namespace pv
