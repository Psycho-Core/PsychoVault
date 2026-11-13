#include "Psycho_image.hpp"
#include "Psycho_raster.hpp"
#include <cstring>
#ifdef _WIN32
#define NOMINMAX
#include <windows.h>
#include <d2d1.h>
#include <d2d1helper.h>
#include <dwrite.h>
#include <wrl/client.h>
#endif
namespace pv {
static bool rounded_contains(Rect rect, double radius, double x, double y) {
  if (!rect.contains(x, y)) return false;
  radius = std::clamp(radius, 0.0, std::min(rect.w, rect.h) / 2.0);
  if (radius <= 0) return true;
  const double cx = x < rect.x + radius ? rect.x + radius
                    : x > rect.x + rect.w - radius ? rect.x + rect.w - radius : x;
  const double cy = y < rect.y + radius ? rect.y + radius
                    : y > rect.y + rect.h - radius ? rect.y + rect.h - radius : y;
  const double dx = x - cx, dy = y - cy;
  return dx * dx + dy * dy <= radius * radius;
}
uint32_t blend(uint32_t d, uint32_t s) {
  uint32_t a = s >> 24;
  if (!a)
    return d;
  if (a == 255)
    return s;
  uint32_t ia = 255 - a;
  uint32_t r = ((s >> 16 & 255) * a + (d >> 16 & 255) * ia) / 255,
           g = ((s >> 8 & 255) * a + (d >> 8 & 255) * ia) / 255,
           b = ((s & 255) * a + (d & 255) * ia) / 255;
  return 0xff000000 | (r << 16) | (g << 8) | b;
}
void Canvas::polygon(const std::vector<Point> &points, uint32_t c) {
  if (points.size() < 3)
    return;
  double top = points[0].y, bottom = points[0].y;
  for (const auto &p : points) {
    top = std::min(top, p.y);
    bottom = std::max(bottom, p.y);
  }
  int y0 = std::clamp(int(std::floor(std::max(top, clip.y))), 0, height);
  int y1 = std::clamp(int(std::ceil(std::min(bottom, clip.y + clip.h))), 0, height);
  std::vector<double> crossings;
  for (int y = y0; y < y1; ++y) {
    double scan = y + 0.5;
    crossings.clear();
    for (size_t i = 0; i < points.size(); ++i) {
      const Point &a = points[i], &b = points[(i + 1) % points.size()];
      if (a.y == b.y)
        continue;
      double low = std::min(a.y, b.y), high = std::max(a.y, b.y);
      if (scan < low || scan >= high)
        continue;
      crossings.push_back(a.x + (scan - a.y) * (b.x - a.x) / (b.y - a.y));
    }
    if (crossings.size() < 2)
      continue;
    std::sort(crossings.begin(), crossings.end());
    for (size_t i = 0; i + 1 < crossings.size(); i += 2) {
      double left = std::max(crossings[i], clip.x), right = std::min(crossings[i + 1], clip.x + clip.w);
      int x0 = std::clamp(int(std::ceil(left - 0.5)), 0, width);
      int x1 = std::clamp(int(std::ceil(right - 0.5)), 0, width);
      for (int x = x0; x < x1; ++x)
        pixel(x, y, c);
    }
  }
}
void Canvas::polyline(const std::vector<Point> &points, uint32_t c, double width, bool closed) {
  if (points.empty())
    return;
  double half = std::max(0.5, width / 2);
  size_t count = closed ? points.size() : points.size() - 1;
  for (size_t i = 0; i < count; ++i) {
    const Point &a = points[i], &b = points[(i + 1) % points.size()];
    double dx = b.x - a.x, dy = b.y - a.y;
    double length = std::sqrt(dx * dx + dy * dy);
    if (length < 0.0001) {
      pixel(int(a.x), int(a.y), c);
      continue;
    }
    // I walk the segment and stamp a small square, which keeps a stroke solid
    // without needing a second rasteriser.
    int steps = int(length) + 1;
    for (int s = 0; s <= steps; ++s) {
      double t = double(s) / steps;
      double px = a.x + dx * t, py = a.y + dy * t;
      for (int oy = int(-half); oy <= int(half); ++oy)
        for (int ox = int(-half); ox <= int(half); ++ox)
          pixel(int(px) + ox, int(py) + oy, c);
    }
  }
}
void Canvas::circle(double cx, double cy, double radius, uint32_t c) {
  ellipse(cx, cy, radius, radius, c);
}
void Canvas::ellipse(double cx, double cy, double rx, double ry, uint32_t c) {
  if (rx <= 0 || ry <= 0)
    return;
  int y0 = std::clamp(int(std::floor(cy - ry)), 0, height);
  int y1 = std::clamp(int(std::ceil(cy + ry)), 0, height);
  for (int y = y0; y < y1; ++y) {
    double dy = (y + 0.5 - cy) / ry;
    if (dy < -1 || dy > 1)
      continue;
    double half = rx * std::sqrt(std::max(0.0, 1 - dy * dy));
    int x0 = std::clamp(int(std::ceil(cx - half - 0.5)), 0, width);
    int x1 = std::clamp(int(std::ceil(cx + half - 0.5)), 0, width);
    for (int x = x0; x < x1; ++x)
      pixel(x, y, c);
  }
}
uint32_t gradient_shade(const Gradient &gradient, double x, double y) {
  if (gradient.stops.empty())
    return 0x00000000u;
  double t = 0;
  if (gradient.radial) {
    double dx = x - gradient.x0, dy = y - gradient.y0;
    double distance = std::sqrt(dx * dx + dy * dy);
    double span = gradient.r1 - gradient.r0;
    t = span > 0.0001 ? (distance - gradient.r0) / span : 0;
  } else {
    double dx = gradient.x1 - gradient.x0, dy = gradient.y1 - gradient.y0;
    double length = dx * dx + dy * dy;
    t = length > 0.0001 ? ((x - gradient.x0) * dx + (y - gradient.y0) * dy) / length : 0;
  }
  t = std::clamp(t, 0.0, 1.0);
  if (gradient.stops.size() == 1 || t <= gradient.stops.front().at)
    return gradient.stops.front().color;
  if (t >= gradient.stops.back().at)
    return gradient.stops.back().color;
  for (size_t i = 1; i < gradient.stops.size(); ++i) {
    const GradientStop &right = gradient.stops[i];
    if (t > right.at)
      continue;
    const GradientStop &left = gradient.stops[i - 1];
    double span = right.at - left.at;
    double f = span > 0.0001 ? (t - left.at) / span : 0;
    auto mix = [&](int shift) {
      double a = double((left.color >> shift) & 255), b = double((right.color >> shift) & 255);
      return uint32_t(a + (b - a) * f);
    };
    return (mix(24) << 24) | (mix(16) << 16) | (mix(8) << 8) | mix(0);
  }
  return gradient.stops.back().color;
}
void Canvas::fill_gradient(Rect r, const Gradient &gradient, double radius) {
  int x0 = std::clamp(int(std::floor(std::max(r.x, clip.x))), 0, width),
      y0 = std::clamp(int(std::floor(std::max(r.y, clip.y))), 0, height),
      x1 = std::clamp(int(std::ceil(std::min(r.x + r.w, clip.x + clip.w))), 0, width),
      y1 = std::clamp(int(std::ceil(std::min(r.y + r.h, clip.y + clip.h))), 0, height);
  for (int y = y0; y < y1; ++y)
    for (int x = x0; x < x1; ++x)
      if (rounded_contains(r, radius, x + 0.5, y + 0.5))
        pixel(x, y, gradient_shade(gradient, x + 0.5, y + 0.5));
}
Canvas::Canvas(int w, int h, uint32_t color)
    : width(std::clamp(w, 1, 8192)), height(std::clamp(h, 1, 16384)),
      pixels(size_t(width) * height * 4), clip{0, 0, double(width), double(height)} {
  for (size_t i = 0; i < pixels.size(); i += 4) {
    pixels[i] = color;
    pixels[i + 1] = color >> 8;
    pixels[i + 2] = color >> 16;
    pixels[i + 3] = color >> 24;
  }
}
static bool path_clip_contains(const Canvas::PathClip &clip, double x, double y) {
  int winding = 0;
  bool parity = false;
  for (const auto &contour : clip.contours) {
    if (contour.size() < 3) continue;
    for (size_t i = 0; i < contour.size(); ++i) {
      const Point &a = contour[i], &b = contour[(i + 1) % contour.size()];
      if ((a.y > y) == (b.y > y)) continue;
      const double cross_x = a.x + (y - a.y) * (b.x - a.x) / (b.y - a.y);
      if (cross_x <= x) continue;
      parity = !parity;
      winding += b.y > a.y ? 1 : -1;
    }
  }
  return clip.even_odd ? parity : winding != 0;
}
void Canvas::pixel(int x, int y, uint32_t c) {
  if (x < 0 || y < 0 || x >= width || y >= height || !clip.contains(x + 0.5, y + 0.5))
    return;
  for (const auto &[rect, radius] : rounded_clips)
    if (!rounded_contains(rect, radius, x + 0.5, y + 0.5)) return;
  for (const PathClip &path_clip : path_clips)
    if (!path_clip_contains(path_clip, x + 0.5, y + 0.5)) return;
  const size_t i = (size_t(y) * width + x) * 4;
  const uint32_t sa = c >> 24;
  const uint32_t da = pixels[i + 3];
  const uint32_t inverse = 255 - sa;
  const uint32_t oa = sa + (da * inverse + 127) / 255;
  if (oa == 0) { pixels[i] = pixels[i + 1] = pixels[i + 2] = pixels[i + 3] = 0; return; }
  auto channel = [&](uint32_t source, uint32_t destination) {
    const uint32_t premultiplied = source * sa + (destination * da * inverse + 127) / 255;
    return (premultiplied + oa / 2) / oa;
  };
  pixels[i] = uint8_t(channel(c & 255, pixels[i]));
  pixels[i + 1] = uint8_t(channel((c >> 8) & 255, pixels[i + 1]));
  pixels[i + 2] = uint8_t(channel((c >> 16) & 255, pixels[i + 2]));
  pixels[i + 3] = uint8_t(oa);
}
void Canvas::clear_pixel(int x, int y) {
  if (x < 0 || y < 0 || x >= width || y >= height || !clip.contains(x + 0.5, y + 0.5))
    return;
  for (const auto &[rect, radius] : rounded_clips)
    if (!rounded_contains(rect, radius, x + 0.5, y + 0.5)) return;
  for (const PathClip &path_clip : path_clips)
    if (!path_clip_contains(path_clip, x + 0.5, y + 0.5)) return;
  const size_t i = (size_t(y) * width + x) * 4;
  pixels[i] = pixels[i + 1] = pixels[i + 2] = pixels[i + 3] = 0;
}
void Canvas::fill(Rect r, uint32_t c) {
  int x0 = std::clamp(int(std::floor(std::max(r.x, clip.x))), 0, width),
      y0 = std::clamp(int(std::floor(std::max(r.y, clip.y))), 0, height),
      x1 = std::clamp(int(std::ceil(std::min(r.x + r.w, clip.x + clip.w))), 0, width),
      y1 = std::clamp(int(std::ceil(std::min(r.y + r.h, clip.y + clip.h))), 0, height);
  if ((c >> 24) == 255 && rounded_clips.empty() && path_clips.empty()) {
    for (int y = y0; y < y1; ++y) {
      uint8_t *row = pixels.data() + (size_t(y) * width + x0) * 4;
      for (int x = x0; x < x1; ++x) {
        row[0] = c;
        row[1] = c >> 8;
        row[2] = c >> 16;
        row[3] = c >> 24;
        row += 4;
      }
    }
  } else
    for (int y = y0; y < y1; ++y)
      for (int x = x0; x < x1; ++x)
        pixel(x, y, c);
}
void Canvas::fill_rounded(Rect r, double radius, uint32_t c) {
  const int x0 = std::clamp(int(std::floor(std::max(r.x, clip.x))), 0, width);
  const int y0 = std::clamp(int(std::floor(std::max(r.y, clip.y))), 0, height);
  const int x1 = std::clamp(int(std::ceil(std::min(r.x + r.w, clip.x + clip.w))), 0, width);
  const int y1 = std::clamp(int(std::ceil(std::min(r.y + r.h, clip.y + clip.h))), 0, height);
  for (int y = y0; y < y1; ++y)
    for (int x = x0; x < x1; ++x)
      if (rounded_contains(r, radius, x + 0.5, y + 0.5)) pixel(x, y, c);
}
void Canvas::stroke(Rect r, uint32_t c, double t) {
  fill({r.x, r.y, r.w, t}, c);
  fill({r.x, r.y + r.h - t, r.w, t}, c);
  fill({r.x, r.y, t, r.h}, c);
  fill({r.x + r.w - t, r.y, t, r.h}, c);
}
void Canvas::stroke_rounded(Rect r, double radius, uint32_t c, double thickness) {
  thickness = std::clamp(thickness, 0.0, std::min(r.w, r.h) / 2.0);
  if (thickness <= 0) return;
  const int x0 = std::clamp(int(std::floor(std::max(r.x, clip.x))), 0, width);
  const int y0 = std::clamp(int(std::floor(std::max(r.y, clip.y))), 0, height);
  const int x1 = std::clamp(int(std::ceil(std::min(r.x + r.w, clip.x + clip.w))), 0, width);
  const int y1 = std::clamp(int(std::ceil(std::min(r.y + r.h, clip.y + clip.h))), 0, height);
  Rect inner{r.x + thickness, r.y + thickness,
             std::max(0.0, r.w - thickness * 2), std::max(0.0, r.h - thickness * 2)};
  for (int y = y0; y < y1; ++y)
    for (int x = x0; x < x1; ++x) {
      const double px = x + 0.5, py = y + 0.5;
      if (rounded_contains(r, radius, px, py) &&
          !rounded_contains(inner, std::max(0.0, radius - thickness), px, py)) pixel(x, y, c);
    }
}
void Canvas::box_shadow(Rect r, double radius, double offset_x, double offset_y,
                        double blur, double spread, uint32_t color) {
  if ((color >> 24) == 0) return;
  blur = std::clamp(blur, 0.0, 64.0);
  spread = std::clamp(spread, -64.0, 64.0);
  const Rect shifted{r.x + offset_x, r.y + offset_y, r.w, r.h};
  const int rings = blur > 0 ? 6 : 1;
  for (int ring = rings; ring >= 1; --ring) {
    const double extent = spread + blur * double(ring) / rings;
    Rect shape{shifted.x - extent, shifted.y - extent,
               std::max(0.0, shifted.w + extent * 2), std::max(0.0, shifted.h + extent * 2)};
    uint32_t alpha = color >> 24;
    if (blur > 0) alpha = uint32_t(std::max(1.0, alpha * (0.08 + 0.045 * (rings - ring))));
    const uint32_t tinted = (color & 0x00ffffffu) | (alpha << 24);
    fill_rounded(shape, std::max(0.0, radius + extent), tinted);
  }
}
void Canvas::line(double x1, double y1, double x2, double y2, uint32_t c, double t) {
  int steps = int(std::max(std::fabs(x2 - x1), std::fabs(y2 - y1))) + 1;
  if (steps > 16384)
    return;
  for (int i = 0; i <= steps; ++i) {
    double f = double(i) / steps;
    fill({x1 + (x2 - x1) * f, y1 + (y2 - y1) * f, t, t}, c);
  }
}
#ifdef _WIN32
namespace {
using Microsoft::WRL::ComPtr;

static std::wstring wide_text(std::string_view text) {
  std::wstring result;
  for (uint32_t cp : utf8_decode(text)) {
    if (cp <= 0xffff && !(cp >= 0xd800 && cp <= 0xdfff))
      result.push_back(wchar_t(cp));
    else if (cp <= 0x10ffff) {
      cp -= 0x10000;
      result.push_back(wchar_t(0xd800 + (cp >> 10)));
      result.push_back(wchar_t(0xdc00 + (cp & 0x3ff)));
    } else
      result.push_back(wchar_t(0xfffd));
  }
  return result;
}

static std::wstring first_installed_family(std::string_view css_stack,
                                           IDWriteFontCollection *collection) {
  std::wstring stack = wide_text(css_stack.empty() ? "Segoe UI" : css_stack);
  size_t start = 0;
  while (start < stack.size()) {
    size_t end = stack.find(L',', start);
    if (end == std::wstring::npos)
      end = stack.size();
    std::wstring candidate = stack.substr(start, end - start);
    auto first = candidate.find_first_not_of(L" \t\r\n\"'");
    auto last = candidate.find_last_not_of(L" \t\r\n\"'");
    if (first != std::wstring::npos)
      candidate = candidate.substr(first, last - first + 1);
    else
      candidate.clear();
    if (candidate == L"system-ui" || candidate == L"-apple-system")
      candidate = L"Segoe UI";
    if (!candidate.empty()) {
      UINT32 index = 0;
      BOOL exists = FALSE;
      if (SUCCEEDED(collection->FindFamilyName(candidate.c_str(), &index, &exists)) && exists) {
        ComPtr<IDWriteFontFamily> family;
        ComPtr<IDWriteLocalizedStrings> names;
        WCHAR locale[LOCALE_NAME_MAX_LENGTH]{};
        int locale_length = GetUserDefaultLocaleName(locale, LOCALE_NAME_MAX_LENGTH);
        if (SUCCEEDED(collection->GetFontFamily(index, &family)) &&
            SUCCEEDED(family->GetFamilyNames(&names)) && names && names->GetCount() > 0) {
          UINT32 name_index = 0;
          BOOL locale_exists = FALSE;
          if (locale_length > 0)
            names->FindLocaleName(locale, &name_index, &locale_exists);
          if (!locale_exists)
            name_index = 0;
          UINT32 length = 0;
          if (SUCCEEDED(names->GetStringLength(name_index, &length))) {
            std::wstring canonical(size_t(length) + 1, L'\0');
            if (SUCCEEDED(names->GetString(name_index, canonical.data(), length + 1))) {
              canonical.resize(length);
              return canonical;
            }
          }
        }
        return candidate;
      }
    }
    start = end + 1;
  }
  return L"Segoe UI";
}

struct DirectWriteContext {
  ComPtr<IDWriteFactory> write_factory;
  ComPtr<ID2D1Factory> d2d_factory;
  ComPtr<IDWriteFontCollection> font_collection;
  ComPtr<ID2D1DCRenderTarget> render_target;
  std::map<std::wstring, ComPtr<IDWriteTextFormat>> formats;

