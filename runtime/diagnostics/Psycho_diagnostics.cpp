// PsychoVault original source. Diagnostics orchestration is deliberately
// separate from the network implementation: each named stage consumes its own
// broker adapter and reports an honest result. See Psycho_diagnostics.hpp.
#include "Psycho_diagnostics.hpp"
#include <exception>
#include <utility>

namespace pv::rt {

namespace {

Error refuse(const char *code, const std::string &message) {
  Error error;
  error.code = code;
  error.message = message;
  return error;
}

using Probe = std::function<DiagnosticProbeResult()>;

DiagnosticStageResult run_stage(const char *name, const Probe &probe) {
  DiagnosticStageResult stage;
  stage.name = name;
  if (!probe) {
    stage.detail = "no network adapter is wired for this stage";
    stage.error = "diagnostics.unavailable";
    return stage;
  }
  try {
    DiagnosticProbeResult result = probe();
    stage.ok = result.ok;
    stage.detail = std::move(result.detail);
  } catch (const std::exception &exception) {
    stage.detail = exception.what();
    stage.error = "diagnostics.probe-failed";
  } catch (...) {
    stage.detail = "network probe failed with a non-standard exception";
    stage.error = "diagnostics.probe-failed";
  }
  return stage;
}

} // namespace

const std::vector<std::string> &DiagnosticsService::stage_names() {
  static const std::vector<std::string> names = {
      "dns", "tcp", "tls", "http", "proxy", "dns-leak"};
  return names;
}

void DiagnosticsService::handle(const Envelope &request, Envelope &response,
                                Error &error) {
  error = Error{};
  if (request.channel != Channel::Diagnostics) {
    error = refuse("diagnostics.channel", "diagnostics only answers its own channel");
    return;
  }
  response.channel = Channel::Diagnostics;
  response.method = request.method;
  if (request.method == "diagnostics:nettest") {
    Json stages = Json::make_array();
    for (const DiagnosticStageResult &stage : run_nettest()) {
      Json item = Json::make_object();
      item.set("stage", Json(stage.name));
      item.set("ok", Json(stage.ok));
      item.set("result", Json(stage.detail));
      if (!stage.error.empty()) item.set("error", Json(stage.error));
      stages.array.push_back(std::move(item));
    }
    Json payload = Json::make_object();
    payload.set("stages", std::move(stages));
    response.payload = std::move(payload);
    return;
  }
  if (request.method == "diagnostics:version") {
    VersionInfo info;
    if (!version(info, error)) return;
    Json flags = Json::make_array();
    for (const std::string &flag : info.flags) flags.array.push_back(Json(flag));
    Json hashes = Json::make_object();
    for (const auto &[name, value] : info.hashes)
      hashes.set(name, Json(value));
    Json payload = Json::make_object();
    payload.set("runtime", Json(info.runtime_version));
    payload.set("engine", Json(info.engine_version));
    payload.set("profile", Json(info.profile));
    payload.set("flags", std::move(flags));
    payload.set("hashes", std::move(hashes));
    response.payload = std::move(payload);
    return;
  }
  error = refuse("diagnostics.method", "unknown diagnostics request '" +
                                            request.method + "'");
}

std::vector<DiagnosticStageResult> DiagnosticsService::run_nettest() const {
  const Probe probes[] = {seams_.dns,      seams_.tcp,   seams_.tls,
                          seams_.http,     seams_.proxy, seams_.dns_leak};
  std::vector<DiagnosticStageResult> results;
  results.reserve(stage_names().size());
  for (size_t i = 0; i < stage_names().size(); ++i)
    results.push_back(run_stage(stage_names()[i].c_str(), probes[i]));
  return results;
}

bool DiagnosticsService::version(VersionInfo &out, Error &error) const {
  error = Error{};
  out = VersionInfo{};
  if (!seams_.version_info) {
    error = refuse("diagnostics.version-unavailable",
                   "the broker supplied no version facts");
    return false;
  }
  try {
    if (!seams_.version_info(out, error)) {
      if (!error)
        error = refuse("diagnostics.version-failed",
                       "the broker could not provide version facts");
      return false;
    }
  } catch (const std::exception &exception) {
    out = VersionInfo{};
    error = refuse("diagnostics.version-failed", exception.what());
    return false;
  } catch (...) {
    out = VersionInfo{};
    error = refuse("diagnostics.version-failed",
                   "version provider failed with a non-standard exception");
    return false;
  }
  return true;
}

} // namespace pv::rt
