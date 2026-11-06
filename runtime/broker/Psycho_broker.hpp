// PsychoVault original source. My broker - the privileged process of my own
// application runtime. P1/S020 of docs/Psycho_MASTER-PLAN.md.
//
// What S020 means in this file, honestly: the roles (broker, renderer, network,
// storage, extension host), the capabilities each role gets, the service table,
// and message routing are all real and tested. What is NOT here yet is S022:
// wrapping renderer endpoints in real Windows processes with job objects and
// restricted tokens. Until then a "renderer" is an in-process endpoint with a
// restricted capability mask - which already blocks it speaking on privileged
// channels, but cannot yet stop it touching the filesystem. I say so in every
// report until S022 lands.
#pragma once
#include "../ipc/Psycho_bus.hpp"
#include <chrono>
#include <map>
#include <memory>
#include <vector>

namespace pv::rt {

enum class Role { Broker, Renderer, Network, Storage, Extensions };

// The capability masks are my security wall while processes are in memory:
// a renderer may drive navigation, ask for storage through the storage service
// and listen to lifecycle; it may never touch vault, settings-write, extension
// control or the proton channel directly.
Capability capabilities_for(Role role);

class Service {
public:
  virtual ~Service() = default;
  virtual std::string name() const = 0;
  // I handle one validated envelope. If it carries a request id, my reply goes
  // back down the same link with the id preserved, so callers correlate.
  virtual void handle(const Envelope &request, Envelope &response, Error &error) = 0;
};

class RendererHarness;

class Broker {
public:
  Broker();
  Endpoint &renderer_side(int index);
  Endpoint &broker_side(int index);

  // S026 arrives in slices; the ordered tab list is the first slice to exist.
  int spawn_renderer(const std::string &label); // quarantined until the harness passes
  bool has_renderer(int index) const;
  bool renderer_qualified(int index) const;

  void add_service(Channel channel, std::unique_ptr<Service> service);

  // I pump every renderer link once: validated messages in, services answer,
  // answers route back. Returns how many envelopes I delivered this pass.
  size_t pump();

  const Schemas &schemas() const { return schemas_; }
  const std::vector<Error> &log() const { return log_; }
  void note(Error error) { log_.push_back(std::move(error)); }

private:
  friend class RendererHarness;
  bool set_renderer_qualified(int index, bool qualified);

  struct Host {
    std::string label;
    Role role = Role::Renderer;
    bool qualified = false;
    std::unique_ptr<Link> link;
  };
  std::vector<std::unique_ptr<Host>> hosts_;
  Schemas schemas_ = Schemas::stock();
  std::map<Channel, std::unique_ptr<Service>> services_;
  std::vector<Error> log_;
};

// The lifecycle service is built in and minimal on purpose: ping/pong with an
// uptime figure, so heartbeat (S022's supervision contract) is exercised before
// there are processes to supervise.
class LifecycleService : public Service {
public:
  LifecycleService();
  std::string name() const override { return "lifecycle"; }
  void handle(const Envelope &request, Envelope &response, Error &error) override;

private:
  std::chrono::steady_clock::time_point started_;
};

} // namespace pv::rt
