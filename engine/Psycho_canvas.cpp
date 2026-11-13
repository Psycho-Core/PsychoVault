// PsychoVault original source. The Canvas 2D drawing surface. I implement it on
// top of my own raster so a page, an image and a canvas all share one renderer.
#include "Psycho_canvas.hpp"
#include <algorithm>
#include <cmath>
#include <cstring>

namespace pv {
namespace {
constexpr double kTau = 6.28318530717958647692;

double font_size_of(std::string_view font) {
  double size = 10;
  size_t at = 0;
  while (at < font.size()) {
    if (std::isdigit(static_cast<unsigned char>(font[at]))) {
      char *end = nullptr;
      std::string piece(font.substr(at));
      double value = std::strtod(piece.c_str(), &end);
      if (end && *end == 'p')
        return std::clamp(value, 1.0, 400.0);
      if (end && *end)
        at += size_t(end - piece.c_str());
      else
        break;
    } else
      ++at;
  }
  return size;
}
bool font_bold(std::string_view font) {
  return font.find("bold") != std::string_view::npos || font.find("700") != std::string_view::npos ||
         font.find("800") != std::string_view::npos || font.find("900") != std::string_view::npos;
}
} // namespace

Canvas2D::Canvas2D(int width, int height)
    : surface(std::clamp(width, 1, 8192), std::clamp(height, 1, 8192), 0x00000000) {}

void Canvas2D::resize(int width, int height) {
  width = std::clamp(width, 1, 8192);
  height = std::clamp(height, 1, 8192);
  ++revision;
  // Resizing resets the bitmap and the context's drawing state.
  surface = Canvas(width, height, 0x00000000);
  state = CanvasState{};
  path.clear();
  current.clear();
  saved.clear();
  dashes_.clear();
}

void Canvas2D::begin_path() {
  path.clear();
  current.clear();
}

void Canvas2D::flush_subpath() {
  if (current.size() > 1)
    path.push_back(current);
  current.clear();
}

void Canvas2D::move_to(double x, double y) {
  flush_subpath();
  push(map(x, y));
}

void Canvas2D::line_to(double x, double y) {
  push(map(x, y));
}

void Canvas2D::close_path() {
  if (!current.empty()) {
    const Point start = current.front();
    current.push_back(start);
    flush_subpath();
    current.push_back(start);
  }
}

void Canvas2D::add_rect(double x, double y, double w, double h) {
  flush_subpath();
  current.push_back(map(x, y));
  current.push_back(map(x + w, y));
  current.push_back(map(x + w, y + h));
  current.push_back(map(x, y + h));
  close_path();
}

void Canvas2D::add_arc(double cx, double cy, double r, double start, double end, bool clockwise) {
  if (!std::isfinite(r) || r < 0) return;
  add_ellipse(cx, cy, r, r, start, end, clockwise);
}

void Canvas2D::add_ellipse(double cx, double cy, double rx, double ry, double start, double end,
                           bool clockwise, double rotation) {
  if (!std::isfinite(cx) || !std::isfinite(cy) || !std::isfinite(rx) || !std::isfinite(ry) ||
      !std::isfinite(start) || !std::isfinite(end) || !std::isfinite(rotation) || rx < 0 || ry < 0)
    return;
  const double delta = end - start;
  if (!std::isfinite(delta)) return;
  double sweep;
  if (clockwise && delta >= kTau) sweep = kTau;
  else if (!clockwise && delta <= -kTau) sweep = -kTau;
  else {
    sweep = std::fmod(delta, kTau);
    if (clockwise && sweep < 0) sweep += kTau;
    if (!clockwise && sweep > 0) sweep -= kTau;
  }
  const int steps = std::clamp(int(std::fabs(sweep) * 8) + 4, 4, 360);
  const double cs = std::cos(rotation), sn = std::sin(rotation);
  for (int i = 0; i <= steps; ++i) {
    const double t = start + sweep * double(i) / steps;
    const double px = rx * std::cos(t), py = ry * std::sin(t);
    current.push_back(map(cx + px * cs - py * sn, cy + px * sn + py * cs));
  }
}

void Canvas2D::add_quadratic(double cx, double cy, double x, double y) {
  const Point control = map(cx, cy), end = map(x, y);
  if (current.empty()) push(control);
  const Point from = current.back();
  for (int step = 1; step <= 12; ++step) {
    double t = double(step) / 12, mt = 1 - t;
    push({mt * mt * from.x + 2 * mt * t * control.x + t * t * end.x,
          mt * mt * from.y + 2 * mt * t * control.y + t * t * end.y});
  }
}

void Canvas2D::add_bezier(double c1x, double c1y, double c2x, double c2y, double x, double y) {
  const Point c1 = map(c1x, c1y), c2 = map(c2x, c2y), end = map(x, y);
  if (current.empty()) push(c1);
  const Point from = current.back();
  for (int step = 1; step <= 16; ++step) {
    double t = double(step) / 16, mt = 1 - t;
    push({mt * mt * mt * from.x + 3 * mt * mt * t * c1.x + 3 * mt * t * t * c2.x + t * t * t * end.x,
          mt * mt * mt * from.y + 3 * mt * mt * t * c1.y + 3 * mt * t * t * c2.y + t * t * t * end.y});
  }
}

uint32_t Canvas2D::shade(const Gradient &gradient, double x, double y) const {
  // One shading routine serves my canvas and my page backgrounds.
  return gradient_shade(gradient, x, y);
}

void Canvas2D::fill_polygon(std::vector<Point> points, uint32_t color) {
  if (points.size() < 3)
    return;
  surface.polygon(points, color);
}

void Canvas2D::fill_polygon_gradient(std::vector<Point> points, const Gradient &gradient) {
  if (points.size() < 3)
    return;
  double top = points[0].y, bottom = points[0].y;
  for (auto &p : points) {
    top = std::min(top, p.y);
    bottom = std::max(bottom, p.y);
  }
  int y0 = std::clamp(int(std::floor(top)), 0, surface.height);
  int y1 = std::clamp(int(std::ceil(bottom)), 0, surface.height);
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
      int x0 = std::clamp(int(std::ceil(crossings[i] - 0.5)), 0, surface.width);
      int x1 = std::clamp(int(std::ceil(crossings[i + 1] - 0.5)), 0, surface.width);
      for (int x = x0; x < x1; ++x)
        surface.pixel(x, y, tint(shade(gradient, x + 0.5, y + 0.5)));
    }
  }
}

