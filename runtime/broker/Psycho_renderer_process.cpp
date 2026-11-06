// PsychoVault original source. The broker keeps network authority while the
// engine worker requests document/subresource fetches over its process pipe.
#include "Psycho_renderer_process.hpp"
#include <utility>

namespace pv::rt {
namespace {
ResourceType resource_type_from(double raw) {
  const int value = int(raw);
  if (value < int(ResourceType::Document) || value > int(ResourceType::Other))
    return ResourceType::Other;
  return static_cast<ResourceType>(value);
}

Envelope fetch_reply(const Envelope &request, Resource resource) {
  Envelope response;
  response.channel = Channel::Network;
  response.method = "network:fetchResult";
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
                                     Envelope &reply, Error &error) {
  error = Error{};
  if (next_request_id_ == 0) ++next_request_id_;
  const uint32_t request_id = next_request_id_++;
  Envelope request;
  request.channel = Channel::Renderer;
  request.method = method;
  request.request_id = request_id;
  request.payload = std::move(payload);
  if (!transport_.send(request, error)) return false;

  for (;;) {
    Envelope incoming;
    if (!transport_.recv(incoming, error)) return false;
    if (incoming.is_response && incoming.request_id == request_id &&
        incoming.method == method) {
      reply = std::move(incoming);
      error = Error{};
      return true;
    }
    if (!incoming.is_response && incoming.channel == Channel::Network &&
        incoming.method == "network:fetch") {
      Resource resource;
      const std::string requested_url = incoming.payload.str("url");
      resource.url = requested_url;
      if (fetcher) {
        resource = fetcher(requested_url,
            resource_type_from(incoming.payload.num("resource_type", -1)));
        if (resource.url.empty()) resource.url = requested_url;
      } else {
        resource.ok = false;
        resource.refused = true;
        resource.error = "broker.fetch.unavailable";
      }
      Envelope response = fetch_reply(incoming, std::move(resource));
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
                                     Envelope &reply, Error &error) {
  Json payload = Json::make_object();
  payload.set("url", Json(url));
  return exchange("renderer:navigate", std::move(payload), fetcher, reply, error);
}

bool RendererProcessClient::input(const Json &command,
                                  const Host::Fetcher &fetcher,
                                  Envelope &reply, Error &error) {
  return exchange("renderer:input", command, fetcher, reply, error);
}

} // namespace pv::rt
