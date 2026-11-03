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
  if (width == surface.width && height == surface.height)
    return;
  ++revision;
  // A resize clears the bitmap, as the spec says.
  surface = Canvas(width, height, 0x00000000);
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
  push({x, y});
}

void Canvas2D::line_to(double x, double y) {
  push({x, y});
}

void Canvas2D::close_path() {
  if (!current.empty()) {
    current.push_back(current.front());
    flush_subpath();
  }
}

void Canvas2D::add_rect(double x, double y, double w, double h) {
  flush_subpath();
  current.push_back({x, y});
  current.push_back({x + w, y});
  current.push_back({x + w, y + h});
  current.push_back({x, y + h});
  current.push_back({x, y});
  flush_subpath();
}

void Canvas2D::add_arc(double cx, double cy, double r, double start, double end, bool clockwise) {
  add_ellipse(cx, cy, r, r, start, end, clockwise);
}

void Canvas2D::add_ellipse(double cx, double cy, double rx, double ry, double start, double end,
                           bool clockwise) {
  double sweep = end - start;
  if (clockwise && sweep < 0)
    sweep += kTau;
  if (!clockwise && sweep > 0)
    sweep -= kTau;
  if (std::fabs(sweep) > kTau)
    sweep = sweep > 0 ? kTau : -kTau;
  int steps = std::clamp(int(std::fabs(sweep) * 8) + 4, 4, 360);
  bool started = current.empty();
  for (int i = 0; i <= steps; ++i) {
    double t = start + sweep * double(i) / steps;
    Point p{cx + rx * std::cos(t), cy + ry * std::sin(t)};
    if (started && i == 0) {
      flush_subpath();
      current.push_back(p);
    } else
      current.push_back(p);
  }
}

void Canvas2D::add_quadratic(double cx, double cy, double x, double y) {
  Point from = current.empty() ? Point{x, y} : current.back();
  for (int step = 1; step <= 12; ++step) {
    double t = double(step) / 12, mt = 1 - t;
    push({mt * mt * from.x + 2 * mt * t * cx + t * t * x,
          mt * mt * from.y + 2 * mt * t * cy + t * t * y});
  }
}

void Canvas2D::add_bezier(double c1x, double c1y, double c2x, double c2y, double x, double y) {
  Point from = current.empty() ? Point{x, y} : current.back();
  for (int step = 1; step <= 16; ++step) {
    double t = double(step) / 16, mt = 1 - t;
    push({mt * mt * mt * from.x + 3 * mt * mt * t * c1x + 3 * mt * t * t * c2x + t * t * t * x,
          mt * mt * mt * from.y + 3 * mt * mt * t * c1y + 3 * mt * t * t * c2y + t * t * t * y});
  }
}

uint32_t Canvas2D::shade(const Gradient &gradient, double x, double y) const {
  // One shading routine serves my canvas and my page backgrounds.
  return gradient_shade(gradient, x, y);
}

std::vector<Point> Canvas2D::mapped(const std::vector<Point> &points) const {
  // The path holds user coordinates, so the transform that is live at fill or
  // stroke time is the one that applies, as the spec says.
  if (state.a == 1 && state.b == 0 && state.c == 0 && state.d == 1 && state.e == 0 && state.f == 0)
    return points;
  std::vector<Point> out;
  out.reserve(points.size());
  for (auto &p : points)
    out.push_back(map(p.x, p.y));
  return out;
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
  flush_subpath();
  for (auto &subpath : path) {
    auto points = mapped(subpath);
    if (state.has_gradient)
      fill_polygon_gradient(points, state.fill_gradient);
    else
      fill_polygon(points, tint(state.fill));
  }
}

void Canvas2D::stroke() {
  ++revision;
  flush_subpath();
  double width = std::max(1.0, state.line_width);
  for (auto &subpath : path) {
    auto points = mapped(subpath);
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

void Canvas2D::fill_rect(double x, double y, double w, double h) {
  ++revision;
  if (state.a == 1 && state.b == 0 && state.c == 0 && state.d == 1 && state.b == 0) {
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
  surface.polyline(corners, tint(state.stroke), std::max(1.0, state.line_width), true);
}

void Canvas2D::clear_rect(double x, double y, double w, double h) {
  ++revision;
  Point top_left = map(x, y), bottom_right = map(x + w, y + h);
  int x0 = std::clamp(int(std::floor(std::min(top_left.x, bottom_right.x))), 0, surface.width);
  int y0 = std::clamp(int(std::floor(std::min(top_left.y, bottom_right.y))), 0, surface.height);
  int x1 = std::clamp(int(std::ceil(std::max(top_left.x, bottom_right.x))), 0, surface.width);
  int y1 = std::clamp(int(std::ceil(std::max(top_left.y, bottom_right.y))), 0, surface.height);
  for (int py = y0; py < y1; ++py)
    for (int px = x0; px < x1; ++px) {
      size_t i = (size_t(py) * surface.width + px) * 4;
      surface.pixels[i] = surface.pixels[i + 1] = surface.pixels[i + 2] = surface.pixels[i + 3] = 0;
    }
}

void Canvas2D::fill_text(double x, double y, std::string_view text) {
  ++revision;
  double size = font_size_of(state.font);
  double width = surface.text_width(text, size, font_bold(state.font));
  Point at = map(x, y);
  if (state.text_align == "center")
    at.x -= width / 2;
  else if (state.text_align == "right" || state.text_align == "end")
    at.x -= width;
  surface.text(at.x, at.y, text, size, tint(state.fill), font_bold(state.font));
}

void Canvas2D::stroke_text(double x, double y, std::string_view text) {
  fill_text(x, y, text);
}

double Canvas2D::text_width(std::string_view text) const {
  return surface.text_width(text, font_size_of(state.font), font_bold(state.font));
}

void Canvas2D::draw_image(const Image &source, double x, double y, double w, double h) {
  ++revision;
  if (!source.valid())
    return;
  // A scaled context should scale the picture with it, so I map both corners
  // and paint into the box they describe.
  Point a = map(x, y), b = map(x + w, y + h);
  Rect destination = {std::min(a.x, b.x), std::min(a.y, b.y), std::abs(b.x - a.x),
                      std::abs(b.y - a.y)};
  if (state.global_alpha >= 0.999)
    surface.image(source, destination);
  else {
    // A faded image goes through a scratch copy so the alpha applies to pixels.
    Image copy = source;
    for (size_t i = 3; i < copy.pixels.size(); i += 4)
      copy.pixels[i] = uint8_t(double(copy.pixels[i]) * state.global_alpha);
    surface.image(copy, destination);
  }
}

void Canvas2D::draw_image_rect(const Image &source, double sx, double sy, double sw, double sh,
                               double x, double y, double w, double h) {
  ++revision;
  if (!source.valid() || sw <= 0 || sh <= 0)
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
    int sample_y = std::clamp(iy + row, 0, source.height - 1);
    for (int column = 0; column < iw; ++column) {
      int sample_x = std::clamp(ix + column, 0, source.width - 1);
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
  begin_path();
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
  dashes_.clear();
  for (double dash : dashes)
    if (dash > 0)
      dashes_.push_back(dash);
  if (dashes_.size() % 2)
    dashes_.insert(dashes_.end(), dashes_.begin(), dashes_.end());
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
  if (saved.size() < 64)
    saved.push_back(state);
}

void Canvas2D::restore() {
  if (saved.empty())
    return;
  state = saved.back();
  saved.pop_back();
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