void Canvas2D::fill() {
  ++revision;
  Canvas::PathClip region;
  for (const auto &contour : path)
    if (contour.size() >= 3) region.contours.push_back(contour);
  if (current.size() >= 3) region.contours.push_back(current);
  if (region.contours.empty()) return;
  double left = surface.width, top = surface.height, right = 0, bottom = 0;
  for (const auto &contour : region.contours)
    for (const Point &point : contour) {
      left = std::min(left, point.x); top = std::min(top, point.y);
      right = std::max(right, point.x); bottom = std::max(bottom, point.y);
    }
  surface.path_clips.push_back(std::move(region));
  if (state.has_gradient)
    fill_polygon_gradient({{left, top}, {right, top}, {right, bottom}, {left, bottom}},
                          state.fill_gradient);
  else
    surface.fill({left, top, right - left, bottom - top}, tint(state.fill));
  surface.path_clips.pop_back();
}

void Canvas2D::stroke() {
  ++revision;
  const double transform_scale = std::sqrt(std::abs(state.a * state.d - state.b * state.c));
  double width = std::max(1.0, state.line_width * transform_scale);
  size_t dash_steps = 0;
  std::vector<const std::vector<Point> *> subpaths;
  for (const auto &subpath : path) subpaths.push_back(&subpath);
  if (!current.empty()) subpaths.push_back(&current);
  for (const auto *subpath : subpaths) {
    const auto &points = *subpath;
    if (points.size() < 2)
      continue;
    if (dashes_.empty()) {
      surface.polyline(points, tint(state.stroke), width, false);
      continue;
    }
    // A dash pattern walks the whole path and paints only the "on" lengths.
    size_t dash = 0;
    double left = dashes_[0];
    bool on = true;
    for (size_t i = 1; i < points.size(); ++i) {
      Point from = points[i - 1], to = points[i];
      double dx = to.x - from.x, dy = to.y - from.y;
      double length = std::sqrt(dx * dx + dy * dy);
      if (length <= 0)
        continue;
      double travelled = 0;
      while (travelled < length) {
        if (++dash_steps > 100000) return;
        double step = std::min(left, length - travelled);
        if (on) {
          Point a{from.x + dx * (travelled / length), from.y + dy * (travelled / length)};
          Point b{from.x + dx * ((travelled + step) / length),
                  from.y + dy * ((travelled + step) / length)};
          surface.polyline({a, b}, tint(state.stroke), width, false);
        }
        travelled += step;
        left -= step;
        if (left <= 1e-6) {
          dash = (dash + 1) % dashes_.size();
          left = dashes_[dash];
          on = !on;
        }
      }
    }
  }
}

