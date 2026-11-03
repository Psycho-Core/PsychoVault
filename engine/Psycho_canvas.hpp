// PsychoVault original source. My own Canvas 2D drawing surface. Every call
// here ends up in the same raster I use for pages, so a canvas and a page share
// one renderer.
#pragma once
#include "Psycho_image.hpp"
#include "Psycho_raster.hpp"
#include <string>
#include <vector>

namespace pv {
// I keep the whole drawing state in one place so save() and restore() are a
// plain copy rather than a stack of separate values.
struct CanvasState {
  uint32_t fill = 0xff000000u, stroke = 0xff000000u;
  // I keep the text a script handed me, because a script is allowed to read it
  // back and expects the string it wrote, not a computed colour.
  std::string fill_style = "#000000", stroke_style = "#000000";
  double line_width = 1, global_alpha = 1;
  std::string font = "10px sans-serif";
  std::string text_align = "start";
  bool has_gradient = false;
  int fill_gradient_id = -1;
  Gradient fill_gradient;
  double a = 1, b = 0, c = 0, d = 1, e = 0, f = 0; // affine transform
};
class Canvas2D {
public:
  Canvas2D(int width = 300, int height = 150);
  void resize(int width, int height);
  int width() const {
    return surface.width;
  }
  int height() const {
    return surface.height;
  }
  Canvas &backing() {
    return surface;
  }
  const Canvas &backing() const {
    return surface;
  }
  // The drawing state a script reads and writes.
  CanvasState state;
  // I bump this on every drawing call so my page knows when to copy pixels.
  uint64_t revision = 1;
  // Path building. A path holds several subpaths, as the spec does.
  void begin_path();
  void move_to(double x, double y);
  void line_to(double x, double y);
  void close_path();
  void add_rect(double x, double y, double w, double h);
  void add_arc(double cx, double cy, double r, double start, double end, bool clockwise);
  void add_ellipse(double cx, double cy, double rx, double ry, double start, double end,
                   bool clockwise);
  void add_quadratic(double cx, double cy, double x, double y);
  void add_bezier(double c1x, double c1y, double c2x, double c2y, double x, double y);
  void fill();
  void stroke();
  void fill_rect(double x, double y, double w, double h);
  void stroke_rect(double x, double y, double w, double h);
  void clear_rect(double x, double y, double w, double h);
  void fill_text(double x, double y, std::string_view text);
  void stroke_text(double x, double y, std::string_view text);
  double text_width(std::string_view text) const;
  void draw_image(const Image &source, double x, double y, double w, double h);
  // The nine argument form takes a piece of the source image.
  void draw_image_rect(const Image &source, double sx, double sy, double sw, double sh, double x,
                       double y, double w, double h);
  // A rounded rectangle is a path, so fill and stroke both work on it.
  void add_round_rect(double x, double y, double w, double h, double radius);
  void set_line_dash(const std::vector<double> &dashes);
  // I fit a source canvas into a destination, which is what drawImage does when
  // the source is another canvas.
  void draw_canvas(const Canvas2D &source, double x, double y, double w, double h);
  Image snapshot() const;
  std::string data_url() const;
  // Pixels are BGRA in my buffer and RGBA in the array a script reads.
  std::vector<uint8_t> get_pixels(int x, int y, int w, int h) const;
  void put_pixels(int x, int y, int w, int h, const std::vector<uint8_t> &rgba);
  void save();
  void restore();
  void apply_transform(double a, double b, double c, double d, double e, double f);
  void set_transform(double a, double b, double c, double d, double e, double f);
  void translate(double x, double y);
  void scale(double x, double y);
  void rotate(double radians);
  Point map(double x, double y) const;
  // I blend a colour with the current globalAlpha so a whole group can fade.
  uint32_t tint(uint32_t color) const {
    return (uint32_t(double(color >> 24) * state.global_alpha) << 24) | (color & 0x00ffffffu);
  }

private:
  Canvas surface;
  std::vector<std::vector<Point>> path;
  std::vector<Point> current;
  std::vector<CanvasState> saved;
  std::vector<double> dashes_;
  void push(Point p) {
    current.push_back(p);
  }
  void flush_subpath();
  uint32_t shade(const Gradient &gradient, double x, double y) const;
  void fill_polygon(std::vector<Point> points, uint32_t color);
  void fill_polygon_gradient(std::vector<Point> points, const Gradient &gradient);
  std::vector<Point> mapped(const std::vector<Point> &points) const;
};
} // namespace pv
