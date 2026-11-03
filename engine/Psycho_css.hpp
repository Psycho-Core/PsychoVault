#pragma once
#include "Psycho_dom.hpp"
namespace pv {
struct Color {
  uint8_t r = 0, g = 0, b = 0, a = 255;
  bool transparent = true;
  uint32_t bgra() const {
    return uint32_t(a) << 24 | uint32_t(r) << 16 | uint32_t(g) << 8 | b;
  }
};
Color parse_color(std::string_view value);
struct Style {
  std::string display = "block", font_family = "Segoe UI", text_align = "left", weight = "normal",
              box_sizing = "content-box", white_space = "normal", position = "static",
              overflow = "visible", flex_direction = "row", justify_content = "flex-start",
              align_items = "stretch";
  Color color{230, 237, 243, 255, false}, background{0, 0, 0, 0, true},
      border_color{70, 80, 95, 255, false};
  double font_size = 16, line_height = 1.45, opacity = 1, gap = 0;
  double margin[4] = {0, 0, 0, 0}, padding[4] = {0, 0, 0, 0}, border[4] = {0, 0, 0, 0};
  double width = -1, height = -1, min_width = 0, max_width = -1, min_height = 0, max_height = -1,
         radius = 0;
  double top = 0, left = 0;
  bool bold = false, italic = false, underline = false, list_item = false;
  std::map<std::string, std::string> raw;
};
struct CssRule {
  std::string selector, media;
  std::map<std::string, std::string> declarations;
  std::set<std::string> important;
  int order = 0;
};
struct Stylesheet {
  std::vector<CssRule> rules;
  std::vector<Diagnostic> diagnostics;
  void parse(std::string_view text, int &order);
};
std::vector<Style> compute_styles(const Document &doc, const std::vector<Stylesheet> &sheets,
                                  double viewport_width);
double css_length(const std::string &value, double parent, double font_size, double root_font,
                  double viewport, const std::string &property, double fallback);
int specificity(std::string_view selector);
} // namespace pv
