// PsychoVault original source. The window decisions: what a window is allowed
// to be, where it is allowed to sit, and who holds focus. Handles come later;
// these rules stay identical.
#include "Psycho_window.hpp"
#include <cstdlib>

namespace pv::rt {

WindowRules::WindowRules(Seams seams) : seams_(std::move(seams)) {
  // One stable taskbar identity for the whole runtime; Windows groups and pins
  // by it. Assigned here so every window record carries it.
  taskbar_id_ = "PsychoCore.PsychoVault.Browser";
}

WindowRecord *WindowRules::found(int id) {
  for (auto &window : windows_)
    if (window.id == id)
      return &window;
  return nullptr;
}

const WindowRecord *WindowRules::find(int id) const {
  for (const auto &window : windows_)
    if (window.id == id)
      return &window;
  return nullptr;
}

WindowRules::Monitor WindowRules::monitor_for(const std::string &id, WinRect near_rect) const {
  std::vector<Monitor> monitors =
      seams_.monitors ? seams_.monitors() : std::vector<Monitor>{{"primary", {0, 0, 1920, 1040}}};
  if (monitors.empty())
    monitors.push_back({"primary", {0, 0, 1920, 1040}});
  if (!id.empty())
    for (const Monitor &monitor : monitors)
      if (monitor.id == id)
        return monitor;
  // No id: the monitor whose work area holds the rectangle's centre wins; on a
  // tie the first declared keeps it, so the choice never flickers.
  long cx = near_rect.x + near_rect.w / 2, cy = near_rect.y + near_rect.h / 2;
  for (const Monitor &monitor : monitors) {
    const WinRect &w = monitor.work_area;
    if (cx >= w.x && cx < w.x + w.w && cy >= w.y && cy < w.y + w.h)
      return monitor;
  }
  return monitors.front();
}

WinRect WindowRules::clamped(WinRect bounds, const std::string &monitor_id) const {
  if (bounds.w < kMinWidth)
    bounds.w = kMinWidth;
  if (bounds.h < kMinHeight)
    bounds.h = kMinHeight;
  const Monitor monitor = monitor_for(monitor_id, bounds);
  const WinRect work = monitor.work_area;
  if (bounds.w > work.w)
    bounds.w = work.w;
  if (bounds.h > work.h)
    bounds.h = work.h;
  // The whole window stays reachable: never a corner past the work area edges.
  if (bounds.x < work.x)
    bounds.x = work.x;
  if (bounds.y < work.y)
    bounds.y = work.y;
  if (bounds.x + bounds.w > work.x + work.w)
    bounds.x = work.x + work.w - bounds.w;
  if (bounds.y + bounds.h > work.y + work.h)
    bounds.y = work.y + work.h - bounds.h;
  return bounds;
}

void WindowRules::push(const WindowRecord &record) {
  if (seams_.apply)
    seams_.apply(record);
}

int WindowRules::create(const std::string &title, WinRect bounds, std::string &error) {
  WindowRecord record;
  record.id = next_id_++;
  record.title = title;
  record.bounds = clamped(bounds, "");
  record.restore = record.bounds;
  record.monitor_id = monitor_for("", record.bounds).id;
  record.taskbar_id = taskbar_id_;
  windows_.push_back(record);
  error.clear();
  push(windows_.back());
  return record.id;
}

void WindowRules::destroy(int id) {
  for (size_t i = 0; i < windows_.size(); ++i)
    if (windows_[i].id == id) {
      bool was_focused = windows_[i].focused;
      windows_.erase(windows_.begin() + long(i));
      if (was_focused && !windows_.empty()) {
        // Focus is a decision, not an accident: when the focused window dies I
        // choose the most recently created survivor and push it myself.
        windows_.back().focused = true;
        push(windows_.back());
      }
      return;
    }
}

bool WindowRules::move_resize(int id, WinRect bounds) {
  WindowRecord *record = found(id);
  if (!record || record->state != WindowState::Normal)
    return false; // geometry belongs to the state until restore
  // Resolve the destination monitor first - a drag across the edge must be able
  // to change monitors - then clamp inside that monitor so the window always
  // lands somewhere its whole frame stays reachable.
  std::string destination = monitor_for("", bounds).id;
  record->bounds = clamped(bounds, destination);
  record->monitor_id = monitor_for(destination, record->bounds).id;
  record->restore = record->bounds;
  push(*record);
  return true;
}

bool WindowRules::focus(int id) {
  WindowRecord *record = found(id);
  if (!record || record->state == WindowState::Minimized)
    return false; // a minimised window takes focus by restoring, not by asking
  for (auto &window : windows_)
    window.focused = (window.id == id);
  push(*record);
  return true;
}

bool WindowRules::minimize(int id) {
  WindowRecord *record = found(id);
  if (!record)
    return false;
  record->state = WindowState::Minimized;
  record->focused = false;
  push(*record);
  return true;
}

bool WindowRules::maximize(int id) {
  WindowRecord *record = found(id);
  if (!record)
    return false;
  if (record->state == WindowState::Normal)
    record->restore = record->bounds;
  record->state = WindowState::Maximized;
  record->bounds = monitor_for(record->monitor_id, record->bounds).work_area;
  push(*record);
  return true;
}

bool WindowRules::fullscreen(int id) {
  WindowRecord *record = found(id);
  if (!record)
    return false;
  if (record->state == WindowState::Normal)
    record->restore = record->bounds;
  record->state = WindowState::Fullscreen;
  record->bounds = monitor_for(record->monitor_id, record->bounds).work_area;
  push(*record);
  return true;
}

bool WindowRules::restore(int id) {
  WindowRecord *record = found(id);
  if (!record)
    return false;
  record->state = WindowState::Normal;
  record->bounds = clamped(record->restore, record->monitor_id);
  push(*record);
  return true;
}

void WindowRules::dpi_changed(int id, double scale) {
  WindowRecord *record = found(id);
  if (!record || scale <= 0)
    return;
  record->dpi_scale = scale;
  push(*record);
}

} // namespace pv::rt
