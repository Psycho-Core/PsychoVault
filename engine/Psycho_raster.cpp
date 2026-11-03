#include "Psycho_image.hpp"
#include "Psycho_raster.hpp"
#include "Psycho_font5x7.hpp"
#include <cstring>
#ifdef _WIN32
#define NOMINMAX
#include <windows.h>
#endif
namespace pv {
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
void Canvas::fill_gradient(Rect r, const Gradient &gradient) {
  int x0 = std::clamp(int(std::floor(std::max(r.x, clip.x))), 0, width),
      y0 = std::clamp(int(std::floor(std::max(r.y, clip.y))), 0, height),
      x1 = std::clamp(int(std::ceil(std::min(r.x + r.w, clip.x + clip.w))), 0, width),
      y1 = std::clamp(int(std::ceil(std::min(r.y + r.h, clip.y + clip.h))), 0, height);
  for (int y = y0; y < y1; ++y)
    for (int x = x0; x < x1; ++x)
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
void Canvas::pixel(int x, int y, uint32_t c) {
  if (x < 0 || y < 0 || x >= width || y >= height || !clip.contains(x + 0.5, y + 0.5))
    return;
  size_t i = (size_t(y) * width + x) * 4;
  uint32_t d =
      0xff000000 | (uint32_t(pixels[i + 2]) << 16) | (uint32_t(pixels[i + 1]) << 8) | pixels[i];
  uint32_t o = blend(d, c);
  pixels[i] = o;
  pixels[i + 1] = o >> 8;
  pixels[i + 2] = o >> 16;
  pixels[i + 3] = o >> 24;
}
void Canvas::fill(Rect r, uint32_t c) {
  int x0 = std::clamp(int(std::floor(std::max(r.x, clip.x))), 0, width),
      y0 = std::clamp(int(std::floor(std::max(r.y, clip.y))), 0, height),
      x1 = std::clamp(int(std::ceil(std::min(r.x + r.w, clip.x + clip.w))), 0, width),
      y1 = std::clamp(int(std::ceil(std::min(r.y + r.h, clip.y + clip.h))), 0, height);
  if ((c >> 24) == 255) {
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
void Canvas::stroke(Rect r, uint32_t c, double t) {
  fill({r.x, r.y, r.w, t}, c);
  fill({r.x, r.y + r.h - t, r.w, t}, c);
  fill({r.x, r.y, t, r.h}, c);
  fill({r.x + r.w - t, r.y, t, r.h}, c);
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
struct WinFont {
  HDC dc = nullptr;
  HFONT font = nullptr;
  std::wstring key;
  ~WinFont() {
    if (font) {
      SelectObject(dc, old);
      DeleteObject(font);
    }
    if (dc)
      DeleteDC(dc);
  }
  HGDIOBJ old = nullptr;
  void select(double size, bool bold, std::string_view family) {
    std::wstring fam;
    for (auto c : utf8_decode(family))
      fam += wchar_t(c);
    std::wstring k = fam + L"|" + std::to_wstring(int(size * 10)) + L"|" + (bold ? L"1" : L"0");
    if (!dc)
      dc = CreateCompatibleDC(nullptr);
    if (k == key && font)
      return;
    HFONT f = CreateFontW(-int(size), 0, 0, 0, bold ? FW_BOLD : FW_NORMAL, FALSE, FALSE, FALSE,
                          DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
                          CLEARTYPE_QUALITY, DEFAULT_PITCH, fam.c_str());
    if (!font) {
      old = SelectObject(dc, f);
      font = f;
    } else {
      SelectObject(dc, f);
      DeleteObject(font);
      font = f;
    }
    key = k;
  }
};
static std::wstring wide_text(std::string_view s) {
  std::wstring w;
  for (auto c : utf8_decode(s))
    w += wchar_t(c);
  return w;
}
} // namespace
#endif
double Canvas::text_width(std::string_view s, double size, bool bold,
                          std::string_view family) const {
#ifdef _WIN32
  thread_local WinFont cache;
  cache.select(size, bold, family);
  auto w = wide_text(s);
  SIZE sz{};
  GetTextExtentPoint32W(cache.dc, w.c_str(), int(w.size()), &sz);
  return sz.cx;
#else
  (void)bold;
  (void)family;
  return utf8_decode(s).size() * size * 0.6;
#endif
}
void Canvas::text(double x, double y, std::string_view text, double size, uint32_t color, bool bold,
                  std::string_view family) {
#ifdef _WIN32
  auto w = wide_text(text);
  if (w.empty())
    return;
  thread_local WinFont measure;
  measure.select(size, bold, family);
  SIZE sz{};
  GetTextExtentPoint32W(measure.dc, w.c_str(), int(w.size()), &sz);
  int tw = std::max(1, int(sz.cx) + 4), th = std::max(1, int(size * 2) + 4);
  HDC dc = CreateCompatibleDC(nullptr);
  BITMAPINFO bi{};
  bi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
  bi.bmiHeader.biWidth = tw;
  bi.bmiHeader.biHeight = -th;
  bi.bmiHeader.biPlanes = 1;
  bi.bmiHeader.biBitCount = 32;
  bi.bmiHeader.biCompression = BI_RGB;
  void *bits = nullptr;
  HBITMAP bmp = CreateDIBSection(dc, &bi, DIB_RGB_COLORS, &bits, nullptr, 0);
  if (!bmp || !bits) {
    if (bmp)
      DeleteObject(bmp);
    DeleteDC(dc);
    return;
  }
  std::memset(bits, 0, size_t(tw) * th * 4);
  HGDIOBJ oldbmp = SelectObject(dc, bmp);
  HFONT font = CreateFontW(-int(size), 0, 0, 0, bold ? FW_BOLD : FW_NORMAL, FALSE, FALSE, FALSE,
                           DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
                           CLEARTYPE_QUALITY, DEFAULT_PITCH, wide_text(family).c_str());
  HGDIOBJ oldfont = SelectObject(dc, font);
  SetBkMode(dc, TRANSPARENT);
  SetTextColor(dc, RGB(255, 255, 255));
  TextOutW(dc, 0, 0, w.c_str(), int(w.size()));
  GdiFlush();
  uint8_t *p = static_cast<uint8_t *>(bits);
  for (int row = 0; row < th; ++row)
    for (int col = 0; col < tw; ++col) {
      uint8_t intensity = p[(size_t(row) * tw + col) * 4];
      if (intensity)
        pixel(int(x) + col, int(y) + row, (uint32_t(intensity) << 24) | (color & 0x00ffffff));
    }
  SelectObject(dc, oldfont);
  DeleteObject(font);
  SelectObject(dc, oldbmp);
  DeleteObject(bmp);
  DeleteDC(dc);
#else
  (void)family;
  auto cps = utf8_decode(text);
  double scale = std::max(0.5, size / 10.0), advance = scale * 6, start = x;
  for (auto c : cps) {
    if (c == '\n') {
      y += size * 1.3;
      x = start;
      continue;
    }
    auto g = glyph5x7(c);
    for (int row = 0; row < 7; ++row)
      for (int col = 0; col < 5; ++col)
        if (g[row] & (1 << (4 - col)))
          fill({x + col * scale, y + row * scale, scale + (bold ? 0.5 : 0), scale}, color);
    x += advance;
  }
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
void Canvas::image(const Image &source, Rect destination) {
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
