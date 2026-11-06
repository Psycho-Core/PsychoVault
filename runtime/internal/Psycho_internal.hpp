// PsychoVault original source. My internal scheme psychovault:// - the page
// registry, the privileged-origin rules, and the web-access gate.
// P1/S028 of docs/Psycho_MASTER-PLAN.md.
//
// The three rules, stated so nobody forgets them:
//   1. Internal pages exist by registry only. If a page is not registered, the
//      address is refused - there is no catch-all and no guess.
//   2. Every internal page is its own origin. psychovault://settings and
//      psychovault://history share nothing; cross-internal fetches refuse.
//   3. Web content gets NOTHING by default. Reaching an internal origin needs
//      the Internal-channel capability AND a page that opted into it - both,
//      never either. Web pages can never top-level navigate here at all.
#pragma once
#include "../ipc/Psycho_bus.hpp"
#include <string>
#include <vector>

namespace pv::rt {

struct InternalPage {
  std::string name;       // canonical host token: "newtab", "settings", ...
  std::string category;   // "shell" (browser UI) | "diagnostics" | "crash"
  std::string summary;    // one honest line for the registry listing
  bool web_reachable = false; // may a right-holding web origin embed me at all?
};

struct InternalUrl {
  std::string page;  // registry-validated page token (the origin)
  std::string path;  // "" or "/file.css" - traversal and escapes are refused
  std::string canon; // canonical full form
};

class InternalRegistry {
public:
  // My pages, known by name. The version, nettest and crash tools are
  // registered in the stock address space; web probing can never invent one.
  static InternalRegistry stock();

  const InternalPage *find(const std::string &name) const;
  const std::vector<InternalPage> &pages() const { return pages_; }
  void add(InternalPage page); // custom registries (tests, later embedders)

  // Parse + validate + canonicalise one address. Refusals name their law:
  // internal.scheme / internal.control / internal.page-required /
  // internal.page-name / internal.unknown-page / internal.traversal /
  // internal.escape / internal.path
  bool parse(const std::string &raw, InternalUrl &out, Error &error) const;

private:
  std::vector<InternalPage> pages_;
};

// The gate every fetch and navigation into my scheme must pass.
class InternalGate {
public:
  explicit InternalGate(const InternalRegistry &registry) : registry_(registry) {}

  // The broker's own shell loading a registered page: the affirmative path.
  bool broker_navigate(const InternalUrl &url, Error &error) const;

  // Web content fetching an internal resource. Capability check first (so a
  // right-less probe learns nothing about what exists), page opt-in second.
  bool web_fetch(const InternalUrl &url, const Capability &granted, Error &error) const;

  // Web content asking to top-level navigate into my scheme: refused, always.
  // Web pages never steer the browser into its shell.
  bool web_navigate(const InternalUrl &url, Error &error) const;

  // An internal page fetching a resource. Same-origin only: a settings page
  // may hold psychovault://settings/app.css, never psychovault://history/css.
  bool internal_fetch(const std::string &origin_page, const InternalUrl &target,
                      Error &error) const;

private:
  const InternalRegistry &registry_;
};

// The widened admission policy for broker-driven navigation (S027's seam):
// psychovault:// addresses validate against the registry, everything else
// falls through to the stock http/https rules. Renderer-originated requests
// are refused before this point by the gate, so this seam is never the wall
// web content leans on.
bool admit_browser_url(const std::string &raw, const InternalRegistry &registry,
                       std::string &canon, Error &error);

} // namespace pv::rt
