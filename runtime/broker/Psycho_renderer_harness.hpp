// PsychoVault original source. The broker-side admission test a renderer must
// pass before it is exposed to ordinary UI traffic. P1/S043.
#pragma once
#include "Psycho_broker.hpp"
#include <chrono>
#include <cstdint>
#include <functional>
#include <string>
#include <vector>

namespace pv::rt {

enum class RendererProbe { Echo, Navigation, CrashRecovery };
const char *renderer_probe_name(RendererProbe probe);

struct RendererProbeResult {
  RendererProbe probe = RendererProbe::Echo;
  bool passed = false;
  int64_t elapsed_us = 0;
  std::string detail;
};

struct RendererQualification {
  int renderer_id = -1;
  bool passed = false;
  bool admitted = false;
  std::vector<RendererProbeResult> probes;
};

class RendererHarness {
public:
  struct Seams {
    using TimePoint = std::chrono::steady_clock::time_point;
    // Broker-owned test actions. The adapters must actually verify a committed
    // navigation and a crash/recovery cycle; an absent adapter fails closed.
    std::function<bool(int renderer_id, std::string &detail, Error &error)> navigation;
    std::function<bool(int renderer_id, std::string &detail, Error &error)> crash_recover;
    std::function<TimePoint()> now;
  };

  RendererHarness(Broker &broker, Seams seams);

  // Runs echo, navigation, crash/recover in that order. The broker keeps the
  // renderer quarantined on any failure and admits it only after all three pass.
  bool qualify(int renderer_id, RendererQualification &report, Error &error);
  void invalidate(int renderer_id); // a crash/replacement requires requalification
  bool admitted(int renderer_id) const { return broker_.renderer_qualified(renderer_id); }

private:
  bool echo_round_trip(int renderer_id, RendererProbeResult &result, Error &error);
  bool run_adapter(RendererProbe probe,
                   const std::function<bool(int, std::string &, Error &)> &adapter,
                   int renderer_id, RendererProbeResult &result, Error &error);
  Seams::TimePoint now() const;

  Broker &broker_;
  Seams seams_;
  uint32_t next_request_id_ = 0x80000000u;
};

} // namespace pv::rt
