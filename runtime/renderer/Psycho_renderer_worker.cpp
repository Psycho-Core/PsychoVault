// PsychoVault original source. Engine work stays behind a typed process boundary.
#include "Psycho_renderer_worker.hpp"
#include "../../engine/Psycho_image.hpp"
#include "../../engine/Psycho_page.hpp"
#include <algorithm>
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

Json renderer_cookie_state(const CookieJar &jar) {
  Json all = jar.to_json();
  Json visible = Json::make_array();
  for (const Json &cookie : all.array)
    if (cookie.type == Json::Type::Object && !cookie.flag("httpOnly"))
      visible.array.push_back(cookie);
  return visible;
}

bool renderer_url_allowed(const std::string &raw) {
  if (raw == "about:blank") return true;
  auto parsed = parse_url(raw);
  if (parsed && parsed->scheme == "psychovault" && parsed->host == "newtab" &&
      (parsed->path.empty() || parsed->path == "/") && parsed->query.empty() &&
      parsed->fragment.empty())
    return true;
  return parsed && parsed->network();
}

Resource broker_network_request(ProcessTransport &transport, Envelope request,
                                const std::string &response_method,
                                const std::string &fallback_url) {
  Resource resource;
  resource.url = fallback_url;
  Error error;
  if (!transport.send(request, error)) {
    resource.ok = false;
    resource.refused = true;
    resource.error = error.code + ": " + error.message;
    return resource;
  }
  Envelope response;
  if (!transport.recv(response, error) || !response.is_response ||
      response.request_id != request.request_id || response.method != response_method) {
    resource.ok = false;
    resource.refused = true;
    resource.error = error ? error.code + ": " + error.message
                           : "network response did not match its request";
    return resource;
  }
  resource.url = response.payload.str("url", fallback_url);
  resource.mime = response.payload.str("mime");
  resource.text = response.payload.str("text");
  resource.status = int(response.payload.num("status", 0));
  resource.ok = response.payload.flag("ok");
  resource.refused = response.payload.flag("refused");
  resource.error = response.payload.str("detail");
  if (const Json *state = response.payload.find("cookie_state"))
    resource.broker_cookie_state = *state;
  if (const Json *state = response.payload.find("storage_state"))
    resource.broker_storage_state = *state;
  try {
    resource.bytes = base64_decode(response.payload.str("bytes_b64"));
  } catch (const std::exception &) {
    resource.ok = false;
    resource.refused = true;
    resource.error = "network response contained invalid encoded bytes";
  }
  return resource;
}
} // namespace

Host::Fetcher broker_fetcher(ProcessTransport &transport) {
  return [&transport](const std::string &url, ResourceType type) {
    Envelope request;
    request.channel = Channel::Network;
    request.method = "network:fetch";
    request.request_id = next_fetch_id.fetch_add(1);
    if (request.request_id == 0) request.request_id = next_fetch_id.fetch_add(1);
    request.payload = Json::make_object();
    request.payload.set("url", Json(url));
    request.payload.set("resource_type", Json(int(static_cast<uint8_t>(type))));
    return broker_network_request(transport, std::move(request),
                                  "network:fetchResult", url);
  };
}

RendererWorker::RendererWorker(ProcessTransport &transport,
                               Host::Fetcher broker_fetcher)
    : transport_(transport), broker_fetcher_(std::move(broker_fetcher)),
      broker_poster_([&transport](const std::string &url, const std::string &body,
                                  const std::string &content_type) {
        Envelope request;
        request.channel = Channel::Network;
        request.method = "network:post";
        request.request_id = next_fetch_id.fetch_add(1);
        if (request.request_id == 0) request.request_id = next_fetch_id.fetch_add(1);
        request.payload = Json::make_object();
        request.payload.set("url", Json(url));
        request.payload.set("body", Json(body));
        request.payload.set("content_type", Json(content_type));
        return broker_network_request(transport, std::move(request),
                                      "network:postResult", url);
      }),
      started_(std::chrono::steady_clock::now()) {}

