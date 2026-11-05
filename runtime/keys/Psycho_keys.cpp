// PsychoVault original source. See the header for the four laws - this file
// is how I keep them.
#include "Psycho_keys.hpp"
#include "../services/Psycho_services.hpp" // MenuService::canonical_accelerator
#include <cctype>
#include <set>

namespace pv::rt {

namespace {

Error refuse(const char *code, const std::string &message) {
  Error error;
  error.code = code;
  error.message = message;
  return error;
}

bool printable_char(char c) { return c >= 0x21 && c <= 0x7e; }

bool known_named_key(const std::string &key) {
  static const std::set<std::string> names = {
      "F1",  "F2",  "F3",  "F4",   "F5",    "F6",   "F7",  "F8",   "F9",
      "F10", "F11", "F12", "F13",  "F14",   "F15",  "F16", "F17",  "F18",
      "F19", "F20", "F21", "F22",  "F23",   "F24",  "ESCAPE", "TAB", "ENTER",
      "SPACE", "BACKSPACE", "DELETE", "INSERT", "HOME", "END", "PAGEUP",
      "PAGEDOWN", "LEFT", "RIGHT", "UP", "DOWN"};
  std::string upper = key;
  for (char &c : upper) c = (char)std::toupper((unsigned char)c);
  return names.count(upper) != 0;
}

// A key token is bindable when it is one printable character or a name my
// windowing layer knows.
bool valid_key_token(const std::string &key) {
  if (key.size() == 1 && printable_char(key[0])) return true;
  return known_named_key(key);
}

} // namespace

std::string ShortcutRegistry::accelerator_for(const KeyEvent &event) {
  std::string raw;
  if (event.ctrl) raw += "Ctrl+";
  if (event.alt) raw += "Alt+";
  if (event.shift) raw += "Shift+";
  raw += event.key;
  return MenuService::canonical_accelerator(raw);
}

bool ShortcutRegistry::bind(KeyScope scope, const std::string &accelerator,
                            const std::string &command, Error &error) {
  error = Error{};
  const std::string canon = MenuService::canonical_accelerator(accelerator);
  // The canonical form's key part tells me what was actually bound.
  const size_t last_plus = canon.rfind('+');
  const std::string key =
      last_plus == std::string::npos ? canon : canon.substr(last_plus + 1);
  if (key.empty() || !valid_key_token(key)) {
    error = refuse("keys.key", "'" + accelerator + "' has no bindable key token");
    return false;
  }
  const bool has_modifier = canon.find('+') != std::string::npos;
  if (!has_modifier && key.size() == 1) {
    error = refuse("keys.bare", "typing is not a shortcut: '" + accelerator +
                                    "' binds no bare printable key");
    return false;
  }
  if (command.empty()) {
    error = refuse("keys.command", "a binding needs a command id");
    return false;
  }
  auto &scope_table = table_[scope];
  if (scope_table.count(canon)) {
    error = refuse("keys.duplicate",
                   "'" + canon + "' is already bound in this scope");
    return false;
  }
  scope_table[canon] = command;
  return true;
}

bool ShortcutRegistry::unbind_command(const std::string &command) {
  bool removed = false;
  for (auto &[scope, bindings] : table_) {
    for (auto it = bindings.begin(); it != bindings.end();) {
      if (it->second == command) {
        it = bindings.erase(it);
        removed = true;
      } else {
        ++it;
      }
    }
  }
  return removed;
}

size_t ShortcutRegistry::size() const {
  size_t total = 0;
  for (const auto &[scope, bindings] : table_) total += bindings.size();
  return total;
}

std::string ShortcutRegistry::resolve(const KeyEvent &event,
                                      const KeyContext &context) const {
  const std::string canon = accelerator_for(event);
  if (canon.empty()) return "";
  // Editable focus gets its own table first - shadow law applied honestly.
  if (context.editable_focused) {
    auto editable = table_.find(KeyScope::Editable);
    if (editable != table_.end()) {
      auto hit = editable->second.find(canon);
      if (hit != editable->second.end()) return hit->second;
    }
  }
  auto app = table_.find(KeyScope::App);
  if (app != table_.end()) {
    auto hit = app->second.find(canon);
    if (hit != app->second.end()) return hit->second;
  }
  return ""; // unclaimed: the key belongs to the focused content
}

} // namespace pv::rt
