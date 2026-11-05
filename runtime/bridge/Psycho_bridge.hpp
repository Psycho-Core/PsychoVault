// PsychoVault original source. My preload bridge: the curated, frozen
// namespace a page gets - window.psycho - and the isolated preload context
// that backs it. P1/S029 of docs/Psycho_MASTER-PLAN.md.
//
// The four laws, stated so nobody forgets them:
//   1. ISOLATION. Preload code runs in its own js::Runtime - a world page
//      scripts cannot reach. Nothing is shared by reference between worlds;
//      the only thing that crosses is JSON-shaped data.
//   2. CURATION. The facade contains exactly the schema-registered bus methods
//      the broker declared. Undeclared names do not exist, and the dispatcher
//      refuses them even if preload code itself asks.
//   3. FREEZE. The installed graph is frozen; page code cannot patch, shadow
//      or extend it (engine-enforced via Runtime::set_member, S029).
//   4. NO DIRECT BROKER ACCESS. The page world has no link, no endpoint, no
//      dispatcher - window.psycho's natives are trampolines through ME.
#pragma once
#include "../../engine/Psycho_js.hpp"
#include "../ipc/Psycho_bus.hpp"
#include <functional>
#include <map>
#include <string>

namespace pv::rt {

struct ExposedEntry {
  Channel channel;
  std::string method; // schema-registered envelope method, e.g. "settings:get"
};

class PreloadBridge {
public:
  struct Seams {
    // The one way calls leave the page: a validated envelope round-trip
    // through the broker. Tests hand a fake; the broker wiring arrives above.
    std::function<bool(Channel channel, const std::string &method, Json payload,
                       Json &reply, Error &error)>
        invoke = {};
  };

  explicit PreloadBridge(Seams seams);

  // Curation: only names my schema registry knows can ever be exposed -
  // "bridge.unknown-method" otherwise; re-exposing is "bridge.duplicate".
  bool expose(Channel channel, const std::string &method, Error &error);
  bool exposed(const std::string &method) const { return table_.count(method) != 0; }

  // My isolated preload world and its entry point. The runner installs the
  // native __psycho_call dispatcher there - preload code uses it, page code
  // will never see it - then runs the source. Script errors refuse cleanly.
  js::Runtime &preload_world() { return preload_; }
  bool run_preload(std::string_view source, Error &error);

  // Materialise window.psycho in the page world: wire names are colon-shaped
  // ("settings:get"), the facade dot-shaped (psycho.settings.get) - nested
  // plain objects from the segments, native trampolines at the leaves,
  // the whole graph frozen before install.
  // frozen before install. The page global gets "psycho" (the page world IS
  // the window - my page host attaches DOM plumbing above, in S030).
  bool install(js::Runtime &page_world, Error &error);

  // The dispatcher every trampoline funnels through: curation check, schema
  // validation, then the broker seam. Failures throw BridgeError in-page.
  bool dispatch(const std::string &method, const Json &payload, Json &reply,
                Error &error);

private:
  js::Value trampoline(js::Runtime &page_world, const std::string &method);
  void freeze_graph(js::Value object) const;

  Seams seams_;
  Schemas schemas_ = Schemas::stock();
  std::map<std::string, ExposedEntry> table_;
  js::Runtime preload_; // the isolated world - no window, no page objects
};

} // namespace pv::rt
