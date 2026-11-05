// PsychoVault original source. My keyboard shortcut registry: one broker-side
// table resolves every key event the same way, wherever it was pressed.
// P1/S032 of docs/Psycho_MASTER-PLAN.md.
//
// The laws, stated so nobody forgets them:
//   1. ONE REGISTRY, ONE CANONICAL LANGUAGE. Accelerators here are parsed by
//      the same function menus use (S031), so "what the menu shows" and "what
//      the key does" cannot drift apart by even a letter's case.
//   2. THE BROKER RESOLVES FIRST. A claimed shortcut never reaches page code;
//      an unclaimed key always propagates to the focused content. Resolution
//      is a lookup, not an event - the page never outruns the registry.
//   3. SCOPES, NOT CHAOS. App-scope bindings fire everywhere; editable-scope
//      bindings (copy/paste/select-all and friends) only fire while an
//      editable field owns focus. An editable binding deliberately shadows an
//      app binding on the same keys - but only while editable focus lasts.
//   4. TYPING IS NOT A SHORTCUT. Bare printable characters cannot bind; named
//      keys (F5, Escape, Home) may. Nobody may bind the letter A to "About".
#pragma once
#include "../ipc/Psycho_bus.hpp"
#include <map>
#include <string>

namespace pv::rt {

// A platform key event, reduced to policy: modifier flags plus a key token
// ("t", "F5", "Escape", "1", "=").
struct KeyEvent {
  bool ctrl = false, alt = false, shift = false;
  std::string key;
};

// What was focused when the key arrived.
struct KeyContext {
  bool editable_focused = false;
};

enum class KeyScope { App, Editable };

class ShortcutRegistry {
public:
  // Bind laws refuse: unknown key tokens ("keys.key"), bare printable
  // characters ("keys.bare"), and the same accelerator twice in one scope
  // ("keys.duplicate"). The same accelerator in DIFFERENT scopes is legal -
  // that is the shadow law, declared where I can see it.
  bool bind(KeyScope scope, const std::string &accelerator, const std::string &command,
            Error &error);
  // A command's bindings all die together, across every scope.
  bool unbind_command(const std::string &command);
  size_t size() const;

  // The lookup. Empty string means unclaimed: the key event propagates to the
  // focused content. Never refuses - runtime input is never an error.
  std::string resolve(const KeyEvent &event, const KeyContext &context) const;

  // The canonical accelerator this event names - THE one canonicalizer,
  // shared with my menu service so menu labels and key behaviour agree.
  static std::string accelerator_for(const KeyEvent &event);

private:
  std::map<KeyScope, std::map<std::string, std::string>> table_;
};

} // namespace pv::rt
