// PsychoVault original source. See the header for intent and boundaries -
// this file is the services' rulebook worked out in full.
#include "Psycho_services.hpp"
#include <algorithm>
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

// The menu tree law, shared by the application menu and context menus.
// Fills `nodes`/`parents` when non-null, so MenuService keeps lookup maps.
bool validate_menu(std::vector<MenuItem> &items, MenuItem *parent,
                   std::map<std::string, MenuItem *> *nodes,
                   std::map<std::string, MenuItem *> *parents, int &count,
                   std::set<std::string> &seen, Error &error) {
  std::set<std::string> sibling_accelerators;
  for (MenuItem &item : items) {
    ++count;
    if (count > 256) {
      error = refuse("menu.too-large", "a menu tree may hold 256 items, not more");
      return false;
    }
    if (item.type == MenuItem::Type::Separator) {
      if (!item.id.empty()) {
        error = refuse("menu.separator", "a separator carries no command id");
        return false;
      }
    } else {
      if (item.id.empty()) {
        error = refuse("menu.id", "a menu item needs a command id");
        return false;
      }
      if (item.label.empty()) {
        error = refuse("menu.label", "a menu item needs a label");
        return false;
      }
      // Uniqueness is unconditional - a context menu's ids are law too.
      if (!seen.insert(item.id).second) {
        error = refuse("menu.duplicate", "command id '" + item.id + "' appears twice");
        return false;
      }
      item.accelerator = MenuService::canonical_accelerator(item.accelerator);
      if (!item.accelerator.empty() &&
          !sibling_accelerators.insert(item.accelerator).second) {
        error = refuse("menu.accelerator",
                       "'" + item.accelerator + "' is taken at this menu level");
        return false;
      }
      if (nodes) {
        (*nodes)[item.id] = &item;
        (*parents)[item.id] = parent;
      }
    }
    if (!validate_menu(item.submenu, &item, nodes, parents, count, seen, error))
      return false;
  }
  return true;
}

void collect_ids(const std::vector<MenuItem> &items, std::set<std::string> &out) {
  for (const MenuItem &item : items) {
    if (!item.id.empty()) out.insert(item.id);
    collect_ids(item.submenu, out);
  }
}

} // namespace

// ---------- dialogs ----------

bool DialogService::run(const DialogRequest &request, DialogResult &result,
                        Error &error) {
  error = Error{};
  result = DialogResult{};
  if (!seams_.show) {
    error = refuse("dialog.unavailable", "this build has no platform dialog seam");
    return false;
  }
  switch (request.kind) {
  case DialogRequest::Kind::Message:
    if (request.message.empty()) {
      error = refuse("dialog.message", "a message dialog needs a message");
      return false;
    }
    break;
  case DialogRequest::Kind::Open:
  case DialogRequest::Kind::Save:
    // File pickers are trusted-chrome only: a page never spawns one.
    if (!request.from_shell) {
      error = refuse("dialog.origin", "open/save dialogs come from my UI, not pages");
      return false;
    }
    if (request.filters.size() > kFilterLimit) {
      error = refuse("dialog.filters", "a picker takes at most 8 filters");
      return false;
    }
    for (const std::string &filter : request.filters) {
      const std::string token = lower(filter);
      if (token.empty() || token.size() > 8 ||
          !std::all_of(token.begin(), token.end(),
                       [](char c) { return std::isalnum((unsigned char)c); })) {
        error = refuse("dialog.filter", "filter tokens are 1-8 alnum characters");
        return false;
      }
    }
    if (request.kind == DialogRequest::Kind::Save && request.default_name.empty()) {
      error = refuse("dialog.name", "a save dialog needs a default name");
      return false;
    }
    break;
  }
  if (modals_.count(request.window_id)) {
    error = refuse("dialog.busy", "window " + std::to_string(request.window_id) +
                                      " already has a dialog up");
    return false;
  }
  modals_.insert(request.window_id);
  DialogResult answer = seams_.show(request);
  modals_.erase(request.window_id);
  if (!answer.cancelled && request.kind != DialogRequest::Kind::Message &&
      answer.path.empty()) {
    error = refuse("dialog.result", "a non-cancelled picker answers with a path");
    return false;
  }
  result = answer;
  return true;
}

