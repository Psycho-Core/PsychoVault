// PsychoVault original source. See the header for the five laws - this file
// is how I keep them.
#include "Psycho_focus.hpp"

namespace pv::rt {

namespace {

Error refuse(const char *code, const std::string &message) {
  Error error;
  error.code = code;
  error.message = message;
  return error;
}

} // namespace

void FocusService::emit(FocusEvent::Op op, FocusEvent::Level level, int id,
                        const std::string &element) {
  if (!seams_.on_event) return; // no listener: the state law already moved
  FocusEvent event;
  event.op = op;
  event.level = level;
  event.id = id;
  event.element = element;
  seams_.on_event(event);
}

bool FocusService::window_opened(int window_id, Error &error) {
  error = Error{};
  if (windows_.count(window_id)) {
    error = refuse("focus.duplicate", "window already registered");
    return false;
  }
  windows_[window_id] = WindowState{};
  window_order_.push_back(window_id);
  // A new window activates; law 2's blur rides along. The first window hears
  // no blur because there was nothing to blur.
  if (active_window_ != -1) {
    emit(FocusEvent::Op::Blur, FocusEvent::Level::Window, active_window_);
  }
  active_window_ = window_id;
  emit(FocusEvent::Op::Focus, FocusEvent::Level::Window, window_id);
  return true;
}

bool FocusService::window_closed(int window_id, Error &error) {
  error = Error{};
  if (!windows_.count(window_id)) {
    error = refuse("focus.unknown", "closing a window I never registered");
    return false;
  }
  // Everything the window owned dies with it: tabs, element focus, modality.
  for (int tab : tabs_[window_id]) {
    tab_window_.erase(tab);
    element_focus_.erase(tab);
  }
  tabs_.erase(window_id);
  windows_.erase(window_id);
  for (auto it = window_order_.begin(); it != window_order_.end(); ++it) {
    if (*it == window_id) {
      window_order_.erase(it);
      break;
    }
  }
  if (active_window_ != window_id) return true; // a background death is quiet
  // Law 4's fallback: the most recently opened remaining window, or nothing.
  emit(FocusEvent::Op::Blur, FocusEvent::Level::Window, window_id);
  active_window_ = window_order_.empty() ? -1 : window_order_.back();
  if (active_window_ != -1) {
    emit(FocusEvent::Op::Focus, FocusEvent::Level::Window, active_window_);
  }
  return true;
}

bool FocusService::tab_opened(int window_id, int tab_id, bool activate,
                              Error &error) {
  error = Error{};
  auto win = windows_.find(window_id);
  if (win == windows_.end()) {
    error = refuse("focus.unknown", "opening a tab in a window I never registered");
    return false;
  }
  if (tab_window_.count(tab_id)) {
    error = refuse("focus.duplicate", "tab already registered");
    return false;
  }
  if (activate && win->second.modal) {
    error = refuse("focus.modal", "a modal dialog captures this window");
    return false;
  }
  tab_window_[tab_id] = window_id;
  tabs_[window_id].push_back(tab_id);
  if (!activate) return true; // a background tab hears nothing
  if (win->second.focused_tab != -1) {
    emit(FocusEvent::Op::Blur, FocusEvent::Level::Tab, win->second.focused_tab);
  }
  win->second.focused_tab = tab_id;
  emit(FocusEvent::Op::Focus, FocusEvent::Level::Tab, tab_id);
  return true;
}

bool FocusService::tab_closed(int window_id, int tab_id, Error &error) {
  error = Error{};
  auto win = windows_.find(window_id);
  if (win == windows_.end() || !tab_window_.count(tab_id) ||
      tab_window_[tab_id] != window_id) {
    error = refuse("focus.unknown", "closing a tab I never registered here");
    return false;
  }
  const bool was_focused = win->second.focused_tab == tab_id;
  if (was_focused) emit(FocusEvent::Op::Blur, FocusEvent::Level::Tab, tab_id);
  // Death is not an event: a dying tab's focused element hears no blur.
  element_focus_.erase(tab_id);
  tab_window_.erase(tab_id);
  auto &order = tabs_[window_id];
  size_t at = 0;
  for (; at < order.size(); ++at)
    if (order[at] == tab_id) break;
  order.erase(order.begin() + (ptrdiff_t)at);
  if (!was_focused) return true;
  // Law 4's fallback: the tab after the closed one in open order, else the
  // one before, else nothing focused.
  int next = -1;
  if (at < order.size()) next = order[at];
  else if (!order.empty()) next = order.back();
  win->second.focused_tab = next;
  if (next != -1) emit(FocusEvent::Op::Focus, FocusEvent::Level::Tab, next);
  return true;
}

bool FocusService::focus_window(int window_id, Error &error) {
  error = Error{};
  if (!windows_.count(window_id)) {
    error = refuse("focus.unknown", "focusing a window I never registered");
    return false;
  }
  if (active_window_ == window_id) return true; // already holds it: silence
  emit(FocusEvent::Op::Blur, FocusEvent::Level::Window, active_window_);
  active_window_ = window_id;
  emit(FocusEvent::Op::Focus, FocusEvent::Level::Window, window_id);
  return true;
}

bool FocusService::focus_tab(int window_id, int tab_id, Error &error) {
  error = Error{};
  auto win = windows_.find(window_id);
  if (win == windows_.end() || !tab_window_.count(tab_id) ||
      tab_window_[tab_id] != window_id) {
    error = refuse("focus.unknown", "focusing a tab I never registered here");
    return false;
  }
  if (win->second.modal) {
    error = refuse("focus.modal", "a modal dialog captures this window");
    return false;
  }
  if (win->second.focused_tab == tab_id) return true; // law 2's silence
  if (win->second.focused_tab != -1) {
    emit(FocusEvent::Op::Blur, FocusEvent::Level::Tab, win->second.focused_tab);
  }
  win->second.focused_tab = tab_id;
  emit(FocusEvent::Op::Focus, FocusEvent::Level::Tab, tab_id);
  return true;
}

bool FocusService::focus_element(int tab_id, const std::string &element_id,
                                 Error &error) {
  error = Error{};
  auto owner = tab_window_.find(tab_id);
  if (owner == tab_window_.end()) {
    error = refuse("focus.unknown", "focusing an element of a tab I never registered");
    return false;
  }
  if (element_id.empty()) {
    error = refuse("focus.arg", "an element needs a name");
    return false;
  }
  const WindowState &win = windows_[owner->second];
  if (win.modal) {
    error = refuse("focus.modal", "a modal dialog captures this window");
    return false;
  }
  if (win.focused_tab != tab_id) {
    error = refuse("focus.inactive",
                   "a background tab's document cannot take focus");
    return false;
  }
  std::string &held = element_focus_[tab_id];
  if (held == element_id) return true; // law 2's silence
  if (!held.empty()) {
    emit(FocusEvent::Op::Blur, FocusEvent::Level::Element, tab_id, held);
  }
  held = element_id;
  emit(FocusEvent::Op::Focus, FocusEvent::Level::Element, tab_id, element_id);
  return true;
}

bool FocusService::blur_element(int tab_id, Error &error) {
  error = Error{};
  if (!tab_window_.count(tab_id)) {
    error = refuse("focus.unknown", "blurring a tab I never registered");
    return false;
  }
  std::string &held = element_focus_[tab_id];
  if (held.empty()) return true; // the body already holds it
  emit(FocusEvent::Op::Blur, FocusEvent::Level::Element, tab_id, held);
  held.clear();
  return true;
}

bool FocusService::modal_open(int window_id, Error &error) {
  error = Error{};
  auto win = windows_.find(window_id);
  if (win == windows_.end()) {
    error = refuse("focus.unknown", "a modal over a window I never registered");
    return false;
  }
  if (win->second.modal) {
    error = refuse("focus.modal", "this window is already captured");
    return false;
  }
  win->second.modal = true;
  return true;
}

bool FocusService::modal_close(int window_id, Error &error) {
  error = Error{};
  auto win = windows_.find(window_id);
  if (win == windows_.end()) {
    error = refuse("focus.unknown", "closing a modal over a window I never registered");
    return false;
  }
  if (!win->second.modal) {
    error = refuse("focus.modal", "no modal dialog holds this window");
    return false;
  }
  win->second.modal = false;
  return true;
}

bool FocusService::set_occluded(int window_id, bool occluded, Error &error) {
  error = Error{};
  auto win = windows_.find(window_id);
  if (win == windows_.end()) {
    error = refuse("focus.unknown", "occluding a window I never registered");
    return false;
  }
  win->second.occluded = occluded;
  return true;
}

int FocusService::focused_tab(int window_id) const {
  auto win = windows_.find(window_id);
  return win == windows_.end() ? -1 : win->second.focused_tab;
}

std::string FocusService::focused_element(int tab_id) const {
  auto it = element_focus_.find(tab_id);
  return it == element_focus_.end() ? "" : it->second;
}

bool FocusService::is_modal(int window_id) const {
  auto win = windows_.find(window_id);
  return win != windows_.end() && win->second.modal;
}

bool FocusService::has_page_focus(int tab_id) const {
  auto owner = tab_window_.find(tab_id);
  if (owner == tab_window_.end()) return false;
  const WindowState &win = windows_.at(owner->second);
  return active_window_ == owner->second && !win.occluded &&
         win.focused_tab == tab_id && !focused_element(tab_id).empty();
}

PageVisibility FocusService::visibility_of(int tab_id) const {
  auto owner = tab_window_.find(tab_id);
  if (owner == tab_window_.end()) return PageVisibility::Hidden; // law 1
  const WindowState &win = windows_.at(owner->second);
  if (win.focused_tab != tab_id) return PageVisibility::Hidden;
  if (win.occluded) return PageVisibility::Hidden;
  return active_window_ == owner->second ? PageVisibility::Active
                                         : PageVisibility::Visible;
}

bool FocusService::timer_floor(int tab_id, int &floor_ms, Error &error) const {
  error = Error{};
  if (!tab_window_.count(tab_id)) {
    error = refuse("focus.unknown", "throttling a tab I never registered");
    return false;
  }
  switch (visibility_of(tab_id)) {
  case PageVisibility::Active: floor_ms = kActiveFloorMs; break;
  case PageVisibility::Visible: floor_ms = kVisibleFloorMs; break;
  case PageVisibility::Hidden: floor_ms = kHiddenFloorMs; break;
  }
  return true;
}

} // namespace pv::rt