void Canvas2D::clip(bool even_odd) {
  // Beyond the resource limit, fail closed rather than silently painting
  // outside an omitted clip. One empty sentinel preserves restore semantics.
  if (surface.path_clips.size() >= 65) return;
  Canvas::PathClip region;
  region.even_odd = even_odd;
  if (surface.path_clips.size() < 64) {
    for (const auto &contour : path)
      if (contour.size() >= 3) region.contours.push_back(contour);
    if (current.size() >= 3) region.contours.push_back(current);
  }
  surface.path_clips.push_back(std::move(region));
}

void Canvas2D::fill_rect(double x, double y, double w, double h) {
  ++revision;
  if (w < 0) { x += w; w = -w; }
  if (h < 0) { y += h; h = -h; }
  if (w == 0 || h == 0) return;
  if (state.a == 1 && state.b == 0 && state.c == 0 && state.d == 1) {
    Rect r{x + state.e, y + state.f, w, h};
    if (state.has_gradient) {
      fill_polygon_gradient({{r.x, r.y}, {r.x + r.w, r.y}, {r.x + r.w, r.y + r.h}, {r.x, r.y + r.h}},
                            state.fill_gradient);
    } else
      surface.fill(r, tint(state.fill));
    return;
  }
  std::vector<Point> corners{map(x, y), map(x + w, y), map(x + w, y + h), map(x, y + h)};
  if (state.has_gradient)
    fill_polygon_gradient(corners, state.fill_gradient);
  else
    fill_polygon(corners, tint(state.fill));
}

void Canvas2D::stroke_rect(double x, double y, double w, double h) {
  ++revision;
  std::vector<Point> corners{map(x, y), map(x + w, y), map(x + w, y + h), map(x, y + h)};
  const double scale = std::sqrt(std::abs(state.a * state.d - state.b * state.c));
  surface.polyline(corners, tint(state.stroke), std::max(1.0, state.line_width * scale), true);
}

void Canvas2D::clear_rect(double x, double y, double w, double h) {
  ++revision;
  if (w == 0 || h == 0 || std::abs(state.a * state.d - state.b * state.c) < 1e-12) return;
  const std::vector<Point> corners{map(x, y), map(x + w, y), map(x + w, y + h), map(x, y + h)};
  double min_x = corners[0].x, max_x = corners[0].x;
  double min_y = corners[0].y, max_y = corners[0].y;
  for (const Point &point : corners) {
    min_x = std::min(min_x, point.x); max_x = std::max(max_x, point.x);
    min_y = std::min(min_y, point.y); max_y = std::max(max_y, point.y);
  }
  const int x0 = std::clamp(int(std::floor(min_x)), 0, surface.width);
  const int y0 = std::clamp(int(std::floor(min_y)), 0, surface.height);
  const int x1 = std::clamp(int(std::ceil(max_x)), 0, surface.width);
  const int y1 = std::clamp(int(std::ceil(max_y)), 0, surface.height);
  for (int py = y0; py < y1; ++py)
    for (int px = x0; px < x1; ++px) {
      const double sx = px + 0.5, sy = py + 0.5;
      bool inside = true;
      double sign = 0;
      for (size_t i = 0; i < corners.size(); ++i) {
        const Point &a = corners[i], &b = corners[(i + 1) % corners.size()];
        const double cross = (b.x - a.x) * (sy - a.y) - (b.y - a.y) * (sx - a.x);
        if (std::abs(cross) <= 1e-9) continue;
        if (sign == 0) sign = cross;
        else if ((sign > 0) != (cross > 0)) { inside = false; break; }
      }
      if (inside) surface.clear_pixel(px, py);
    }
}

