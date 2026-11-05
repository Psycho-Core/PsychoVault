// PsychoVault original source. My window model - the broker-side rules behind
// every real window: geometry, state, focus, DPI, monitor placement, taskbar
// identity. P1/S024 of docs/Psycho_MASTER-PLAN.md.
//
// Boundary discipline as before: this file keeps the decisions portable and
// testable; the Win32 handle work lives in Psycho_window_win32.cpp and is
// proven on the first authorized Windows build.
#pragma once
#include "../../engine/Psycho_base.hpp"
#include <functional>
#include <map>
#include <optional>
#include <string>
#include <vector>

namespace pv::rt {

struct WinRect {
  int x = 0, y = 0, w = 0, h = 0;
  bool operator==(const WinRect &) const = default;
};

enum class WindowState { Normal, Minimized, Maximized, Fullscreen };

struct WindowRecord {
  int id = -1;
  std::string title;
  WinRect bounds;
  // Where I return when leaving maximised or fullscreen. Normal-state geometry
  // is preserved here so restore never guesses.
  WinRect restore;
  WindowState state = WindowState::Normal;
  std::string monitor_id;      // stable name of the monitor I live on
  double dpi_scale = 1.0;      // 1.0 == 96 DPI
  std::string taskbar_id;      // my AppUserModelID-derived identity for the taskbar
  bool focused = false;
};

// The decisions the broker needs from me, none of them a handle.
class WindowRules {
public:
  struct Monitor {
    std::string id;
    WinRect work_area; // physical pixel space the window must stay inside
  };

  struct Seams {
    // Monitor inventory comes from the platform; tests hand me a fake one.
    std::function<std::vector<Monitor>()> monitors;
    // Every accepted decision is applied through this seam to the real window.
    std::function<void(const WindowRecord &)> apply;
  };

  explicit WindowRules(Seams seams);

  int create(const std::string &title, WinRect bounds, std::string &error);
  void destroy(int id);
  bool move_resize(int id, WinRect bounds);
  bool focus(int id);
  bool minimize(int id);
  bool maximize(int id);
  bool fullscreen(int id);
  bool restore(int id);
  void dpi_changed(int id, double scale);
  const WindowRecord *find(int id) const;
  const std::vector<WindowRecord> &all() const { return windows_; }

  std::string taskbar_identity() const { return taskbar_id_; }

private:
  WindowRecord *found(int id);
  WinRect clamped(WinRect bounds, const std::string &monitor_id) const;
  Monitor monitor_for(const std::string &id, WinRect near_rect) const;
  void push(const WindowRecord &record);

  Seams seams_;
  std::vector<WindowRecord> windows_;
  int next_id_ = 1;
  std::string taskbar_id_;
  static constexpr int kMinWidth = 320, kMinHeight = 200;
};

// The Windows halves (declared everywhere, defined only in the Windows build):
// taskbar identity before the first window, then create/apply/destroy of the
// real OS window driven by an accepted WindowRecord.
bool windows_taskbar_identity(std::string &error);
intptr_t windows_window_create(const WindowRecord &record, std::string &error);
bool windows_window_apply(intptr_t hwnd, const WindowRecord &record, std::string &error);
void windows_window_destroy(intptr_t hwnd);

} // namespace pv::rt
