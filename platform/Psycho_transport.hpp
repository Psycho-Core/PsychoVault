// PsychoVault original source. My own transport for a proxy tunnel: the socket,
// the SOCKS5 handshake, the HTTP CONNECT request, the HTTP framing and the
// fail-closed rule are all mine. The one thing I do not write is TLS itself,
// which comes from the security provider the operating system ships, because a
// certificate chain I built myself would be worth less than the one Windows
// already maintains.
#pragma once
#include "../engine/Psycho_base.hpp"
#include "Psycho_proxy.hpp"
#include "../engine/Psycho_security.hpp"
#include <cstdint>

namespace pv {

// Where a tunnel has to reach. A name stays a name all the way to the proxy, so
// the DNS lookup happens on the far side and my machine never asks for it.
struct TunnelTarget {
  std::string host;
  int port = 0;
};

// Everything my transport needs from a browser policy, so the transport itself
// knows nothing about pages, cookies or profile storage.
struct TunnelConfig {
  ProxyServer proxy;
  std::string user, pass;
  bool has_auth = false;
  std::string user_agent = "PsychoVault/0.3 (owned engine; development)";
  std::string accept_language, cookie_header;
  bool send_dnt = false;
  bool strip_referrer = false;
  // When this is true a failed tunnel is the end of the request. Nothing falls
  // back to a direct socket, because that would be the leak I am avoiding.
  bool fail_closed = true;
  // Where a Strict-Transport-Security header from an HTTPS hop is recorded, so the
  // same policy applies whether a request went through the tunnel or WinHTTP.
  HttpsPolicy *https = nullptr;
};

// A raw TCP connection. Windows and my test builds both go through this, so the
// handshake code above it is the same code everywhere.
class Socket {
public:
  Socket() = default;
  ~Socket();
  Socket(const Socket &) = delete;
  Socket &operator=(const Socket &) = delete;
  Socket(Socket &&other) noexcept : fd_(other.fd_) {
    other.fd_ = -1;
  }
  Socket &operator=(Socket &&other) noexcept {
    if (this != &other) {
      close();
      fd_ = other.fd_;
      other.fd_ = -1;
    }
    return *this;
  }
  bool connect_to(const std::string &host, int port, int timeout_ms, std::string &error);
  bool write_all(const uint8_t *data, size_t size, std::string &error);
  // I return false with an empty error only at a clean end of stream.
  bool read_some(uint8_t *out, size_t capacity, size_t &got, std::string &error);
  void close();
  bool open() const;
  // The handle itself, because TLS sits straight on top of it.
  intptr_t handle() const;

private:
  intptr_t fd_ = -1;
};

// A byte stream that may be a plain socket or TLS over one.
class Stream {
public:
  virtual ~Stream() = default;
  virtual bool write_all(const uint8_t *data, size_t size, std::string &error) = 0;
  virtual bool read_some(uint8_t *out, size_t capacity, size_t &got, std::string &error) = 0;
  virtual bool alive() const = 0;
};

class SocketStream : public Stream {
public:
  explicit SocketStream(Socket socket) : socket_(std::move(socket)) {}
  bool write_all(const uint8_t *data, size_t size, std::string &error) override {
    return socket_.write_all(data, size, error);
  }
  bool read_some(uint8_t *out, size_t capacity, size_t &got, std::string &error) override {
    return socket_.read_some(out, capacity, got, error);
  }
  bool alive() const override {
    return socket_.open();
  }

private:
  Socket socket_;
};

// TLS over a socket. I never fall back to plaintext: a build without a provider
// refuses the connection and says why.
class TlsStream : public Stream {
public:
  ~TlsStream() override;
  // The transport underneath, kept for the life of this stream.
  Stream *under_ = nullptr;
  // I wrap any stream, not just a socket, which is what lets an HTTPS proxy and
  // an HTTPS origin stack their encryption the way a browser needs.
  static std::unique_ptr<TlsStream> open(Stream &underneath, const std::string &server_name,
                                         std::string &error);
  bool write_all(const uint8_t *data, size_t size, std::string &error) override;
  bool read_some(uint8_t *out, size_t capacity, size_t &got, std::string &error) override;
  bool alive() const override;
  // The certificate the far side presented, as I checked it.
  const std::string &peer_subject() const;

private:
  TlsStream();
  struct Impl;
  std::unique_ptr<Impl> impl_;
};

// RFC 1928 and RFC 1929. A name goes to the proxy as a name, which is what keeps
// the lookup off my machine.
bool socks5_connect(Stream &stream, const TunnelTarget &target, const std::string &user,
                    const std::string &pass, bool has_auth, std::string &error);

// HTTP CONNECT, plain or already inside TLS when the proxy itself speaks HTTPS.
bool http_connect(Stream &stream, const TunnelTarget &target, const std::string &user,
                  const std::string &pass, bool has_auth, std::string &error);

struct HttpRequest {
  std::string method = "GET", path = "/", host;
  int port = 80;
  std::vector<std::pair<std::string, std::string>> headers;
  std::string body;
  size_t max_bytes = kMaxResourceBytes;
};

struct HttpResponse {
  int status = 0;
  std::string reason;
  std::vector<std::pair<std::string, std::string>> headers;
  std::string body;
  Bytes bytes;
  bool chunked = false;
};

// One HTTP/1.1 exchange over whatever stream I am given: status line, headers,
// then a body framed by Content-Length, by chunks, or by the connection closing.
bool http_exchange(Stream &stream, const HttpRequest &request, HttpResponse &response,
                   std::string &error);

// The whole path a proxied request takes through my own code: connect, handshake,
// TLS where it belongs, request, response, redirects. A failure here is an error,
// never a quiet direct connection.
Resource tunneled_fetch(const Url &url, const TunnelConfig &config, const std::string &method,
                        const std::string &body, const std::string &content_type,
                        int timeout_ms, size_t max_bytes);
} // namespace pv