void Canvas2D::draw_text(double x, double y, std::string_view text, uint32_t color) {
  const double size = font_size_of(state.font);
  const double width = surface.text_width(text, size, font_bold(state.font));
  double left = x;
  if (state.text_align == "center") left -= width / 2;
  else if (state.text_align == "right" || state.text_align == "end") left -= width;
  double top = y;
  if (state.text_baseline == "alphabetic") top -= size * 0.8;
  else if (state.text_baseline == "hanging") top -= size * 0.2;
  else if (state.text_baseline == "middle") top -= size * 0.5;
  else if (state.text_baseline == "ideographic") top -= size * 0.9;
  else if (state.text_baseline == "bottom") top -= size;
  const int bitmap_width = std::clamp(int(std::ceil(width)) + 8, 1, 8192);
  const int bitmap_height = std::clamp(int(std::ceil(size * 2)) + 8, 1, 8192);
  Canvas glyphs(bitmap_width, bitmap_height, 0x00000000u);
  glyphs.text(4, 4, text, size, color, font_bold(state.font));
  Image image;
  image.width = bitmap_width;
  image.height = bitmap_height;
  image.pixels = std::move(glyphs.pixels);
  draw_image(image, left - 4, top - 4, bitmap_width, bitmap_height);
}

void Canvas2D::fill_text(double x, double y, std::string_view text) {
  ++revision;
  draw_text(x, y, text, state.fill);
}

void Canvas2D::stroke_text(double x, double y, std::string_view text) {
  ++revision;
  draw_text(x, y, text, state.stroke);
}

double Canvas2D::text_width(std::string_view text) const {
  return surface.text_width(text, font_size_of(state.font), font_bold(state.font));
}

void Canvas2D::draw_image(const Image &source, double x, double y, double w, double h) {
  ++revision;
  if (!source.valid() || w == 0 || h == 0 || state.global_alpha <= 0) return;
  const double determinant = state.a * state.d - state.b * state.c;
  if (std::abs(determinant) < 1e-12) return;
  const std::vector<Point> corners{map(x, y), map(x + w, y), map(x + w, y + h), map(x, y + h)};
  double min_x = corners[0].x, max_x = corners[0].x;
  double min_y = corners[0].y, max_y = corners[0].y;
  for (const Point &point : corners) {
    min_x = std::min(min_x, point.x); max_x = std::max(max_x, point.x);
    min_y = std::min(min_y, point.y); max_y = std::max(max_y, point.y);
  }
  const int x0 = std::clamp(int(std::floor(std::max(min_x, surface.clip.x))), 0, surface.width);
  const int y0 = std::clamp(int(std::floor(std::max(min_y, surface.clip.y))), 0, surface.height);
  const int x1 = std::clamp(int(std::ceil(std::min(max_x, surface.clip.x + surface.clip.w))), 0, surface.width);
  const int y1 = std::clamp(int(std::ceil(std::min(max_y, surface.clip.y + surface.clip.h))), 0, surface.height);
  for (int py = y0; py < y1; ++py)
    for (int px = x0; px < x1; ++px) {
      const double dx = px + 0.5 - state.e, dy = py + 0.5 - state.f;
      const double user_x = (state.d * dx - state.c * dy) / determinant;
      const double user_y = (-state.b * dx + state.a * dy) / determinant;
      const double u = (user_x - x) / w, v = (user_y - y) / h;
      if (u < 0 || u >= 1 || v < 0 || v >= 1) continue;
      const int sx = std::clamp(int(u * source.width), 0, source.width - 1);
      const int sy = std::clamp(int(v * source.height), 0, source.height - 1);
      const size_t offset = (size_t(sy) * source.width + sx) * 4;
      const uint32_t alpha = uint32_t(std::lround(source.pixels[offset + 3] * state.global_alpha));
      if (!alpha) continue;
      const uint32_t color = (alpha << 24) | (uint32_t(source.pixels[offset + 2]) << 16) |
          (uint32_t(source.pixels[offset + 1]) << 8) | source.pixels[offset];
      surface.pixel(px, py, color);
    }
}

