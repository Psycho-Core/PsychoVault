// PsychoVault original source. One private browser session: isolated in-memory
// site stores, session-only permissions, and an explicit extension switch.
// P1/S037 of docs/Psycho_MASTER-PLAN.md.
//
// There is intentionally no profile path, load/save callback, or persistence
// interface in this type. Each instance owns its own stores and permission
// service; destroying it destroys that session's browser state.
#pragma once
#include "../permissions/Psycho_permissions.hpp"
#include <map>
#include <string>
#include <utility>

namespace pv::rt {

enum class PrivateStore { Cookies, LocalStorage };

class PrivateSession {
public:
  struct Seams {
    // The only external effect is the visible permission prompt. In
    // particular, no profile or disk-writing seam can be supplied.
    std::function<void(const std::string &origin, const std::string &kind)>
        show_permission_prompt = {};
  };

  PrivateSession();
  explicit PrivateSession(Seams seams);

  bool set(PrivateStore store, const std::string &origin,
           const std::string &key, const std::string &value, Error &error);
  bool get(PrivateStore store, const std::string &origin,
           const std::string &key, std::string &value, bool &found,
           Error &error) const;
  bool erase(PrivateStore store, const std::string &origin,
             const std::string &key, Error &error);
  void clear_stores();
  size_t size(PrivateStore store) const;

  // A separate permission service per session, deliberately without profile
  // seams. Remembering is impossible; prompt answers live only in this object.
  PermissionService &permissions() { return permissions_; }
  const PermissionService &permissions() const { return permissions_; }

  // Private extensions are off unless the user explicitly flips this switch.
  // The caller that starts extension workers must consult this policy first.
  bool extensions_enabled() const { return extensions_enabled_; }
  void set_extensions_enabled_by_user(bool enabled) {
    extensions_enabled_ = enabled;
  }
  bool may_run_extensions() const { return extensions_enabled_; }

private:
  using SiteKey = std::pair<std::string, std::string>;
  using Store = std::map<SiteKey, std::string>;
  static bool valid_store(PrivateStore store);
  static bool check_key(PrivateStore store, const std::string &origin,
                        const std::string &key, Error &error);

  Store cookies_;
  Store local_storage_;
  PermissionService permissions_;
  bool extensions_enabled_ = false;
};

} // namespace pv::rt
