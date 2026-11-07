#include "Psycho_image.hpp"
#include "Psycho_layout.hpp"
namespace pv {
namespace {
// No background rectangle was painted for this box.
constexpr size_t kNoPaint = static_cast<size_t>(-1);
void pop_utf8_grapheme(std::string &text) {
  auto pop_codepoint = [&]() -> uint32_t {
    if (text.empty())
      return 0;
    size_t first = text.size() - 1;
    while (first > 0 && (uint8_t(text[first]) & 0xc0) == 0x80)
      --first;
    uint8_t lead = uint8_t(text[first]);
    uint32_t cp = lead;
    if ((lead & 0xe0) == 0xc0)
      cp = lead & 0x1f;
    else if ((lead & 0xf0) == 0xe0)
      cp = lead & 0x0f;
    else if ((lead & 0xf8) == 0xf0)
      cp = lead & 0x07;
    for (size_t i = first + 1; i < text.size(); ++i)
      cp = (cp << 6) | (uint8_t(text[i]) & 0x3f);
    text.resize(first);
    return cp;
  };
  uint32_t cp = pop_codepoint();
  auto extension = [](uint32_t c) {
    return (c >= 0x0300 && c <= 0x036f) || (c >= 0x1ab0 && c <= 0x1aff) ||
           (c >= 0x1dc0 && c <= 0x1dff) || (c >= 0xfe00 && c <= 0xfe0f) ||
           (c >= 0xfe20 && c <= 0xfe2f) || (c >= 0x1f3fb && c <= 0x1f3ff);
  };
  while (!text.empty() && extension(cp))
    cp = pop_codepoint();
}
} // namespace
struct Run {
  std::string text;
  int node;
  double width;
  Style style;
  bool break_before = false;
};
struct Layout {
  const Document &doc;
  const std::vector<Style> &styles;
  Canvas &measure;
  const std::map<std::string, Resource> &resources;
  LayoutResult out;
  double viewport_width;
  double viewport_height = 720;
  Layout(const Document &d, const std::vector<Style> &s, Canvas &c,
         const std::map<std::string, Resource> &r, double w)
      : doc(d), styles(s), measure(c), resources(r), viewport_width(w) {}
  double outer_extra(const Style &s) {
    return s.margin[1] + s.margin[3] + s.padding[1] + s.padding[3] + s.border[1] + s.border[3];
  }
  double resolved_width(const Style &s, double containing_width) const {
    if (s.width_expression.empty()) return s.width;
    return css_length(s.width_expression, containing_width, s.font_size, s.root_font_size,
                      s.viewport_width, "width", -1, s.viewport_height);
  }
  double resolved_height(const Style &s, double containing_height) const {
    if (s.height_expression.empty() || containing_height <= 0) return s.height;
    return css_length(s.height_expression, containing_height, s.font_size, s.root_font_size,
                      s.viewport_width, "height", -1, s.viewport_height);
  }

