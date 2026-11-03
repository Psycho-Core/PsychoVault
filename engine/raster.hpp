#pragma once
#include "base.hpp"
namespace pv {
struct Image;
struct Rect {
  double x = 0, y = 0, w = 0, h = 0;
  bool contains(double px, double py) const {
    return px >= x && py >= y && px < x + w && py < y + h;
  }
};
struct Canvas {
  int width = 0, height = 0;
  Bytes pixels;
  Rect clip;
  Canvas(int w, int h, uint32_t color = 0xff000000);
  void fill(Rect r, uint32_t color);
  void stroke(Rect r, uint32_t color, double thickness = 1);
  void line(double x1, double y1, double x2, double y2, uint32_t color, double thickness = 1);
  void text(double x, double y, std::string_view text, double size, uint32_t color,
            bool bold = false, std::string_view family = "Segoe UI");
  double text_width(std::string_view text, double size, bool bold = false,
                    std::string_view family = "Segoe UI") const;
  void pixel(int x, int y, uint32_t color);
  void image(const Image &source, Rect destination);
};
Bytes encode_png(int width, int height, const Bytes &bgra);
uint32_t blend(uint32_t dst, uint32_t src);
} // namespace pv