// ---------- clipboard ----------

bool ClipboardService::write_text(const std::string &origin, const std::string &text,
                                  Error &error) {
  error = Error{};
  if (text.size() > kTextLimit) {
    error = refuse("clipboard.too-large", "clipboard text is capped at 1 MiB");
    return false;
  }
  text_ = text;
  writer_ = origin;
  ++generation_;
  if (seams_.apply) seams_.apply(text_);
  return true;
}

bool ClipboardService::read_text(const std::string &origin, bool trusted,
                                 std::string &out, Error &error) {
  error = Error{};
  if (!trusted) {
    error = refuse("clipboard.denied", "origin '" + origin + "' may not read");
    return false;
  }
  out = text_; // an unwritten clipboard reads as empty, honestly
  return true;
}

// ---------- notifications ----------

void NotificationService::mute(const std::string &origin, bool muted) {
  if (muted)
    muted_.insert(origin);
  else
    muted_.erase(origin);
}

bool NotificationService::notify(const std::string &origin, const std::string &title,
                                 const std::string &body, bool has_permission,
                                 Error &error) {
  error = Error{};
  if (!has_permission) {
    error = refuse("notify.permission",
                   "origin '" + origin + "' holds no notification permission");
    return false;
  }
  if (muted_.count(origin)) {
    error = refuse("notify.muted", "origin '" + origin + "' is muted");
    return false;
  }
  if (title.empty()) {
    error = refuse("notify.title", "a notification needs a title");
    return false;
  }
  while (recent_.size() >= kRecentLimit) recent_.pop_front();
  recent_.push_back({next_id_++, origin, title, body});
  if (seams_.post) seams_.post(recent_.back());
  return true;
}

// ---------- tray ----------

bool TrayService::create(const std::string &tooltip, Error &error) {
  error = Error{};
  if (alive_) {
    error = refuse("tray.exists", "my app has exactly one tray icon");
    return false;
  }
  if (tooltip.size() > kTooltipLimit) {
    error = refuse("tray.too-long", "tooltips stop at 128 characters");
    return false;
  }
  alive_ = true;
  tooltip_ = tooltip;
  return true;
}

bool TrayService::destroy(Error &error) {
  error = Error{};
  if (!alive_) {
    error = refuse("tray.missing", "there is no tray icon to destroy");
    return false;
  }
  alive_ = false;
  tooltip_.clear();
  return true;
}

bool TrayService::set_tooltip(const std::string &tooltip, Error &error) {
  error = Error{};
  if (!alive_) {
    error = refuse("tray.missing", "there is no tray icon to retitle");
    return false;
  }
  if (tooltip.size() > kTooltipLimit) {
    error = refuse("tray.too-long", "tooltips stop at 128 characters");
    return false;
  }
  tooltip_ = tooltip;
  return true;
}

void TrayService::event(const std::string &name) {
  if (alive_ && seams_.on_event) seams_.on_event(name);
}

// ---------- menus ----------

std::string MenuService::canonical_accelerator(const std::string &raw) {
  std::vector<std::string> mods;
  std::string key;
  size_t at = 0;
  while (at <= raw.size()) {
    const size_t plus = raw.find('+', at);
    std::string token = lower(raw.substr(
        at, plus == std::string::npos ? std::string::npos : plus - at));
    while (!token.empty() && std::isspace((unsigned char)token.front()))
      token.erase(0, 1);
    while (!token.empty() && std::isspace((unsigned char)token.back()))
      token.pop_back();
    if (token == "ctrl" || token == "control")
      mods.push_back("Ctrl");
    else if (token == "alt")
      mods.push_back("Alt");
    else if (token == "shift")
      mods.push_back("Shift");
    else if (token.empty())
      return "";
    else {
      for (char &c : token) c = (char)std::toupper((unsigned char)c);
      key = token;
    }
    if (plus == std::string::npos) break;
    at = plus + 1;
  }
  if (key.empty()) return "";
  std::sort(mods.begin(), mods.end());
  mods.erase(std::unique(mods.begin(), mods.end()), mods.end());
  std::string out;
  for (const std::string &mod : mods) out += mod + "+";
  return out + key;
}