  // ---- geometry helpers -------------------------------------------------
  bool positioned(const Style &s) const {
    return s.position == "absolute" || s.position == "fixed";
  }
  void shift_range(size_t from, double dx, double dy) {
    if (dx == 0 && dy == 0)
      return;
    for (size_t i = from; i < out.paint.size(); ++i) {
      out.paint[i].rect.x += dx;
      out.paint[i].rect.y += dy;
    }
  }
  void shift_boxes(int id, double dx, double dy) {
    if (id < 0 || id >= int(doc.nodes.size()))
      return;
    auto it = out.boxes.find(id);
    if (it != out.boxes.end()) {
      it->second.x += dx;
      it->second.y += dy;
    }
    for (int c : doc.nodes[id].children)
      shift_boxes(c, dx, dy);
  }
  void mark_fixed(size_t from) {
    for (size_t i = from; i < out.paint.size(); ++i)
      out.paint[i].fixed = true;
  }
  // I lay a box out for its height only, then throw the commands away.
  double measure_h(int id, double available, int depth) {
    auto saved_paint = std::move(out.paint);
    auto saved_boxes = std::move(out.boxes);
    out.paint.clear();
    out.boxes.clear();
    double h = block(id, 0, 0, available, depth + 1, nullptr);
    out.paint = std::move(saved_paint);
    out.boxes = std::move(saved_boxes);
    return h;
  }
  static double number_of(const std::string &text, double fallback) {
    try {
      size_t used = 0;
      double value = std::stod(text, &used);
      return used ? value : fallback;
    } catch (...) {
      return fallback;
    }
  }
  static std::vector<std::string> tokens(const std::string &text) {
    std::vector<std::string> out;
    std::string current;
    for (char c : text) {
      if (c == ' ' || c == '\t') {
        if (!current.empty())
          out.push_back(current);
        current.clear();
      } else
        current += c;
    }
    if (!current.empty())
      out.push_back(current);
    return out;
  }
  // I turn grid-template-columns into a width per track.
  std::vector<double> track_sizes(const std::string &text, double available, double gap,
                                  size_t limit = 12) {
    std::vector<std::string> flat;
    auto list = tokens(text);
    for (size_t i = 0; i < list.size() && flat.size() < limit * 4; ++i) {
      auto token = list[i];
      if (token.rfind("repeat(", 0) == 0) {
        // I read repeat(3, 1fr 20px) by taking the count and everything up to the
        // matching bracket on this token, or the following tokens.
        std::string inner = token.substr(7);
        std::string rest;
        if (inner.find(')') == std::string::npos) {
          size_t j = i + 1;
          for (; j < list.size(); ++j) {
            inner += " " + list[j];
            if (list[j].find(')') != std::string::npos)
              break;
          }
          i = j;
        }
        auto close = inner.find(')');
        if (close != std::string::npos)
          inner = inner.substr(0, close);
        // repeat(3, 1fr) puts a comma between the count and the track.
        std::replace(inner.begin(), inner.end(), ',', ' ');
        auto parts = tokens(inner);
        int count = parts.empty() ? 0 : int(std::clamp(number_of(parts[0], 0), 0.0, 12.0));
        std::vector<std::string> each(parts.begin() + (parts.empty() ? 0 : 1), parts.end());
        for (int n = 0; n < count && flat.size() < limit * 4; ++n)
          for (auto &item : each)
            flat.push_back(item);
        continue;
      }
      flat.push_back(token);
    }
    std::vector<double> fixed;
    std::vector<double> fractions;
    double used = 0;
    size_t count = 0;
    for (auto &token : flat) {
      if (count >= limit)
        break;
      ++count;
      double size = -1, fraction = 0;
      if (auto star = token.find("fr"); star != std::string::npos && star + 2 == token.size()) {
        fraction = std::max(0.0, number_of(token.substr(0, star), 1));
      } else if (auto pct = token.find('%'); pct != std::string::npos && pct + 1 == token.size()) {
        size = std::max(0.0, number_of(token.substr(0, pct), 0) * available / 100);
      } else if (token == "auto" || token == "min-content" || token == "max-content") {
        size = -1;
        fraction = 1;
      } else if (auto px = token.find("px"); px != std::string::npos && px + 2 == token.size()) {
        size = std::max(0.0, number_of(token.substr(0, px), 0));
      } else {
        size = std::max(0.0, number_of(token, -1));
        if (size < 0)
          fraction = 1;
      }
      if (size >= 0) {
        fixed.push_back(size);
        used += size;
      } else {
        fixed.push_back(-1);
        fractions.push_back(fraction > 0 ? fraction : 1);
      }
    }
    double fraction_total = 0;
    for (double f : fractions)
      fraction_total += f;
    double gaps = gap * std::max(0.0, double(fixed.size()) - 1.0);
    double share = fraction_total > 0 ? std::max(0.0, available - used - gaps) / fraction_total : 0;
    std::vector<double> out_sizes;
    size_t next = 0;
    for (double size : fixed) {
      if (size >= 0)
        out_sizes.push_back(size);
      else
        out_sizes.push_back(share * (next < fractions.size() ? fractions[next++] : 1));
    }
    return out_sizes;
  }
  // I move everything a box painted, which is how a relative offset works.
  void move_box(size_t from, int id, double dx, double dy) {
    shift_range(from, dx, dy);
    shift_boxes(id, dx, dy);
  }
  // ---- flex -------------------------------------------------------------
  void flex_children(int id, const Style &s, double inner_x, double inner_y, double inner_w,
                     double &cursor, int depth, const Rect *cb) {
    const auto &n = doc.nodes[id];
    bool row = s.flex_direction == "row" || s.flex_direction == "row-reverse";
    bool reverse = s.flex_direction == "row-reverse" || s.flex_direction == "column-reverse";
    bool wrap = s.flex_wrap == "wrap" || s.flex_wrap == "wrap-reverse";
    std::vector<int> items;
    for (int c : n.children) {
      const auto &cs = styles[c];
      if (cs.display == "none" || doc.nodes[c].type != Node::Type::Element || positioned(cs))
        continue;
      items.push_back(c);
    }
    if (items.empty())
      return;
    // `order` runs before the direction reverses anything.
    std::stable_sort(items.begin(), items.end(),
                     [&](int a, int b) { return styles[a].order < styles[b].order; });
    if (reverse)
      std::reverse(items.begin(), items.end());
    struct Entry {
      int id;
      double base, grow, shrink;
    };
    std::vector<Entry> entries;
    for (int c : items) {
      const auto &cs = styles[c];
      double base = 0;
      if (row) {
        double extra = outer_extra(cs);
        const double specified_width = resolved_width(cs, inner_w);
        if (specified_width >= 0)
          base = specified_width + extra;
        else if (cs.flex_basis >= 0)
          base = cs.flex_basis + extra;
        else if (cs.flex_grow > 0)
          base = extra;
        else
          base = std::max(extra, inner_w / double(items.size()));
      } else {
        double extra = cs.margin[0] + cs.margin[2] + cs.padding[0] + cs.padding[2] +
                       cs.border[0] + cs.border[2];
        if (cs.height >= 0)
          base = cs.height + extra;
        else if (cs.flex_basis >= 0)
          base = cs.flex_basis + extra;
        else
          base = measure_h(c, inner_w - cs.margin[1] - cs.margin[3], depth);
      }
      entries.push_back({c, std::max(0.0, base), cs.flex_grow, cs.flex_shrink});
    }
    std::vector<std::vector<size_t>> lines;
    if (row && wrap) {
      std::vector<size_t> current;
      double used = 0;
      for (size_t i = 0; i < entries.size(); ++i) {
        double add = entries[i].base + (current.empty() ? 0 : s.gap);
        if (!current.empty() && used + add > inner_w + 0.5) {
          lines.push_back(current);
          current.clear();
          used = 0;
          add = entries[i].base;
        }
        current.push_back(i);
        used += add;
      }
      if (!current.empty())
        lines.push_back(current);
    } else {
      std::vector<size_t> all;
      for (size_t i = 0; i < entries.size(); ++i)
        all.push_back(i);
      lines.push_back(all);
    }
    // A row advances along x while it grows along y, so I keep the two axes in
    // separate cursors. Adding the main cursor to the wrong axis used to place
    // every flex row at the top of its parent.
    double main = row ? inner_x : inner_y;
    double cross_y = inner_y;
    for (auto &line : lines) {
      double used = s.gap * double(line.size() - (line.empty() ? 0 : 1));
      for (size_t i : line)
        used += entries[i].base;
      double free = row ? inner_w - used : 0;
      std::vector<double> sizes(line.size());
      for (size_t k = 0; k < line.size(); ++k)
        sizes[k] = entries[line[k]].base;
      if (free > 0) {
        double grow = 0;
        for (size_t k = 0; k < line.size(); ++k)
          grow += entries[line[k]].grow;
        if (grow > 0)
          for (size_t k = 0; k < line.size(); ++k)
            sizes[k] += free * entries[line[k]].grow / grow;
      } else if (free < 0) {
        double shrink = 0;
        for (size_t k = 0; k < line.size(); ++k)
          shrink += entries[line[k]].shrink * entries[line[k]].base;
        if (shrink > 0)
          for (size_t k = 0; k < line.size(); ++k)
            sizes[k] += free * (entries[line[k]].shrink * entries[line[k]].base) / shrink;
      }
      double span = s.gap * double(line.size() - (line.empty() ? 0 : 1));
      for (double size : sizes)
        span += size;
      double slack = std::max(0.0, (row ? inner_w : 0) - span);
      double offset = 0, extra_gap = 0;
      if (s.justify_content == "flex-end" || s.justify_content == "end")
        offset = slack;
      else if (s.justify_content == "center")
        offset = slack / 2;
      else if (s.justify_content == "space-between" && line.size() > 1)
        extra_gap = slack / double(line.size() - 1);
      else if (s.justify_content == "space-around" && !line.empty()) {
        extra_gap = slack / double(line.size());
        offset = extra_gap / 2;
      } else if (s.justify_content == "space-evenly" && !line.empty()) {
        extra_gap = slack / double(line.size() + 1);
        offset = extra_gap;
      }
      // I measure the cross size first so stretch and centring can use it.
      std::vector<double> cross(line.size(), 0);
      double line_cross = 0;
      for (size_t k = 0; k < line.size(); ++k) {
        int c = entries[line[k]].id;
        cross[k] = measure_h(c, row ? sizes[k] : inner_w, depth);
        line_cross = std::max(line_cross, cross[k]);
      }
      // One line in a box with a height fills that height, so align-items and
      // stretch measure against the box and not against the tallest item.
      if (row && lines.size() == 1 && s.height >= 0)
        line_cross = std::max(line_cross, s.height);
      double pos = main + offset;
      for (size_t k = 0; k < line.size(); ++k) {
        int c = entries[line[k]].id;
        const auto &cs = styles[c];
        // align-self wins over the container's align-items for this one item.
        const std::string &align = cs.align_self == "auto" ? s.align_items : cs.align_self;
        if (row) {
          double forced = -1;
          if (align == "stretch" && cs.height < 0) {
            forced = std::max(0.0, line_cross - cs.margin[0] - cs.margin[2]);
            cross[k] = forced + cs.margin[0] + cs.margin[2];
          }
          // I work the cross-axis offset out before laying the item out, so the
          // item and everything inside it land in one place.
          double dy = 0;
          if (align == "center")
            dy = std::max(0.0, (line_cross - cross[k]) / 2);
          else if (align == "flex-end" || align == "end")
            dy = std::max(0.0, line_cross - cross[k]);
          double content_w = std::max(0.0, sizes[k] - outer_extra(cs));
          block(c, pos, cross_y + dy, sizes[k], depth + 1, cb, forced, content_w);
        } else {
          double forced = std::max(0.0, sizes[k] - cs.margin[0] - cs.margin[2]);
          block(c, inner_x + cross_offset(align, cs, inner_w), pos, inner_w, depth + 1, cb,
                forced);
        }
        pos += sizes[k] + s.gap + extra_gap;
      }
      if (row)
        cross_y += line_cross + (s.row_gap >= 0 ? s.row_gap : s.gap);
      else
        main += span + s.gap;
    }
    cursor = lines.empty() ? cursor : (row ? cross_y - s.gap : std::max(cursor, main - s.gap));
  }
  // ---- grid -------------------------------------------------------------
  void grid_children(int id, const Style &s, double inner_x, double inner_y, double inner_w,
                     double &cursor, int depth, const Rect *cb) {
    const auto &n = doc.nodes[id];
    std::vector<int> items;
    for (int c : n.children) {
      const auto &cs = styles[c];
      if (cs.display == "none" || doc.nodes[c].type != Node::Type::Element || positioned(cs))
        continue;
      items.push_back(c);
    }
    if (items.empty())
      return;
    auto columns = track_sizes(s.grid_columns, inner_w, s.gap);
    if (columns.empty())
      columns.push_back(inner_w);
    double row_gap = s.row_gap >= 0 ? s.row_gap : s.gap;
    // An explicit row list fixes track heights; otherwise a row is as tall as
    // its tallest item.
    auto rows = track_sizes(s.grid_rows, 0, row_gap);
    struct Cell {
      int id;
      size_t column, row;
      double x, w, h;
    };
    std::vector<Cell> cells;
    size_t column = 0, row = 0;
    for (int c : items) {
      const auto &cs = styles[c];
      size_t span = size_t(std::clamp(cs.grid_span, 1, int(columns.size())));
      if (column + span > columns.size()) {
        column = 0;
        ++row;
      }
      double x = inner_x, w = 0;
      for (size_t i = 0; i < column; ++i)
        x += columns[i] + s.gap;
      for (size_t i = column; i < column + span && i < columns.size(); ++i)
        w += columns[i] + (i > column ? s.gap : 0);
      cells.push_back({c, column, row, x, w, measure_h(c, w, depth)});
      column += span;
      if (column >= columns.size()) {
        column = 0;
        ++row;
      }
    }
    std::vector<double> heights(row + 1, 0);
    for (auto &cell : cells)
      heights[cell.row] = std::max(heights[cell.row], cell.h);
    for (size_t r = 0; r < rows.size() && r < heights.size(); ++r)
      heights[r] = std::max(heights[r], rows[r]);
    std::vector<double> row_y(heights.size());
    double y = inner_y;
    for (size_t r = 0; r < heights.size(); ++r) {
      row_y[r] = y;
      y += heights[r] + row_gap;
    }
    for (auto &cell : cells) {
      const auto &cs = styles[cell.id];
      // A grid stretches an item that has no height of its own.
      double forced = cs.height < 0 && cs.height_percent < 0 ? heights[cell.row] : -1;
      block(cell.id, cell.x, row_y[cell.row], cell.w, depth + 1, cb, forced, -1,
            heights[cell.row]);
    }
    cursor = std::max(cursor, heights.empty() ? inner_y : row_y.back() + heights.back());
  }
  // ---- positioning ------------------------------------------------------
  void place_positioned(int id, const Style &s, const Rect &cb, double left, double top,
                        double border_w, double border_h, int depth) {
    if (id < 0 || id >= int(doc.nodes.size()))
      return;
    // A positioned child is measured against the padding box of its ancestor,
    // so the border is outside that box and padding is inside it.
    Rect self{left + s.border[3], top + s.border[0],
              std::max(0.0, border_w - s.border[1] - s.border[3]),
              std::max(0.0, border_h - s.border[0] - s.border[2])};
    Rect box = s.position == "static" ? cb : self;
    for (int c : doc.nodes[id].children) {
      const auto &cs = styles[c];
      if (cs.display == "none" || doc.nodes[c].type != Node::Type::Element || !positioned(cs))
        continue;
      Rect used = cs.position == "fixed" ? Rect{0, 0, viewport_width, viewport_height} : box;
      double x = used.x + (cs.has_left ? cs.left : 0);
      double y = used.y + (cs.has_top ? cs.top : 0);
      double extra_w = outer_extra(cs);
      const double specified_width = resolved_width(cs, used.w);
      if (cs.has_right && !cs.has_left)
        x = used.x + used.w - cs.right - (specified_width >= 0 ? specified_width + extra_w : 0);
      double extra_h = cs.margin[0] + cs.margin[2] + cs.padding[0] + cs.padding[2] +
                       cs.border[0] + cs.border[2];
      const double specified_height = resolved_height(cs, used.h);
      if (cs.has_bottom && !cs.has_top)
        y = used.y + used.h - cs.bottom - (specified_height >= 0 ? specified_height + extra_h : 0);
      size_t from = out.paint.size();
      block(c, x, y, std::max(0.0, used.w - (cs.has_left ? cs.left : 0.0)), depth + 1, &used, -1,
            -1, used.h);
      if (cs.position == "fixed")
        mark_fixed(from);
    }
  }
  // ---- transforms -------------------------------------------------------
  void apply_transform(size_t from, size_t to, const Style &s, const Rect &box, int node) {
    if (s.transform.empty() || s.transform == "none")
      return;
    double tx = 0, ty = 0, sx = 1, sy = 1;
    size_t p = 0;
    while ((p = s.transform.find('(', p)) != std::string::npos) {
      size_t close = s.transform.find(')', p);
      if (close == std::string::npos)
        break;
      std::string name = lower(trim(s.transform.substr(0, p)));
      auto dot = name.rfind(' ');
      if (dot != std::string::npos)
        name = name.substr(dot + 1);
      std::string arg_text = s.transform.substr(p + 1, close - p - 1);
      for (auto &c : arg_text)
        if (c == ',')
          c = ' ';
      auto args = tokens(arg_text);
      p = close + 1;
      if (name == "translate" || name == "translate3d") {
        if (!args.empty())
          tx += number_of(args[0], 0);
        if (args.size() > 1)
          ty += number_of(args[1], 0);
      } else if (name == "translatex") {
        if (!args.empty())
          tx += number_of(args[0], 0);
      } else if (name == "translatey") {
        if (!args.empty())
          ty += number_of(args[0], 0);
      } else if (name == "scale") {
        double x = args.empty() ? 1 : number_of(args[0], 1);
        sx *= x;
        sy *= args.size() > 1 ? number_of(args[1], x) : x;
      } else if (name == "scalex") {
        if (!args.empty())
          sx *= number_of(args[0], 1);
      } else if (name == "scaley") {
        if (!args.empty())
          sy *= number_of(args[0], 1);
      }
    }
    if (tx == 0 && ty == 0 && sx == 1 && sy == 1)
      return;
    // I scale about the middle of the box, which is what the default origin does.
    double ox = box.x + box.w / 2, oy = box.y + box.h / 2;
    std::vector<int> moved{node};
    for (size_t i = from; i < to && i < out.paint.size(); ++i) {
      auto &r = out.paint[i].rect;
      r.x = tx + ox + (r.x - ox) * sx;
      r.y = ty + oy + (r.y - oy) * sy;
      r.w *= sx;
      r.h *= sy;
      if (out.paint[i].kind == PaintCommand::Kind::Text)
        out.paint[i].size *= sy;
      if (out.paint[i].node >= 0 &&
          std::find(moved.begin(), moved.end(), out.paint[i].node) == moved.end())
        moved.push_back(out.paint[i].node);
    }
    // A transform moves the element for a click as well as for the eye, so the
    // boxes I report and hit-test with move too.
    for (int node : moved) {
      auto found = out.boxes.find(node);
      if (found == out.boxes.end())
        continue;
      auto &b = found->second;
      b.x = tx + ox + (b.x - ox) * sx;
      b.y = ty + oy + (b.y - oy) * sy;
      b.w *= sx;
      b.h *= sy;
    }
  }

