// PsychoVault original source. See the header for the three rules - this file
// is how I keep them.
#include "Psycho_internal.hpp"
#include "../content/Psycho_content.hpp" // for admit_http_url, my stock web policy
#include <cctype>

namespace pv::rt {

namespace {

Error refuse(const char *code, const std::string &message) {
  Error error;
  error.code = code;
  error.message = message;
  return error;
}

std::string lower(std::string text) {
  for (char &c : text) c = (char)std::tolower((unsigned char)c);
  return text;
}

bool page_char(char c, bool first) {
  const bool alnum = (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9');
  return alnum || (!first && c == '-');
}

} // namespace

InternalRegistry InternalRegistry::stock() {
  InternalRegistry registry;
  auto shell = [&](const char *name, const char *summary) {
    registry.add({name, "shell", summary, false});
  };
  auto diagnostics = [&](const char *name, const char *summary) {
    registry.add({name, "diagnostics", summary, false});
  };
  shell("newtab", "the page every new tab opens into");
  shell("settings", "every category of my settings UI");
  shell("history", "my browsing history manager");
  shell("downloads", "my downloads manager");
  shell("extensions", "my extension manager");
  shell("apps", "the services rail's app view");
  diagnostics("version", "build, version and revision honesty");
  diagnostics("nettest", "S038's network diagnostics page - registered ahead");
  diagnostics("crash", "S039's renderer crash record page - registered ahead");
  return registry;
}

const InternalPage *InternalRegistry::find(const std::string &name) const {
  for (const InternalPage &page : pages_) {
    if (page.name == name) return &page;
  }
  return nullptr;
}

void InternalRegistry::add(InternalPage page) { pages_.push_back(std::move(page)); }

bool InternalRegistry::parse(const std::string &raw, InternalUrl &out, Error &error) const {
  error = Error{};
  out = InternalUrl{};
  std::string url = raw;
  while (!url.empty() && std::isspace((unsigned char)url.front())) url.erase(0, 1);
  while (!url.empty() && std::isspace((unsigned char)url.back())) url.pop_back();
  for (char c : url) {
    if ((unsigned char)c < 0x20 || c == 0x7f) {
      error = refuse("internal.control", "control bytes do not belong in addresses");
      return false;
    }
  }
  const size_t scheme_end = url.find("://");
  if (scheme_end == std::string::npos || scheme_end == 0 ||
      lower(url.substr(0, scheme_end)) != "psychovault") {
    error = refuse("internal.scheme", "this parser only admits psychovault://");
    return false;
  }
  const size_t begin = scheme_end + 3;
  const size_t page_end = url.find_first_of("/?#", begin);
  const std::string page = lower(
      url.substr(begin, page_end == std::string::npos ? std::string::npos
                                                      : page_end - begin));
  if (page.empty()) {
    error = refuse("internal.page-required",
                   "psychovault:// alone is not an address; name a registered page");
    return false;
  }
  for (size_t i = 0; i < page.size(); ++i) {
    if (!page_char(page[i], i == 0)) {
      error = refuse("internal.page-name",
                     "page tokens are [a-z0-9] followed by [a-z0-9-]");
      return false;
    }
  }
  const InternalPage *known = find(page);
  if (!known) {
    error = refuse("internal.unknown-page",
                   "page '" + page + "' is not in my registry; nothing guesses");
    return false;
  }
  // Path: from the first / to ? or #. A lone "/" normalises away; a bare page
  // keeps its query/fragment tail; anything else obeys static-name discipline.
  size_t path_begin = page_end;
  size_t path_end = url.size();
  if (path_begin != std::string::npos && url[path_begin] == '/') {
    const size_t mark = url.find_first_of("?#", path_begin);
    path_end = mark == std::string::npos ? url.size() : mark;
  } else {
    path_begin = path_end = page_end;
  }
  std::string path;
  if (path_begin != std::string::npos && path_begin < path_end) {
    path = url.substr(path_begin, path_end - path_begin);
    if (path == "/") path.clear();
  }
  if (!path.empty()) {
    if (path.find('%') != std::string::npos || path.find('\\') != std::string::npos) {
      error = refuse("internal.escape",
                     "internal resources use plain names, never escapes");
      return false;
    }
    size_t at = 0;
    while (at <= path.size()) {
      const size_t slash = path.find('/', at);
      const std::string segment =
          path.substr(at, slash == std::string::npos ? std::string::npos : slash - at);
      if (segment == "..") {
        error = refuse("internal.traversal", "'..' never reaches outside its page");
        return false;
      }
      if (slash == std::string::npos) break;
      at = slash + 1;
    }
  }
  out.page = page;
  out.path = path;
  out.canon = "psychovault://" + page + path;
  if (path_begin != std::string::npos && path_end < url.size())
    out.canon += url.substr(path_end); // query and fragment ride along untouched
  return true;
}

bool InternalGate::broker_navigate(const InternalUrl &url, Error &error) const {
  error = Error{};
  if (!registry_.find(url.page)) {
    error = refuse("internal.unknown-page",
                   "page '" + url.page + "' is not in my registry");
    return false;
  }
  return true;
}

bool InternalGate::web_fetch(const InternalUrl &url, const Capability &granted,
                             Error &error) const {
  error = Error{};
  // Capability first: a right-less probe must not learn what pages exist.
  if (!granted.allows(Channel::Internal)) {
    error = refuse("internal.rights",
                   "web content holds no internal-scheme capability");
    return false;
  }
  const InternalPage *page = registry_.find(url.page);
  if (!page) {
    error = refuse("internal.unknown-page",
                   "page '" + url.page + "' is not in my registry");
    return false;
  }
  if (!page->web_reachable) {
    error = refuse("internal.embed",
                   "page '" + url.page + "' is not web-reachable; none of mine are");
    return false;
  }
  return true;
}

bool InternalGate::web_navigate(const InternalUrl &, Error &error) const {
  error = Error{};
  error = refuse("internal.web-navigate",
                 "web content never steering the browser into its own shell");
  return false;
}

bool InternalGate::internal_fetch(const std::string &origin_page,
                                  const InternalUrl &target, Error &error) const {
  error = Error{};
  if (!registry_.find(origin_page)) {
    error = refuse("internal.origin-unknown",
                   "origin '" + origin_page + "' is not a registered page");
    return false;
  }
  if (!registry_.find(target.page)) {
    error = refuse("internal.unknown-page",
                   "page '" + target.page + "' is not in my registry");
    return false;
  }
  if (origin_page != target.page) {
    error = refuse("internal.origin-cross",
                   "every internal page is its own origin; " + origin_page +
                       " cannot reach " + target.page);
    return false;
  }
  return true;
}

bool admit_browser_url(const std::string &raw, const InternalRegistry &registry,
                       std::string &canon, Error &error) {
  error = Error{};
  std::string probe = raw;
  while (!probe.empty() && std::isspace((unsigned char)probe.front())) probe.erase(0, 1);
  while (!probe.empty() && std::isspace((unsigned char)probe.back())) probe.pop_back();
  const size_t scheme_end = probe.find("://");
  if (scheme_end != std::string::npos && scheme_end > 0 &&
      lower(probe.substr(0, scheme_end)) == "psychovault") {
    InternalUrl internal;
    if (!registry.parse(raw, internal, error)) return false;
    canon = internal.canon;
    return true;
  }
  return admit_http_url(raw, canon, error);
}

} // namespace pv::rt
