// PsychoVault original source. See the header for intent and boundaries -
// this file is the rulebook working it out, with no OS involvement at all.
#include "Psycho_tabs.hpp"
#include <algorithm>

namespace pv::rt {

namespace {

Error fail(const char *code, const std::string &message) {
  Error error;
  error.code = code;
  error.message = message;
  return error;
}

} // namespace

TabModel::Found TabModel::lookup(int tab_id) {
  Found found;
  auto it = tabs_.find(tab_id);
  if (it == tabs_.end()) return found;
  found.tab = &it->second;
  WindowTabs &window = windows_.at(it->second.window_id);
  found.window = &window;
  for (size_t i = 0; i < window.order.size(); ++i) {
    if (window.order[i] == tab_id) { found.index = i; break; }
  }
  return found;
}

size_t TabModel::pinned_count(const WindowTabs &window) const {
  size_t pins = 0;
  while (pins < window.order.size() && tabs_.at(window.order[pins]).pinned) ++pins;
  return pins;
}

size_t TabModel::clamp_index(const WindowTabs &window, int index, bool pinned) const {
  size_t idx =
      index < 0 ? window.order.size()
                : std::min(static_cast<size_t>(index), window.order.size());
  const size_t pins = pinned_count(window);
  if (pinned) {
    if (idx > pins) idx = pins;
  } else {
    if (idx < pins) idx = pins;
  }
  return idx;
}

void TabModel::assign_active(WindowTabs &window, int tab_id) {
  window.active = tab_id;
  Tab &tab = tabs_.at(tab_id);
  // The constitution: the active tab is never discarded. Waking one up is a
  // fresh load, and the generation counter is the honest record of that.
  if (tab.state == TabState::Discarded) {
    tab.state = TabState::Live;
    ++tab.generation;
  }
}

void TabModel::replace_active(WindowTabs &window, size_t removed_index) {
  if (window.order.empty()) {
    window.active = -1;
    return;
  }
  const size_t next =
      removed_index < window.order.size() ? removed_index : window.order.size() - 1;
  assign_active(window, window.order[next]);
}

void TabModel::remember_closed(const Tab &tab) {
  ClosedTab entry;
  entry.tab_id = tab.id;
  entry.window_id = tab.window_id;
  entry.label = tab.label;
  entry.pinned = tab.pinned;
  entry.muted = tab.muted;
  entry.state = tab.state;
  entry.group_id = tab.group_id;
  auto git = groups_.find(tab.group_id);
  if (git != groups_.end()) {
    entry.group_name = git->second.name;
    entry.group_color = git->second.color;
  }
  auto wit = windows_.find(tab.window_id);
  if (wit != windows_.end()) {
    for (size_t i = 0; i < wit->second.order.size(); ++i) {
      if (wit->second.order[i] == tab.id) { entry.index = static_cast<int>(i); break; }
    }
  }
  while (closed_.size() >= kClosedLimit) closed_.pop_front();
  closed_.push_back(entry);
}

bool TabModel::transplant(const std::vector<int> &ids, WindowTabs &src, WindowTabs &dst,
                          int dest_window, int index) {
  if (ids.empty()) return true;
  const bool pinned = tabs_.at(ids.front()).pinned;
  size_t first_removed = src.order.size();
  for (int id : ids) {
    auto pos = std::find(src.order.begin(), src.order.end(), id);
    if (pos != src.order.end()) {
      first_removed =
          std::min(first_removed, static_cast<size_t>(pos - src.order.begin()));
      src.order.erase(pos);
    }
  }
  const bool dst_was_empty = dst.order.empty() && &src != &dst;
  const size_t at = clamp_index(dst, index, pinned);
  dst.order.insert(dst.order.begin() + static_cast<ptrdiff_t>(at), ids.begin(), ids.end());
  const bool active_left = src.active != -1 &&
      std::find(src.order.begin(), src.order.end(), src.active) == src.order.end();
  if (active_left) replace_active(src, first_removed);
  for (int id : ids) {
    Tab &tab = tabs_.at(id);
    tab.window_id = dest_window;
    if (tab.group_id != -1) groups_.at(tab.group_id).window_id = dest_window;
  }
  if (dst_was_empty) assign_active(dst, ids.front());
  return true;
}

bool TabModel::attach_window(int window_id, Error &error) {
  error = Error{};
  if (windows_.count(window_id)) {
    error = fail("tabs.window-exists", "window " + std::to_string(window_id) +
                                           " is already in my tab model");
    return false;
  }
  windows_[window_id] = WindowTabs{};
  return true;
}

bool TabModel::detach_window(int window_id, Error &error) {
  error = Error{};
  auto it = windows_.find(window_id);
  if (it == windows_.end()) {
    error = fail("tabs.window-missing", "window " + std::to_string(window_id) +
                                            " is not in my tab model");
    return false;
  }
  if (!it->second.order.empty()) {
    error = fail("tabs.window-busy", "window " + std::to_string(window_id) +
                                         " still holds tabs; move them before detaching");
    return false;
  }
  windows_.erase(it);
  return true;
}

int TabModel::open_tab(int window_id, const std::string &label, int index, Error &error) {
  error = Error{};
  auto it = windows_.find(window_id);
  if (it == windows_.end()) {
    error = fail("tabs.window-missing", "cannot open a tab in window " +
                                            std::to_string(window_id) +
                                            ": it is not in my tab model");
    return -1;
  }
  WindowTabs &window = it->second;
  Tab tab;
  tab.id = next_tab_id_++;
  tab.window_id = window_id;
  tab.label = label;
  const int id = tab.id;
  tabs_[id] = tab;
  window.order.insert(window.order.begin() +
                          static_cast<ptrdiff_t>(clamp_index(window, index, false)),
                      id);
  if (window.order.size() == 1) window.active = id;
  return id;
}

bool TabModel::close_tab(int tab_id, Error &error) {
  error = Error{};
  Found found = lookup(tab_id);
  if (!found.tab) {
    error = fail("tabs.missing", "tab " + std::to_string(tab_id) + " does not exist");
    return false;
  }
  Tab snapshot = *found.tab;
  const bool was_active = found.window->active == tab_id;
  // Memory goes first: the closed entry captures my index and my group's
  // identity while both are still true.
  remember_closed(snapshot);
  // A group whose last member closes evaporates: an empty group is not a thing.
  if (snapshot.group_id != -1) {
    size_t members = 0;
    for (int id : found.window->order) {
      auto it = tabs_.find(id);
      if (it != tabs_.end() && it->second.group_id == snapshot.group_id) ++members;
    }
    if (members <= 1) groups_.erase(snapshot.group_id);
  }
  found.window->order.erase(found.window->order.begin() +
                            static_cast<ptrdiff_t>(found.index));
  if (was_active) replace_active(*found.window, found.index);
  tabs_.erase(tab_id);
  return true;
}

bool TabModel::activate(int tab_id, Error &error) {
  error = Error{};
  Found found = lookup(tab_id);
  if (!found.tab) {
    error = fail("tabs.missing", "tab " + std::to_string(tab_id) + " does not exist");
    return false;
  }
  assign_active(*found.window, tab_id);
  return true;
}

const Tab *TabModel::active_in(int window_id) const {
  auto it = windows_.find(window_id);
  if (it == windows_.end() || it->second.active == -1) return nullptr;
  auto tit = tabs_.find(it->second.active);
  return tit == tabs_.end() ? nullptr : &tit->second;
}

bool TabModel::move_within_window(int window_id, int tab_id, int index, Error &error) {
  error = Error{};
  auto wit = windows_.find(window_id);
  if (wit == windows_.end()) {
    error = fail("tabs.window-missing", "window " + std::to_string(window_id) +
                                            " is not in my tab model");
    return false;
  }
  Found found = lookup(tab_id);
  if (!found.tab) {
    error = fail("tabs.missing", "tab " + std::to_string(tab_id) + " does not exist");
    return false;
  }
  if (found.tab->window_id != window_id) {
    error = fail("tabs.wrong-window", "tab " + std::to_string(tab_id) +
                                          " does not live in window " +
                                          std::to_string(window_id));
    return false;
  }
  WindowTabs &window = wit->second;
  const bool pinned = found.tab->pinned;
  window.order.erase(window.order.begin() + static_cast<ptrdiff_t>(found.index));
  window.order.insert(window.order.begin() +
                          static_cast<ptrdiff_t>(clamp_index(window, index, pinned)),
                      tab_id);
  return true;
}

bool TabModel::move_to_window(int tab_id, int dest_window, int index, Error &error) {
  error = Error{};
  if (!windows_.count(dest_window)) {
    error = fail("tabs.window-missing", "destination window " +
                                            std::to_string(dest_window) +
                                            " is not in my tab model");
    return false;
  }
  Found found = lookup(tab_id);
  if (!found.tab) {
    error = fail("tabs.missing", "tab " + std::to_string(tab_id) + " does not exist");
    return false;
  }
  if (found.tab->group_id != -1) {
    error = fail("tabs.grouped-move",
                 "tab " + std::to_string(tab_id) +
                     " belongs to a group; groups travel as one block");
    return false;
  }
  if (found.tab->window_id == dest_window)
    return move_within_window(dest_window, tab_id, index, error);
  return transplant({tab_id}, windows_.at(found.tab->window_id),
                    windows_.at(dest_window), dest_window, index);
}

bool TabModel::move_group_to_window(int group_id, int dest_window, int index,
                                    Error &error) {
  error = Error{};
  auto git = groups_.find(group_id);
  if (git == groups_.end()) {
    error = fail("group.missing", "group " + std::to_string(group_id) + " does not exist");
    return false;
  }
  if (!windows_.count(dest_window)) {
    error = fail("tabs.window-missing", "destination window " +
                                            std::to_string(dest_window) +
                                            " is not in my tab model");
    return false;
  }
  WindowTabs &src = windows_.at(git->second.window_id);
  std::vector<int> block;
  for (int id : src.order) {
    if (tabs_.at(id).group_id == group_id) block.push_back(id);
  }
  return transplant(block, src, windows_.at(dest_window), dest_window, index);
}

bool TabModel::move_window_to_window(int src_window, int dest_window, int index,
                                     Error &error) {
  error = Error{};
  if (src_window == dest_window) {
    error = fail("tabs.same-window", "moving a window's tabs into itself is a no-op");
    return false;
  }
  if (!windows_.count(src_window) || !windows_.count(dest_window)) {
    error = fail("tabs.window-missing", "both windows must live in my tab model");
    return false;
  }
  WindowTabs &src = windows_.at(src_window);
  WindowTabs &dst = windows_.at(dest_window);
  // The source's active tab is leaving no matter what, so I disarm the
  // per-move successor repair: nothing may wake a tab that is about to move.
  src.active = -1;
  std::vector<int> pins, unpins;
  for (int id : src.order) (tabs_.at(id).pinned ? pins : unpins).push_back(id);
  transplant(pins, src, dst, dest_window, -1);
  transplant(unpins, src, dst, dest_window, index);
  return true;
}

bool TabModel::set_pinned(int tab_id, bool pinned, Error &error) {
  error = Error{};
  Found found = lookup(tab_id);
  if (!found.tab) {
    error = fail("tabs.missing", "tab " + std::to_string(tab_id) + " does not exist");
    return false;
  }
  if (found.tab->pinned == pinned) return true;
  WindowTabs &window = *found.window;
  // A grouped tab pins as one block: the family never straddles the boundary.
  std::vector<int> block;
  if (found.tab->group_id == -1) {
    block.push_back(tab_id);
  } else {
    for (int id : window.order) {
      if (tabs_.at(id).group_id == found.tab->group_id) block.push_back(id);
    }
  }
  for (int id : block) {
    auto pos = std::find(window.order.begin(), window.order.end(), id);
    window.order.erase(pos);
  }
  for (int id : block) tabs_.at(id).pinned = pinned;
  const size_t at = pinned_count(window); // after removal: exactly the boundary
  window.order.insert(window.order.begin() + static_cast<ptrdiff_t>(at), block.begin(),
                      block.end());
  return true;
}

bool TabModel::set_muted(int tab_id, bool muted, Error &error) {
  error = Error{};
  Found found = lookup(tab_id);
  if (!found.tab) {
    error = fail("tabs.missing", "tab " + std::to_string(tab_id) + " does not exist");
    return false;
  }
  found.tab->muted = muted;
  return true;
}

bool TabModel::discard(int tab_id, Error &error) {
  error = Error{};
  Found found = lookup(tab_id);
  if (!found.tab) {
    error = fail("tabs.missing", "tab " + std::to_string(tab_id) + " does not exist");
    return false;
  }
  if (found.window->active == tab_id) {
    error = fail("tab.discard-active",
                 "the active tab cannot be discarded; activate another tab first");
    return false;
  }
  found.tab->state = TabState::Discarded;
  return true;
}

int TabModel::group_tabs(const std::vector<int> &tab_ids, const std::string &name,
                         const std::string &color, Error &error) {
  error = Error{};
  if (tab_ids.empty()) {
    error = fail("group.empty", "a group needs at least one tab");
    return -1;
  }
  int window_id = -1;
  bool pinned = false;
  std::vector<Found> members;
  for (int id : tab_ids) {
    Found found = lookup(id);
    if (!found.tab) {
      error = fail("tabs.missing", "tab " + std::to_string(id) + " does not exist");
      return -1;
    }
    if (found.tab->group_id != -1) {
      error = fail("group.duplicate", "tab " + std::to_string(id) +
                                          " is already grouped; ungroup it first");
      return -1;
    }
    if (window_id == -1) {
      window_id = found.tab->window_id;
      pinned = found.tab->pinned;
    } else if (found.tab->window_id != window_id) {
      error = fail("group.window-split", "a group never spans windows");
      return -1;
    } else if (found.tab->pinned != pinned) {
      error = fail("group.pin-split", "a group never straddles the pinned block");
      return -1;
    }
    members.push_back(found);
  }
  std::sort(members.begin(), members.end(),
            [](const Found &a, const Found &b) { return a.index < b.index; });
  WindowTabs &window = windows_.at(window_id);
  const size_t anchor = members.front().index;
  TabGroup group;
  group.id = next_group_id_++;
  group.window_id = window_id;
  group.name = name;
  group.color = color;
  groups_[group.id] = group;
  for (const Found &member : members) {
    auto pos = std::find(window.order.begin(), window.order.end(), member.tab->id);
    window.order.erase(pos);
    tabs_.at(member.tab->id).group_id = group.id;
  }
  for (size_t i = 0; i < members.size(); ++i) {
    window.order.insert(window.order.begin() + static_cast<ptrdiff_t>(anchor + i),
                        members[i].tab->id);
  }
  return group.id;
}

bool TabModel::ungroup(int group_id, Error &error) {
  error = Error{};
  auto git = groups_.find(group_id);
  if (git == groups_.end()) {
    error = fail("group.missing", "group " + std::to_string(group_id) + " does not exist");
    return false;
  }
  for (auto &[id, tab] : tabs_) {
    if (tab.group_id == group_id) tab.group_id = -1;
  }
  groups_.erase(git);
  return true;
}

bool TabModel::set_group(int group_id, const std::string &name, const std::string &color,
                         Error &error) {
  error = Error{};
  auto git = groups_.find(group_id);
  if (git == groups_.end()) {
    error = fail("group.missing", "group " + std::to_string(group_id) + " does not exist");
    return false;
  }
  git->second.name = name;
  git->second.color = color;
  return true;
}

int TabModel::reopen_last(Error &error) {
  error = Error{};
  if (closed_.empty()) {
    error = fail("tabs.closed-empty", "the recently-closed stack is empty");
    return -1;
  }
  const ClosedTab entry = closed_.back();
  auto wit = windows_.find(entry.window_id);
  if (wit == windows_.end()) {
    // Refused - and the entry stays on the stack. Nothing is silently dropped.
    error = fail("tabs.closed-window-missing",
                 "the window that tab lived in is gone; it stays remembered");
    return -1;
  }
  closed_.pop_back();
  WindowTabs &window = wit->second;
  Tab tab;
  tab.id = next_tab_id_++;
  tab.window_id = entry.window_id;
  tab.label = entry.label;
  tab.pinned = entry.pinned;
  tab.muted = entry.muted;
  tab.state = entry.state;

  size_t at = 0;
  bool rejoined = false;
  if (entry.group_id != -1) {
    auto git = groups_.find(entry.group_id);
    if (git != groups_.end() && git->second.window_id == entry.window_id) {
      // The group survived and still lives here: rejoin beside my old siblings
      // and adopt the family's pinnedness, whatever changed while I was gone.
      for (int id : window.order) {
        if (tabs_.at(id).group_id == entry.group_id) tab.pinned = tabs_.at(id).pinned;
      }
      size_t last_member = 0;
      for (size_t i = 0; i < window.order.size(); ++i) {
        if (tabs_.at(window.order[i]).group_id == entry.group_id) last_member = i;
      }
      at = last_member + 1;
      tab.group_id = entry.group_id;
      rejoined = true;
    }
  }
  if (!rejoined) {
    at = clamp_index(window, entry.index, tab.pinned);
    if (entry.group_id != -1) {
      // The group is gone: restore it from the snapshot under a fresh id. Old
      // ids are history; only the name and colour travel back.
      TabGroup group;
      group.id = next_group_id_++;
      group.window_id = entry.window_id;
      group.name = entry.group_name;
      group.color = entry.group_color;
      groups_[group.id] = group;
      tab.group_id = group.id;
    }
  }
  tabs_[tab.id] = tab;
  window.order.insert(window.order.begin() + static_cast<ptrdiff_t>(at), tab.id);
  assign_active(window, tab.id); // reopening activates, so a sleeper wakes
  return tab.id;
}

std::vector<int> TabModel::order(int window_id) const {
  auto it = windows_.find(window_id);
  return it == windows_.end() ? std::vector<int>{} : it->second.order;
}

const Tab *TabModel::find(int tab_id) const {
  auto it = tabs_.find(tab_id);
  return it == tabs_.end() ? nullptr : &it->second;
}

const TabGroup *TabModel::find_group(int group_id) const {
  auto it = groups_.find(group_id);
  return it == groups_.end() ? nullptr : &it->second;
}

} // namespace pv::rt
