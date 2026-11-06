// PsychoVault original source. The Windows half of my window model: real
// windows - registration, creation, geometry, state, taskbar identity. The
// decisions all happened already in Psycho_window.cpp; this file is only the
// hands that apply them. P1/S024.
//
// Windows-only by construction (empty translation unit elsewhere), proven on
// the first authorized manual Windows build like every platform envelope here.
#include "Psycho_window.hpp"

#ifdef _WIN32

#include <windows.h>
#include <shobjidl.h>
#include <shellscalingapi.h>

namespace pv::rt {
namespace {

std::wstring widen(const std::string &text) {
  if (text.empty())
    return L"";
  const int wide = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, text.c_str(), -1,
                                       nullptr, 0);
  if (wide <= 0)
    return {};
  std::wstring buffer(size_t(wide), L'\0');
  if (MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, text.c_str(), -1,
                          buffer.data(), wide) != wide)
    return {};
  buffer.resize(size_t(wide - 1));
  return buffer;
}

std::string win_error(const char *what) {
  return std::string(what) + " failed: " + std::to_string(GetLastError());
}

// My runtime's window class is mine: my WndProc, my styles, my identity. The
// message pump lands with S025's presentation work; this class owns creation,
// geometry and state transitions.
const wchar_t *window_class_name() { return L"PsychoVaultRuntimeWindow"; }

LRESULT CALLBACK runtime_wndproc(HWND window, UINT message, WPARAM w_param, LPARAM l_param) {
  switch (message) {
  case WM_DESTROY:
    // The broker's rule object owns the tab count and quit logic; here I only
    // let the default path keep running.
    break;
  case WM_DPICHANGED: {
    // WM_DPICHANGED hands me the suggested rectangle; the rules object decides
    // whether the move is accepted, so the platform window obeys the model.
    const RECT *suggested = reinterpret_cast<const RECT *>(l_param);
    if (suggested)
      SetWindowPos(window, nullptr, suggested->left, suggested->top,
                   suggested->right - suggested->left, suggested->bottom - suggested->top,
                   SWP_NOZORDER | SWP_NOACTIVATE);
    return 0;
  }
  }
  return DefWindowProcW(window, message, w_param, l_param);
}

} // namespace

// Taskbar identity: set once per process, before the first window, so grouping
// and pinning use my AppUserModelID and never the exe name's default.
bool windows_taskbar_identity(std::string &error) {
  if (FAILED(SetCurrentProcessExplicitAppUserModelID(L"PsychoCore.PsychoVault.Browser"))) {
    error = "SetCurrentProcessExplicitAppUserModelID failed";
    return false;
  }
  return true;
}

// Create (or reuse) my registered window class, then make a real window whose
// geometry/state come straight from the accepted WindowRecord.
intptr_t windows_window_create(const WindowRecord &record, std::string &error) {
  HINSTANCE instance = GetModuleHandleW(nullptr);
  WNDCLASSEXW registration{};
  registration.cbSize = sizeof(registration);
  registration.lpfnWndProc = runtime_wndproc;
  registration.hInstance = instance;
  registration.lpszClassName = window_class_name();
  registration.hCursor = LoadCursor(nullptr, IDC_ARROW);
  registration.hbrBackground = reinterpret_cast<HBRUSH>(COLOR_WINDOW + 1);
  if (!RegisterClassExW(&registration) && GetLastError() != ERROR_CLASS_ALREADY_EXISTS) {
    error = win_error("RegisterClassExW");
    return 0;
  }
  HWND window = CreateWindowExW(WS_EX_APPWINDOW, window_class_name(), widen(record.title).c_str(),
                                WS_OVERLAPPEDWINDOW | WS_VISIBLE | WS_CLIPCHILDREN,
                                record.bounds.x, record.bounds.y, record.bounds.w,
                                record.bounds.h, nullptr, nullptr, instance, nullptr);
  if (!window) {
    error = win_error("CreateWindowExW");
    return 0;
  }
  int command = SW_SHOWNORMAL;
  if (record.state == WindowState::Minimized)
    command = SW_MINIMIZE;
  else if (record.state == WindowState::Maximized || record.state == WindowState::Fullscreen)
    command = SW_SHOWMAXIMIZED;
  ShowWindow(window, command);
  return reinterpret_cast<intptr_t>(window);
}

bool windows_window_apply(intptr_t hwnd, const WindowRecord &record, std::string &error) {
  HWND window = reinterpret_cast<HWND>(hwnd);
  if (!window) {
    error = "window.handle.missing";
    return false;
  }
  switch (record.state) {
  case WindowState::Normal:
    ShowWindow(window, SW_RESTORE);
    break;
  case WindowState::Minimized:
    ShowWindow(window, SW_MINIMIZE);
    return true; // minimised windows keep geometry untouched by design
  case WindowState::Maximized:
  case WindowState::Fullscreen:
    ShowWindow(window, SW_SHOWMAXIMIZED);
    break;
  }
  if (record.state == WindowState::Normal &&
      !SetWindowPos(window, nullptr, record.bounds.x, record.bounds.y, record.bounds.w,
                    record.bounds.h, SWP_NOZORDER | SWP_NOACTIVATE)) {
    error = win_error("SetWindowPos");
    return false;
  }
  SetWindowTextW(window, widen(record.title).c_str());
  return true;
}

void windows_window_destroy(intptr_t hwnd) {
  HWND window = reinterpret_cast<HWND>(hwnd);
  if (window)
    DestroyWindow(window);
}

} // namespace pv::rt

#endif // _WIN32
