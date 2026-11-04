// PsychoVault original source. My HTTPS policy and my filter list engine. Both are
// plain code over URLs, so both are checked by my test suite without a network.
#include "Psycho_security.hpp"

namespace pv {
namespace {
// The registrable-ish parent of a host, which is what a subdomain match compares.
// I do not ship the public suffix list yet, so this is deliberately conservative:
// it walks one label at a time and stops at two labels or at a known two-part
// suffix such as co.uk.
bool ends_with_label(const std::string &host, const std::string &suffix) {
  if (host.size() < suffix.size())
    return false;
  if (host.compare(host.size() - suffix.size(), suffix.size(), suffix) != 0)
    return false;
  if (host.size() == suffix.size())
    return true;
  return host[host.size() - suffix.size() - 1] == '.';
}
bool is_two_part_suffix(const std::string &tail) {
  static const char *const kSuffixes[] = {"co.uk", "com.au", "co.jp", "co.nz", "com.br",
                                          "co.za", "com.cn", "com.mx", "co.in", "org.uk"};
  for (const char *suffix : kSuffixes)
    if (tail == suffix)
      return true;
  return false;
}
// A host and the labels a subdomain rule may walk up to.
std::vector<std::string> host_chain(const std::string &host) {
  std::vector<std::string> chain;
  std::string rest = lower(host);
  while (!rest.empty()) {
    chain.push_back(rest);
    auto dot = rest.find('.');
    if (dot == std::string::npos)
      break;
    std::string tail = rest.substr(dot + 1);
    if (tail.find('.') == std::string::npos)
      break;
    // Stop before a two-part suffix, so `example.co.uk` is not compared with `uk`.
    if (tail.find('.') != std::string::npos && is_two_part_suffix(tail.substr(tail.find('.') + 1)))
      break;
    rest = tail;
  }
  return chain;
}
bool is_third_party(const std::string &page_host, const std::string &host) {
  if (page_host.empty() || host.empty())
    return false;
  auto page = lower(page_host), target = lower(host);
  if (page == target)
    return false;
  // Same registrable name is first party, which is why the chain stops at the
  // suffix rather than at the last dot.
  for (const auto &candidate : host_chain(page))
    if (ends_with_label(target, candidate))
      return false;
  return true;
}

// `^` in this syntax means "a separator or the end", never a literal caret.
bool is_separator(char c) {
  return !(std::isalnum(static_cast<unsigned char>(c)) || c == '_' || c == '-' || c == '.' ||
           c == '%');
}

// A pattern with `*` and `^` matches from `at`. `require_end` is what makes a
// trailing `|` mean anything: without it, a pattern finishes when the pattern is
// exhausted, which is how every unanchored rule behaves.
bool match_pattern(const std::string &pattern, size_t p, const std::string &subject, size_t s,
                   bool require_end) {
  while (p < pattern.size()) {
    char token = pattern[p];
    if (token == '*') {
      while (p < pattern.size() && pattern[p] == '*')
        ++p;
      if (p == pattern.size())
        return !require_end || s == subject.size();
      for (size_t take = s; take <= subject.size(); ++take)
        if (match_pattern(pattern, p, subject, take, require_end))
          return true;
      return false;
    }
    if (token == '^') {
      // A separator, or the end of the subject. The separator is consumed when it
      // is there, so `||example.com^` covers both `example.com` and
      // `example.com/path`.
      if (s < subject.size()) {
        if (!is_separator(subject[s]))
          return false;
        ++s;
      }
      ++p;
      continue;
    }
    if (s >= subject.size() || subject[s] != token)
      return false;
    ++p;
    ++s;
  }
  return !require_end || s == subject.size();
}
// A wildcarded host pattern such as `ads.*` has to cover the whole host from a label
// boundary, so `ads.*` covers `ads.example.com` and not `myads.example.com`.
bool end_matches(const std::string &pattern, const std::string &host) {
  for (size_t at = 0; at < host.size(); ++at) {
    if (at > 0 && host[at - 1] != '.')
      continue;
    if (match_pattern(pattern, 0, host, at, false))
      return true;
  }
  return false;
}
} // namespace

// ---- HSTS ----------------------------------------------------------------
bool HttpsPolicy::record_header(const std::string &host, bool over_https,
                                const std::string &value, int64_t now_ms) {
  if (!over_https || host.empty())
    return false;
  auto parameters = lower(value);
  auto at = parameters.find("max-age");
  if (at == std::string::npos)
    return false;
  auto equals = parameters.find('=', at);
  if (equals == std::string::npos)
    return false;
  auto start = equals + 1;
  while (start < parameters.size() && std::isspace(static_cast<unsigned char>(parameters[start])))
    ++start;
  int64_t seconds = 0;
  size_t digits = 0;
  while (start + digits < parameters.size() &&
         std::isdigit(static_cast<unsigned char>(parameters[start + digits])) && digits < 12) {
    seconds = seconds * 10 + (parameters[start + digits] - '0');
    ++digits;
  }
  if (digits == 0) {
    // A header I cannot read is not permission to pin anything.
    return false;
  }
  // The value is capped before it is multiplied, so an absurd max-age cannot wrap.
  int64_t millis = std::min(seconds, kMaxHstsMillis / 1000) * 1000;
  bool subdomains = parameters.find("includesubdomains") != std::string::npos;
  auto target = lower(host);
  auto existing = std::find_if(entries.begin(), entries.end(),
                               [&](const HstsEntry &entry) { return entry.host == target; });
  if (millis <= 0) {
    // max-age=0 is how a site turns this off, and I honour it.
    if (existing != entries.end())
      entries.erase(existing);
    return true;
  }
  HstsEntry entry{target, now_ms + millis, subdomains};
  if (existing != entries.end())
    *existing = entry;
  else
    entries.push_back(entry);
  return true;
}

const HstsEntry *HttpsPolicy::find(const std::string &host) const {
  auto target = lower(host);
  for (const auto &candidate : host_chain(target))
    for (const auto &entry : entries) {
      bool matches = entry.host == candidate;
      if (!matches && entry.include_subdomains)
        matches = ends_with_label(target, entry.host);
      if (matches)
        return &entry;
    }
  return nullptr;
}

bool HttpsPolicy::requires_https(const std::string &host) const {
  return find(host) != nullptr;
}

bool HttpsPolicy::should_upgrade(const Url &url) const {
  if (url.scheme != "http" || url.host.empty())
    return false;
  return requires_https(url.host);
}

bool HttpsPolicy::blocks_mixed_content(const Url &page, const Url &resource) const {
  if (page.scheme != "https" || resource.scheme != "http")
    return false;
  // A loopback address is not on the network in any meaningful sense, and browsers
  // allow it, so I do too.
  const std::string &host = resource.host;
  if (host == "127.0.0.1" || host == "localhost" || host == "[::1]")
    return false;
  return true;
}

bool HttpsPolicy::blocks_insecure_submit(const Url &page, const Url &target) const {
  return page.scheme == "https" && target.scheme == "http";
}

Json HttpsPolicy::to_json() const {
  Json out = Json::make_object();
  Json list = Json::make_array();
  for (const auto &entry : entries) {
    Json j = Json::make_object();
    j.set("host", entry.host);
    j.set("expires", double(entry.expires_at));
    j.set("subdomains", entry.include_subdomains);
    list.array.push_back(std::move(j));
  }
  out.set("entries", std::move(list));
  return out;
}

void HttpsPolicy::from_json(const Json &json) {
  entries.clear();
  if (const Json *list = json.find("entries"))
    for (const auto &item : list->array) {
      HstsEntry entry;
      entry.host = lower(item.str("host"));
      entry.expires_at = int64_t(item.num("expires"));
      entry.include_subdomains = item.flag("subdomains");
      if (!entry.host.empty() && entry.expires_at > 0)
        entries.push_back(std::move(entry));
    }
}

void HttpsPolicy::prune(int64_t now_ms) {
  entries.erase(std::remove_if(entries.begin(), entries.end(),
                               [&](const HstsEntry &entry) { return entry.expires_at <= now_ms; }),
                entries.end());
}

void HttpsPolicy::clear() {
  entries.clear();
  upgraded = blocked_mixed = blocked_submit = 0;
}

// ---- filter lists --------------------------------------------------------
std::string resource_type_name(ResourceType type) {
  switch (type) {
  case ResourceType::Document:
    return "document";
  case ResourceType::Subdocument:
    return "subdocument";
  case ResourceType::Script:
    return "script";
  case ResourceType::Image:
    return "image";
  case ResourceType::Stylesheet:
    return "stylesheet";
  case ResourceType::Font:
    return "font";
  case ResourceType::Media:
    return "media";
  case ResourceType::XmlHttpRequest:
    return "xmlhttprequest";
  default:
    return "other";
  }
}

namespace {
// `script`, `image`, `third-party`, `domain=a.com|~b.com` and the rest of the
// options I actually enforce. An option I do not implement makes the whole rule
// unusable rather than silently ignored, because a rule that quietly does less than
// it says is how a blocker washes a site out.
bool apply_options(FilterRule &rule, const std::string &options) {
  std::stringstream stream(options);
  std::string option;
  while (std::getline(stream, option, ',')) {
    option = lower(trim(option));
    if (option.empty())
      continue;
    bool excluded = option.front() == '~';
    std::string name = excluded ? option.substr(1) : option;
    auto equals = name.find('=');
    if (equals != std::string::npos) {
      std::string key = name.substr(0, equals);
      std::string value = name.substr(equals + 1);
      if (key != "domain" && key != "sitekey" && key != "redirect")
        return false;
      if (key != "domain")
        return false; // I do not implement those, so I refuse the rule
      std::stringstream domains(value);
      std::string domain;
      while (std::getline(domains, domain, '|')) {
        domain = lower(trim(domain));
        if (domain.empty())
          continue;
        if (domain.front() == '~')
          rule.excluded_domains.push_back(domain.substr(1));
        else
          rule.domains.push_back(domain);
      }
      continue;
    }
    if (name == "third-party") {
      rule.third_party_only = !excluded;
      rule.first_party_only = excluded;
      continue;
    }
    if (name == "first-party")
      return false; // not a real option, so the line is broken
    struct TypeName {
      const char *name;
      ResourceType type;
    };
    static const TypeName kTypes[] = {
        {"script", ResourceType::Script},
        {"image", ResourceType::Image},
        {"stylesheet", ResourceType::Stylesheet},
        {"css", ResourceType::Stylesheet},
        {"object", ResourceType::Other},
        {"xmlhttprequest", ResourceType::XmlHttpRequest},
        {"xhr", ResourceType::XmlHttpRequest},
        {"subdocument", ResourceType::Subdocument},
        {"document", ResourceType::Document},
        {"media", ResourceType::Media},
        {"font", ResourceType::Font},
        {"other", ResourceType::Other},
        {"websocket", ResourceType::Other},
    };
    bool known = false;
    for (const auto &entry : kTypes)
      if (name == entry.name) {
        known = true;
        if (!excluded) {
          rule.types.insert(entry.type);
          rule.any_type = false;
        }
      }
    if (!known)
      return false;
  }
  return true;
}
// Does this rule apply to the page that asked? A rule with no domain option
// applies everywhere, and an excluded domain always wins.
bool page_allows(const FilterRule &rule, const std::string &page_host) {
  if (page_host.empty())
    return rule.excluded_domains.empty();
  for (const auto &excluded : rule.excluded_domains)
    if (ends_with_label(page_host, excluded))
      return false;
  if (rule.domains.empty())
    return true;
  for (const auto &allowed : rule.domains)
    if (ends_with_label(page_host, allowed))
      return true;
  return false;
}
} // namespace

bool FilterList::parse(const std::string &text, std::string &error) {
  std::stringstream stream(text);
  std::string line;
  int number = 0;
  while (std::getline(stream, line)) {
    ++number;
    if (!line.empty() && line.back() == '\r')
      line.pop_back();
    auto trimmed = trim(line);
    if (trimmed.empty() || trimmed[0] == '!' || trimmed[0] == '[')
      continue;
    // Cosmetic rules are not mine to run: I do not rewrite page elements.
    if (trimmed.find("##") != std::string::npos || trimmed.find("#@#") != std::string::npos ||
        trimmed.rfind("#?#", 0) == 0 || trimmed.rfind("#$#", 0) == 0) {
      skipped.push_back(std::to_string(number) + ": cosmetic rule, not a request rule");
      continue;
    }
    FilterRule rule;
    rule.source = trimmed;
    std::string body = trimmed;
    if (body.rfind("@@", 0) == 0) {
      rule.exception = true;
      body = body.substr(2);
    }
    auto options_at = body.find('$');
    std::string options;
    if (options_at != std::string::npos) {
      options = body.substr(options_at + 1);
      body = body.substr(0, options_at);
    }
    if (body.empty()) {
      skipped.push_back(std::to_string(number) + ": a rule with no pattern");
      continue;
    }
    if (!options.empty() && !apply_options(rule, options)) {
      skipped.push_back(std::to_string(number) + ": an option I do not implement");
      continue;
    }
    if (body.rfind("||", 0) == 0) {
      rule.host_anchor = true;
      body = body.substr(2);
    } else if (body.rfind("|", 0) == 0) {
      rule.start_anchor = true;
      body = body.substr(1);
    }
    if (!body.empty() && body.back() == '|') {
      rule.end_anchor = true;
      body.pop_back();
    }
    if (body.empty()) {
      skipped.push_back(std::to_string(number) + ": a rule with only anchors");
      continue;
    }
    rule.pattern = lower(body);
    rules.push_back(std::move(rule));
  }
  if (rules.empty() && skipped.empty()) {
    error = "that filter list had nothing in it I could use";
    return false;
  }
  // Exceptions first, so a match reads in the order the deciding rule deserves.
  std::stable_sort(rules.begin(), rules.end(),
                   [](const FilterRule &a, const FilterRule &b) { return a.exception && !b.exception; });
  return true;
}

size_t FilterList::load_file(const std::string &path, std::string &error) {
  std::string text = read_text_file(path);
  if (text.empty()) {
    error = "I could not read that filter list";
    return 0;
  }
  size_t before = rules.size();
  if (!parse(text, error))
    return rules.size() - before;
  return rules.size() - before;
}

size_t FilterList::exceptions() const {
  return size_t(std::count_if(rules.begin(), rules.end(), [](const FilterRule &r) { return r.exception; }));
}

const FilterRule *FilterList::match(const Url &url, ResourceType type,
                                    const std::string &page_host) const {
  if (rules.empty())
    return nullptr;
  const std::string host = lower(url.host);
  const std::string subject = host + url.path + (url.query.empty() ? "" : "?" + url.query);
  const bool third_party = is_third_party(page_host, host);
  for (const auto &rule : rules) {
    if (!rule.any_type && rule.types.count(type) == 0)
      continue;
    if (rule.third_party_only && !third_party)
      continue;
    if (rule.first_party_only && third_party)
      continue;
    if (!page_allows(rule, lower(page_host)))
      continue;
    bool hit = false;
    if (rule.host_anchor) {
      // `||name` means that name as a host, or any subdomain of it, and whatever
      // follows in the pattern continues from the end of the host. Bare `||name`
      // matches the host alone, so `||ads.example` does not match
      // `ads.example.evil.test`.
      auto cut = rule.pattern.find_first_of("/^");
      const std::string host_pattern = rule.pattern.substr(0, cut);
      const std::string rest = cut == std::string::npos ? std::string()
                                                        : rule.pattern.substr(cut);
      bool host_ok = host_pattern.find('*') != std::string::npos
                         ? end_matches(host_pattern, host)
                         : ends_with_label(host, host_pattern);
      if (host_ok && rest.empty())
        hit = host == host_pattern || host_pattern.find('*') != std::string::npos;
      else if (host_ok)
        hit = match_pattern(rest, 0, subject, host.size(), rule.end_anchor);
    } else if (rule.start_anchor) {
      hit = match_pattern(rule.pattern, 0, subject, 0, rule.end_anchor);
    } else {
      // An unanchored pattern may begin anywhere in the host and path.
      for (size_t at = 0; at <= subject.size() && !hit; ++at)
        hit = match_pattern(rule.pattern, 0, subject, at, rule.end_anchor);
    }
    if (hit)
      return &rule;
  }
  return nullptr;
}

} // namespace pv
