// PsychoVault original source. My cookie jar follows the matching rules I rely
// on: host and domain matching, path prefixes, expiry and HttpOnly.
#include "Psycho_cookies.hpp"
#include <algorithm>
#include <cctype>

namespace pv {
namespace {
std::string lower(std::string text) {
  std::transform(text.begin(), text.end(), text.begin(),
                 [](unsigned char c) { return char(std::tolower(c)); });
  return text;
}
std::string trim(const std::string &text) {
  size_t begin = 0, end = text.size();
  while (begin < end && std::isspace(static_cast<unsigned char>(text[begin])))
    ++begin;
  while (end > begin && std::isspace(static_cast<unsigned char>(text[end - 1])))
    --end;
  return text.substr(begin, end - begin);
}
std::vector<std::string> split(const std::string &text, char separator) {
  std::vector<std::string> out;
  std::string current;
  for (char c : text) {
    if (c == separator) {
      out.push_back(current);
      current.clear();
    } else
      current += c;
  }
  out.push_back(current);
  return out;
}
int month_from(const std::string &name) {
  static const char *months[] = {"jan", "feb", "mar", "apr", "may", "jun",
                                 "jul", "aug", "sep", "oct", "nov", "dec"};
  auto low = lower(name.substr(0, 3));
  for (int i = 0; i < 12; ++i)
    if (low == months[i])
      return i;
  return -1;
}
// I turn a civil date into seconds since the epoch so expiry needs no library.
double days_from_civil(int year, int month, int day) {
  year -= month <= 2;
  int era = (year >= 0 ? year : year - 399) / 400;
  unsigned yoe = unsigned(year - era * 400);
  unsigned doy = unsigned((153 * (month + (month > 2 ? -3 : 9)) + 2) / 5 + day - 1);
  unsigned doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;
  return double(era * 146097 + int(doe) - 719468) * 86400.0;
}
} // namespace

std::string CookieJar::normalize_path(const std::string &path) {
  if (path.empty())
    return "/";
  auto slash = path.find('?');
  std::string clean = slash == std::string::npos ? path : path.substr(0, slash);
  if (clean.empty() || clean[0] != '/')
    clean = "/" + clean;
  // A default path is everything up to the last slash.
  auto last = clean.rfind('/');
  if (last == std::string::npos || last == 0)
    return "/";
  return clean.substr(0, last);
}

double CookieJar::parse_date(const std::string &text) {
  // I read the common cookie date forms: "Wed, 21 Oct 2015 07:28:00 GMT" and
  // "21-Oct-2015 07:28:00".
  std::string cleaned;
  for (char c : text) {
    if (c == ',' || c == '-')
      cleaned += ' ';
    else
      cleaned += c;
  }
  auto parts = split(cleaned, ' ');
  std::vector<std::string> words;
  for (auto &part : parts) {
    auto value = trim(part);
    if (!value.empty())
      words.push_back(value);
  }
  int day = -1, month = -1, year = -1, hour = 0, minute = 0, second = 0;
  for (auto &word : words) {
    if (month < 0 && month_from(word) >= 0 && word.size() >= 3) {
      month = month_from(word);
      continue;
    }
    if (word.find(':') != std::string::npos) {
      auto clock = split(word, ':');
      if (clock.size() == 3) {
        hour = std::atoi(clock[0].c_str());
        minute = std::atoi(clock[1].c_str());
        second = std::atoi(clock[2].c_str());
      }
      continue;
    }
    if (word.size() == 4 && std::all_of(word.begin(), word.end(), [](unsigned char c) {
          return std::isdigit(c) != 0;
        })) {
      year = std::atoi(word.c_str());
      continue;
    }
    if (day < 0 && !word.empty() &&
        std::all_of(word.begin(), word.end(),
                    [](unsigned char c) { return std::isdigit(c) != 0; }))
      day = std::atoi(word.c_str());
  }
  if (day < 1 || day > 31 || month < 0 || year < 1601)
    return -1;
  return days_from_civil(year, month + 1, day) + hour * 3600 + minute * 60 + second;
}

std::string CookieJar::key_for(const StoredCookie &cookie) {
  return lower(cookie.domain) + "|" + cookie.path + "|" + cookie.name;
}

bool CookieJar::domain_matches(const StoredCookie &cookie, const std::string &host) {
  auto target = lower(host), domain = lower(cookie.domain);
  if (cookie.host_only)
    return target == domain;
  if (target == domain)
    return true;
  // A domain cookie matches a subdomain, and never a bare suffix that is not a
  // label boundary.
  if (target.size() <= domain.size() + 1)
    return false;
  return target.compare(target.size() - domain.size(), domain.size(), domain) == 0 &&
         target[target.size() - domain.size() - 1] == '.';
}

bool CookieJar::path_matches(const StoredCookie &cookie, const std::string &path) {
  if (cookie.path == "/" || cookie.path.empty())
    return true;
  if (path.compare(0, cookie.path.size(), cookie.path) != 0)
    return false;
  return path.size() == cookie.path.size() || cookie.path.back() == '/' ||
         path[cookie.path.size()] == '/';
}

bool CookieJar::fresh(const StoredCookie &cookie, double now) {
  return cookie.expires < 0 || cookie.expires > now;
}

void CookieJar::set_from_header(const Url &url, const std::string &header, double now) {
  store(url, header, now, false);
}

void CookieJar::set_from_document(const Url &url, const std::string &pair, double now) {
  store(url, pair, now, true);
}

void CookieJar::store(const Url &url, const std::string &text, double now, bool from_document) {
  auto parts = split(text, ';');
  if (parts.empty())
    return;
  auto pair = parts[0];
  auto equals = pair.find('=');
  if (equals == std::string::npos)
    return;
  StoredCookie cookie;
  cookie.name = trim(pair.substr(0, equals));
  cookie.value = trim(pair.substr(equals + 1));
  if (cookie.name.empty())
    return;
  cookie.domain = lower(url.host);
  cookie.host_only = true;
  bool default_path = true;
  bool expired = false;
  bool has_max_age = false, has_expires = false;
  double expires_date = -1;
  for (size_t i = 1; i < parts.size(); ++i) {
    auto attribute = trim(parts[i]);
    auto split_at = attribute.find('=');
    auto name = lower(trim(attribute.substr(0, split_at)));
    auto value = split_at == std::string::npos ? std::string() : trim(attribute.substr(split_at + 1));
    if (name == "domain" && !value.empty()) {
      auto domain = lower(value);
      if (!domain.empty() && domain[0] == '.')
        domain = domain.substr(1);
      // I only accept a domain the request host actually belongs to.
      if (domain == lower(url.host) ||
          (lower(url.host).size() > domain.size() &&
           lower(url.host).compare(lower(url.host).size() - domain.size(), domain.size(), domain) ==
               0 &&
           lower(url.host)[lower(url.host).size() - domain.size() - 1] == '.')) {
        cookie.domain = domain;
        cookie.host_only = false;
      } else {
        return; // a cookie for somebody else's domain is refused outright
      }
    } else if (name == "path" && !value.empty()) {
      cookie.path = value[0] == '/' ? value : "/";
      default_path = false;
    } else if (name == "expires") {
      // Max-Age wins over Expires whatever order the two appear in, so I keep
      // the date aside until I know no Max-Age was given.
      double when = parse_date(value);
      if (when > 0)
        expires_date = when;
      has_expires = when > 0;
    } else if (name == "max-age" && !has_max_age) {
      has_max_age = true;
      int seconds = std::atoi(value.c_str());
      if (seconds <= 0) {
        expired = true;
        cookie.expires = 0;
      } else
        cookie.expires = now + seconds;
    } else if (name == "secure") {
      cookie.secure = true;
    } else if (name == "httponly") {
      // A script cannot create an HttpOnly cookie, whatever it writes.
      if (!from_document)
        cookie.http_only = true;
    }
  }
  if (default_path)
    cookie.path = normalize_path(url.path);
  if (!has_max_age && has_expires) {
    cookie.expires = expires_date;
    if (expires_date <= now)
      expired = true;
  }
  std::lock_guard<std::mutex> lock(mutex_);
  auto key = key_for(cookie);
  if (expired) {
    jar_.erase(key);
    return;
  }
  jar_[key] = cookie;
}


std::string CookieJar::header_for(const Url &url, double now, bool cross_site,
                                  bool block_third_party) const {
  if (cross_site && block_third_party)
    return {};
  std::string header;
  std::lock_guard<std::mutex> lock(mutex_);
  for (const auto &entry : jar_) {
    const auto &cookie = entry.second;
    if (!fresh(cookie, now) || !domain_matches(cookie, url.host) ||
        !path_matches(cookie, url.path.empty() ? "/" : url.path))
      continue;
    if (cookie.secure && url.scheme != "https")
      continue;
    if (!header.empty())
      header += "; ";
    header += cookie.name + "=" + cookie.value;
  }
  return header;
}

std::string CookieJar::document_string(const Url &url, double now) const {
  std::string header;
  std::lock_guard<std::mutex> lock(mutex_);
  for (const auto &entry : jar_) {
    const auto &cookie = entry.second;
    if (cookie.http_only || !fresh(cookie, now) || !domain_matches(cookie, url.host) ||
        !path_matches(cookie, url.path.empty() ? "/" : url.path))
      continue;
    if (cookie.secure && url.scheme != "https")
      continue;
    if (!header.empty())
      header += "; ";
    header += cookie.name + "=" + cookie.value;
  }
  return header;
}

void CookieJar::clear() {
  std::lock_guard<std::mutex> lock(mutex_);
  jar_.clear();
}

size_t CookieJar::size() const {
  std::lock_guard<std::mutex> lock(mutex_);
  return jar_.size();
}

std::vector<StoredCookie> CookieJar::cookies() const {
  std::lock_guard<std::mutex> lock(mutex_);
  std::vector<StoredCookie> out;
  for (const auto &entry : jar_)
    out.push_back(entry.second);
  return out;
}

Json CookieJar::to_json() const {
  Json list = Json::make_array();
  std::lock_guard<std::mutex> lock(mutex_);
  for (const auto &entry : jar_) {
    const auto &cookie = entry.second;
    Json item = Json::make_object();
    item.set("name", cookie.name);
    item.set("value", cookie.value);
    item.set("domain", cookie.domain);
    item.set("path", cookie.path);
    item.set("expires", cookie.expires);
    item.set("secure", cookie.secure);
    item.set("httpOnly", cookie.http_only);
    item.set("hostOnly", cookie.host_only);
    list.array.push_back(std::move(item));
  }
  return list;
}

void CookieJar::from_json(const Json &json) {
  if (json.type != Json::Type::Array)
    return;
  std::lock_guard<std::mutex> lock(mutex_);
  jar_.clear();
  for (const auto &item : json.array) {
    StoredCookie cookie;
    cookie.name = item.str("name");
    cookie.value = item.str("value");
    cookie.domain = item.str("domain");
    cookie.path = item.str("path", "/");
    cookie.expires = item.num("expires", -1);
    cookie.secure = item.flag("secure");
    cookie.http_only = item.flag("httpOnly");
    cookie.host_only = item.flag("hostOnly", true);
    if (cookie.name.empty() || cookie.domain.empty())
      continue;
    jar_[key_for(cookie)] = cookie;
  }
}
} // namespace pv