bool MenuService::install(std::vector<MenuItem> root, Error &error) {
  error = Error{};
  int count = 0;
  std::map<std::string, MenuItem *> nodes;
  std::map<std::string, MenuItem *> parents;
  std::set<std::string> seen;
  if (!validate_menu(root, nullptr, &nodes, &parents, count, seen, error)) return false;
  root_ = std::move(root);
  nodes_ = std::move(nodes);
  parents_ = std::move(parents);
  return true;
}

const MenuItem *MenuService::find(const std::string &id) const {
  auto it = nodes_.find(id);
  return it == nodes_.end() ? nullptr : it->second;
}

bool MenuService::dispatch(const std::string &id, Error &error) {
  error = Error{};
  auto it = nodes_.find(id);
  if (it == nodes_.end()) {
    error = refuse("menu.missing", "no command id '" + id + "' is installed");
    return false;
  }
  MenuItem *item = it->second;
  if (!item->enabled) {
    error = refuse("menu.disabled", "'" + id + "' is disabled");
    return false;
  }
  if (item->type == MenuItem::Type::Checkbox) {
    item->checked = !item->checked;
  } else if (item->type == MenuItem::Type::Radio) {
    Error refuse_error;
    set_checked(id, true, refuse_error); // sibling law lives there
  }
  if (seams_.on_command) seams_.on_command(id);
  return true;
}

bool MenuService::set_checked(const std::string &id, bool checked, Error &error) {
  error = Error{};
  auto it = nodes_.find(id);
  if (it == nodes_.end()) {
    error = refuse("menu.missing", "no command id '" + id + "' is installed");
    return false;
  }
  MenuItem *item = it->second;
  if (checked && item->type == MenuItem::Type::Radio) {
    // Radio law: checking one unchecks every radio sibling under my parent.
    MenuItem *parent = parents_[id];
    std::vector<MenuItem> &siblings = parent ? parent->submenu : root_;
    for (MenuItem &sibling : siblings) {
      if (sibling.type == MenuItem::Type::Radio && sibling.id != id)
        sibling.checked = false;
    }
  }
  item->checked = checked;
  return true;
}

bool MenuService::set_enabled(const std::string &id, bool enabled, Error &error) {
  error = Error{};
  auto it = nodes_.find(id);
  if (it == nodes_.end()) {
    error = refuse("menu.missing", "no command id '" + id + "' is installed");
    return false;
  }
  it->second->enabled = enabled;
  return true;
}

// ---------- context menus ----------

bool ContextMenus::register_provider(
    std::string kind,
    std::function<std::vector<MenuItem>(const ContextInfo &)> provider, Error &error) {
  error = Error{};
  if (providers_.count(kind)) {
    error = refuse("context.duplicate-provider", "'" + kind + "' already has one");
    return false;
  }
  providers_[std::move(kind)] = std::move(provider);
  return true;
}

uint64_t ContextMenus::open(const ContextInfo &info, Error &error) {
  error = Error{};
  auto it = providers_.find(info.kind);
  if (it == providers_.end()) {
    error = refuse("context.no-provider", "no menu provider for kind '" + info.kind + "'");
    return 0;
  }
  std::vector<MenuItem> items = it->second(info);
  int count = 0;
  std::set<std::string> seen;
  if (!validate_menu(items, nullptr, nullptr, nullptr, count, seen, error)) return 0;
  if (open_.size() >= kOutstandingLimit) {
    error = refuse("context.too-many", "at most 16 context menus outstanding");
    return 0;
  }
  Session session;
  session.info = info;
  collect_ids(items, session.offered);
  const uint64_t token = next_token_++;
  open_[token] = std::move(session);
  return token;
}

bool ContextMenus::choose(uint64_t token, const std::string &command_id, Error &error) {
  error = Error{};
  auto it = open_.find(token);
  if (it == open_.end()) {
    error = refuse("context.token", "that context menu is not open");
    return false;
  }
  if (!it->second.offered.count(command_id)) {
    error = refuse("context.not-offered",
                   "'" + command_id + "' was not on that menu; no freelancing");
    return false;
  }
  const ContextInfo info = it->second.info;
  open_.erase(it);
  if (seams_.on_command) seams_.on_command(command_id, info);
  return true;
}

bool ContextMenus::cancel(uint64_t token) { return open_.erase(token) != 0; }

} // namespace pv::rt
