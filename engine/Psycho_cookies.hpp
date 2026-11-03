// PsychoVault original source. I keep cookies in my own jar so a page gets the
// cookies it set and nothing else.
#pragma once
#include "../engine/Psycho_base.hpp"
#include <map>
#include <mutex>
#include <string>
#include <vector>

namespace pv {

struct StoredCookie {
  std::string name, value, domain, path = "/";
  // Seconds since the epoch. A negative value means this cookie lasts only as
  // long as the browser session.
  double expires = -1;
  bool secure = false, http_only = false;
  // A host-only cookie is only sent back to the exact host that set it.
  bool host_only = true;
};

// I am the cookie jar for one profile. Every method takes the URL the cookie is
// for, because matching is done against the request and never against a guess.
class CookieJar {
public:
  // I read one Set-Cookie header value, or one document.cookie assignment.
  void set_from_header(const Url &url, const std::string &header, double now);
  // I read a document.cookie assignment. It may carry attributes, but it can
  // never mark a cookie HttpOnly.
  void set_from_document(const Url &url, const std::string &pair, double now);
  // I build the Cookie header for a request. An empty result means no header.
  std::string header_for(const Url &url, double now, bool cross_site = false,
                         bool block_third_party = false) const;
  // I build what document.cookie should return, which never includes HttpOnly.
  std::string document_string(const Url &url, double now) const;
  void clear();
  size_t size() const;
  Json to_json() const;
  void from_json(const Json &json);
  std::vector<StoredCookie> cookies() const;

private:
  mutable std::mutex mutex_;
  std::map<std::string, StoredCookie> jar_;
  void store(const Url &url, const std::string &text, double now, bool from_document);
  static std::string key_for(const StoredCookie &cookie);
  static bool domain_matches(const StoredCookie &cookie, const std::string &host);
  static bool path_matches(const StoredCookie &cookie, const std::string &path);
  static bool fresh(const StoredCookie &cookie, double now);
  static double parse_date(const std::string &text);
  static std::string normalize_path(const std::string &path);
};
} // namespace pv
