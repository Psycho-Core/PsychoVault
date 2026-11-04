// PsychoVault original source. My certificate and transport policy: what a page is
// allowed to load, what gets upgraded to HTTPS before a socket opens, and what I
// refuse outright. The certificate check itself belongs to the transport, because
// only the transport can see the chain. This file is the policy around it.
#pragma once
#include "Psycho_base.hpp"

namespace pv {

// One host that told me to keep using HTTPS, and until when. I store the expiry as
// a wall clock millisecond count, capped, so a hostile header cannot pin a host for
// a million years.
struct HstsEntry {
  std::string host;
  int64_t expires_at = 0;
  bool include_subdomains = false;
};

// The longest HSTS lifetime I will honour is one year. Chrome caps at about the same
// place, and an unbounded value is a foot-gun when a site gets it wrong.
constexpr int64_t kMaxHstsMillis = 365LL * 24 * 60 * 60 * 1000;

class HttpsPolicy {
public:
  // I read Strict-Transport-Security from an HTTPS answer. A header from an HTTP
  // answer is ignored, because otherwise anyone on the path could pin me.
  bool record_header(const std::string &host, bool over_https, const std::string &value,
                     int64_t now_ms);
  bool requires_https(const std::string &host) const;
  // True when the URL is http and a host in the store requires HTTPS.
  bool should_upgrade(const Url &url) const;
  // An https page loading an http subresource. Scripts and stylesheets are active
  // content, so they are always refused; images and media are refused as well,
  // because half-blocked mixed content is how a page ends up looking broken and
  // saying nothing.
  bool blocks_mixed_content(const Url &page, const Url &resource) const;
  // An https page posting a form to an http address would send the body in clear.
  bool blocks_insecure_submit(const Url &page, const Url &target) const;

  std::vector<HstsEntry> entries;
  // What this policy refused or changed since the browser started, so a person can
  // see that the setting does something.
  uint64_t upgraded = 0, blocked_mixed = 0, blocked_submit = 0;

  Json to_json() const;
  void from_json(const Json &json);
  void prune(int64_t now_ms);
  void clear();

private:
  // The longest matching entry for a host, honouring includeSubdomains.
  const HstsEntry *find(const std::string &host) const;
};

// The kind of thing a request is for. A rule that names no type applies to all of
// them; a rule that names one applies only there.
enum class ResourceType { Document, Subdocument, Script, Image, Stylesheet, Font, Media,
                          XmlHttpRequest, Other };

std::string resource_type_name(ResourceType type);

// One rule from a filter list, in the shape I can actually evaluate.
struct FilterRule {
  bool exception = false;
  // The part before any `$options`, with its anchors still in place.
  std::string pattern;
  bool host_anchor = false;   // starts with ||
  bool start_anchor = false;  // starts with a single |
  bool end_anchor = false;    // ends with a single |
  bool any_type = true;
  std::set<ResourceType> types;
  bool third_party_only = false;
  bool first_party_only = false;
  std::vector<std::string> domains;           // domain= a.com|b.com
  std::vector<std::string> excluded_domains;  // ~c.com
  std::string source;                         // the line as written, for the panel
};

// My own filter list engine: the subset of the usual syntax that I actually
// implement, matched against a URL, a resource type and the page that asked.
class FilterList {
public:
  // I parse one list and add it. `error` names the line that defeated me.
  bool parse(const std::string &text, std::string &error);
  size_t load_file(const std::string &path, std::string &error);
  // The rule that decides this request, or nothing when no rule matches. An
  // exception wins over a block, which is the only ordering that keeps a site
  // usable when a list gets greedy.
  const FilterRule *match(const Url &url, ResourceType type, const std::string &page_host) const;
  size_t size() const {
    return rules.size();
  }
  size_t exceptions() const;
  void clear() {
    rules.clear();
  }
  // Lines I skipped and why, so a broken list is visible rather than silent.
  std::vector<std::string> skipped;

  std::vector<FilterRule> rules;
};

// What my blocker did, counted where it happens.
struct FilterCounters {
  uint64_t blocked = 0;
  uint64_t allowed_by_exception = 0;
  uint64_t evaluated = 0;
  void add(const FilterCounters &other) {
    blocked += other.blocked;
    allowed_by_exception += other.allowed_by_exception;
    evaluated += other.evaluated;
  }
};

} // namespace pv
