// PsychoVault original source. My app-layer JS host: the thing that actually
// runs UI scripts inside a page world with exactly the privileges that page's
// origin earns - and surfaces every failure to the broker's log.
// P1/S030 of docs/Psycho_MASTER-PLAN.md.
//
// Standing on the layers below me: S029's bridge gives the isolated preload
// world, the curated frozen window.psycho and the data-only crossing; S028's
// registry says which internal pages exist. What I add is policy per origin
// kind and honest failure handling.
//
// The two laws here:
//   1. CURATED MINIMUM, PER ORIGIN. A web page gets NO facade at all. A shell
//      page gets the small frozen set the UI needs - and deliberately not the
//      vault, proton or extensions channels (extension-less means exactly
//      that). A diagnostics page gets an even smaller read-mostly set.
//   2. ERRORS BECOME LOG RECORDS. Script throws and runaway-script fuel
//      exhaustion are caught here and surfaced to the broker as structured
//      Errors naming the page and the script label. Nothing throws across
//      the host boundary, ever.
#pragma once
#include "../bridge/Psycho_bridge.hpp"
#include "../internal/Psycho_internal.hpp"
#include <string_view>

namespace pv::rt {

enum class PageKind { Web, Shell, Diagnostics, Crash };
enum class RendererPolicy { Regular, Private };

class AppHost {
public:
  struct Seams {
    // Broker round-trip for the bridge's dispatcher (same shape as S029's).
    std::function<bool(Channel channel, const std::string &method, Json payload,
                       Json &reply, Error &error)>
        invoke = {};
    // The broker's log: every script failure lands here as a structured
    // record. The host keeps no copy - the log belongs to the broker.
    std::function<void(const Error &error)> log = {};
  };

  // page == nullptr means a web origin. Any other value must come straight
  // from an InternalRegistry parse; the kind is derived from its category.
  AppHost(int tab_id, const InternalPage *page, Seams seams,
          RendererPolicy policy = RendererPolicy::Regular);

  PageKind kind() const { return kind_; }
  RendererPolicy policy() const { return policy_; }
  int tab_id() const { return tab_id_; }

  // Run one script in the page world. Returns false iff it failed - and the
  // failure is already in the broker's log as apphost.script (page throw) or
  // apphost.bound (fuel limit: a runaway UI script dies, the broker lives).
  bool run_ui(std::string_view source, const std::string &label);

  js::Runtime &page_world() { return page_; }
  PreloadBridge &bridge() { return bridge_; }

private:
  int tab_id_ = -1;
  PageKind kind_ = PageKind::Web;
  RendererPolicy policy_ = RendererPolicy::Regular;
  std::string page_name_ = "web"; // for honest log records
  Seams seams_;
  PreloadBridge bridge_;
  js::Runtime page_;
};

} // namespace pv::rt