  bool ready() {
    if (!write_factory && FAILED(DWriteCreateFactory(
                                DWRITE_FACTORY_TYPE_SHARED, __uuidof(IDWriteFactory),
                                reinterpret_cast<IUnknown **>(write_factory.GetAddressOf()))))
      return false;
    if (!font_collection && FAILED(write_factory->GetSystemFontCollection(&font_collection, FALSE)))
      return false;
    if (!d2d_factory && FAILED(D2D1CreateFactory(D2D1_FACTORY_TYPE_SINGLE_THREADED,
                                                 d2d_factory.GetAddressOf())))
      return false;
    if (!render_target) {
      D2D1_RENDER_TARGET_PROPERTIES properties{};
      properties.type = D2D1_RENDER_TARGET_TYPE_DEFAULT;
      properties.pixelFormat.format = DXGI_FORMAT_B8G8R8A8_UNORM;
      properties.pixelFormat.alphaMode = D2D1_ALPHA_MODE_PREMULTIPLIED;
      properties.dpiX = 96.0f;
      properties.dpiY = 96.0f;
      if (FAILED(d2d_factory->CreateDCRenderTarget(&properties, &render_target)))
        return false;
    }
    return true;
  }

  IDWriteTextFormat *format(std::string_view family, double size, bool bold) {
    if (!ready())
      return nullptr;
    double clamped_size = std::clamp(size, 1.0, 512.0);
    std::wstring selected = first_installed_family(family, font_collection.Get());
    std::wstring key = selected + L"|" + std::to_wstring(int(std::lround(clamped_size * 64))) +
                       (bold ? L"|bold" : L"|regular");
    auto found = formats.find(key);
    if (found != formats.end())
      return found->second.Get();
    if (formats.size() >= 128)
      formats.clear();
    ComPtr<IDWriteTextFormat> created;
    HRESULT hr = write_factory->CreateTextFormat(
        selected.c_str(), font_collection.Get(), bold ? DWRITE_FONT_WEIGHT_BOLD : DWRITE_FONT_WEIGHT_NORMAL,
        DWRITE_FONT_STYLE_NORMAL, DWRITE_FONT_STRETCH_NORMAL, FLOAT(clamped_size), L"en-us", &created);
    if (FAILED(hr) || !created)
      return nullptr;
    created->SetWordWrapping(DWRITE_WORD_WRAPPING_NO_WRAP);
    auto [it, inserted] = formats.emplace(std::move(key), std::move(created));
    (void)inserted;
    return it->second.Get();
  }

