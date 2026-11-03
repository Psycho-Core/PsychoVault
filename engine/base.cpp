#include "base.hpp"
#include <cstdio>
#include <cstdlib>
#include <fstream>
#ifdef _WIN32
#define NOMINMAX
#include <windows.h>
#include <bcrypt.h>
#else
#include <fcntl.h>
#include <unistd.h>
#endif

namespace pv {
Bytes random_bytes(size_t count) {
  if (count > 1024 * 1024)
    throw std::runtime_error("random request limit");
  Bytes out(count);
#ifdef _WIN32
  if (BCryptGenRandom(nullptr, out.data(), ULONG(count), BCRYPT_USE_SYSTEM_PREFERRED_RNG) != 0)
    throw std::runtime_error("OS random source failed");
#else
  int fd = open("/dev/urandom", O_RDONLY);
  if (fd < 0)
    throw std::runtime_error("OS random source failed");
  size_t pos = 0;
  while (pos < count) {
    ssize_t n = read(fd, out.data() + pos, count - pos);
    if (n <= 0) {
      close(fd);
      throw std::runtime_error("OS random source failed");
    }
    pos += size_t(n);
  }
  close(fd);
#endif
  return out;
}

std::string lower(std::string_view value) {
  std::string out(value);
  for (char &c : out)
    if (c >= 'A' && c <= 'Z')
      c += 32;
  return out;
}
std::string trim(std::string_view value) {
  size_t a = 0, b = value.size();
  while (a < b && static_cast<unsigned char>(value[a]) <= 32)
    ++a;
  while (b > a && static_cast<unsigned char>(value[b - 1]) <= 32)
    --b;
  return std::string(value.substr(a, b - a));
}
bool starts(std::string_view v, std::string_view p) {
  return v.substr(0, p.size()) == p;
}
bool ends(std::string_view v, std::string_view p) {
  return v.size() >= p.size() && v.substr(v.size() - p.size()) == p;
}
std::vector<std::string> split(std::string_view v, char d) {
  std::vector<std::string> out;
  size_t pos = 0;
  while (pos <= v.size()) {
    size_t n = v.find(d, pos);
    out.emplace_back(v.substr(pos, n == std::string_view::npos ? n : n - pos));
    if (n == std::string_view::npos)
      break;
    pos = n + 1;
  }
  return out;
}
std::string replace_all(std::string text, std::string_view from, std::string_view to) {
  if (from.empty())
    return text;
  size_t p = 0;
  while ((p = text.find(from, p)) != std::string::npos) {
    text.replace(p, from.size(), to);
    p += to.size();
  }
  return text;
}
std::string utf8_encode(uint32_t c) {
  std::string s;
  if (c > 0x10ffff || (c >= 0xd800 && c <= 0xdfff))
    c = 0xfffd;
  if (c < 0x80)
    s += char(c);
  else if (c < 0x800) {
    s += char(0xc0 | (c >> 6));
    s += char(0x80 | (c & 63));
  } else if (c < 0x10000) {
    s += char(0xe0 | (c >> 12));
    s += char(0x80 | ((c >> 6) & 63));
    s += char(0x80 | (c & 63));
  } else {
    s += char(0xf0 | (c >> 18));
    s += char(0x80 | ((c >> 12) & 63));
    s += char(0x80 | ((c >> 6) & 63));
    s += char(0x80 | (c & 63));
  }
  return s;
}
std::vector<uint32_t> utf8_decode(std::string_view s) {
  std::vector<uint32_t> out;
  out.reserve(s.size());
  for (size_t i = 0; i < s.size();) {
    uint8_t c = uint8_t(s[i++]);
    uint32_t cp;
    int extra;
    if (c < 0x80) {
      cp = c;
      extra = 0;
    } else if (c >= 0xc2 && c <= 0xdf) {
      cp = c & 31;
      extra = 1;
    } else if (c >= 0xe0 && c <= 0xef) {
      cp = c & 15;
      extra = 2;
    } else if (c >= 0xf0 && c <= 0xf4) {
      cp = c & 7;
      extra = 3;
    } else {
      out.push_back(0xfffd);
      continue;
    }
    if (i + extra > s.size()) {
      out.push_back(0xfffd);
      break;
    }
    bool bad = false;
    for (int k = 0; k < extra; ++k) {
      uint8_t n = uint8_t(s[i++]);
      if ((n & 0xc0) != 0x80)
        bad = true;
      cp = (cp << 6) | (n & 63);
    }
    if (bad || (extra == 2 && cp < 0x800) || (extra == 3 && cp < 0x10000) || cp > 0x10ffff ||
        (cp >= 0xd800 && cp <= 0xdfff))
      cp = 0xfffd;
    out.push_back(cp);
  }
  return out;
}
std::string html_escape(std::string_view t) {
  std::string s;
  s.reserve(t.size());
  for (char c : t)
    switch (c) {
    case '&':
      s += "&amp;";
      break;
    case '<':
      s += "&lt;";
      break;
    case '>':
      s += "&gt;";
      break;
    case '"':
      s += "&quot;";
      break;
    case '\'':
      s += "&#39;";
      break;
    default:
      s += c;
    }
  return s;
}
std::string entity_decode(std::string_view t) {
  static const std::map<std::string, std::string> names = {{"amp", "&"},
                                                           {"lt", "<"},
                                                           {"gt", ">"},
                                                           {"quot", "\""},
                                                           {"apos", "'"},
                                                           {"nbsp", "\xc2\xa0"},
                                                           {"copy", "\xc2\xa9"},
                                                           {"reg", "\xc2\xae"},
                                                           {"trade", "\xe2\x84\xa2"},
                                                           {"hellip", "\xe2\x80\xa6"},
                                                           {"mdash", "-"},
                                                           {"ndash", "-"},
                                                           {"lsquo", "\xe2\x80\x98"},
                                                           {"rsquo", "\xe2\x80\x99"},
                                                           {"ldquo", "\xe2\x80\x9c"},
                                                           {"rdquo", "\xe2\x80\x9d"},
                                                           {"bull", "\xe2\x80\xa2"},
                                                           {"middot", "\xc2\xb7"},
                                                           {"laquo", "\xc2\xab"},
                                                           {"raquo", "\xc2\xbb"},
                                                           {"times", "\xc3\x97"},
                                                           {"divide", "\xc3\xb7"},
                                                           {"deg", "\xc2\xb0"},
                                                           {"plusmn", "\xc2\xb1"},
                                                           {"euro", "\xe2\x82\xac"},
                                                           {"pound", "\xc2\xa3"},
                                                           {"yen", "\xc2\xa5"},
                                                           {"cent", "\xc2\xa2"},
                                                           {"sect", "\xc2\xa7"},
                                                           {"para", "\xc2\xb6"},
                                                           {"dagger", "\xe2\x80\xa0"},
                                                           {"larr", "\xe2\x86\x90"},
                                                           {"rarr", "\xe2\x86\x92"},
                                                           {"uarr", "\xe2\x86\x91"},
                                                           {"darr", "\xe2\x86\x93"},
                                                           {"harr", "\xe2\x86\x94"},
                                                           {"check", "\xe2\x9c\x93"},
                                                           {"star", "\xe2\x98\x86"},
                                                           {"hearts", "\xe2\x99\xa5"}};
  std::string out;
  out.reserve(t.size());
  for (size_t i = 0; i < t.size();) {
    if (t[i] != '&') {
      out += t[i++];
      continue;
    }
    size_t e = t.find(';', i + 1);
    if (e == std::string_view::npos || e - i > 34) {
      out += t[i++];
      continue;
    }
    auto name = std::string(t.substr(i + 1, e - i - 1));
    bool consumed = true;
    if (!name.empty() && name[0] == '#') {
      bool hex = name.size() > 1 && (name[1] == 'x' || name[1] == 'X');
      size_t start = hex ? 2 : 1;
      uint32_t cp = 0;
      bool valid = start < name.size();
      for (size_t k = start; k < name.size(); ++k) {
        char c = name[k];
        int digit = c >= '0' && c <= '9' ? c - '0'
                    : hex && lower(name)[k] >= 'a' && lower(name)[k] <= 'f'
                        ? lower(name)[k] - 'a' + 10
                        : -1;
        if (digit < 0 || cp > 0x10ffff) {
          valid = false;
          break;
        }
        cp = cp * (hex ? 16 : 10) + digit;
      }
      if (valid)
        out += utf8_encode(cp);
      else
        consumed = false;
    } else {
      auto it = names.find(name);
      if (it != names.end())
        out += it->second;
      else
        consumed = false;
    }
    if (consumed)
      i = e + 1;
    else
      out += t[i++];
  }
  return out;
}
std::string percent_encode(std::string_view s, bool query) {
  const char *h = "0123456789ABCDEF";
  std::string out;
  for (unsigned char c : s) {
    if ((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') ||
        std::string_view("-._~!$&'()*+,;=:@/?").find(c) != std::string_view::npos ||
        (query && c == ' '))
      out += query && c == ' ' ? '+' : char(c);
    else {
      out += '%';
      out += h[c >> 4];
      out += h[c & 15];
    }
  }
  return out;
}
std::string legacy_decode(const Bytes &bytes) {
  if (bytes.size() >= 3 && bytes[0] == 0xef && bytes[1] == 0xbb && bytes[2] == 0xbf)
    return std::string(bytes.begin() + 3, bytes.end());
  if (bytes.size() >= 2 && bytes[0] == 0xff && bytes[1] == 0xfe) {
    std::string out;
    for (size_t i = 2; i + 1 < bytes.size(); i += 2)
      out += utf8_encode(bytes[i] | (uint32_t(bytes[i + 1]) << 8));
    return out;
  }
  static const uint16_t high[128] = {0x20ac, 0x81,   0x201a, 0x192,  0x201e, 0x2026, 0x2020, 0x2021,
                                     0x2c6,  0x2030, 0x160,  0x2039, 0x152,  0x8d,   0x17d,  0x8f,
                                     0x90,   0x2018, 0x2019, 0x201c, 0x201d, 0x2022, 0x2013, 0x2014,
                                     0x2dc,  0x2122, 0x161,  0x203a, 0x153,  0x9d,   0x17e,  0x178};
  std::string out;
  bool utf = true;
  auto cps =
      utf8_decode(std::string_view(reinterpret_cast<const char *>(bytes.data()), bytes.size()));
  for (auto c : cps)
    if (c == 0xfffd) {
      utf = false;
      break;
    }
  // A replacement character in valid UTF-8 must not cause a second decoding.
  if (utf)
    return std::string(bytes.begin(), bytes.end());
  for (uint8_t c : bytes)
    out += c < 0x80   ? std::string(1, char(c))
           : c < 0xa0 ? utf8_encode(high[c - 0x80])
                      : utf8_encode(c);
  return out;
}
static bool hexval(char c, int &v) {
  if (c >= '0' && c <= '9')
    v = c - '0';
  else if (lower(std::string_view(&c, 1))[0] >= 'a' && lower(std::string_view(&c, 1))[0] <= 'f')
    v = lower(std::string_view(&c, 1))[0] - 'a' + 10;
  else
    return false;
  return true;
}
static std::string normalize_percent(std::string_view s) {
  std::string out;
  for (size_t i = 0; i < s.size();) {
    int a, b;
    if (s[i] == '%' && i + 2 < s.size() && hexval(s[i + 1], a) && hexval(s[i + 2], b)) {
      unsigned char c = static_cast<unsigned char>(a * 16 + b);
      const char *hex = "0123456789ABCDEF";
      out += '%';
      out += hex[c >> 4];
      out += hex[c & 15];
      i += 3;
    } else {
      uint8_t c = uint8_t(s[i++]);
      if (c <= 32 || c >= 127 ||
          std::string_view("<>\"{}|\\^`").find(c) != std::string_view::npos) {
        const char *hex = "0123456789ABCDEF";
        out += '%';
        out += hex[c >> 4];
        out += hex[c & 15];
      } else
        out += char(c);
    }
  }
  return out;
}
static std::string remove_dots(std::string_view path) {
  std::vector<std::string> parts;
  bool trailing = false;
  for (auto &p : split(path, '/')) {
    if (p == "." || p == "%2e") {
      trailing = true;
      continue;
    }
    if (p == ".." || lower(p) == "%2e." || lower(p) == ".%2e" || lower(p) == "%2e%2e") {
      if (!parts.empty())
        parts.pop_back();
      trailing = true;
      continue;
    }
    if (!p.empty())
      parts.push_back(p);
    trailing = false;
  }
  std::string out = "/";
  for (size_t i = 0; i < parts.size(); ++i) {
    if (i)
      out += '/';
    out += parts[i];
  }
  if (trailing && out.back() != '/')
    out += '/';
  return out;
}
static bool valid_host(std::string host) {
  if (host.empty() || host.size() > 253)
    return false;
  if (host.front() == '[')
    return host.back() == ']' && host.find(':') != std::string::npos && host.size() > 2;
  for (unsigned char c : host)
    if (c <= 32 || std::string_view("/?#@\\<>\"'^{}|").find(c) != std::string_view::npos)
      return false;
  for (unsigned char c : host)
    if (c >= 128)
      return false; // IDNA is not implemented; fail rather than silently
                    // resolve another host.
  return true;
}
std::string Url::authority() const {
  return port.empty() ? host : host + ":" + port;
}
std::string Url::origin() const {
  return network()                 ? scheme + "://" + authority()
         : scheme == "psychovault" ? scheme + "://" + host
                                   : "null";
}
uint16_t Url::effective_port() const {
  return port.empty() ? (scheme == "https" ? 443 : 80) : uint16_t(std::stoi(port));
}
std::string Url::href(bool frag) const {
  if (!valid)
    return {};
  std::string out = scheme + ":";
  if (!host.empty() || network())
    out += "//" + authority();
  out += path;
  if (!query.empty())
    out += "?" + query;
  if (frag && !fragment.empty())
    out += "#" + fragment;
  return out;
}
std::optional<Url> parse_url(std::string_view input) {
  std::string s;
  for (char c : input)
    if (c != '\t' && c != '\r' && c != '\n')
      s += c;
  s = trim(s);
  if (s.empty() || s.size() > 32768)
    return std::nullopt;
  size_t colon = s.find(':');
  if (colon == std::string::npos || colon == 0)
    return std::nullopt;
  Url u;
  u.scheme = lower(std::string_view(s).substr(0, colon));
  if (!std::isalpha(static_cast<unsigned char>(u.scheme[0])))
    return std::nullopt;
  for (char c : u.scheme)
    if (!std::isalnum(static_cast<unsigned char>(c)) && c != '+' && c != '-' && c != '.')
      return std::nullopt;
  std::string rest = s.substr(colon + 1);
  size_t hash = rest.find('#');
  if (hash != std::string::npos) {
    u.fragment = normalize_percent(rest.substr(hash + 1));
    rest = rest.substr(0, hash);
  }
  if (starts(rest, "//")) {
    rest = rest.substr(2);
    size_t e = rest.find_first_of("/?");
    std::string authority = rest.substr(0, e);
    rest = e == std::string::npos ? "" : rest.substr(e);
    if (authority.find('@') != std::string::npos)
      return std::nullopt;
    size_t pc = authority.rfind(':');
    if (!authority.empty() && authority[0] == '[') {
      size_t close = authority.find(']');
      if (close == std::string::npos ||
          (close + 1 != authority.size() && authority[close + 1] != ':'))
        return std::nullopt;
      pc = close + 1 < authority.size() ? close + 1 : std::string::npos;
    }
    if (pc != std::string::npos) {
      u.port = authority.substr(pc + 1);
      authority = authority.substr(0, pc);
      if (u.port.empty() || u.port.size() > 5)
        return std::nullopt;
      int port = 0;
      for (char c : u.port) {
        if (!std::isdigit(static_cast<unsigned char>(c)))
          return std::nullopt;
        port = port * 10 + c - '0';
      }
      if (port == 0 || port > 65535)
        return std::nullopt;
      if ((u.scheme == "http" && port == 80) || (u.scheme == "https" && port == 443))
        u.port.clear();
    }
    u.host = lower(authority);
    if (!valid_host(u.host))
      return std::nullopt;
  } else if (u.network())
    return std::nullopt;
  size_t q = rest.find('?');
  if (q != std::string::npos) {
    u.query = normalize_percent(rest.substr(q + 1));
    rest = rest.substr(0, q);
  }
  u.path = u.host.empty() ? rest : remove_dots(normalize_percent(rest.empty() ? "/" : rest));
  if (!u.host.empty() && u.path.empty())
    u.path = "/";
  if (u.scheme == "psychovault" && u.host.empty()) {
    u.host = lower(u.path);
    u.path.clear();
    if (u.host.empty() || u.host.find('/') != std::string::npos)
      return std::nullopt;
  }
  u.valid = true;
  return u;
}
std::optional<Url> resolve_url(const Url &base, std::string_view in) {
  std::string s = trim(in);
  for (char c : std::string("\t\r\n"))
    s = replace_all(s, std::string(1, c), "");
  if (s.empty())
    return base;
  if (s.find(':') != std::string::npos && s.find(':') < s.find('/'))
    return parse_url(s);
  if (!base.valid)
    return parse_url(s);
  Url u = base;
  u.fragment.clear();
  if (starts(s, "#")) {
    u.fragment = normalize_percent(std::string_view(s).substr(1));
    return u;
  }
  if (starts(s, "//"))
    return parse_url(base.scheme + ":" + s);
  if (starts(s, "?")) {
    u.query = normalize_percent(std::string_view(s).substr(1));
    return u;
  }
  if (starts(s, "/")) {
    auto v = parse_url(base.scheme + "://" + base.authority() + s);
    return v;
  }
  std::string dir = base.path.substr(0, base.path.rfind('/') + 1);
  if (dir.empty())
    dir = "/";
  return parse_url(base.scheme + "://" + base.authority() + dir + s);
}
std::optional<Url> navigate_input(std::string_view input, const std::string &search_template) {
  std::string s = trim(input);
  if (s.empty())
    return std::nullopt;
  if (starts(lower(s), "http://") || starts(lower(s), "https://") ||
      starts(lower(s), "psychovault://") || s == "about:blank")
    return parse_url(s);
  bool space = s.find_first_of(" \t\r\n") != std::string::npos;
  if (!space && (s == "localhost" || starts(s, "localhost:") || s.find('.') != std::string::npos ||
                 starts(s, "["))) {
    auto u = parse_url("http://" + s);
    if (u)
      return u;
  }
  if (search_template.find("%s") == std::string::npos)
    return std::nullopt;
  return parse_url(replace_all(search_template, "%s", percent_encode(s, true)));
}

Json Json::make_object() {
  Json j;
  j.type = Type::Object;
  return j;
}
Json Json::make_array() {
  Json j;
  j.type = Type::Array;
  return j;
}
const Json *Json::find(std::string_view key) const {
  if (type != Type::Object)
    return nullptr;
  for (auto &p : object)
    if (p.first == key)
      return &p.second;
  return nullptr;
}
Json &Json::at(std::string_view key) {
  for (auto &p : object)
    if (p.first == key)
      return p.second;
  throw std::runtime_error("missing JSON key");
}
const Json &Json::at(std::string_view key) const {
  auto p = find(key);
  if (!p)
    throw std::runtime_error("missing JSON key");
  return *p;
}
void Json::set(std::string key, Json value) {
  if (type != Type::Object)
    *this = make_object();
  for (auto &p : object)
    if (p.first == key) {
      p.second = std::move(value);
      return;
    }
  object.emplace_back(std::move(key), std::move(value));
}
std::string Json::str(std::string_view key, std::string fallback) const {
  auto p = find(key);
  return p && p->type == Type::String ? p->string : std::move(fallback);
}
double Json::num(std::string_view key, double fallback) const {
  auto p = find(key);
  return p && p->type == Type::Number && std::isfinite(p->number) ? p->number : fallback;
}
bool Json::flag(std::string_view key, bool fallback) const {
  auto p = find(key);
  return p && p->type == Type::Bool ? p->boolean : fallback;
}
static void json_string(std::ostream &o, std::string_view s) {
  o << '"';
  const char *h = "0123456789abcdef";
  for (unsigned char c : s) {
    switch (c) {
    case '"':
      o << "\\\"";
      break;
    case '\\':
      o << "\\\\";
      break;
    case '\n':
      o << "\\n";
      break;
    case '\r':
      o << "\\r";
      break;
    case '\t':
      o << "\\t";
      break;
    default:
      if (c < 32)
        o << "\\u00" << h[c >> 4] << h[c & 15];
      else
        o << char(c);
    }
  }
  o << '"';
}
static void json_dump(std::ostream &o, const Json &j, int depth = 0) {
  if (depth > 96)
    throw std::runtime_error("JSON nesting limit");
  switch (j.type) {
  case Json::Type::Null:
    o << "null";
    break;
  case Json::Type::Bool:
    o << (j.boolean ? "true" : "false");
    break;
  case Json::Type::Number:
    if (!std::isfinite(j.number))
      o << "null";
    else {
      o.precision(17);
      o << j.number;
    }
    break;
  case Json::Type::String:
    json_string(o, j.string);
    break;
  case Json::Type::Array:
    o << '[';
    for (size_t i = 0; i < j.array.size(); ++i) {
      if (i)
        o << ',';
      json_dump(o, j.array[i], depth + 1);
    }
    o << ']';
    break;
  case Json::Type::Object:
    o << '{';
    for (size_t i = 0; i < j.object.size(); ++i) {
      if (i)
        o << ',';
      json_string(o, j.object[i].first);
      o << ':';
      json_dump(o, j.object[i].second, depth + 1);
    }
    o << '}';
    break;
  }
}
std::string Json::dump() const {
  std::ostringstream o;
  json_dump(o, *this);
  return o.str();
}
struct JsonParser {
  std::string_view s;
  size_t p = 0;
  void ws() {
    while (p < s.size() && (s[p] == ' ' || s[p] == '\t' || s[p] == '\n' || s[p] == '\r'))
      ++p;
  }
  bool lit(std::string_view v) {
    if (s.substr(p, v.size()) != v)
      return false;
    p += v.size();
    return true;
  }
  std::string string() {
    if (p >= s.size() || s[p++] != '"')
      throw std::runtime_error("JSON string expected");
    std::string out;
    while (p < s.size()) {
      unsigned char c = s[p++];
      if (c == '"')
        return out;
      if (c < 32)
        throw std::runtime_error("JSON control character");
      if (c != '\\') {
        out += char(c);
        continue;
      }
      if (p >= s.size())
        break;
      c = s[p++];
      switch (c) {
      case '"':
        out += '"';
        break;
      case '\\':
        out += '\\';
        break;
      case '/':
        out += '/';
        break;
      case 'b':
        out += '\b';
        break;
      case 'f':
        out += '\f';
        break;
      case 'n':
        out += '\n';
        break;
      case 'r':
        out += '\r';
        break;
      case 't':
        out += '\t';
        break;
      case 'u': {
        if (p + 4 > s.size())
          throw std::runtime_error("JSON escape");
        uint32_t cp = 0;
        for (int k = 0; k < 4; ++k) {
          int v;
          if (!hexval(s[p++], v))
            throw std::runtime_error("JSON hex");
          cp = cp * 16 + v;
        }
        if (cp >= 0xd800 && cp <= 0xdbff && p + 6 <= s.size() && s[p] == '\\' && s[p + 1] == 'u') {
          size_t save = p;
          p += 2;
          uint32_t low = 0;
          for (int k = 0; k < 4; ++k) {
            int v;
            if (!hexval(s[p++], v))
              throw std::runtime_error("JSON hex");
            low = low * 16 + v;
          }
          if (low >= 0xdc00 && low <= 0xdfff)
            cp = 0x10000 + ((cp - 0xd800) << 10) + low - 0xdc00;
          else
            p = save;
        }
        out += utf8_encode(cp);
        break;
      }
      default:
        throw std::runtime_error("JSON escape unknown");
      }
    }
    throw std::runtime_error("JSON string unterminated");
  }
  Json value(int depth = 0) {
    if (depth > 96 || s.size() > kMaxDocument)
      throw std::runtime_error("JSON limit");
    ws();
    if (p >= s.size())
      throw std::runtime_error("JSON empty");
    if (s[p] == '"')
      return Json(string());
    if (lit("true"))
      return Json(true);
    if (lit("false"))
      return Json(false);
    if (lit("null"))
      return Json();
    if (s[p] == '[') {
      ++p;
      Json j = Json::make_array();
      ws();
      if (p < s.size() && s[p] == ']') {
        ++p;
        return j;
      }
      while (true) {
        j.array.push_back(value(depth + 1));
        if (j.array.size() > 100000)
          throw std::runtime_error("JSON array limit");
        ws();
        if (p < s.size() && s[p++] == ']')
          return j;
        if (p > s.size() || s[p - 1] != ',')
          throw std::runtime_error("JSON comma");
      }
    }
    if (s[p] == '{') {
      ++p;
      Json j = Json::make_object();
      ws();
      if (p < s.size() && s[p] == '}') {
        ++p;
        return j;
      }
      while (true) {
        ws();
        auto key = string();
        ws();
        if (p >= s.size() || s[p++] != ':')
          throw std::runtime_error("JSON colon");
        Json v = value(depth + 1);
        j.set(std::move(key), std::move(v));
        if (j.object.size() > 100000)
          throw std::runtime_error("JSON object limit");
        ws();
        if (p < s.size() && s[p++] == '}')
          return j;
        if (p > s.size() || s[p - 1] != ',')
          throw std::runtime_error("JSON comma");
      }
    }
    size_t start = p;
    if (s[p] == '-')
      ++p;
    bool digits = false;
    while (p < s.size() && std::isdigit(static_cast<unsigned char>(s[p]))) {
      ++p;
      digits = true;
    }
    if (!digits)
      throw std::runtime_error("JSON number");
    if (p < s.size() && s[p] == '.') {
      ++p;
      digits = false;
      while (p < s.size() && std::isdigit(static_cast<unsigned char>(s[p]))) {
        ++p;
        digits = true;
      }
      if (!digits)
        throw std::runtime_error("JSON fraction");
    }
    if (p < s.size() && (s[p] == 'e' || s[p] == 'E')) {
      ++p;
      if (p < s.size() && (s[p] == '+' || s[p] == '-'))
        ++p;
      digits = false;
      while (p < s.size() && std::isdigit(static_cast<unsigned char>(s[p]))) {
        ++p;
        digits = true;
      }
      if (!digits)
        throw std::runtime_error("JSON exponent");
    }
    auto token = s.substr(start, p - start);
    char *end = nullptr;
    std::string number(token);
    double n = std::strtod(number.c_str(), &end);
    if (!end || *end || !std::isfinite(n))
      throw std::runtime_error("JSON number range");
    return Json(n);
  }
};
std::optional<Json> Json::parse(std::string_view s, size_t max) {
  if (s.size() > max)
    return std::nullopt;
  try {
    JsonParser p{s};
    Json j = p.value();
    p.ws();
    if (p.p != s.size())
      return std::nullopt;
    return j;
  } catch (...) {
    return std::nullopt;
  }
}

#ifdef _WIN32
static std::wstring wide_path(const std::string &s) {
  int n = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, s.c_str(), int(s.size()), nullptr, 0);
  if (!n)
    throw std::runtime_error("invalid UTF-8 path");
  std::wstring out(n, 0);
  MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, s.c_str(), int(s.size()), out.data(), n);
  return out;
}
#endif
std::string read_text_file(const std::string &path) {
#ifdef _WIN32
  HANDLE h = CreateFileW(wide_path(path).c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr,
                         OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
  if (h == INVALID_HANDLE_VALUE)
    throw std::runtime_error("cannot open input");
#else
  int h = open(path.c_str(), O_RDONLY);
  if (h < 0)
    throw std::runtime_error("cannot open input");
#endif
  std::string out;
  char buf[16384];
  while (true) {
#ifdef _WIN32
    DWORD n = 0;
    BOOL ok = ReadFile(h, buf, sizeof(buf), &n, nullptr);
    if (!ok) {
      CloseHandle(h);
      throw std::runtime_error("cannot read input");
    }
#else
    ssize_t n = read(h, buf, sizeof(buf));
    if (n < 0) {
      close(h);
      throw std::runtime_error("cannot read input");
    }
#endif
    if (!n)
      break;
    out.append(buf, size_t(n));
    if (out.size() > kMaxDocument) {
#ifdef _WIN32
      CloseHandle(h);
#else
      close(h);
#endif
      throw std::runtime_error("input size limit");
    }
  }
#ifdef _WIN32
  CloseHandle(h);
#else
  close(h);
#endif
  return out;
}
bool write_file_atomic(const std::string &path, const std::string &data, std::string &error) {
  if (data.size() > kMaxDocument) {
    error = "profile record exceeds size limit";
    return false;
  }
  std::string tmp = path + ".tmp";
#ifdef _WIN32
  HANDLE h = CreateFileW(wide_path(tmp).c_str(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS,
                         FILE_ATTRIBUTE_NORMAL, nullptr);
  if (h == INVALID_HANDLE_VALUE) {
    error = "cannot create profile temporary file";
    return false;
  }
  DWORD written = 0;
  BOOL ok =
      WriteFile(h, data.data(), DWORD(data.size()), &written, nullptr) && written == data.size();
  ok = FlushFileBuffers(h) && ok;
  CloseHandle(h);
  if (!ok || !MoveFileExW(wide_path(tmp).c_str(), wide_path(path).c_str(),
                          MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH)) {
    DeleteFileW(wide_path(tmp).c_str());
    error = "cannot commit profile file";
    return false;
  }
#else
  int h = open(tmp.c_str(), O_WRONLY | O_CREAT | O_TRUNC, 0600);
  if (h < 0) {
    error = "cannot create profile temporary file";
    return false;
  }
  size_t pos = 0;
  while (pos < data.size()) {
    auto n = write(h, data.data() + pos, data.size() - pos);
    if (n <= 0) {
      close(h);
      ::unlink(tmp.c_str());
      error = "cannot write profile";
      return false;
    }
    pos += size_t(n);
  }
  bool ok = fsync(h) == 0;
  close(h);
  if (!ok || rename(tmp.c_str(), path.c_str()) != 0) {
    ::unlink(tmp.c_str());
    error = "cannot commit profile file";
    return false;
  }
#endif
  return true;
}
} // namespace pv
