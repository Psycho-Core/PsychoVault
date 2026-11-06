// PsychoVault original source. My broker implementation - role masks, service
// registration and the routing pump. Process envelopes arrive at S022; the
// rules below do not change when they do.
#include "Psycho_broker.hpp"

namespace pv::rt {

Capability capabilities_for(Role role) {
  auto mask_of = [](std::initializer_list<Channel> channels) {
    Capability cap;
    for (Channel c : channels)
      cap.allow |= uint64_t(1) << static_cast<uint16_t>(c);
    return cap;
  };
  switch (role) {
  case Role::Broker:
    return Capability::all();
  case Role::Renderer:
    return mask_of({Channel::Nav, Channel::Tab, Channel::Find, Channel::Zoom,
                    Channel::View, Channel::Clip, Channel::Spell, Channel::Perf,
                    Channel::Settings, Channel::Diagnostics, Channel::Lifecycle});
  case Role::Network:
    return mask_of({Channel::Nav, Channel::Downloads, Channel::Diagnostics, Channel::Lifecycle});
  case Role::Storage:
    return mask_of({Channel::History, Channel::Settings, Channel::Bookmarks, Channel::Vault,
                    Channel::Lifecycle});
  case Role::Extensions:
    return mask_of({Channel::Extensions, Channel::Proton, Channel::Tab, Channel::Nav,
                    Channel::Diagnostics, Channel::Lifecycle});
  }
  return {};
}

LifecycleService::LifecycleService() : started_(std::chrono::steady_clock::now()) {}

void LifecycleService::handle(const Envelope &request, Envelope &response, Error &error) {
  if (request.method == "lifecycle:ping") {
    response.channel = Channel::Lifecycle;
    response.method = "lifecycle:pong";
    response.request_id = request.request_id;
    Json payload = Json::make_object();
    auto elapsed = std::chrono::steady_clock::now() - started_;
    payload.set("uptime_ms", Json(double(
        std::chrono::duration_cast<std::chrono::milliseconds>(elapsed).count())));
    response.payload = std::move(payload);
    return;
  }
  if (request.method == "lifecycle:echo") {
    response.channel = Channel::Lifecycle;
    response.method = request.method;
    response.request_id = request.request_id;
    response.payload = request.payload;
    return;
  }
  error = {"lifecycle.unknown", "unknown lifecycle request"};
}

Broker::Broker() {
  auto lifecycle = std::make_unique<LifecycleService>();
  services_[Channel::Lifecycle] = std::move(lifecycle);
}

int Broker::spawn_renderer(const std::string &label) {
  auto host = std::make_unique<Host>();
  host->label = label;
  host->role = Role::Renderer;
  host->link = std::make_unique<Link>(capabilities_for(Role::Renderer), label,
                                      Capability::all(), "broker:" + label);
  host->link->set_schemas(&schemas_);
  hosts_.push_back(std::move(host));
  return int(hosts_.size()) - 1;
}

Endpoint &Broker::renderer_side(int index) { return hosts_[size_t(index)]->link->a(); }
Endpoint &Broker::broker_side(int index) { return hosts_[size_t(index)]->link->b(); }

bool Broker::has_renderer(int index) const {
  return index >= 0 && size_t(index) < hosts_.size();
}

bool Broker::renderer_qualified(int index) const {
  return has_renderer(index) && hosts_[size_t(index)]->qualified;
}

bool Broker::set_renderer_qualified(int index, bool qualified) {
  if (!has_renderer(index)) return false;
  hosts_[size_t(index)]->qualified = qualified;
  return true;
}

void Broker::add_service(Channel channel, std::unique_ptr<Service> service) {
  services_[channel] = std::move(service);
}

size_t Broker::pump() {
  size_t delivered = 0;
  for (auto &host : hosts_) {
    Endpoint &mine = host->link->b();
    for (;;) {
      Envelope request;
      Error error;
      if (!mine.recv(request, error)) {
        if (error)
          note(error);
        break;
      }
      ++delivered;
      // The only pre-admission conversation is the broker's harmless echo
      // probe. All renderer UI/service traffic is refused until S043 passes.
      if (!host->qualified &&
          !(request.channel == Channel::Lifecycle &&
            request.method == "lifecycle:echo")) {
        Error denied{"broker.renderer-unqualified",
                     "renderer must pass the broker harness before UI traffic"};
        note(denied);
        if (request.request_id != 0) {
          Envelope refusal;
          refusal.channel = request.channel;
          refusal.method = request.method;
          refusal.request_id = request.request_id;
          Json payload = Json::make_object();
          payload.set("error", Json(denied.code + ": " + denied.message));
          refusal.payload = std::move(payload);
          Error send_error;
          if (!mine.send(refusal, send_error)) note(send_error);
        }
        continue;
      }
      auto it = services_.find(request.channel);
      if (it == services_.end()) {
        note({"broker.no-service",
              "channel " + channel_name(request.channel) + " has no service"});
        if (request.request_id != 0) {
          // A request with no service still gets an answer: a structured refusal,
          // so callers surface a reason instead of hanging.
          Envelope refuse;
          refuse.channel = request.channel;
          refuse.method = request.method;
          refuse.request_id = request.request_id;
          Json payload = Json::make_object();
          payload.set("error", Json(std::string("no service for ") + request.method));
          refuse.payload = std::move(payload);
          Error send_error;
          if (!mine.send(refuse, send_error))
            note(send_error);
        }
        continue;
      }
      Envelope response;
      Error service_error;
      it->second->handle(request, response, service_error);
      if (service_error) {
        note(service_error);
        if (request.request_id != 0) {
          Envelope failure;
          failure.channel = request.channel;
          failure.method = request.method;
          failure.request_id = request.request_id;
          Json payload = Json::make_object();
          payload.set("error", Json(service_error.code + ": " + service_error.message));
          failure.payload = std::move(payload);
          Error send_error;
          if (!mine.send(failure, send_error))
            note(send_error);
        }
        continue;
      }
      if (request.request_id != 0 && response.method.empty()) {
        note({"broker.no-reply", "service left a request unanswered: " + request.method});
        continue;
      }
      if (request.request_id != 0) {
        response.request_id = request.request_id;
        Error send_error;
        if (!mine.send(response, send_error))
          note(send_error);
      }
    }
  }
  return delivered;
}

} // namespace pv::rt
