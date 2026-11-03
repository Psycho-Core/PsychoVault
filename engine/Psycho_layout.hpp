#pragma once
#include "Psycho_css.hpp"
#include "Psycho_raster.hpp"
namespace pv {
struct Image;
struct PaintCommand {
  // I keep the order stable because the frame I send my shell carries it as a number.
  enum class Kind { Rect, Border, Text, Image, Line, Canvas, PushClip, PopClip } kind;
  Rect rect;
  uint32_t color = 0xff000000;
  std::string text;
  double size = 16, thickness = 1;
  bool bold = false;
  int node = -1;
  std::string url;
  // A gradient background keeps its CSS text until the paint pass parses it.
  std::string gradient;
  // A fixed command stays where it is when the page scrolls.
  bool fixed = false;
};
struct LayoutResult {
  std::vector<PaintCommand> paint;
  std::unordered_map<int, Rect> boxes;
  double height = 0;
};
LayoutResult layout_document(const Document &doc, const std::vector<Style> &styles, double width,
                             double height, const std::map<std::string, Resource> &resources,
                             Canvas &measure);
void paint_commands(Canvas &canvas, const std::vector<PaintCommand> &commands, double scroll_y,
                    const std::map<std::string, Image> &images);
} // namespace pv