void Canvas2D::draw_image_rect(const Image &source, double sx, double sy, double sw, double sh,
                               double x, double y, double w, double h) {
  ++revision;
  if (!source.valid() || !std::isfinite(sx) || !std::isfinite(sy) || !std::isfinite(sw) ||
      !std::isfinite(sh) || sw <= 0 || sh <= 0 || sw > 8192 || sh > 8192 ||
      sx < -8192 || sy < -8192 || sx > 16384 || sy > 16384 ||
      std::ceil(sw) * std::ceil(sh) * 4 > double(kMaxImageRawBytes))
    return;
  // I sample the source rectangle into a temporary image so the rest of my
  // pipeline only ever has to blit whole pictures.
  int ix = int(std::floor(sx)), iy = int(std::floor(sy));
  int iw = std::max(1, int(std::ceil(sw))), ih = std::max(1, int(std::ceil(sh)));
  Image piece;
  piece.width = iw;
  piece.height = ih;
  piece.pixels.assign(size_t(iw) * size_t(ih) * 4, 0);
  for (int row = 0; row < ih; ++row) {
    const int sample_y = iy + row;
    if (sample_y < 0 || sample_y >= source.height) continue;
    for (int column = 0; column < iw; ++column) {
      const int sample_x = ix + column;
      if (sample_x < 0 || sample_x >= source.width) continue;
      const uint8_t *from = &source.pixels[(size_t(sample_y) * source.width + sample_x) * 4];
      uint8_t *to = &piece.pixels[(size_t(row) * iw + column) * 4];
      to[0] = from[0];
      to[1] = from[1];
      to[2] = from[2];
      to[3] = from[3];
    }
  }
  draw_image(piece, x, y, w, h);
}

void Canvas2D::add_round_rect(double x, double y, double w, double h, double radius) {
  ++revision;
  radius = std::clamp(radius, 0.0, std::min(std::abs(w), std::abs(h)) / 2);
  double r = radius;
  if (r <= 0.5) {
    add_rect(x, y, w, h);
    return;
  }
  // I walk the four corners with short segments, which is enough for the
  // rounding to read as a curve at screen sizes.
  auto arc = [&](double cx, double cy, double from, double to) {
    for (int step = 0; step <= 6; ++step) {
      double angle = from + (to - from) * double(step) / 6.0;
      double px = cx + r * std::cos(angle), py = cy + r * std::sin(angle);
      if (current.empty())
        move_to(px, py);
      else
        line_to(px, py);
    }
  };
  move_to(x + r, y);
  line_to(x + w - r, y);
  arc(x + w - r, y + r, -1.5707963267948966, 0.0);
  line_to(x + w, y + h - r);
  arc(x + w - r, y + h - r, 0.0, 1.5707963267948966);
  line_to(x + r, y + h);
  arc(x + r, y + h - r, 1.5707963267948966, 3.141592653589793);
  line_to(x, y + r);
  arc(x + r, y + r, 3.141592653589793, 4.71238898038469);
  close_path();
}

void Canvas2D::set_line_dash(const std::vector<double> &dashes) {
  std::vector<double> next;
  double total = 0;
  for (double dash : dashes) {
    if (!std::isfinite(dash) || dash < 0) return;
    next.push_back(dash);
    total += dash;
  }
  if (total == 0) next.clear();
  else if (next.size() % 2) {
    const std::vector<double> repeated = next;
    next.insert(next.end(), repeated.begin(), repeated.end());
  }
  dashes_ = std::move(next);
}

