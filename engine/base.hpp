// PsychoVault original source. No third-party browser engine is used.
#pragma once
#include <algorithm>
#include <array>
#include <cctype>
#include <cmath>
#include <cstdint>
#include <functional>
#include <limits>
#include <map>
#include <memory>
#include <optional>
#include <set>
#include <sstream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <unordered_map>
#include <utility>
#include <variant>
#include <vector>

namespace pv {
using Bytes = std::vector<uint8_t>;
constexpr size_t kMaxDocument = 8u * 1024u * 1024u;
constexpr size_t kMaxResources = 32;
constexpr size_t kMaxResourceBytes = 16u * 1024u * 1024u;
constexpr int kMaxDomNodes = 30000;
constexpr int kMaxDomDepth = 256;
std::string lower(std::string_view value);
std::string trim(std::string_view value);
bool starts(std::string_view value, std::string_view prefix);
bool ends(std::string_view value, std::string_view suffix);
std::vector<std::string> split(std::string_view value, char delimiter);
std::string replace_all(std::string text, std::string_view from, std::string_view to);
std::string utf8_encode(uint32_t cp);
std::vector<uint32_t> utf8_decode(std::string_view text);
std::string html_escape(std::string_view text);
std::string entity_decode(std::string_view text);
std::string percent_encode(std::string_view text, bool query = false);
std::string legacy_decode(const Bytes &bytes);
Bytes random_bytes(size_t count);

struct Url {
  std::string scheme, host, path, query, fragment, port;
  bool valid = false;
  std::string origin() const;
  std::string href(bool include_fragment = true) const;
  std::string authority() const;
  uint16_t effective_port() const;
  bool network() const {
    return scheme == "http" || scheme == "https";
  }
};
std::optional<Url> parse_url(std::string_view input);
std::optional<Url> resolve_url(const Url &base, std::string_view input);
std::optional<Url> navigate_input(std::string_view input, const std::string &search_template);

struct Json {
  enum class Type { Null, Bool, Number, String, Array, Object };
  Type type = Type::Null;
  bool boolean = false;
  double number = 0;
  std::string string;
  std::vector<Json> array;
  std::vector<std::pair<std::string, Json>> object;
  Json() = default;
  static Json make_object();
  static Json make_array();
  Json(bool v) : type(Type::Bool), boolean(v) {}
  Json(double v) : type(Type::Number), number(v) {}
  Json(int v) : type(Type::Number), number(v) {}
  Json(size_t v) : type(Type::Number), number(static_cast<double>(v)) {}
  Json(const char *v) : type(Type::String), string(v) {}
  Json(std::string v) : type(Type::String), string(std::move(v)) {}
  const Json *find(std::string_view key) const;
  Json &at(std::string_view key);
  const Json &at(std::string_view key) const;
  void set(std::string key, Json value);
  std::string str(std::string_view key, std::string fallback = {}) const;
  double num(std::string_view key, double fallback = 0) const;
  bool flag(std::string_view key, bool fallback = false) const;
  std::string dump() const;
  static std::optional<Json> parse(std::string_view text, size_t max_bytes = 8u * 1024u * 1024u);
};

struct Resource {
  std::string url, mime, text;
  Bytes bytes;
  int status = 200;
  bool ok = true;
  std::string error;
};
struct Diagnostic {
  std::string subsystem, message;
};
std::string read_text_file(const std::string &path);
bool write_file_atomic(const std::string &path, const std::string &data, std::string &error);
} // namespace pv
