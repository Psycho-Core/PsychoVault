// PsychoVault original source. My psychovault://nettest and psychovault://version
// diagnostics contracts: named network stages, honest build/profile metadata,
// and no hidden transport or filesystem behavior. P1/S038 of
// docs/Psycho_MASTER-PLAN.md.
#pragma once
#include "../broker/Psycho_broker.hpp"
#include <functional>
#include <map>
#include <string>
#include <vector>

namespace pv::rt {

struct DiagnosticProbeResult {
  bool ok = false;
  std::string detail;
};

struct DiagnosticStageResult {
  std::string name;
  bool ok = false;
  std::string detail;
  std::string error; // nonempty only when the probe itself was unavailable/broke
};

struct VersionInfo {
  std::string runtime_version;
  std::string engine_version;
  std::string profile;
  std::vector<std::string> flags;
  std::map<std::string, std::string> hashes;
};

class DiagnosticsService : public Service {
public:
  struct Seams {
    // Each stage is a separate adapter over the network service. Missing
    // adapters produce an explicit failed stage; stages never vanish silently.
    std::function<DiagnosticProbeResult()> dns;
    std::function<DiagnosticProbeResult()> tcp;
    std::function<DiagnosticProbeResult()> tls;
    std::function<DiagnosticProbeResult()> http;
    std::function<DiagnosticProbeResult()> proxy;
    std::function<DiagnosticProbeResult()> dns_leak;
    // The broker supplies actual runtime/engine/profile/flags/hash facts. I do
    // not guess a version or manufacture a hash when that source is absent.
    std::function<bool(VersionInfo &, Error &)> version_info;
  };

  explicit DiagnosticsService(Seams seams) : seams_(std::move(seams)) {}

  std::string name() const override { return "diagnostics"; }
  void handle(const Envelope &request, Envelope &response,
              Error &error) override;

  // Always returns all six stages in plan order, even when one fails. A broken
  // stage cannot hide later independent diagnostics.
  std::vector<DiagnosticStageResult> run_nettest() const;
  bool version(VersionInfo &out, Error &error) const;

  static const std::vector<std::string> &stage_names();

private:
  Seams seams_;
};

} // namespace pv::rt