void Canvas2D::draw_canvas(const Canvas2D &source, double x, double y, double w, double h) {
  Image copy;
  copy.width = source.width();
  copy.height = source.height();
  copy.pixels = source.backing().pixels;
  draw_image(copy, x, y, w, h);
}

Image Canvas2D::snapshot() const {
  Image image;
  image.width = surface.width;
  image.height = surface.height;
  image.pixels = surface.pixels;
  return image;
}

std::string Canvas2D::data_url() const {
  Bytes png = encode_png(surface.width, surface.height, surface.pixels);
  return "data:image/png;base64," + base64_encode(png);
}

std::vector<uint8_t> Canvas2D::get_pixels(int x, int y, int w, int h) const {
  w = std::clamp(w, 1, 8192);
  h = std::clamp(h, 1, 8192);
  if (size_t(w) * size_t(h) * 4 > kMaxImageRawBytes)
    throw std::runtime_error("canvas: getImageData asked for more pixels than I will copy");
  std::vector<uint8_t> out(size_t(w) * h * 4, 0);
  for (int row = 0; row < h; ++row)
    for (int column = 0; column < w; ++column) {
      int sx = x + column, sy = y + row;
      if (sx < 0 || sy < 0 || sx >= surface.width || sy >= surface.height)
        continue;
      size_t source = (size_t(sy) * surface.width + sx) * 4;
      size_t target = (size_t(row) * w + column) * 4;
      out[target] = surface.pixels[source + 2];
      out[target + 1] = surface.pixels[source + 1];
      out[target + 2] = surface.pixels[source];
      out[target + 3] = surface.pixels[source + 3];
    }
  return out;
}

void Canvas2D::put_pixels(int x, int y, int w, int h, const std::vector<uint8_t> &rgba) {
  ++revision;
  if (w <= 0 || h <= 0 || rgba.size() < size_t(w) * size_t(h) * 4)
    return;
  for (int row = 0; row < h; ++row)
    for (int column = 0; column < w; ++column) {
      int dx = x + column, dy = y + row;
      if (dx < 0 || dy < 0 || dx >= surface.width || dy >= surface.height)
        continue;
      size_t source = (size_t(row) * w + column) * 4;
      size_t target = (size_t(dy) * surface.width + dx) * 4;
      surface.pixels[target] = rgba[source + 2];
      surface.pixels[target + 1] = rgba[source + 1];
      surface.pixels[target + 2] = rgba[source];
      surface.pixels[target + 3] = rgba[source + 3];
    }
}

void Canvas2D::save() {
  if (saved.size() < 64) saved.push_back({state, surface.path_clips.size(), dashes_});
}

void Canvas2D::restore() {
  if (saved.empty()) return;
  SavedState previous = std::move(saved.back());
  saved.pop_back();
  state = std::move(previous.state);
  dashes_ = std::move(previous.dashes);
  surface.path_clips.resize(std::min(previous.clip_count, surface.path_clips.size()));
}

void Canvas2D::apply_transform(double a, double b, double c, double d, double e, double f) {
  double na = state.a * a + state.c * b;
  double nb = state.b * a + state.d * b;
  double nc = state.a * c + state.c * d;
  double nd = state.b * c + state.d * d;
  double ne = state.a * e + state.c * f + state.e;
  double nf = state.b * e + state.d * f + state.f;
  state.a = na;
  state.b = nb;
  state.c = nc;
  state.d = nd;
  state.e = ne;
  state.f = nf;
}

void Canvas2D::set_transform(double a, double b, double c, double d, double e, double f) {
  state.a = a;
  state.b = b;
  state.c = c;
  state.d = d;
  state.e = e;
  state.f = f;
}

void Canvas2D::translate(double x, double y) {
  apply_transform(1, 0, 0, 1, x, y);
}

void Canvas2D::scale(double x, double y) {
  apply_transform(x, 0, 0, y, 0, 0);
}

void Canvas2D::rotate(double radians) {
  apply_transform(std::cos(radians), std::sin(radians), -std::sin(radians), std::cos(radians), 0, 0);
}

Point Canvas2D::map(double x, double y) const {
  return {state.a * x + state.c * y + state.e, state.b * x + state.d * y + state.f};
}

} // namespace pv