  // In a column the cross axis is horizontal, so align-items only moves the box
  // when it has a width of its own.
  double cross_offset(const std::string &align, const Style &cs, double inner_w) {
    const double child_width = resolved_width(cs, inner_w);
    if (child_width < 0 || inner_w <= 0)
      return 0;
    if (align == "center")
      return std::max(0.0, (inner_w - child_width) / 2);
    if (align == "flex-end" || align == "end")
      return std::max(0.0, inner_w - child_width);
    return 0;
  }
  double block(int id, double x, double y, double available, int depth = 0,
               const Rect *cb = nullptr, double forced_h = -1, double forced_w = -1,
               double avail_h = -1) {
    (void)cb;
    size_t start = out.paint.size();
    if (id < 0 || id >= int(doc.nodes.size()) || depth > kMaxDomDepth)
      return 0;
    const auto &n = doc.nodes[id];
    const auto &s = styles[id];
    if (s.display == "none" || n.type == Node::Type::Comment)
      return 0;
    if (n.type == Node::Type::Text)
      return 0;
    double mw = std::max(0.0, available - s.margin[1] - s.margin[3]);
    double sideways = s.padding[1] + s.padding[3] + s.border[1] + s.border[3];
    const double specified_width = resolved_width(s, available);
    const double specified_height = resolved_height(s, avail_h);
    double w = 0;
    if (forced_w >= 0) {
      // Flex and grid have already decided this one.
      w = forced_w;
    } else if (specified_width >= 0) {
      w = specified_width;
      if (s.box_sizing == "border-box")
        w -= sideways;
    } else {
      // An auto width fills what is left, so padding and border come out of it
      // rather than pushing the box past its parent.
      w = std::max(0.0, mw - sideways);
    }
    if (s.max_width >= 0)
      w = std::min(w, specified_width >= 0 ? s.max_width : std::max(0.0, s.max_width - sideways));
    w = std::max({w, s.min_width, 0.0});
    double left = x + s.margin[3];
    // An auto margin takes the free space, so two of them centre the box.
    if (forced_w < 0) {
      double free_space = available - w - sideways - s.margin[1] - s.margin[3];
      if (s.auto_left && s.auto_right)
        left = x + s.margin[3] + std::max(0.0, free_space / 2);
      else if (s.auto_left)
        left = x + s.margin[3] + std::max(0.0, free_space);
    }
    double top = y + s.margin[0];
    double inner_x = left + s.border[3] + s.padding[3], inner_y = top + s.border[0] + s.padding[0];
    double inner_w = std::max(0.0, w);
    // An element with no background paints nothing. I used to push an opaque
    // black rectangle for every box, which put a black bar behind every heading
    // on a page that only set a colour on the body.
    size_t bg = kNoPaint;
    if (!s.background.transparent || !s.background_image.empty()) {
      out.paint.push_back(
          {PaintCommand::Kind::Rect,
           {left, top, w + s.padding[1] + s.padding[3] + s.border[1] + s.border[3], 0},
           s.background.bgra(),
           {},
           0,
           0,
           false,
           id,
           {}, {}});
      bg = out.paint.size() - 1;
      if (!s.background_image.empty())
        out.paint[bg].gradient = s.background_image;
    }
    // Overflow hides what spills out, so I open a clip here and close it after
    // the last child has painted.
    bool clips_children = s.overflow == "hidden" || s.overflow == "clip" || s.overflow == "auto" ||
                          s.overflow == "scroll";
    size_t clip_at = 0;
    if (clips_children) {
      out.paint.push_back({PaintCommand::Kind::PushClip,
                           {left, top, w + s.padding[1] + s.padding[3] + s.border[1] + s.border[3], 0},
                           0,
                           {},
                           0,
                           0,
                           false,
                           id,
                           {},
                           {},
                           {}});
      clip_at = out.paint.size() - 1;
    }
    double cursor = inner_y;
    if (s.display == "flex" || s.display == "inline-flex") {
      flex_children(id, s, inner_x, inner_y, inner_w, cursor, depth, cb);
    } else if (s.display == "grid" || s.display == "inline-grid") {
      grid_children(id, s, inner_x, inner_y, inner_w, cursor, depth, cb);
    } else {
      std::vector<Run> line;
      double line_width = 0, line_height = 0;
      auto flush = [&]() {
        if (line.empty() && line_height <= 0)
          return;
        if (s.white_space == "nowrap" && s.text_overflow == "ellipsis" && line_width > inner_w &&
            !line.empty()) {
          const std::string ellipsis = "\xE2\x80\xA6";
          Run ellipsis_run = line.back();
          double ellipsis_width = measure.text_width(ellipsis, ellipsis_run.style.font_size,
                                                     ellipsis_run.style.bold,
                                                     ellipsis_run.style.font_family);
          while (!line.empty() && line_width + ellipsis_width > inner_w) {
            Run &tail = line.back();
            if (tail.text.empty() || tail.text == " ") {
              line_width -= tail.width;
              line.pop_back();
              continue;
            }
            pop_utf8_grapheme(tail.text);
            tail.width = measure.text_width(tail.text, tail.style.font_size, tail.style.bold,
                                            tail.style.font_family);
            if (tail.text.empty())
              line.pop_back();
            line_width = 0;
            for (const auto &run : line)
              line_width += run.width;
          }
          if (ellipsis_width <= inner_w) {
            ellipsis_run.text = ellipsis;
            ellipsis_run.width = ellipsis_width;
            ellipsis_run.break_before = false;
            line.push_back(std::move(ellipsis_run));
            line_width += ellipsis_width;
          }
        }
        line_height = std::max(line_height, 16.0);
        double tx = inner_x;
        if (s.text_align == "center")
          tx += std::max(0.0, (inner_w - line_width) / 2);
        else if (s.text_align == "right")
          tx += std::max(0.0, inner_w - line_width);
        for (auto &run : line) {
          if (run.text == "\n") {
            continue;
          }
          out.paint.push_back({PaintCommand::Kind::Text,
                               {tx, cursor, run.width, line_height},
                               run.style.color.bgra(),
                               run.text,
                               run.style.font_size,
                               0,
                               run.style.bold,
                               run.node,
                               {}, {}, {}});
          if (run.style.underline)
            out.paint.push_back({PaintCommand::Kind::Line,
                                 {tx, cursor + run.style.font_size + 2, run.width, 1},
                                 run.style.color.bgra(),
                                 {},
                                 0,
                                 1,
                                 false,
                                 run.node,
                                 {}, {}, {}});
          auto box = out.boxes.find(run.node);
          if (box == out.boxes.end())
            out.boxes[run.node] = {tx, cursor, run.width, line_height};
          else {
            box->second.x = std::min(box->second.x, tx);
            box->second.y = std::min(box->second.y, cursor);
            box->second.w = std::max(box->second.w, tx + run.width - box->second.x);
            box->second.h = std::max(box->second.h, line_height);
          }
          tx += run.width;
        }
        cursor += line_height;
        line.clear();
        line_width = line_height = 0;
      };
      auto add_text = [&](std::string text, int node, const Style &st) {
        if (text.empty())
          return;
        bool preserve = st.white_space == "pre" || st.white_space == "pre-wrap";
        if (!preserve) {
          std::string collapsed;
          bool space = false;
          for (char c : text) {
            if (std::isspace(static_cast<unsigned char>(c))) {
              if (!collapsed.empty())
                space = true;
            } else {
              if (space && !collapsed.empty())
                collapsed += ' ';
              space = false;
              collapsed += c;
            }
          }
          text = collapsed;
          if (text.empty())
            return;
        }
        std::vector<std::string> words;
        size_t pos = 0;
        while (pos < text.size()) {
          size_t n = text.find_first_of(" \n", pos);
          auto word = text.substr(pos, n == std::string::npos ? n : n - pos);
          if (!word.empty())
            words.push_back(word);
          if (n == std::string::npos)
            break;
          words.push_back(text[n] == '\n' ? "\n" : " ");
          pos = n + 1;
        }
        for (auto &word : words) {
          if (word == "\n") {
            flush();
            continue;
          }
          double ww = measure.text_width(word, st.font_size, st.bold, st.font_family);
          if (st.white_space != "nowrap" && line_width + ww > inner_w && !line.empty() && word != " ")
            flush();
          if (word == " " && line.empty())
            continue;
          Run r;
          r.text = word;
          r.node = node;
          r.width = ww;
          r.style = st;
          line.push_back(r);
          line_width += ww;
          line_height = std::max(line_height, st.font_size * st.line_height);
        }
      };
      std::function<void(int, int)> inline_child = [&](int c, int dep) {
        if (dep > kMaxDomDepth)
          return;
        const auto &child = doc.nodes[c];
        const auto &cs = styles[c];
        if (cs.display == "none")
          return;
        if (child.type == Node::Type::Text) {
          add_text(child.text, c, cs);
          return;
        }
        if (child.name == "br") {
          flush();
          return;
        }
        if (child.name == "video" || child.name == "audio") {
          flush();
          // An audio element with no controls draws nothing, which is how a page
          // plays a sound without taking up room for it.
          std::string controls;
          if (child.name == "audio" && !child.get_attribute("controls", controls))
            return;
          auto attr_length = [&](const char *name, double fallback) -> double {
            auto value = trim(child.attribute(name));
            if (value.empty())
              return fallback;
            try {
              size_t used = 0;
              double n = std::stod(value, &used);
              if (used == value.size() && n > 0 && n <= 8192)
                return n;
            } catch (...) {
            }
            return fallback;
          };
          // A video keeps the size a page gives it, and an audio element is a strip
          // for its controls because that is all it draws.
          bool moving = child.name == "video";
          double natural_w = attr_length("width", 300);
          double natural_h = attr_length("height", moving ? 150 : 32);
          const double specified_width = resolved_width(cs, inner_w);
          double vw = specified_width >= 0 ? specified_width : natural_w;
          double vh = cs.height >= 0 ? cs.height : natural_h;
          Rect box{inner_x + cs.margin[3], cursor + cs.margin[0], vw, std::max(1.0, vh)};
          // The element owns a dark box before its picture arrives, which is what
          // an unloaded video looks like, and my page paints the frame over it.
          out.paint.push_back({PaintCommand::Kind::Rect,
                               box,
                               moving ? 0xff000000u : 0xff0d1420u,
                               {},
                               cs.font_size,
                               0,
                               false,
                               c,
                               std::string(), {}, {}});
          out.boxes[c] = box;
          cursor += box.h + cs.margin[0] + cs.margin[2];
          return;
        }
        if (child.name == "img") {
          flush();
          std::string source = child.attribute("src");
          auto resolved = resolve_url(doc.url, source);
          std::string key = resolved ? resolved->href() : source;
          auto found = resources.find(key);
          double natural_w = 0, natural_h = 0;
          if (found != resources.end() && found->second.ok && found->second.image_width > 0) {
            natural_w = found->second.image_width;
            natural_h = found->second.image_height;
          }
          auto attr_length = [&](const char *name) -> double {
            auto value = trim(child.attribute(name));
            if (value.empty())
              return -1;
            try {
              size_t used = 0;
              double n = std::stod(value, &used);
              if (used == value.size() && n >= 0 && n <= 16384)
                return n;
            } catch (...) {
            }
            return -1;
          };
          double aw = attr_length("width"), ah = attr_length("height");
          const double specified_width = resolved_width(cs, inner_w);
          double iw = specified_width >= 0 ? specified_width : aw >= 0 ? aw : natural_w > 0 ? natural_w : 120;
          double ih = cs.height >= 0                   ? cs.height
                      : ah >= 0                        ? ah
                      : natural_w > 0 && natural_h > 0 ? iw * natural_h / natural_w
                      : aw >= 0 && natural_w > 0       ? aw * natural_h / natural_w
                                                       : 80;
          if (specified_width < 0 && cs.height >= 0 && natural_w > 0 && natural_h > 0)
            iw = ih * natural_w / natural_h;
          iw = std::clamp(iw, 0.0, 16384.0);
          ih = std::clamp(ih, 0.0, 16384.0);
          out.paint.push_back({PaintCommand::Kind::Image,
                               {inner_x + cs.margin[3], cursor + cs.margin[0], iw, ih},
                               0,
                               child.attribute("alt"),
                               cs.font_size,
                               0,
                               false,
                               c,
                               key, {}, {}});
          out.boxes[c] = {inner_x + cs.margin[3], cursor + cs.margin[0], iw, ih};
          cursor += ih + cs.margin[0] + cs.margin[2];
          return;
        }
        if (child.name == "canvas") {
          flush();
          auto attr_length = [&](const char *name, double fallback) -> double {
            auto value = trim(child.attribute(name));
            if (value.empty())
              return fallback;
            try {
              size_t used = 0;
              double n = std::stod(value, &used);
              if (used == value.size() && n > 0 && n <= 8192)
                return n;
            } catch (...) {
            }
            return fallback;
          };
          // The width and height attributes set the bitmap; CSS sets the box.
          double bw = attr_length("width", 300), bh = attr_length("height", 150);
          const double specified_width = resolved_width(cs, inner_w);
          double cw = specified_width >= 0 ? specified_width : bw;
          double ch = cs.height >= 0 ? cs.height : bh;
          out.paint.push_back({PaintCommand::Kind::Canvas,
                               {inner_x + cs.margin[3], cursor + cs.margin[0], cw, ch},
                               0,
                               {},
                               cs.font_size,
                               0,
                               false,
                               c,
                               "canvas:" + std::to_string(c), {}, {}});
          size_t entry = out.paint.size() - 1;
          out.boxes[c] = out.paint[entry].rect;
          cursor += ch + cs.margin[0] + cs.margin[2];
          return;
        }
        // A form control keeps its own shrink-to-fit sizing, which my control
        // branch below already works out.
        bool control = child.name == "input" || child.name == "button" ||
                       child.name == "select" || child.name == "textarea";
        if (cs.display == "inline-block" && !control) {
          const double specified_width = resolved_width(cs, inner_w);
          double bw = specified_width >= 0 ? specified_width : std::max(0.0, inner_w - line_width);
          // A box that no longer fits starts a new line.
          if (line_width > 0 && line_width + bw > inner_w + 0.5)
            flush();
          double bx = inner_x + line_width;
          block(c, bx, cursor, bw, dep + 1);
          auto found = out.boxes.find(c);
          line_width += bw;
          line_height = std::max(line_height, found == out.boxes.end() ? 0.0 : found->second.h);
          return;
        }
        if (child.name == "input" || child.name == "button" || child.name == "select" ||
            child.name == "textarea") {
          flush();
          std::string label =
              child.name == "button" ? doc.text_content(c) : child.attribute("value");
          if (child.name == "input" && child.attribute("type") == "checkbox")
            label = child.get_attribute("checked", label) ? "[x]" : "[ ]";
          if (child.name == "select") {
            int option = -1;
            for (int o : child.children)
              if (doc.nodes[o].name == "option") {
                if (option < 0 || doc.nodes[o].get_attribute("selected", label))
                  option = o;
              }
            if (option >= 0)
              label = doc.text_content(option);
          }
          if (label.empty())
            label = child.attribute("placeholder");
          const double specified_width = resolved_width(cs, inner_w);
          double cw = specified_width >= 0
                          ? specified_width
                          : std::max(80.0, measure.text_width(label, cs.font_size) +
                                                cs.padding[1] + cs.padding[3] + 16),
                 ch = cs.height >= 0             ? cs.height
                      : child.name == "textarea" ? 80
                                                 : cs.font_size + cs.padding[0] + cs.padding[2] +
                                                       cs.border[0] + cs.border[2];
          if (!cs.background.transparent)
            out.paint.push_back({PaintCommand::Kind::Rect,
                                 {inner_x, cursor, cw, ch},
                                 cs.background.bgra(),
                                 {},
                                 0,
                                 0,
                                 false,
                                 c,
                                 {}, {}, {}});
          out.paint.push_back({PaintCommand::Kind::Border,
                               {inner_x, cursor, cw, ch},
                               cs.border_color.bgra(),
                               {},
                               0,
                               1,
                               false,
                               c,
                               {}, {}, {}});
          out.paint.push_back(
              {PaintCommand::Kind::Text,
               {inner_x + cs.padding[3] + 4, cursor + cs.padding[0] + 2, cw - 8, ch - 4},
               cs.color.bgra(),
               label,
               cs.font_size,
               0,
               cs.bold,
               c,
               {}, {}});
          out.boxes[c] = {inner_x, cursor, cw, ch};
          cursor += ch + cs.margin[0] + cs.margin[2];
          return;
        }
        for (int gc : child.children)
          inline_child(gc, dep + 1);
      };
      double last_margin = 0;
      // A percentage height resolves against the containing block, never the
      // width, so I pass the height a child may use.
      double child_height = specified_height >= 0 ? specified_height
                            : (s.height_percent >= 0 && avail_h > 0 ? avail_h * s.height_percent
                                                                    : -1);
      for (int c : n.children) {
        const auto &cs = styles[c];
        if (cs.display == "none" || positioned(cs))
          continue;
        bool is_block = doc.nodes[c].type == Node::Type::Element && cs.display != "inline" &&
                        cs.display != "inline-block" && c != 0;
        if (is_block) {
          flush();
          // Adjacent vertical margins collapse to the larger of the two, so I
          // step back over the margin I already counted.
          double gap = std::max(last_margin, cs.margin[0]);
          double y = cursor - last_margin + gap - cs.margin[0];
          double h = block(c, inner_x, y, inner_w, depth + 1, cb, -1, -1, child_height);
          cursor = y + h;
          last_margin = cs.margin[2];
        } else
          inline_child(c, 0);
      }
      flush();
    }
    double content_h = cursor - inner_y;
    double h = forced_h >= 0 ? forced_h : (specified_height >= 0 ? specified_height : content_h);
    if (forced_h < 0 && specified_height < 0 && s.height_percent >= 0 && avail_h > 0)
      h = avail_h * s.height_percent;
    // In border-box the height I was given already contains padding and border.
    if (forced_h < 0 && specified_height >= 0 && s.box_sizing == "border-box")
      h -= s.padding[0] + s.padding[2] + s.border[0] + s.border[2];
    if (s.max_height >= 0)
      h = std::min(h, s.max_height);
    h = std::max(h, s.min_height);
    double border_box = h + s.padding[0] + s.padding[2] + s.border[0] + s.border[2];
    if (bg != kNoPaint)
      out.paint[bg].rect.h = border_box;
    out.boxes[id] = {left, top, w + s.padding[1] + s.padding[3] + s.border[1] + s.border[3],
                     border_box};
    if (s.border[0] > 0)
      out.paint.push_back({PaintCommand::Kind::Border,
                           {left, top, out.boxes[id].w, border_box},
                           s.border_color.bgra(),
                           {},
                           0,
                           s.border[0],
                           false,
                           id,
                           {}, {}, {}});
    if (s.list_item)
      out.paint.push_back({PaintCommand::Kind::Text,
                           {left - 16, top, 12, s.font_size * 1.4},
                           s.color.bgra(),
                           "-",
                           s.font_size,
                           0,
                           false,
                           id,
                           {}, {}, {}});
    // Anything positioned is measured against the nearest positioned box.
    double box_w = w + s.padding[1] + s.padding[3] + s.border[1] + s.border[3];
    place_positioned(id, s, cb ? *cb : Rect{0, 0, viewport_width, viewport_height}, left, top,
                     box_w, border_box, depth);
    // A relative box keeps its place in the flow but paints where it is told.
    if (clips_children) {
      out.paint[clip_at].rect.h = border_box;
      out.paint.push_back({PaintCommand::Kind::PopClip,
                           {left, top, out.paint[clip_at].rect.w, border_box},
                           0,
                           {},
                           0,
                           0,
                           false,
                           id,
                           {},
                           {},
                           {}});
    }
    // Sticky stays in the flow until a scroll position asks for it, and I have
    // not built that clamp yet, so I leave it where the flow put it.
    if (s.position == "relative")
      move_box(start, id, s.left, s.top);
    if (!s.transform.empty())
      apply_transform(start, out.paint.size(), s, out.boxes[id], id);
    return border_box + s.margin[0] + s.margin[2];
  }
};
LayoutResult layout_document(const Document &doc, const std::vector<Style> &styles, double width,
                             double height, const std::map<std::string, Resource> &resources,
                             Canvas &measure) {
  Layout l(doc, styles, measure, resources, width);
  // The canvas is white unless the body asks for something else. A missing
  // background is transparent, not black.
  const Style &body_style = styles[doc.body];
  uint32_t bg = body_style.background.transparent ? 0xffffffffu : body_style.background.bgra();
  l.out.paint.push_back({PaintCommand::Kind::Rect,
                         {0, 0, width, std::max(height, 1.0)},
                         bg,
                         {},
                         0,
                         0,
                         false,
                         doc.body,
                         {}, {}, {}});
  l.viewport_height = height;
  Rect viewport{0, 0, width, height};
  double used = l.block(doc.body, 0, 0, width, 0, &viewport, -1, -1, height);
  l.out.height = std::max(height, used);
  l.out.paint[0].rect.h = l.out.height;
  return l.out;
}
// I parse the small linear and radial gradients a page uses into stops.
bool parse_gradient(const std::string &spec, Rect box, Gradient &out) {
  size_t open = spec.find("gradient(");
  if (open == std::string::npos)
    return false;
  bool radial = lower(spec.substr(0, open)).find("radial") != std::string::npos;
  size_t close = spec.rfind(')');
  if (close == std::string::npos || close <= open)
    return false;
  std::string body = spec.substr(open + 9, close - open - 9);
  auto parts = split(body, ',');
  std::erase_if(parts, [](const std::string &p) { return trim(p).empty(); });
  if (parts.empty())
    return false;
  double angle = 180; // CSS default is top to bottom
  size_t first_stop = 0;
  auto head = lower(trim(parts[0]));
  if (head.find("deg") != std::string::npos) {
    try {
      angle = std::stod(head);
    } catch (...) {
      angle = 180;
    }
    first_stop = 1;
  } else if (head.find("to ") == 0 || head.find("at ") != std::string::npos) {
    if (head.find("left") != std::string::npos)
      angle = 270;
    else if (head.find("right") != std::string::npos)
      angle = 90;
    else if (head.find("top") != std::string::npos)
      angle = 0;
    else
      angle = 180;
    first_stop = 1;
  }
  std::vector<std::pair<double, uint32_t>> raw;
  for (size_t i = first_stop; i < parts.size(); ++i) {
    auto text = trim(parts[i]);
    auto colour = parse_color(text);
    size_t stop_at = colour.transparent ? 0 : text.find('%');
    if (colour.transparent && text.find('%') != std::string::npos)
      continue;
    double at = -1;
    if (stop_at != std::string::npos) {
      try {
        at = std::clamp(std::stod(text.substr(0, stop_at)) / 100.0, 0.0, 1.0);
      } catch (...) {
      }
    }
    raw.emplace_back(at, colour.bgra());
    if (raw.size() >= 16)
      break;
  }
  if (raw.empty())
    return false;
  // Stops without a position are spread evenly between their neighbours.
  for (size_t i = 0; i < raw.size(); ++i)
    if (raw[i].first < 0)
      raw[i].first = raw.size() == 1 ? 0 : double(i) / double(raw.size() - 1);
  out = Gradient{};
  out.radial = radial;
  double radians = angle * 3.14159265358979323846 / 180.0;
  double cx = box.x + box.w / 2, cy = box.y + box.h / 2;
  if (radial) {
    out.x0 = cx;
    out.y0 = cy;
    out.r0 = 0;
    out.r1 = std::max(box.w, box.h) / 2;
  } else {
    // I project the box onto the gradient direction so the first stop starts at
    // one edge and the last ends at the other.
    double dx = std::sin(radians), dy = -std::cos(radians);
    double half = (std::fabs(dx) * box.w + std::fabs(dy) * box.h) / 2;
    out.x0 = cx - dx * half;
    out.y0 = cy - dy * half;
    out.x1 = cx + dx * half;
    out.y1 = cy + dy * half;
  }
  for (auto &[at, colour] : raw)
    out.stops.push_back({at, colour});
  return out.stops.size() >= 1;
}
std::vector<Rect> find_in_paint(const std::vector<PaintCommand> &commands,
                                std::string_view needle) {
  std::vector<Rect> found;
  if (needle.empty())
    return found;
  auto fold = [](std::string text) {
    for (char &ch : text)
      if (ch >= 'A' && ch <= 'Z')
        ch = char(ch - 'A' + 'a');
    return text;
  };
  std::string query = fold(std::string(needle));
  // I treat every run that shares a baseline as one line of text, so a word
  // split across spans still matches.
  struct Span {
    size_t start, end;
    double x, w;
  };
  struct Line {
    double y, h;
    std::string text;
    std::vector<Span> spans;
  };
  std::vector<Line> lines;
  for (const auto &command : commands) {
    if (command.kind != PaintCommand::Kind::Text || command.text.empty())
      continue;
    Line *line = nullptr;
    for (auto &candidate : lines)
      if (std::abs(candidate.y - command.rect.y) < 0.7) {
        line = &candidate;
        break;
      }
    if (!line) {
      lines.push_back({command.rect.y, std::max(12.0, command.rect.h), {}, {}});
      line = &lines.back();
    }
    size_t start = line->text.size();
    line->text += command.text;
    line->spans.push_back({start, line->text.size(), command.rect.x, command.rect.w});
  }
  for (const auto &line : lines) {
    std::string haystack = fold(line.text);
    size_t at = haystack.find(query);
    while (at != std::string::npos) {
      size_t end = at + query.size();
      Rect rect{0, line.y, 0, line.h};
      bool first = true;
      for (const auto &span : line.spans) {
        if (span.end <= at || span.start >= end)
          continue;
        double total = double(span.end - span.start);
        auto along = [&](size_t index) {
          double local = double(index - span.start);
          return total > 0 ? span.x + span.w * local / total : span.x;
        };
        double x0 = along(at > span.start ? at : span.start);
        double x1 = along(end < span.end ? end : span.end);
        if (first) {
          rect.x = x0;
          rect.w = x1 - x0;
          first = false;
        } else
          rect.w = x1 - rect.x;
      }
      if (rect.w > 0)
        found.push_back(rect);
      at = haystack.find(query, at + 1);
    }
  }
  return found;
}
void paint_commands(Canvas &canvas, const std::vector<PaintCommand> &commands, double scroll_y,
                    const std::map<std::string, Image> &images) {
  std::vector<Rect> clips;
  for (auto &c : commands) {
    Rect r = c.rect;
    if (!c.fixed)
      r.y -= scroll_y;
    if (r.y + r.h < 0 || r.y > canvas.height)
      continue;
    switch (c.kind) {
    case PaintCommand::Kind::PushClip: {
      // Overflow clips what is inside the box, so the stack survives nesting.
      Rect clipped{std::max(r.x, canvas.clip.x), std::max(r.y, canvas.clip.y), 0, 0};
      clipped.w = std::max(0.0, std::min(r.x + r.w, canvas.clip.x + canvas.clip.w) - clipped.x);
      clipped.h = std::max(0.0, std::min(r.y + r.h, canvas.clip.y + canvas.clip.h) - clipped.y);
      clips.push_back(canvas.clip);
      canvas.clip = clipped;
      break;
    }
    case PaintCommand::Kind::PopClip:
      if (!clips.empty()) {
        canvas.clip = clips.back();
        clips.pop_back();
      }
      break;
    case PaintCommand::Kind::Rect: {
      Gradient gradient;
      if (!c.gradient.empty() && parse_gradient(c.gradient, r, gradient))
        canvas.fill_gradient(r, gradient);
      else
        canvas.fill(r, c.color);
      break;
    }
    case PaintCommand::Kind::Border:
      canvas.stroke(r, c.color, c.thickness);
      break;
    case PaintCommand::Kind::Line:
      canvas.line(r.x, r.y, r.x + r.w, r.y, c.color, c.thickness);
      break;
    case PaintCommand::Kind::Text:
      canvas.text(r.x, r.y, c.text, c.size, c.color, c.bold);
      break;
    case PaintCommand::Kind::Canvas: {
      // My page hands me a finished bitmap for each canvas, keyed by its node.
      auto found = images.find(c.url);
      if (found != images.end() && found->second.valid())
        canvas.image(found->second, r);
      break;
    }
    case PaintCommand::Kind::Image: {
      auto found = images.find(c.url);
      if (found != images.end() && found->second.valid()) {
        canvas.image(found->second, r);
      } else {
        canvas.fill(r, 0xff202833);
        canvas.stroke(r, 0xff506070);
        canvas.text(r.x + 4, r.y + 4, c.text.empty() ? "[image unavailable]" : c.text, 12,
                    0xffb8c4d0);
      }
      break;
    }
    }
  }
}
} // namespace pv
