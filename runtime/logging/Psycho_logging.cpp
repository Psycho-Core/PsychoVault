// PsychoVault original source. My structured logging and support-export laws.
#include "Psycho_logging.hpp"
#include "../../engine/Psycho_base.hpp"
#include <algorithm>
#include <array>
#include <chrono>
#include <cctype>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <limits>
#include <system_error>
#include <utility>
#include <vector>

namespace pv::rt {
namespace fs = std::filesystem;

namespace {

Error refuse(const char *code, const std::string &message) {
  Error error;
  error.code = code;
  error.message = message;
  return error;
}

fs::path utf8_path(const std::string &text) {
  const auto *data = reinterpret_cast<const char8_t *>(text.data());
  return fs::path(data, data + text.size());
}

std::string utf8_string(const fs::path &path) {
  const auto text = path.u8string();
  return std::string(reinterpret_cast<const char *>(text.data()), text.size());
}

std::string lower_ascii(std::string text) {
  for (char &c : text) c = (char)std::tolower((unsigned char)c);
  return text;
}

std::string normalized_key(std::string key) {
  key = lower_ascii(std::move(key));
  for (char &c : key)
    if (c == '-' || c == '_' || c == '.') c = ' ';
  return key;
}

bool sensitive_key(const std::string &key) {
  const std::string normalized = normalized_key(key);
  for (const char *part : {"password", "passwd", "secret", "token",
                            "authorization", "cookie", "credential",
                            "api key", "access key", "private key"}) {
    if (normalized.find(part) != std::string::npos) return true;
  }
  size_t at = 0;
  while (at < normalized.size()) {
    while (at < normalized.size() && normalized[at] == ' ') ++at;
    size_t end = normalized.find(' ', at);
    if (end == std::string::npos) end = normalized.size();
    if (normalized.substr(at, end - at) == "auth") return true;
    at = end + 1;
  }
  return false;
}

bool sensitive_query_name(std::string name) {
  name = lower_ascii(std::move(name));
  std::string decoded;
  auto hex_digit = [](char c) -> int {
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    return -1;
  };
  for (size_t i = 0; i < name.size(); ++i) {
    if (name[i] == '%' && i + 2 < name.size()) {
      const int high = hex_digit(name[i + 1]);
      const int low = hex_digit(name[i + 2]);
      if (high >= 0 && low >= 0) {
        const char byte = char((high << 4) | low);
        if (std::isalnum((unsigned char)byte) || byte == '_' || byte == '-' ||
            byte == '.' || byte == ' ') {
          decoded += byte;
          i += 2;
          continue;
        }
      }
    }
    decoded += name[i] == '+' ? ' ' : name[i];
  }
  return sensitive_key(decoded);
}

std::string redact_url(std::string value) {
  const size_t scheme = value.find("://");
  if (scheme != std::string::npos) {
    const size_t authority = scheme + 3;
    const size_t end = value.find_first_of("/?#", authority);
    const size_t authority_end = end == std::string::npos ? value.size() : end;
    const size_t at = value.rfind('@', authority_end);
    if (at != std::string::npos && at >= authority)
      value.replace(authority, at - authority, "[REDACTED]");
  }
  size_t query = value.find('?');
  if (query == std::string::npos) return value;
  size_t query_end = value.find('#', query + 1);
  if (query_end == std::string::npos) query_end = value.size();
  size_t at = query + 1;
  while (at < query_end) {
    const size_t part_end = value.find_first_of("&;", at);
    const size_t end = part_end == std::string::npos || part_end > query_end
                           ? query_end
                           : part_end;
    const size_t equal = value.find('=', at);
    if (equal != std::string::npos && equal < end &&
        sensitive_query_name(value.substr(at, equal - at))) {
      value.replace(equal + 1, end - equal - 1, "[REDACTED]");
      const size_t delta = std::string("[REDACTED]").size() - (end - equal - 1);
      query_end += delta;
      at = equal + 1 + std::string("[REDACTED]").size();
    } else {
      at = end;
    }
    if (at < query_end && (value[at] == '&' || value[at] == ';')) ++at;
    else if (at == end && at < query_end) ++at;
  }
  return value;
}

bool starts_with_case_insensitive(const std::string &text, size_t at,
                                  const char *needle) {
  size_t i = 0;
  while (needle[i]) {
    if (at + i >= text.size() ||
        std::tolower((unsigned char)text[at + i]) !=
            std::tolower((unsigned char)needle[i]))
      return false;
    ++i;
  }
  return true;
}

size_t find_case_insensitive(const std::string &text, const char *needle,
                             size_t start) {
  for (size_t at = start; at < text.size(); ++at)
    if (starts_with_case_insensitive(text, at, needle)) return at;
  return std::string::npos;
}

std::string redact_inline(std::string value) {
  for (const char *scheme : {"bearer ", "basic "}) {
    size_t at = 0;
    while ((at = find_case_insensitive(value, scheme, at)) != std::string::npos) {
      size_t end = at + std::char_traits<char>::length(scheme);
      while (end < value.size() &&
             !std::isspace((unsigned char)value[end]) && value[end] != ',' &&
             value[end] != ';' && value[end] != '"' && value[end] != '\'')
        ++end;
      value.replace(at + std::char_traits<char>::length(scheme),
                    end - at - std::char_traits<char>::length(scheme),
                    "[REDACTED]");
      at += std::char_traits<char>::length(scheme) + 10;
    }
  }
  for (const char *name : {"password=", "passwd=", "token=", "access_token=",
                           "refresh_token=", "secret=", "api_key=", "authorization="}) {
    size_t at = 0;
    while ((at = lower_ascii(value).find(name, at)) != std::string::npos) {
      size_t end = at + std::char_traits<char>::length(name);
      while (end < value.size() && value[end] != '&' && value[end] != ';' &&
             value[end] != '#' && value[end] != ',' && value[end] != ' ' &&
             value[end] != '\n' && value[end] != '\r' && value[end] != '"' &&
             value[end] != '\'')
        ++end;
      value.replace(at + std::char_traits<char>::length(name),
                    end - at - std::char_traits<char>::length(name),
                    "[REDACTED]");
      at += std::char_traits<char>::length(name) + 10;
    }
  }
  return value;
}

void put_u16(std::string &out, uint16_t value) {
  out.push_back(char(value & 0xff));
  out.push_back(char((value >> 8) & 0xff));
}

void put_u32(std::string &out, uint32_t value) {
  put_u16(out, uint16_t(value & 0xffff));
  put_u16(out, uint16_t(value >> 16));
}

uint32_t crc32(const std::string &bytes) {
  uint32_t crc = 0xffffffffu;
  for (unsigned char byte : bytes) {
    crc ^= byte;
    for (int bit = 0; bit < 8; ++bit)
      crc = (crc >> 1) ^ (0xedb88320u & uint32_t(0u - (crc & 1u)));
  }
  return ~crc;
}

struct ZipEntry {
  std::string name;
  std::string data;
  uint32_t crc = 0;
  uint32_t offset = 0;
};

bool zip_store(std::vector<ZipEntry> &entries, std::string &out,
               std::string &error) {
  if (entries.size() > 65535) {
    error = "support bundle has too many files";
    return false;
  }
  out.clear();
  for (ZipEntry &entry : entries) {
    if (entry.name.size() > 65535 || entry.data.size() > UINT32_MAX ||
        out.size() > UINT32_MAX) {
      error = "support bundle exceeds ZIP32 limits";
      return false;
    }
    entry.crc = crc32(entry.data);
    entry.offset = uint32_t(out.size());
    put_u32(out, 0x04034b50u); // local file header
    put_u16(out, 20);
    put_u16(out, 0);           // flags
    put_u16(out, 0);           // stored, no compression
    put_u16(out, 0);           // DOS time
    put_u16(out, 0x0021);      // 1980-01-01
    put_u32(out, entry.crc);
    put_u32(out, uint32_t(entry.data.size()));
    put_u32(out, uint32_t(entry.data.size()));
    put_u16(out, uint16_t(entry.name.size()));
    put_u16(out, 0);           // extra length
    out += entry.name;
    out += entry.data;
    if (out.size() > StructuredLog::kMaxBundleBytes) {
      error = "support bundle exceeds the export size limit";
      return false;
    }
  }
  const uint32_t central_offset = uint32_t(out.size());
  for (const ZipEntry &entry : entries) {
    put_u32(out, 0x02014b50u); // central directory header
    put_u16(out, 20);
    put_u16(out, 20);
    put_u16(out, 0);
    put_u16(out, 0);
    put_u16(out, 0);
    put_u16(out, 0x0021);
    put_u32(out, entry.crc);
    put_u32(out, uint32_t(entry.data.size()));
    put_u32(out, uint32_t(entry.data.size()));
    put_u16(out, uint16_t(entry.name.size()));
    put_u16(out, 0);
    put_u16(out, 0);
    put_u16(out, 0);
    put_u16(out, 0);
    put_u32(out, 0);
    put_u32(out, entry.offset);
    out += entry.name;
  }
  const uint32_t central_size = uint32_t(out.size()) - central_offset;
  if (out.size() + 22 > StructuredLog::kMaxBundleBytes) {
    error = "support bundle exceeds the export size limit";
    return false;
  }
  put_u32(out, 0x06054b50u); // end of central directory
  put_u16(out, 0);
  put_u16(out, 0);
  put_u16(out, uint16_t(entries.size()));
  put_u16(out, uint16_t(entries.size()));
  put_u32(out, central_size);
  put_u32(out, central_offset);
  put_u16(out, 0); // comment length
  return true;
}

bool service_log_filename(const std::string &filename) {
  const size_t log = filename.find(".log");
  if (log == std::string::npos || !StructuredLog::valid_service(filename.substr(0, log)))
    return false;
  const std::string suffix = filename.substr(log + 4);
  if (suffix.empty()) return true;
  if (suffix.front() != '.' || suffix.size() == 1) return false;
  size_t index = 0;
  for (size_t i = 1; i < suffix.size(); ++i) {
    if (!std::isdigit((unsigned char)suffix[i])) return false;
    const size_t digit = size_t(suffix[i] - '0');
    if (index > StructuredLog::kMaxBackups / 10 ||
        index * 10 + digit > StructuredLog::kMaxBackups)
      return false;
    index = index * 10 + digit;
  }
  return index > 0;
}

bool read_binary(const fs::path &path, std::string &data, std::string &error) {
  std::error_code ec;
  const uintmax_t size = fs::file_size(path, ec);
  if (ec || size > StructuredLog::kMaxBundleBytes) {
    error = ec ? "cannot inspect service log" : "a service log exceeds export limits";
    return false;
  }
  std::ifstream in(path, std::ios::binary);
  if (!in) {
    error = "cannot open service log for export";
    return false;
  }
  data.assign(std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>());
  if (!in.eof() && in.fail()) {
    error = "cannot read service log for export";
    return false;
  }
  return true;
}

} // namespace

const char *log_level_name(LogLevel level) {
  switch (level) {
  case LogLevel::Debug: return "debug";
  case LogLevel::Info: return "info";
  case LogLevel::Warn: return "warn";
  case LogLevel::Error: return "error";
  }
  return "unknown";
}

StructuredLog::StructuredLog(std::string directory)
    : StructuredLog(std::move(directory), LogOptions{}, Clock{}) {}

StructuredLog::StructuredLog(std::string directory, LogOptions options, Clock clock)
    : directory_(std::move(directory)), options_(options), clock_(std::move(clock)) {
  if (options_.max_file_bytes == 0) options_.max_file_bytes = LogOptions{}.max_file_bytes;
  if (options_.backups > kMaxBackups) options_.backups = kMaxBackups;
}

bool StructuredLog::valid_service(const std::string &service) {
  if (service.empty() || service.size() > 64 ||
      !std::isalnum((unsigned char)service.front()))
    return false;
  for (char c : service) {
    if (!((c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '-' ||
          c == '_' || c == '.'))
      return false;
  }
  return service.find("..") == std::string::npos;
}

bool StructuredLog::valid_event(const std::string &event) const {
  if (event.empty() || event.size() > 128) return false;
  for (char c : event) {
    if (!((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
          (c >= '0' && c <= '9') || c == '.' || c == '-' || c == '_' || c == ':'))
      return false;
  }
  return true;
}

std::string StructuredLog::log_path(const std::string &service) const {
  if (!valid_service(service) || directory_.empty()) return {};
  return utf8_string(utf8_path(directory_) / (service + ".log"));
}

int64_t StructuredLog::now_ms() const {
  if (clock_) return clock_();
  return std::chrono::duration_cast<std::chrono::milliseconds>(
             std::chrono::system_clock::now().time_since_epoch())
      .count();
}

std::string StructuredLog::redact_value(const std::string &field,
                                        const std::string &value) {
  if (sensitive_key(field)) return "[REDACTED]";
  // Scan every field value for embedded URLs so a caller cannot bypass URL
  // sanitisation merely by naming the field "detail" instead of "url".
  return redact_inline(redact_url(value));
}

bool StructuredLog::rotate(const std::string &service, Error &error) const {
  const fs::path base = utf8_path(log_path(service));
  std::error_code ec;
  if (options_.backups == 0) {
    fs::remove(base, ec);
    if (ec) {
      error = refuse("logging.rotate", "cannot remove full service log");
      return false;
    }
    return true;
  }
  for (size_t i = options_.backups; i > 0; --i) {
    const fs::path destination = utf8_path(log_path(service) + "." + std::to_string(i));
    fs::remove(destination, ec);
    if (ec) {
      error = refuse("logging.rotate", "cannot remove an old service-log backup");
      return false;
    }
    const fs::path source = i == 1
                                ? base
                                : utf8_path(log_path(service) + "." + std::to_string(i - 1));
    ec.clear();
    if (fs::exists(source, ec)) {
      if (ec) {
        error = refuse("logging.rotate", "cannot inspect a service-log generation");
        return false;
      }
      fs::rename(source, destination, ec);
      if (ec) {
        error = refuse("logging.rotate", "cannot rotate a service-log generation");
        return false;
      }
    } else if (ec) {
      error = refuse("logging.rotate", "cannot inspect a service-log generation");
      return false;
    }
  }
  return true;
}

bool StructuredLog::write(const std::string &service, LogLevel level,
                          const std::string &event, const Fields &fields,
                          Error &error) {
  std::lock_guard<std::mutex> guard(mutex_);
  error = Error{};
  if (directory_.empty()) {
    error = refuse("logging.directory", "no log directory was configured");
    return false;
  }
  if (!valid_service(service)) {
    error = refuse("logging.service", "service name is not a safe file token");
    return false;
  }
  if (std::string(log_level_name(level)) == "unknown") {
    error = refuse("logging.level", "unknown log level");
    return false;
  }
  if (!valid_event(event)) {
    error = refuse("logging.event", "event must be a bounded token");
    return false;
  }
  Json values = Json::make_object();
  for (const auto &[key, value] : fields) {
    if (key.empty() || key.size() > 128) {
      error = refuse("logging.field", "field names must be 1..128 bytes");
      return false;
    }
    values.set(key, Json(redact_value(key, value)));
  }
  Json row = Json::make_object();
  try {
    row.set("timestamp_ms", Json(double(now_ms())));
  } catch (const std::exception &exception) {
    error = refuse("logging.clock", exception.what());
    return false;
  } catch (...) {
    error = refuse("logging.clock", "log clock failed");
    return false;
  }
  row.set("service", Json(service));
  row.set("level", Json(log_level_name(level)));
  row.set("event", Json(event));
  row.set("fields", std::move(values));
  const std::string line = row.dump() + "\n";
  if (line.size() > options_.max_file_bytes) {
    error = refuse("logging.record-too-large", "one record exceeds the file rotation limit");
    return false;
  }
  std::error_code ec;
  fs::create_directories(utf8_path(directory_), ec);
  if (ec) {
    error = refuse("logging.directory", "cannot create the service log directory");
    return false;
  }
  const fs::path path = utf8_path(log_path(service));
  uintmax_t current = 0;
  if (fs::exists(path, ec)) {
    if (ec) {
      error = refuse("logging.read", "cannot inspect the current service log");
      return false;
    }
    current = fs::file_size(path, ec);
    if (ec) {
      error = refuse("logging.read", "cannot read the current service-log size");
      return false;
    }
  } else if (ec) {
    error = refuse("logging.read", "cannot inspect the current service log");
    return false;
  }
  if (current && (current > options_.max_file_bytes ||
                  line.size() > options_.max_file_bytes - current)) {
    if (!rotate(service, error)) return false;
  }
  std::ofstream out(path, std::ios::binary | std::ios::app);
  if (!out) {
    error = refuse("logging.write", "cannot open the service log for append");
    return false;
  }
  out.write(line.data(), std::streamsize(line.size()));
  out.flush();
  if (!out) {
    error = refuse("logging.write", "cannot append the structured log record");
    return false;
  }
  return true;
}

bool StructuredLog::export_support_bundle(const std::string &output_path,
                                          Error &error) const {
  std::lock_guard<std::mutex> guard(mutex_);
  error = Error{};
  if (output_path.empty() || directory_.empty()) {
    error = refuse("logging.bundle-path", "support bundle needs configured paths");
    return false;
  }
  std::vector<ZipEntry> entries;
  size_t total_log_bytes = 0;
  Json files = Json::make_array();
  std::error_code ec;
  const fs::path root = utf8_path(directory_);
  if (fs::exists(root, ec)) {
    if (ec || !fs::is_directory(root, ec) || ec) {
      error = refuse("logging.bundle-directory", "log path is not a readable directory");
      return false;
    }
    for (fs::directory_iterator it(root, ec), end; it != end && !ec; it.increment(ec)) {
      const fs::directory_entry &entry = *it;
      const std::string filename = utf8_string(entry.path().filename());
      if (!service_log_filename(filename)) continue;
      if (entries.size() >= 4096) {
        error = refuse("logging.bundle-count", "support bundle has too many log files");
        return false;
      }
      const fs::file_status status = entry.symlink_status(ec);
      if (ec || !fs::is_regular_file(status)) {
        ec.clear(); // symlinks and special entries are not exported
        continue;
      }
      ZipEntry item;
      item.name = "logs/" + filename;
      std::string why;
      if (!read_binary(entry.path(), item.data, why)) {
        error = refuse("logging.bundle-read", why);
        return false;
      }
      if (item.data.size() > kMaxBundleBytes - total_log_bytes) {
        error = refuse("logging.bundle-size", "service logs exceed the export size limit");
        return false;
      }
      total_log_bytes += item.data.size();
      item.crc = crc32(item.data);
      Json descriptor = Json::make_object();
      descriptor.set("path", Json(item.name));
      descriptor.set("bytes", Json(item.data.size()));
      descriptor.set("crc32", Json(std::to_string(item.crc)));
      files.array.push_back(std::move(descriptor));
      entries.push_back(std::move(item));
    }
    if (ec) {
      error = refuse("logging.bundle-read", "cannot enumerate service logs");
      return false;
    }
  } else if (ec) {
    error = refuse("logging.bundle-directory", "cannot inspect log directory");
    return false;
  }
  Json manifest = Json::make_object();
  manifest.set("schema", Json(1));
  try {
    manifest.set("created_ms", Json(double(now_ms())));
  } catch (...) {
    error = refuse("logging.clock", "log clock failed during bundle export");
    return false;
  }
  manifest.set("files", std::move(files));
  entries.push_back({"manifest.json", manifest.dump(), 0, 0});
  std::string archive, zip_error;
  if (!zip_store(entries, archive, zip_error)) {
    error = refuse("logging.bundle-size", zip_error);
    return false;
  }
  std::string write_error;
  if (!write_file_atomic(output_path, archive, write_error)) {
    error = refuse("logging.bundle-write", write_error);
    return false;
  }
  return true;
}

} // namespace pv::rt