bool RendererWorker::app_bridge_invoke(Channel channel, const std::string &method,
                                        const Json &payload, Json &reply,
                                        Error &error) {
  if (next_bridge_id_ == 0) ++next_bridge_id_;
  const uint32_t request_id = next_bridge_id_++;
  Envelope request;
  request.channel = Channel::Renderer;
  request.method = "renderer:bridgeInvoke";
  request.request_id = request_id;
  request.payload = Json::make_object();
  request.payload.set("channel", Json(static_cast<int>(channel)));
  request.payload.set("method", Json(method));
  request.payload.set("payload", payload);
  if (!transport_.send(request, error)) return false;
  Envelope response;
  if (!transport_.recv(response, error)) return false;
  if (!response.is_response || response.request_id != request_id ||
      response.method != request.method) {
    error = {"bridge.reply.mismatch", "broker returned an unrelated app bridge response"};
    return false;
  }
  if (!response.payload.flag("ok")) {
    error = {"bridge.invoke.refused", response.payload.str("detail", "broker refused app action")};
    return false;
  }
  const Json *body = response.payload.find("reply");
  reply = body && body->type == Json::Type::Object ? *body : Json::make_object();
  error = Error{};
  return true;
}

bool RendererWorker::navigate(const Envelope &request, Envelope &response, Error &error) {
  const std::string url = request.payload.str("url");
  auto parsed_navigation = parse_url(url);
  const bool app_ui_page = parsed_navigation &&
                           parsed_navigation->scheme == "psychovault" &&
                           parsed_navigation->host == "newtab" &&
                           (parsed_navigation->path.empty() ||
                            parsed_navigation->path == "/") &&
                           parsed_navigation->query.empty() &&
                           parsed_navigation->fragment.empty();
  host_.reset();
  app_host_.reset();
  Json payload = Json::make_object();
  payload.set("url", Json(url));
  if (const Json *state = request.payload.find("cookie_state"))
    cookies_.from_json(*state);
  if (const Json *state = request.payload.find("storage_state"))
    storage_.from_json(*state);
  last_frame_ = Json();
  pending_images_.clear();
  pending_image_bytes_ = 0;
  pending_image_overflow_ = false;
  navigation_url_.clear();
  url_update_.clear();
  if (!renderer_url_allowed(url) || !broker_fetcher_) {
    host_.reset();
    payload.set("ok", Json(false));
    payload.set("detail", Json(!broker_fetcher_ ? "renderer.fetcher.unavailable"
                                                 : "renderer.navigation.scheme"));
    payload.set("cookie_state", renderer_cookie_state(cookies_));
    payload.set("storage_state", storage_.to_json());
    response.payload = std::move(payload);
    error = Error{};
    return true;
  }

  Host::Fetcher fetcher = [this](const std::string &requested, ResourceType type) {
    Resource resource = broker_fetcher_(requested, type);
    if (resource.broker_cookie_state.type == Json::Type::Array)
      cookies_.from_json(resource.broker_cookie_state);
    if (resource.broker_storage_state.type == Json::Type::Object)
      storage_.from_json(resource.broker_storage_state);
    return resource;
  };
  host_ = std::make_unique<Host>(std::move(fetcher), [this](HostEvent event) {
    if (event.tag == "frame") last_frame_ = std::move(event.frame);
    else if (event.tag == "image" && event.image && event.image->valid()) {
      constexpr size_t max_batch_bytes = 64u * 1024u * 1024u;
      if (pending_images_.size() >= 128 ||
          event.image->pixels.size() > max_batch_bytes - pending_image_bytes_) {
        pending_image_overflow_ = true;
      } else {
        pending_image_bytes_ += event.image->pixels.size();
        pending_images_.push_back({std::move(event.url), *event.image});
      }
    } else if (event.tag == "navigate") {
      navigation_url_ = std::move(event.url);
    } else if (event.tag == "url") {
      url_update_ = std::move(event.url);
    }
  });
  host_->use_cookies(&cookies_);
  host_->use_local_storage(&storage_);
  host_->set_poster([this](const std::string &requested, const std::string &body,
                           const std::string &content_type) {
    Resource resource = broker_poster_(requested, body, content_type);
    if (resource.broker_cookie_state.type == Json::Type::Array)
      cookies_.from_json(resource.broker_cookie_state);
    if (resource.broker_storage_state.type == Json::Type::Object)
      storage_.from_json(resource.broker_storage_state);
    return resource;
  });
  if (app_ui_page) {
    InternalRegistry registry = InternalRegistry::stock();
    app_host_ = std::make_unique<AppHost>(
        -1, registry.find("newtab"),
        AppHost::Seams{
            [this](Channel channel, const std::string &method, Json input,
                   Json &reply, Error &bridge_error) {
              return app_bridge_invoke(channel, method, input, reply, bridge_error);
            },
            {}});
    host_->set_page_bootstrap([this](js::Runtime &page_world) {
      if (!app_host_) throw std::runtime_error("new-tab bridge host is missing");
      Error bridge_error;
      if (!app_host_->install_into(page_world, bridge_error))
        throw std::runtime_error(bridge_error.code + ": " + bridge_error.message);
    });
  }
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
  payload.set("cookie_state", renderer_cookie_state(cookies_));
  payload.set("storage_state", storage_.to_json());
  response.payload = std::move(payload);
  error = Error{};
  return true;
}