  bool layout(std::string_view text, std::string_view family, double size, bool bold,
              ComPtr<IDWriteTextLayout> &out, DWRITE_TEXT_METRICS *metrics = nullptr,
              DWRITE_OVERHANG_METRICS *overhang = nullptr) {
    if (!ready() || text.size() > UINT32_MAX)
      return false;
    std::wstring wide = wide_text(text);
    ComPtr<IDWriteTextFormat> text_format;
    IDWriteTextFormat *cached = format(family, size, bold);
    if (!cached)
      return false;
    // The format cache is thread-local and is not cleared while a call is using it.
    text_format = cached;
    HRESULT hr = write_factory->CreateTextLayout(wide.data(), UINT32(wide.size()), text_format.Get(),
                                                  32768.0f, 32768.0f, &out);
    if (FAILED(hr) || !out)
      return false;
    if (metrics && FAILED(out->GetMetrics(metrics)))
      return false;
    if (overhang && FAILED(out->GetOverhangMetrics(overhang)))
      *overhang = {};
    return true;
  }
};

static DirectWriteContext &direct_write() {
  thread_local DirectWriteContext context;
  return context;
}

struct DibTarget {
  HDC dc = nullptr;
  HBITMAP bitmap = nullptr;
  HGDIOBJ previous = nullptr;
  void *bits = nullptr;
  ~DibTarget() {
    if (dc && previous)
      SelectObject(dc, previous);
    if (bitmap)
      DeleteObject(bitmap);
    if (dc)
      DeleteDC(dc);
  }
};

} // namespace
#endif

#ifndef _WIN32
static double portable_text_width(std::string_view text, double size) {
  double em = std::clamp(size, 1.0, 512.0), width = 0;
  for (uint32_t cp : utf8_decode(text)) {
    if (cp == '\n' || cp == '\r')
      continue;
    if ((cp >= 0x0300 && cp <= 0x036f) || (cp >= 0x1ab0 && cp <= 0x1aff))
      continue;
    if (cp == ' ' || cp == '\t')
      width += em * 0.33;
    else if (cp == 'i' || cp == 'l' || cp == '!' || cp == '|' || cp == '.' || cp == ',' ||
             cp == ':' || cp == ';' || cp == '\'' || cp == '`')
      width += em * 0.30;
    else if (cp == 'W' || cp == 'M' || cp == '@' || cp == '%' || cp == '&')
      width += em * 0.88;
    else if (cp >= 0x2e80)
      width += em;
    else
      width += em * 0.55;
  }
  return width;
}
#endif

double Canvas::text_width(std::string_view text, double size, bool bold, std::string_view family) const {
#ifdef _WIN32
  auto &context = direct_write();
  ComPtr<IDWriteTextLayout> layout;
  DWRITE_TEXT_METRICS metrics{};
  if (context.layout(text, family, size, bold, layout, &metrics))
    return metrics.widthIncludingTrailingWhitespace;
  return 0;
#else
  (void)bold;
  (void)family;
  // Portable advances support layout/unit tests only; this build does not ship
  // or rasterize a substitute font. Windows UI text uses DirectWrite metrics.
  return portable_text_width(text, size);
#endif
}

void Canvas::text(double x, double y, std::string_view text, double size, uint32_t color, bool bold,
                  std::string_view family) {
#ifdef _WIN32
  if (text.empty())
    return;
  auto &context = direct_write();
  ComPtr<IDWriteTextLayout> layout;
  DWRITE_TEXT_METRICS metrics{};
  DWRITE_OVERHANG_METRICS overhang{};
  if (!context.layout(text, family, size, bold, layout, &metrics, &overhang) || !context.render_target)
    return;
  int left = std::max(0, int(std::ceil(overhang.left))) + 2;
  int top = std::max(0, int(std::ceil(overhang.top))) + 2;
  int width = std::clamp(int(std::ceil(metrics.widthIncludingTrailingWhitespace +
                                      std::max(0.0f, overhang.left) +
                                      std::max(0.0f, overhang.right))) + left + 2,
                         1, 8192);
  int height = std::clamp(int(std::ceil(metrics.height + std::max(0.0f, overhang.top) +
                                       std::max(0.0f, overhang.bottom))) + top + 2,
                          1, 8192);
  DibTarget dib;
  dib.dc = CreateCompatibleDC(nullptr);
  if (!dib.dc)
    return;
  BITMAPINFO info{};
  info.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
  info.bmiHeader.biWidth = width;
  info.bmiHeader.biHeight = -height;
  info.bmiHeader.biPlanes = 1;
  info.bmiHeader.biBitCount = 32;
  info.bmiHeader.biCompression = BI_RGB;
  dib.bitmap = CreateDIBSection(dib.dc, &info, DIB_RGB_COLORS, &dib.bits, nullptr, 0);
  if (!dib.bitmap || !dib.bits)
    return;
  dib.previous = SelectObject(dib.dc, dib.bitmap);
  RECT bounds{0, 0, width, height};
  if (FAILED(context.render_target->BindDC(dib.dc, &bounds)))
    return;
  ComPtr<ID2D1SolidColorBrush> brush;
  if (FAILED(context.render_target->CreateSolidColorBrush(D2D1::ColorF(D2D1::ColorF::White), &brush)))
    return;
  context.render_target->SetTextAntialiasMode(D2D1_TEXT_ANTIALIAS_MODE_GRAYSCALE);
  context.render_target->BeginDraw();
  context.render_target->Clear(D2D1::ColorF(0, 0.0f));
  context.render_target->DrawTextLayout(D2D1::Point2F(FLOAT(left), FLOAT(top)), layout.Get(), brush.Get(),
                                        D2D1_DRAW_TEXT_OPTIONS_NONE);
  HRESULT draw_result = context.render_target->EndDraw();
  if (draw_result == D2DERR_RECREATE_TARGET) {
    context.render_target.Reset();
    return;
  }
  if (FAILED(draw_result))
    return;
  const auto *pixels = static_cast<const uint8_t *>(dib.bits);
  for (int row = 0; row < height; ++row)
    for (int col = 0; col < width; ++col) {
      uint8_t coverage = pixels[(size_t(row) * size_t(width) + size_t(col)) * 4 + 3];
      if (coverage)
        pixel(int(x) + col - left, int(y) + row - top,
              (((uint32_t(coverage) * (color >> 24) + 127) / 255) << 24) |
                  (color & 0x00ffffff));
    }
#else
  // A non-Windows build deliberately has no bitmap-font fallback: it retains
  // approximate advances for layout tests but does not rasterize UI glyphs.
  (void)x;
  (void)y;
  (void)text;
  (void)size;
  (void)color;
  (void)bold;
  (void)family;
#endif
}
static void be32(Bytes &out, uint32_t n) {
  out.push_back(n >> 24);
  out.push_back(n >> 16);
  out.push_back(n >> 8);
  out.push_back(n);
}
static void chunk(Bytes &out, const char *type, const Bytes &data) {
  be32(out, uint32_t(data.size()));
  size_t start = out.size();
  out.insert(out.end(), type, type + 4);
  out.insert(out.end(), data.begin(), data.end());
  be32(out, crc32(out.data() + start, out.size() - start));
}
void Canvas::image(const Image &source, Rect destination, double radius) {
  if (!source.valid() || destination.w <= 0 || destination.h <= 0)
    return;
  double left = std::max(destination.x, clip.x), top = std::max(destination.y, clip.y);
  double right = std::min(destination.x + destination.w, clip.x + clip.w);
  double bottom = std::min(destination.y + destination.h, clip.y + clip.h);
  if (right <= left || bottom <= top)
    return;
  int x0 = std::max(0, int(std::ceil(left))), y0 = std::max(0, int(std::ceil(top)));
  int x1 = std::min(width, int(std::floor(right))), y1 = std::min(height, int(std::floor(bottom)));
  for (int y = y0; y < y1; ++y) {
    int sy = std::clamp(int((y + 0.5 - destination.y) * source.height / destination.h), 0,
                        source.height - 1);
    for (int x = x0; x < x1; ++x) {
      if (!rounded_contains(destination, radius, x + 0.5, y + 0.5)) continue;
      int sx = std::clamp(int((x + 0.5 - destination.x) * source.width / destination.w), 0,
                          source.width - 1);
      size_t from = (size_t(sy) * size_t(source.width) + size_t(sx)) * 4;
      uint32_t color = uint32_t(source.pixels[from]) | uint32_t(source.pixels[from + 1]) << 8 |
                       uint32_t(source.pixels[from + 2]) << 16 |
                       uint32_t(source.pixels[from + 3]) << 24;
      pixel(x, y, color);
    }
  }
}
Bytes encode_png(int w, int h, const Bytes &bgra) {
  if (w <= 0 || h <= 0 || w > 8192 || h > 16384 || bgra.size() != size_t(w) * h * 4)
    throw std::runtime_error("invalid PNG surface");
  Bytes raw;
  raw.reserve(size_t(h) * (size_t(w) * 4 + 1));
  for (int y = 0; y < h; ++y) {
    raw.push_back(0);
    const uint8_t *row = bgra.data() + size_t(y) * w * 4;
    for (int x = 0; x < w; ++x) {
      raw.push_back(row[x * 4 + 2]);
      raw.push_back(row[x * 4 + 1]);
      raw.push_back(row[x * 4]);
      raw.push_back(row[x * 4 + 3]);
    }
  }
  Bytes z = {0x78, 0x01};
  size_t p = 0;
  do {
    size_t n = std::min(size_t(65535), raw.size() - p);
    bool last = p + n == raw.size();
    z.push_back(uint8_t(last ? 1 : 0));
    z.push_back(uint8_t(n & 255));
    z.push_back(uint8_t(n >> 8));
    z.push_back(uint8_t(~n & 255));
    z.push_back(uint8_t((~n >> 8) & 255));
    z.insert(z.end(), raw.begin() + p, raw.begin() + p + n);
    p += n;
  } while (p < raw.size());
  be32(z, adler32(raw.data(), raw.size()));
  Bytes out = {137, 80, 78, 71, 13, 10, 26, 10};
  Bytes ihdr;
  be32(ihdr, w);
  be32(ihdr, h);
  ihdr.insert(ihdr.end(), {8, 6, 0, 0, 0});
  chunk(out, "IHDR", ihdr);
  chunk(out, "IDAT", z);
  chunk(out, "IEND", {});
  return out;
}
} // namespace pv
