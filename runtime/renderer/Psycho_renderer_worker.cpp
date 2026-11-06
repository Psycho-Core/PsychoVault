// PsychoVault original source. Engine work stays behind a typed process boundary.
#include "Psycho_renderer_worker.hpp"
#include "../../engine/Psycho_page.hpp"
#include <atomic>
#include <cmath>
#include <utility>
#ifdef _WIN32
#define NOMINMAX
#include <winsock2.h>
#include <ws2tcpip.h>
#include <windows.h>
#endif

namespace pv::rt {
namespace {
std::atomic<uint32_t> next_fetch_id{1};

bool renderer_url_allowed(const std::string &raw) {
  if (raw == "about:blank" || raw == "psychovault://newtab") return true;
  auto parsed = parse_url(raw);
  return parsed && parsed->network();
}
} // namespace

Host::Fetcher broker_fetcher(ProcessTransport &transport) {
  return [&transport](const std::string &url, ResourceType type) {
    Resource resource;
    resource.url = url;
    Envelope request;
    request.channel = Channel::Network;
    request.method = "network:fetch";
    request.request_id = next_fetch_id.fetch_add(1);
    if (request.request_id == 0) request.request_id = next_fetch_id.fetch_add(1);
    request.payload = Json::make_object();
    request.payload.set("url", Json(url));
    request.payload.set("resource_type", Json(int(static_cast<uint8_t>(type))));
    Error error;
    if (!transport.send(request, error)) {
      resource.ok = false;
      resource.refused = true;
      resource.error = error.code + ": " + error.message;
      return resource;
    }
    Envelope response;
    if (!transport.recv(response, error) || !response.is_response ||
        response.request_id != request.request_id ||
        response.method != "network:fetchResult") {
      resource.ok = false;
      resource.refused = true;
      resource.error = error ? error.code + ": " + error.message
                             : "network response did not match its request";
      return resource;
    }
    resource.url = response.payload.str("url", url);
    resource.mime = response.payload.str("mime");
    resource.text = response.payload.str("text");
    resource.status = int(response.payload.num("status", 0));
    resource.ok = response.payload.flag("ok");
    resource.refused = response.payload.flag("refused");
    resource.error = response.payload.str("detail");
    try {
      resource.bytes = base64_decode(response.payload.str("bytes_b64"));
    } catch (const std::exception &) {
      resource.ok = false;
      resource.refused = true;
      resource.error = "network response contained invalid encoded bytes";
    }
    return resource;
  };
}

RendererWorker::RendererWorker(ProcessTransport &transport,
                               Host::Fetcher broker_fetcher)
    : transport_(transport), broker_fetcher_(std::move(broker_fetcher)),
      started_(std::chrono::steady_clock::now()) {}

bool RendererWorker::navigate(const Envelope &request, Envelope &response, Error &error) {
  const std::string url = request.payload.str("url");
  Json payload = Json::make_object();
  payload.set("url", Json(url));
  last_frame_ = Json();
  navigation_url_.clear();
  if (!renderer_url_allowed(url) || !broker_fetcher_) {
    host_.reset();
    payload.set("ok", Json(false));
    payload.set("detail", Json(!broker_fetcher_ ? "renderer.fetcher.unavailable"
                                                 : "renderer.navigation.scheme"));
    response.payload = std::move(payload);
    return true;
  }

  host_ = std::make_unique<Host>(broker_fetcher_, [this](HostEvent event) {
    if (event.tag == "frame") last_frame_ = std::move(event.frame);
    else if (event.tag == "navigate") navigation_url_ = std::move(event.url);
  });
  host_->resize(1024, 768);
  std::string load_error;
  const bool loaded = host_->load(url, load_error);
  if (loaded) host_->pump();
  payload.set("ok", Json(loaded));
  if (!load_error.empty()) payload.set("detail", Json(load_error));
  if (last_frame_.type == Json::Type::Object) {
    payload.set("url", Json(last_frame_.str("url", url)));
    payload.set("frame", last_frame_);
  }
  response.payload = std::move(payload);
  error = Error{};
  return true;
}

bool RendererWorker::input(const Envelope &request, Envelope &response, Error &error) {
  const std::string tag = request.payload.str("tag");
  Json payload = Json::make_object();
  payload.set("tag", Json(tag));
  navigation_url_.clear();
  if (!host_ || !host_->loaded()) {
    payload.set("ok", Json(false));
    payload.set("detail", Json("renderer.page.not-loaded"));
    response.payload = std::move(payload);
    error = Error{};
    return true;
  }

  auto reject = [&](const std::string &reason) {
    payload.set("ok", Json(false));
    payload.set("detail", Json(reason));
    response.payload = std::move(payload);
    error = Error{};
    return true;
  };
  auto finite_number = [&](const char *name, double &value) {
    const Json *field = request.payload.find(name);
    if (!field || field->type != Json::Type::Number || !std::isfinite(field->number))
      return false;
    value = field->number;
    return true;
  };
  if (tag == "resize") {
    double width = 0, height = 0;
    if (!finite_number("width", width) || !finite_number("height", height) ||
        width <= 0 || height <= 0) {
      return reject("resize requires positive finite width and height");
    }
    host_->resize(width, height);
  } else if (tag == "scroll") {
    double y = 0;
    if (!finite_number("y", y) || y < 0) {
      return reject("scroll requires a finite nonnegative y position");
    }
    host_->scroll(y);
  } else if (tag == "click") {
    double x = 0, y = 0;
    if (!finite_number("x", x) || !finite_number("y", y)) {
      return reject("click requires finite x and y coordinates");
    }
    host_->click(x, y);
  } else if (tag == "key") {
    const std::string key = request.payload.str("key");
    const std::string text = request.payload.str("text");
    if (key.empty() || key.size() > 64 || text.size() > 4096) {
      return reject("key input is empty or exceeds its size limit");
    }
    host_->key(key, text);
  } else {
    return reject("renderer input tag is not supported");
  }

  host_->pump();
  payload.set("ok", Json(true));
  if (last_frame_.type == Json::Type::Object) {
    payload.set("url", Json(last_frame_.str("url")));
    payload.set("frame", last_frame_);
  }
  if (!navigation_url_.empty()) payload.set("navigation", Json(navigation_url_));
  response.payload = std::move(payload);
  error = Error{};
  return true;
}

bool RendererWorker::serve_one(Error &error) {
  Envelope request;
  if (!transport_.recv(request, error)) return false;
  if (request.is_response || request.request_id == 0) {
    error = {"renderer.request.kind", "renderer expects a correlated broker request"};
    return false;
  }

  Envelope response;
  response.channel = request.channel;
  response.method = request.method;
  response.request_id = request.request_id;
  response.is_response = true;
  if (request.method == "lifecycle:ping") {
    response.method = "lifecycle:pong";
    const auto elapsed = std::chrono::steady_clock::now() - started_;
    response.payload = Json::make_object();
    response.payload.set("uptime_ms", Json(double(
        std::chrono::duration_cast<std::chrono::milliseconds>(elapsed).count())));
  } else if (request.method == "lifecycle:echo") {
    response.payload = request.payload;
  } else if (request.method == "lifecycle:shutdown") {
    stopping_ = true;
    response.payload = Json::make_object();
  } else if (request.method == "renderer:navigate") {
    if (!navigate(request, response, error)) return false;
  } else if (request.method == "renderer:input") {
    if (!input(request, response, error)) return false;
  } else if (request.method == "renderer:isolationProbe") {
#ifdef _WIN32
    const std::string raw_path = request.payload.str("path");
    const int wide_count = raw_path.size() <= 32767 &&
                                   raw_path.find('\\0') == std::string::npos
                               ? MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS,
                                                     raw_path.data(),
                                                     int(raw_path.size()), nullptr, 0)
                               : 0;
    bool file_readable = false;
    if (wide_count > 0) {
      std::wstring path(size_t(wide_count), L'\\0');
      if (MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS,
                              raw_path.data(), int(raw_path.size()), path.data(),
                              wide_count) == wide_count) {
        HANDLE file = CreateFileW(path.c_str(), GENERIC_READ,
                                  FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
                                  nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
        if (file != INVALID_HANDLE_VALUE) {
          char byte = 0;
          DWORD read = 0;
          file_readable = ReadFile(file, &byte, 1, &read, nullptr) != FALSE;
          CloseHandle(file);
        }
      }
    }

    const double raw_port = request.payload.num("port", 0);
    if (raw_port < 1 || raw_port > 65535 ||
        raw_port != double(static_cast<uint16_t>(raw_port))) {
      error = {"renderer.probe.port", "isolation probe requires a valid TCP port"};
      return false;
    }
    bool socket_created = false;
    bool socket_connected = false;
    WSADATA winsock{};
    if (WSAStartup(MAKEWORD(2, 2), &winsock) == 0) {
      SOCKET socket_handle = ::socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
      if (socket_handle != INVALID_SOCKET) {
        socket_created = true;
        sockaddr_in address{};
        address.sin_family = AF_INET;
        address.sin_port = htons(u_short(request.payload.num("port")));
        const std::string host = request.payload.str("host");
        if (InetPtonA(AF_INET, host.c_str(), &address.sin_addr) == 1) {
          u_long nonblocking = 1;
          if (ioctlsocket(socket_handle, FIONBIO, &nonblocking) == 0) {
            const int connected = connect(socket_handle,
                                          reinterpret_cast<sockaddr *>(&address),
                                          int(sizeof(address)));
            if (connected == 0) {
              socket_connected = true;
            } else if (WSAGetLastError() == WSAEWOULDBLOCK) {
              fd_set writable;
              FD_ZERO(&writable);
              FD_SET(socket_handle, &writable);
              timeval timeout{2, 0};
              if (select(0, nullptr, &writable, nullptr, &timeout) > 0) {
                int socket_error = 0;
                int error_size = int(sizeof(socket_error));
                socket_connected = getsockopt(socket_handle, SOL_SOCKET, SO_ERROR,
                                              reinterpret_cast<char *>(&socket_error),
                                              &error_size) == 0 && socket_error == 0;
              }
            }
          }
        }
        closesocket(socket_handle);
      }
      WSACleanup();
    }
    response.payload = Json::make_object();
    response.payload.set("path", Json(raw_path));
    response.payload.set("host", Json(request.payload.str("host")));
    response.payload.set("port", Json(request.payload.num("port")));
    response.payload.set("file_opened", Json(file_readable));
    response.payload.set("socket_created", Json(socket_created));
    response.payload.set("socket_connected", Json(socket_connected));
#else
    error = {"renderer.probe.unsupported", "isolation probe requires Windows"};
    return false;
#endif
  } else {
    error = {"renderer.method.unknown", "renderer received an unsupported method"};
    return false;
  }
  if (!transport_.send(response, error)) return false;
  error = Error{};
  return true;
}

int RendererWorker::run() {
  while (!stopping_) {
    Error error;
    if (!serve_one(error)) return 1;
  }
  return 0;
}

} // namespace pv::rt
