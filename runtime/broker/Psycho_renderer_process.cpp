// PsychoVault original source. The broker keeps network authority while the
// engine worker requests document/subresource fetches over its process pipe.
#include "Psycho_renderer_process.hpp"
#include "../../engine/Psycho_image.hpp"
#include <cmath>
#include <map>
#include <utility>

namespace pv::rt {
namespace {
ResourceType resource_type_from(double raw) {
  const int value = int(raw);
  if (value < int(ResourceType::Document) || value > int(ResourceType::Other))
    return ResourceType::Other;
  return static_cast<ResourceType>(value);
}

Json renderer_cookie_state(const Json &state) {
  Json visible = Json::make_array();
  if (state.type != Json::Type::Array) return visible;
  for (const Json &cookie : state.array) {
    if (cookie.type == Json::Type::Object && !cookie.flag("httpOnly"))
      visible.array.push_back(cookie);
  }
  return visible;
}

struct ImageTransfer {
  int width = 0, height = 0;
  size_t total = 0, next = 0;
  Bytes pixels;
};

Envelope network_reply(const Envelope &request, Resource resource) {
  Envelope response;
  response.channel = Channel::Network;
  response.method = request.method == "network:post" ? "network:postResult"
                                                      : "network:fetchResult";
  response.request_id = request.request_id;
  response.is_response = true;
  response.payload = Json::make_object();
  response.payload.set("url", Json(resource.url));
  response.payload.set("mime", Json(resource.mime));
  response.payload.set("text", Json(resource.text));
  response.payload.set("bytes_b64", Json(base64_encode(resource.bytes)));
  response.payload.set("status", Json(resource.status));
  response.payload.set("ok", Json(resource.ok));
  response.payload.set("refused", Json(resource.refused));
  if (resource.broker_cookie_state.type == Json::Type::Array)
    response.payload.set("cookie_state",
                         renderer_cookie_state(resource.broker_cookie_state));
  if (resource.broker_storage_state.type == Json::Type::Object)
    response.payload.set("storage_state", resource.broker_storage_state);
  if (!resource.error.empty()) response.payload.set("detail", Json(resource.error));
  return response;
}
} // namespace

bool RendererProcessClient::shutdown(Error &error) {
  if (next_request_id_ == 0) ++next_request_id_;
  const uint32_t request_id = next_request_id_++;
  Envelope request;
  request.channel = Channel::Lifecycle;
  request.method = "lifecycle:shutdown";
  request.request_id = request_id;
  request.payload = Json::make_object();
  if (!transport_.send(request, error)) return false;
  Envelope response;
  if (!transport_.recv(response, error)) return false;
  if (!response.is_response || response.request_id != request_id ||
      response.method != "lifecycle:shutdown") {
    error = {"renderer.shutdown.reply", "renderer returned an unrelated shutdown response"};
    return false;
  }
  error = Error{};
  return true;
}

bool RendererProcessClient::probe_isolation(const std::string &path,
                                            const std::string &host,
                                            uint16_t port, Envelope &reply,
                                            Error &error) {
  if (next_request_id_ == 0) ++next_request_id_;
  const uint32_t request_id = next_request_id_++;
  Envelope request;
  request.channel = Channel::Renderer;
  request.method = "renderer:isolationProbe";
  request.request_id = request_id;
  request.payload = Json::make_object();
  request.payload.set("path", Json(path));
  request.payload.set("host", Json(host));
  request.payload.set("port", Json(port));
  if (!transport_.send(request, error)) return false;
  if (!transport_.recv(reply, error)) return false;
  if (!reply.is_response || reply.request_id != request_id ||
      reply.method != request.method) {
    error = {"renderer.probe.reply", "renderer returned an unrelated isolation probe reply"};
    return false;
  }
  error = Error{};
  return true;
}

bool RendererProcessClient::exchange(const std::string &method, Json payload,
                                     const Host::Fetcher &fetcher,
                                     Envelope &reply, Error &error,
                                     const Json &cookie_state,
                                     const Json &storage_state,
                                     const Host::Poster &poster,
                                     const BridgeInvoker &bridge) {
  error = Error{};
  if (next_request_id_ == 0) ++next_request_id_;
  const uint32_t request_id = next_request_id_++;
  Envelope request;
  request.channel = Channel::Renderer;
  request.method = method;
  request.request_id = request_id;
  request.payload = std::move(payload);
  if (cookie_state.type == Json::Type::Array)
    request.payload.set("cookie_state", renderer_cookie_state(cookie_state));
  if (storage_state.type == Json::Type::Object)
    request.payload.set("storage_state", storage_state);
  if (!transport_.send(request, error)) return false;

  std::map<std::string, ImageTransfer> image_transfers;
  Json completed_images = Json::make_array();
  size_t aggregate_image_bytes = 0;
  for (;;) {
    Envelope incoming;
    if (!transport_.recv(incoming, error)) return false;
    if (incoming.is_response && incoming.request_id == request_id &&
        incoming.method == method) {
      if (!image_transfers.empty()) {
        error = {"renderer.image.incomplete", "renderer ended an image transfer early"};
        return false;
      }
      reply = std::move(incoming);
      if (!completed_images.array.empty())
        reply.payload.set("images", std::move(completed_images));
      error = Error{};
      return true;
    }
    if (!incoming.is_response && incoming.channel == Channel::Renderer &&
        incoming.method == "renderer:imageChunk" &&
        incoming.request_id == request_id) {
      const double raw_width = incoming.payload.num("width", 0);
      const double raw_height = incoming.payload.num("height", 0);
      const double raw_index = incoming.payload.num("index", -1);
      const double raw_total = incoming.payload.num("total", 0);
      if (raw_width <= 0 || raw_height <= 0 || raw_width > 16384 ||
          raw_height > 16384 || raw_index < 0 || raw_index > 255 ||
          raw_total <= 0 || raw_total > 256 ||
          raw_width != double(int(raw_width)) || raw_height != double(int(raw_height)) ||
          raw_index != double(size_t(raw_index)) || raw_total != double(size_t(raw_total))) {
        error = {"renderer.image.metadata", "renderer sent invalid image chunk metadata"};
        return false;
      }
      const int width = int(raw_width), height = int(raw_height);
      const size_t index = size_t(raw_index), total = size_t(raw_total);
      const size_t expected = size_t(width) * size_t(height) * 4;
      if (expected > kMaxImageRawBytes || expected > 64u * 1024u * 1024u ||
          index >= total) {
        error = {"renderer.image.limit", "renderer image transfer exceeded its limit"};
        return false;
      }
      const std::string url = incoming.payload.str("url");
      auto found = image_transfers.find(url);
      if (index == 0) {
        if (url.empty() ||
            completed_images.array.size() + image_transfers.size() >= 128 ||
            found != image_transfers.end()) {
          error = {"renderer.image.order", "renderer started a duplicate or invalid image"};
          return false;
        }
        ImageTransfer transfer;
        transfer.width = width;
        transfer.height = height;
        transfer.total = total;
        found = image_transfers.emplace(url, std::move(transfer)).first;
      }
      if (found == image_transfers.end() || found->second.width != width ||
          found->second.height != height || found->second.total != total ||
          found->second.next != index) {
        error = {"renderer.image.order", "renderer image chunks arrived out of order"};
        return false;
      }
      Bytes part;
      try {
        part = base64_decode(incoming.payload.str("pixels_b64"), 384u * 1024u);
      } catch (const std::exception &) {
        error = {"renderer.image.chunk", "renderer sent invalid image chunk bytes"};
        return false;
      }
      if (part.empty() || part.size() > expected ||
          found->second.pixels.size() > expected - part.size()) {
        error = {"renderer.image.size", "renderer image chunks exceed their declared size"};
        return false;
      }
      if (aggregate_image_bytes > 64u * 1024u * 1024u - part.size()) {
        error = {"renderer.image.limit", "renderer image batch exceeded its safety limit"};
        return false;
      }
      aggregate_image_bytes += part.size();
      found->second.pixels.insert(found->second.pixels.end(), part.begin(), part.end());
      ++found->second.next;
      if (found->second.next == total) {
        if (found->second.pixels.size() != expected) {
          error = {"renderer.image.size", "renderer image size did not match its dimensions"};
          return false;
        }
        Json image = Json::make_object();
        image.set("url", Json(url));
        image.set("width", Json(width));
        image.set("height", Json(height));
        image.set("pixels_b64", Json(base64_encode(found->second.pixels)));
        completed_images.array.push_back(std::move(image));
        image_transfers.erase(found);
      }
      continue;
    }
    if (!incoming.is_response && incoming.channel == Channel::Network &&
        (incoming.method == "network:fetch" || incoming.method == "network:post")) {
      Resource resource;
      const std::string requested_url = incoming.payload.str("url");
      resource.url = requested_url;
      try {
        if (incoming.method == "network:fetch" && fetcher) {
          resource = fetcher(requested_url,
              resource_type_from(incoming.payload.num("resource_type", -1)));
        } else if (incoming.method == "network:post" && poster) {
          resource = poster(requested_url, incoming.payload.str("body"),
                            incoming.payload.str("content_type"));
        } else {
          resource.ok = false;
          resource.refused = true;
          resource.error = incoming.method == "network:post"
                               ? "broker.post.unavailable"
                               : "broker.fetch.unavailable";
        }
      } catch (const std::exception &exception) {
        resource.ok = false;
        resource.refused = true;
        resource.error = exception.what();
      }
      if (resource.url.empty()) resource.url = requested_url;
      Envelope response = network_reply(incoming, std::move(resource));
      if (!transport_.send(response, error)) return false;
      continue;
    }
    if (!incoming.is_response && incoming.channel == Channel::Renderer &&
        incoming.method == "renderer:bridgeInvoke" && incoming.request_id != 0) {
      const double raw_channel = incoming.payload.num("channel", 0);
      const std::string method = incoming.payload.str("method");
      const Json *input = incoming.payload.find("payload");
      Json result = Json::make_object();
      Error bridge_error;
      bool ok = false;
      if (std::isfinite(raw_channel) && raw_channel > 0 && raw_channel < 64 &&
          raw_channel == double(static_cast<int>(raw_channel)) && input &&
          input->type == Json::Type::Object && bridge) {
        ok = bridge(static_cast<Channel>(static_cast<int>(raw_channel)), method,
                    *input, result, bridge_error);
      } else {
        bridge_error = {"bridge.request.invalid",
                        "renderer bridge request lacks a valid broker callback"};
      }
      Envelope response;
      response.channel = Channel::Renderer;
      response.method = incoming.method;
      response.request_id = incoming.request_id;
      response.is_response = true;
      response.payload = Json::make_object();
      response.payload.set("channel", Json(raw_channel));
      response.payload.set("method", Json(method));
      response.payload.set("payload", input ? *input : Json::make_object());
      response.payload.set("ok", Json(ok));
      if (ok) {
        if (result.type != Json::Type::Object) result = Json::make_object();
        response.payload.set("reply", std::move(result));
      } else {
        response.payload.set("detail", Json(bridge_error.code + ": " +
                                              bridge_error.message));
      }
      if (!transport_.send(response, error)) return false;
      continue;
    }
    error = {"renderer.protocol.unexpected",
             "unexpected process message while renderer request was pending"};
    return false;
  }
}

bool RendererProcessClient::navigate(const std::string &url,
                                     const Host::Fetcher &fetcher,
                                     Envelope &reply, Error &error,
                                     Json cookie_state, Json storage_state,
                                     Host::Poster poster, BridgeInvoker bridge,
                                     MediaEnvironment media_environment) {
  Json payload = Json::make_object();
  payload.set("url", Json(url));
  payload.set("width", Json(media_environment.viewport_width));
  payload.set("height", Json(media_environment.viewport_height));
  payload.set("devicePixelRatio", Json(media_environment.device_pixel_ratio));
  payload.set("scrollbarWidth", Json(media_environment.scrollbar_width));
  payload.set("prefersReducedMotion", Json(media_environment.prefers_reduced_motion));
  payload.set("prefersColorScheme", Json(media_environment.prefers_color_scheme));
  payload.set("prefersContrast", Json(media_environment.prefers_contrast));
  payload.set("forcedColors", Json(media_environment.forced_colors));
  return exchange("renderer:navigate", std::move(payload), fetcher, reply, error,
                  cookie_state, storage_state, poster, bridge);
}

bool RendererProcessClient::input(const Json &command,
                                  const Host::Fetcher &fetcher,
                                  Envelope &reply, Error &error,
                                  Json cookie_state, Json storage_state,
                                  Host::Poster poster, BridgeInvoker bridge) {
  return exchange("renderer:input", command, fetcher, reply, error,
                  cookie_state, storage_state, poster, bridge);
}

} // namespace pv::rt