bool RendererWorker::input(const Envelope &request, Envelope &response, Error &error) {
  const std::string tag = request.payload.str("tag");
  if (const Json *state = request.payload.find("cookie_state"))
    cookies_.from_json(*state);
  if (const Json *state = request.payload.find("storage_state"))
    storage_.from_json(*state);
  pending_images_.clear();
  pending_image_bytes_ = 0;
  pending_image_overflow_ = false;
  Json payload = Json::make_object();
  payload.set("tag", Json(tag));
  navigation_url_.clear();
  url_update_.clear();
  if (!host_ || !host_->loaded()) {
    payload.set("ok", Json(false));
    payload.set("detail", Json("renderer.page.not-loaded"));
    payload.set("cookie_state", renderer_cookie_state(cookies_));
    payload.set("storage_state", storage_.to_json());
    response.payload = std::move(payload);
    error = Error{};
    return true;
  }

  auto reject = [&](const std::string &reason) {
    payload.set("ok", Json(false));
    payload.set("detail", Json(reason));
    payload.set("cookie_state", renderer_cookie_state(cookies_));
    payload.set("storage_state", storage_.to_json());
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
  } else if (tag == "wheel") {
    double x = 0, y = 0, delta = 0;
    if (!finite_number("x", x) || !finite_number("y", y) ||
        !finite_number("delta", delta) || std::abs(delta) > 10000) {
      return reject("wheel input requires finite coordinates and a bounded delta");
    }
    host_->wheel(x, y, delta);
  } else if (tag == "scrollkey") {
    const std::string key = request.payload.str("key");
    if (key != "ArrowUp" && key != "ArrowDown" && key != "PageUp" &&
        key != "PageDown" && key != "Home" && key != "End")
      return reject("scrollkey is not a supported scrolling key");
    host_->scroll_key(key);
  } else if (tag == "click" || tag == "pointermove" || tag == "pointerdown" ||
             tag == "pointerup") {
    double x = 0, y = 0;
    if (!finite_number("x", x) || !finite_number("y", y)) {
      return reject("pointer input requires finite x and y coordinates");
    }
    if (tag == "click") host_->click(x, y);
    else if (tag == "pointermove") host_->pointer_move(x, y);
    else if (tag == "pointerdown") host_->pointer_down(x, y);
    else host_->pointer_up(x, y);
  } else if (tag == "pump") {
    host_->pump();
  } else if (tag == "key" || tag == "keyup") {
    const std::string key = request.payload.str("key");
    const std::string text = request.payload.str("text");
    if ((key.empty() && text.empty()) || key.size() > 64 || text.size() > 4096) {
      return reject("key input is empty or exceeds its size limit");
    }
    if (tag == "keyup") host_->key_up(key);
    else host_->key(key, text);
  } else if (tag == "copy" || tag == "cut") {
    const std::string clipboard = host_->edit_clipboard(tag == "cut");
    if (!clipboard.empty()) payload.set("clipboard", Json(clipboard));
  } else if (tag == "paste") {
    const std::string text = request.payload.str("text");
    if (text.size() > 65536) return reject("paste text exceeds its size limit");
    host_->paste_text(text);
  } else if (tag == "compositionstart") {
    host_->composition_start();
  } else if (tag == "compositionupdate") {
    const std::string text = request.payload.str("text");
    const double caret = request.payload.num("caret", 0);
    if (text.size() > 4096 || caret < 0 || caret > 8192 || caret != std::floor(caret))
      return reject("composition update is outside its text or caret bounds");
    host_->composition_update(text, size_t(caret));
  } else if (tag == "compositioncommit") {
    const std::string text = request.payload.str("text");
    if (text.size() > 4096) return reject("composition text exceeds its size limit");
    host_->composition_commit(text);
  } else if (tag == "compositioncancel") {
    host_->composition_cancel();
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
  if (!url_update_.empty()) payload.set("url_change", Json(url_update_));
  payload.set("cookie_state", renderer_cookie_state(cookies_));
  payload.set("storage_state", storage_.to_json());
  response.payload = std::move(payload);
  error = Error{};
  return true;
}

bool RendererWorker::send_images(uint32_t request_id, Error &error) {
  if (pending_image_overflow_) {
    error = {"renderer.image.limit", "renderer generated too many image pixels at once"};
    return false;
  }
  constexpr size_t chunk_bytes = 384u * 1024u;
  constexpr size_t max_batch_bytes = 64u * 1024u * 1024u;
  size_t batch_bytes = 0;
  for (const PendingImage &pending : pending_images_) {
    const Image &image = pending.image;
    if (!image.valid() || image.pixels.size() > kMaxImageRawBytes ||
        image.pixels.size() > max_batch_bytes ||
        batch_bytes > max_batch_bytes - image.pixels.size()) {
      error = {"renderer.image.limit", "image transfer exceeded its safety limit"};
      return false;
    }
    batch_bytes += image.pixels.size();
    const size_t total = (image.pixels.size() + chunk_bytes - 1) / chunk_bytes;
    if (total == 0 || total > 256) {
      error = {"renderer.image.chunks", "image transfer has an invalid chunk count"};
      return false;
    }
    for (size_t index = 0; index < total; ++index) {
      const size_t offset = index * chunk_bytes;
      const size_t count = std::min(chunk_bytes, image.pixels.size() - offset);
      Bytes part(image.pixels.begin() + static_cast<std::ptrdiff_t>(offset),
                 image.pixels.begin() + static_cast<std::ptrdiff_t>(offset + count));
      Envelope chunk;
      chunk.channel = Channel::Renderer;
      chunk.method = "renderer:imageChunk";
      chunk.request_id = request_id;
      chunk.payload = Json::make_object();
      chunk.payload.set("url", Json(pending.url));
      chunk.payload.set("width", Json(image.width));
      chunk.payload.set("height", Json(image.height));
      chunk.payload.set("index", Json(index));
      chunk.payload.set("total", Json(total));
      chunk.payload.set("pixels_b64", Json(base64_encode(part)));
      if (!transport_.send(chunk, error)) return false;
    }
  }
  pending_images_.clear();
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
                                   raw_path.find('\0') == std::string::npos
                               ? MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS,
                                                     raw_path.data(),
                                                     int(raw_path.size()), nullptr, 0)
                               : 0;
    bool file_readable = false;
    if (wide_count > 0) {
      std::wstring path(size_t(wide_count), L'\0');
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
  if ((request.method == "renderer:navigate" || request.method == "renderer:input") &&
      !send_images(request.request_id, error))
    return false;
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
