#pragma once
#include "Psycho_css.hpp"
#include "Psycho_raster.hpp"
#include <unordered_set>
namespace pv {
struct Image;
struct PaintCommand {
  // I keep the order stable because the frame I send my shell carries it as a number.
  enum class Kind { Rect, Border, Text, Image, Line, Canvas, PushClip, PopClip,
                    Shadow, Outline, PushLayer, PopLayer } kind;
  Rect rect;
  uint32_t color = 0xff000000;
  std::string text;
  double size = 16, thickness = 1;
  bool bold = false;
  std::string font_family = "Segoe UI";
  int font_weight = 0; // 0 retains compatibility with legacy bold-only commands.
  int node = -1;
  std::string url;
  // A gradient background keeps its CSS text until the paint pass parses it.
  std::string gradient;
  // Fixed content skips document scrolling; remaining fields parameterize paint effects.
  bool fixed = false;
  double radius = 0, opacity = 1, blur = 0, spread = 0, offset_x = 0, offset_y = 0;
  double rotation = 0;
  std::string filter;
  PaintCommand() = default;
  PaintCommand(Kind paint_kind, Rect bounds, uint32_t paint_color = 0xff000000,
               std::string label = {}, double font_size = 16, double line_width = 1,
               bool is_bold = false, int owner = -1, std::string resource_url = {},
               std::string gradient_text = {}, bool is_fixed = false)
      : kind(paint_kind), rect(bounds), color(paint_color), text(std::move(label)),
        size(font_size), thickness(line_width), bold(is_bold), node(owner),
        url(std::move(resource_url)), gradient(std::move(gradient_text)), fixed(is_fixed) {}
};
struct LayoutResult {
  std::vector<PaintCommand> paint;
  std::unordered_map<int, Rect> boxes;
  // Padding-edge clips are also used by pointer hit testing so clipped content
  // cannot receive input through an invisible portion of its box.
  std::unordered_map<int, Rect> clips;
  std::unordered_set<int> fixed_boxes;
  struct ScrollContainer {
    Rect viewport, track, thumb;
    double offset = 0, maximum = 0, content = 0;
    std::string overscroll = "auto";
  };
  std::unordered_map<int, ScrollContainer> scroll_containers;
  std::unordered_map<int, std::pair<size_t, size_t>> paint_ranges;
  double height = 0;
};
LayoutResult layout_document(const Document &doc, const std::vector<Style> &styles, double width,
                             double height, const std::map<std::string, Resource> &resources,
                             Canvas &measure, double scroll_y = 0,
                             const std::unordered_map<int, double> &scroll_offsets = {});
void paint_commands(Canvas &canvas, const std::vector<PaintCommand> &commands, double scroll_y,
                    const std::map<std::string, Image> &images);
// Find in page. I search the text my layout actually paints, so a highlight
// lines up with the glyphs instead of guessing from the source text.
std::vector<Rect> find_in_paint(const std::vector<PaintCommand> &commands, std::string_view needle);
} // namespace pv
