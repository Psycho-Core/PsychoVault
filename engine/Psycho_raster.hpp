#pragma once
#include "Psycho_base.hpp"
namespace pv {
struct Image;
struct Rect {
  double x = 0, y = 0, w = 0, h = 0;
  bool contains(double px, double py) const {
    return px >= x && py >= y && px < x + w && py < y + h;
  }
};
struct Point {
  double x = 0, y = 0;
};
// One colour stop, and the gradient it belongs to. A canvas and a background
// share these so there is one shading implementation.
struct GradientStop {
  double at = 0;
  uint32_t color = 0xff000000u;
};
struct Gradient {
  bool radial = false;
  double x0 = 0, y0 = 0, x1 = 0, y1 = 0, r0 = 0, r1 = 0;
  std::vector<GradientStop> stops;
};
// I sample a gradient at a point, clamping outside the ends.
uint32_t gradient_shade(const Gradient &gradient, double x, double y);
struct Canvas {
  int width = 0, height = 0;
  Bytes pixels;
  Rect clip;
  std::vector<std::pair<Rect, double>> rounded_clips;
  Canvas(int w, int h, uint32_t color = 0xff000000);
  void fill(Rect r, uint32_t color);
  void fill_rounded(Rect r, double radius, uint32_t color);
  void stroke(Rect r, uint32_t color, double thickness = 1);
  void stroke_rounded(Rect r, double radius, uint32_t color, double thickness = 1);
  void box_shadow(Rect r, double radius, double offset_x, double offset_y,
                  double blur, double spread, uint32_t color);
  void line(double x1, double y1, double x2, double y2, uint32_t color, double thickness = 1);
  void text(double x, double y, std::string_view text, double size, uint32_t color,
            bool bold = false, std::string_view family = "Segoe UI");
  double text_width(std::string_view text, double size, bool bold = false,
                    std::string_view family = "Segoe UI") const;
  void pixel(int x, int y, uint32_t color);
  void image(const Image &source, Rect destination, double radius = 0);
  // I fill a closed polygon with a scan line walk, and I stroke a path as a
  // chain of thick segments. Both clip to my own rectangle.
  void polygon(const std::vector<Point> &points, uint32_t color);
  void polyline(const std::vector<Point> &points, uint32_t color, double width = 1,
                bool closed = false);
  void circle(double cx, double cy, double radius, uint32_t color);
  void ellipse(double cx, double cy, double rx, double ry, uint32_t color);
  // A gradient fills one rectangle, which is what a background needs.
  void fill_gradient(Rect r, const Gradient &gradient, double radius = 0);
};
Bytes encode_png(int width, int height, const Bytes &bgra);
uint32_t blend(uint32_t dst, uint32_t src);
} // namespace pv
