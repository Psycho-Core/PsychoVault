// PsychoVault original source. My structured per-service logger: JSONL records,
// pre-write secret redaction, bounded rotation, and a support ZIP export.
// P1/S040 of docs/Psycho_MASTER-PLAN.md.
#pragma once
#include "../ipc/Psycho_bus.hpp"
#include <functional>
#include <map>
#include <mutex>
#include <string>

namespace pv::rt {

enum class LogLevel { Debug, Info, Warn, Error };
const char *log_level_name(LogLevel level);

struct LogOptions {
  size_t max_file_bytes = 1u << 20; // 1 MiB per current service log
  size_t backups = 3;               // .1 is newest, then .2, ...
};

class StructuredLog {
public:
  using Fields = std::map<std::string, std::string>;
  using Clock = std::function<int64_t()>; // Unix epoch milliseconds

  explicit StructuredLog(std::string directory);
  StructuredLog(std::string directory, LogOptions options, Clock clock = {});

  // Thread-safe across calls on this instance. Service names are path-safe
  // canonical tokens; accepted records go to that service's <name>.log after
  // redaction.
  bool write(const std::string &service, LogLevel level, const std::string &event,
             const Fields &fields, Error &error);

  // Exports regular service logs and their rotated siblings as a ZIP with a
  // manifest. Only logger-owned filenames are included; symlinks are skipped.
  bool export_support_bundle(const std::string &output_path, Error &error) const;

  std::string log_path(const std::string &service) const;
  static std::string redact_value(const std::string &field,
                                  const std::string &value);
  static bool valid_service(const std::string &service);

  static constexpr size_t kMaxBackups = 10;
  static constexpr size_t kMaxBundleBytes = 8u << 20;

private:
  bool rotate(const std::string &service, Error &error) const;
  bool valid_event(const std::string &event) const;
  int64_t now_ms() const;

  std::string directory_;
  LogOptions options_;
  Clock clock_;
  mutable std::mutex mutex_;
};

} // namespace pv::rt
