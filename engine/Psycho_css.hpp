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
              box_sizing = "content-box", white_space = "normal", text_overflow = "clip", position = "static",
              overflow = "visible", scrollbar_gutter = "auto", overscroll_behavior = "auto", flex_direction = "row",
              justify_content = "flex-start",
              align_items = "stretch", align_self = "auto", align_content = "stretch",
              flex_wrap = "nowrap", grid_columns, grid_rows, grid_template_areas,
              grid_column, grid_row, grid_area, grid_auto_flow = "row",
              justify_items = "stretch", justify_self = "auto", transform, filter, box_shadow,
              // Background layers keep their CSS text until layout has the box and resources.
              background_image;
  std::vector<std::string> background_layers;
  Color color{230, 237, 243, 255, false}, background{0, 0, 0, 0, true},
      border_color{70, 80, 95, 255, false};
  double font_size = 16, line_height = 1.45, opacity = 1, gap = 0;
  double margin[4] = {0, 0, 0, 0}, padding[4] = {0, 0, 0, 0}, border[4] = {0, 0, 0, 0};
  // An auto margin is not a length, so I remember it separately instead of
  // pretending it is zero.
  bool auto_left = false, auto_right = false, auto_top = false, auto_bottom = false;
  double width = -1, height = -1, min_width = 0, max_width = -1, min_height = 0, max_height = -1,
         radius = 0, outline_width = 0, outline_offset = 0;
  Color outline_color{230, 237, 243, 255, false};
  // Width percentages and calc() expressions resolve against the containing
  // block, which is known in layout rather than during style cascade.
  std::string width_expression, height_expression, flex_basis_expression;
  std::string min_width_expression, max_width_expression, min_height_expression, max_height_expression;
  double root_font_size = 16, viewport_width = 0, viewport_height = 0;
  double top = 0, left = 0, right = 0, bottom = 0;
  // Auto offsets stay absent so I can tell "not set" from zero.
  bool has_top = false, has_left = false, has_right = false, has_bottom = false;
  double flex_grow = 0, flex_shrink = 1, flex_basis = -1;
  int grid_span = 1, order = 0, z_index = 0;
  bool z_index_auto = true;
  // A percentage height resolves against the containing block at layout time,
  // so I keep the fraction instead of a width-based length.
  double height_percent = -1, row_gap = -1;
  bool bold = false, italic = false, underline = false, list_item = false;
  std::map<std::string, std::string> raw;
  // Custom properties inherit, preserve case-sensitive names, and are resolved
  // only when a consuming property is computed.
  std::map<std::string, std::string> custom_properties;
};
// Numeric weights retained for measurement and paint; relative weights remain
// approximations until the cascade resolves them against the parent.
inline int style_font_weight(const Style &style) {
  const std::string &weight = style.weight;
  if (weight == "normal") return 400;
  if (weight == "bold") return 700;
  if (weight == "bolder" || weight == "lighter") return style.bold ? 700 : 400;
  int value = 0;
  if (weight.empty()) return style.bold ? 700 : 400;
  for (char c : weight) {
    if (c < '0' || c > '9' || value > 1000) return style.bold ? 700 : 400;
    value = value * 10 + (c - '0');
  }
  return value >= 1 && value <= 1000 ? value : (style.bold ? 700 : 400);
}
struct CssDeclaration {
  std::string property, value;
  bool important = false;
};
struct MediaEnvironment {
  double device_pixel_ratio = 1.0;
  double scrollbar_width = 8.0;
  double viewport_width = 1000.0, viewport_height = 700.0;
  std::string prefers_reduced_motion = "no-preference";
  std::string prefers_color_scheme = "light";
  std::string prefers_contrast = "no-preference";
  std::string forced_colors = "none";
};
struct CssKeyframe {
  double offset = 0;
  std::map<std::string, std::string> declarations;
};
struct CssKeyframes {
  std::string name;
  std::vector<CssKeyframe> frames;
};
struct CssRule {
  std::string selector, media;
  std::map<std::string, std::string> declarations;
  std::vector<CssDeclaration> declaration_order;
  std::set<std::string> important;
  int order = 0;
};
struct Stylesheet {
  std::vector<CssRule> rules;
  std::vector<CssKeyframes> keyframes;
  std::vector<Diagnostic> diagnostics;
  void parse(std::string_view text, int &order);
};
std::vector<CssDeclaration> parse_css_declarations(std::string_view text);
void apply_style_overrides(Style &style, const std::map<std::string, std::string> &declarations,
                          double viewport_width, double viewport_height);
std::vector<Style> compute_styles(const Document &doc, const std::vector<Stylesheet> &sheets,
                                  double viewport_width, double viewport_height = -1,
                                  const SelectorState *selector_state = nullptr,
                                  const MediaEnvironment *media_environment = nullptr);
double css_length(const std::string &value, double parent, double font_size, double root_font,
                  double viewport_width, const std::string &property, double fallback,
                  double viewport_height = -1);
int specificity(std::string_view selector);
} // namespace pv
