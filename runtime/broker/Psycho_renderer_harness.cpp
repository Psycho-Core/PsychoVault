// PsychoVault original source. Qualification is fail-closed: an endpoint is
// useful to the shell only after its exact IPC, navigation, and recovery probes.
#include "Psycho_renderer_harness.hpp"
#include <algorithm>
#include <exception>
#include <utility>

namespace pv::rt {
namespace {

int64_t elapsed_us(RendererHarness::Seams::TimePoint start,
                   RendererHarness::Seams::TimePoint end) {
  return std::max<int64_t>(
      0, std::chrono::duration_cast<std::chrono::microseconds>(end - start).count());
}

const char *failure_code(RendererProbe probe) {
  switch (probe) {
  case RendererProbe::Echo: return "renderer.harness.echo";
  case RendererProbe::Navigation: return "renderer.harness.navigation";
  case RendererProbe::CrashRecovery: return "renderer.harness.crash-recover";
  }
  return "renderer.harness.probe";
}

} // namespace

const char *renderer_probe_name(RendererProbe probe) {
  switch (probe) {
  case RendererProbe::Echo: return "echo";
  case RendererProbe::Navigation: return "navigation";
  case RendererProbe::CrashRecovery: return "crash-recover";
  }
  return "unknown";
}

RendererHarness::RendererHarness(Broker &broker, Seams seams)
    : broker_(broker), seams_(std::move(seams)) {}

RendererHarness::Seams::TimePoint RendererHarness::now() const {
  return seams_.now ? seams_.now() : std::chrono::steady_clock::now();
}

bool RendererHarness::echo_round_trip(int renderer_id,
                                      RendererProbeResult &result,
                                      Error &error) {
  error = Error{};
  result.probe = RendererProbe::Echo;
  const auto start = now();
  if (broker_.broker_side(renderer_id).pending() != 0 ||
      broker_.renderer_side(renderer_id).pending() != 0) {
    result.elapsed_us = elapsed_us(start, now());
    result.detail = "renderer has pending IPC before qualification";
    error = {failure_code(result.probe), result.detail};
    return false;
  }
  if (next_request_id_ == 0) ++next_request_id_;
  const uint32_t request_id = next_request_id_++;
  const std::string nonce = "pv-renderer-harness-" + std::to_string(renderer_id) +
                            "-" + std::to_string(request_id);
  Envelope request;
  request.channel = Channel::Lifecycle;
  request.method = "lifecycle:echo";
  request.request_id = request_id;
  request.payload = Json::make_object();
  request.payload.set("nonce", Json(nonce));
  if (!broker_.renderer_side(renderer_id).send(request, error)) {
    result.elapsed_us = elapsed_us(start, now());
    result.detail = error.code + ": " + error.message;
    error = {failure_code(result.probe), result.detail};
    return false;
  }

  broker_.pump();
  bool found = false;
  bool matched = false;
  Error receive_error;
  for (;;) {
    Envelope response;
    if (!broker_.renderer_side(renderer_id).recv(response, receive_error)) break;
    if (response.request_id != request_id) continue;
    found = true;
    matched = response.channel == Channel::Lifecycle &&
              response.method == "lifecycle:echo" &&
              response.payload.str("nonce") == nonce;
  }
  result.elapsed_us = elapsed_us(start, now());
  result.passed = found && matched && !receive_error;
  result.detail = result.passed
                      ? "nonce echoed in " + std::to_string(result.elapsed_us) + " us"
                      : !found ? "broker returned no echo response"
                               : receive_error ? receive_error.code + ": " + receive_error.message
                                              : "echo response did not match request";
  if (!result.passed) {
    error = {failure_code(result.probe), result.detail};
    return false;
  }
  return true;
}

bool RendererHarness::run_adapter(
    RendererProbe probe,
    const std::function<bool(int, std::string &, Error &)> &adapter,
    int renderer_id, RendererProbeResult &result, Error &error) {
  error = Error{};
  result.probe = probe;
  const auto start = now();
  std::string detail;
  Error adapter_error;
  bool passed = false;
  if (!adapter) {
    adapter_error = {failure_code(probe),
                     std::string(renderer_probe_name(probe)) + " probe is not wired"};
  } else {
    try {
      passed = adapter(renderer_id, detail, adapter_error);
    } catch (const std::exception &exception) {
      adapter_error = {failure_code(probe), exception.what()};
    } catch (...) {
      adapter_error = {failure_code(probe), "probe threw a non-standard exception"};
    }
  }
  result.elapsed_us = elapsed_us(start, now());
  result.passed = passed && !adapter_error;
  result.detail = detail;
  if (adapter_error) {
    if (result.detail.empty()) result.detail = adapter_error.message;
    else result.detail = adapter_error.code + ": " + adapter_error.message + " (" +
                         result.detail + ")";
  }
  if (!result.passed) {
    if (result.detail.empty())
      result.detail = std::string(renderer_probe_name(probe)) + " probe failed";
    error = {failure_code(probe), result.detail};
    return false;
  }
  if (result.detail.empty()) result.detail = "probe passed";
  return true;
}

bool RendererHarness::qualify(int renderer_id, RendererQualification &report,
                              Error &error) {
  error = Error{};
  report = RendererQualification{};
  report.renderer_id = renderer_id;
  if (!broker_.has_renderer(renderer_id)) {
    error = {"renderer.harness.renderer-unknown",
             "renderer endpoint does not exist"};
    return false;
  }
  // Requalification closes the gate before any probes run, including after a
  // prior success. A failure can never leave stale UI access enabled.
  broker_.set_renderer_qualified(renderer_id, false);

  RendererProbeResult result;
  if (!echo_round_trip(renderer_id, result, error)) {
    report.probes.push_back(std::move(result));
    return false;
  }
  report.probes.push_back(std::move(result));

  if (!run_adapter(RendererProbe::Navigation, seams_.navigation, renderer_id,
                   result, error)) {
    report.probes.push_back(std::move(result));
    return false;
  }
  report.probes.push_back(std::move(result));

  if (!run_adapter(RendererProbe::CrashRecovery, seams_.crash_recover,
                   renderer_id, result, error)) {
    report.probes.push_back(std::move(result));
    return false;
  }
  report.probes.push_back(std::move(result));

  report.passed = true;
  report.admitted = broker_.set_renderer_qualified(renderer_id, true);
  if (!report.admitted) {
    report.passed = false;
    error = {"renderer.harness.renderer-unknown",
             "renderer disappeared before admission"};
    return false;
  }
  return true;
}

void RendererHarness::invalidate(int renderer_id) {
  broker_.set_renderer_qualified(renderer_id, false);
}

} // namespace pv::rt
