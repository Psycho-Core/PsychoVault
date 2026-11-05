// PsychoVault original source. The Windows half of presentation: my surface
// becomes a DIB and lands on the window's DC - the only file that ever touches
// a window handle with pixels. P1/S025.
//
// Windows-only by construction (empty translation unit elsewhere), proven on
// the first authorized manual Windows build.
#include "Psycho_surface.hpp"
#include "../window/Psycho_window.hpp"

#ifdef _WIN32

#include <windows.h>

namespace pv::rt {

// Present one composite target to one window. Bits go through a DIB section
// because that is the honest path: no page code anywhere near the window.
bool windows_present(intptr_t hwnd, const Surface &target, std::string &error) {
  HWND window = reinterpret_cast<HWND>(hwnd);
  if (!window) {
    error = "present.handle.missing";
    return false;
  }
  if (target.pixels.size() != size_t(target.width) * size_t(target.height) * 4) {
    error = "present.target.malformed";
    return false;
  }
  BITMAPINFO info{};
  info.bmiHeader.biSize = sizeof(info.bmiHeader);
  info.bmiHeader.biWidth = target.width;
  info.bmiHeader.biHeight = -target.height; // top-down, so my row order survives
  info.bmiHeader.biPlanes = 1;
  info.bmiHeader.biBitCount = 32;
  info.bmiHeader.biCompression = BI_RGB;

  HDC window_dc = GetDC(window);
  if (!window_dc) {
    error = "GetDC failed";
    return false;
  }
  HDC frame_dc = CreateCompatibleDC(window_dc);
  void *bits = nullptr;
  HBITMAP bitmap = CreateDIBSection(frame_dc, &info, DIB_RGB_COLORS, &bits, nullptr, 0);
  if (!frame_dc || !bitmap) {
    error = "CreateDIBSection failed";
    if (bitmap)
      DeleteObject(bitmap);
    if (frame_dc)
      DeleteDC(frame_dc);
    ReleaseDC(window, window_dc);
    return false;
  }
  memcpy(bits, target.pixels.data(), target.pixels.size());
  HGDIOBJ previous = SelectObject(frame_dc, bitmap);
  BOOL ok = BitBlt(window_dc, 0, 0, target.width, target.height, frame_dc, 0, 0, SRCCOPY);
  SelectObject(frame_dc, previous);
  DeleteObject(bitmap);
  DeleteDC(frame_dc);
  ReleaseDC(window, window_dc);
  if (!ok) {
    error = "BitBlt failed";
    return false;
  }
  return true;
}

} // namespace pv::rt

#endif // _WIN32
