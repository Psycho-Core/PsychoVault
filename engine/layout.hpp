#pragma once
#include "css.hpp"
#include "raster.hpp"
namespace pv {
struct PaintCommand {
  enum class Kind { Rect, Border, Text, Image, Line } kind;
  Rect rect;
  uint32_t color = 0xff000000;
  std::string text;
  double size = 16, thickness = 1;
  bool bold = false;
  int node = -1;
  std::string url;
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
                    const std::map<std::string, Resource> &resources);
} // namespace pv
