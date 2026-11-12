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
  const std::unordered_map<int, double> &scroll_offsets;
  LayoutResult out;
  double viewport_width;
  double viewport_height = 720, scroll_y = 0;
  Layout(const Document &d, const std::vector<Style> &s, Canvas &c,
         const std::map<std::string, Resource> &r, double w, double scroll,
         const std::unordered_map<int, double> &local_scrolls)
      : doc(d), styles(s), measure(c), resources(r), scroll_offsets(local_scrolls),
        viewport_width(w), scroll_y(scroll) {}
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
  double resolved_bound(const Style &s, const std::string &expression, double value,
                        double containing, double fallback, const char *axis) const {
    if (expression.empty()) return value;
    if (containing < 0) return fallback;
    return css_length(expression, containing, s.font_size, s.root_font_size,
                      s.viewport_width, axis, fallback, s.viewport_height);
  }
  Style resolve_box_style(const Style &source, double containing_width) const {
    Style result = source;
    static const char *side_names[] = {"top", "right", "bottom", "left"};
    for (int side = 0; side < 4; ++side) {
      const auto margin = result.raw.find(std::string("margin-") + side_names[side]);
      if (margin != result.raw.end() && margin->second.find('%') != std::string::npos)
        result.margin[side] = css_length(margin->second, containing_width, result.font_size,
                                         result.root_font_size, result.viewport_width, "margin",
                                         result.margin[side], result.viewport_height);
      const auto padding = result.raw.find(std::string("padding-") + side_names[side]);
      if (padding != result.raw.end() && padding->second.find('%') != std::string::npos)
        result.padding[side] = std::max(0.0, css_length(
            padding->second, containing_width, result.font_size, result.root_font_size,
            result.viewport_width, "padding", result.padding[side], result.viewport_height));
    }
    return result;
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
    if (id < 0 || id >= int(doc.nodes.size()) || out.fixed_boxes.count(id))
      return;
    auto it = out.boxes.find(id);
    if (it != out.boxes.end()) {
      it->second.x += dx;
      it->second.y += dy;
    }
    auto clip = out.clips.find(id);
    if (clip != out.clips.end()) {
      clip->second.x += dx;
      clip->second.y += dy;
    }
    auto scrollbar = out.scroll_containers.find(id);
    if (scrollbar != out.scroll_containers.end()) {
      scrollbar->second.viewport.x += dx; scrollbar->second.viewport.y += dy;
      scrollbar->second.track.x += dx; scrollbar->second.track.y += dy;
      scrollbar->second.thumb.x += dx; scrollbar->second.thumb.y += dy;
    }
    for (int c : doc.nodes[id].children)
      shift_boxes(c, dx, dy);
  }
  void mark_fixed(size_t from, int id) {
    std::function<void(int)> mark_boxes = [&](int node) {
      if (node < 0 || node >= int(doc.nodes.size())) return;
      out.fixed_boxes.insert(node);
      for (int child : doc.nodes[node].children) mark_boxes(child);
    };
    mark_boxes(id);
    for (size_t i = from; i < out.paint.size(); ++i)
      out.paint[i].fixed = true;
  }
  void apply_sticky_positions() {
    for (int id = 0; id < int(doc.nodes.size()); ++id) {
      if (styles[id].position != "sticky") continue;
      auto box = out.boxes.find(id);
      auto range = out.paint_ranges.find(id);
      if (box == out.boxes.end() || range == out.paint_ranges.end()) continue;
      const Rect normal = box->second;
      const Style &style = styles[id];
      const double parent_top = doc.nodes[id].parent >= 0 && out.boxes.count(doc.nodes[id].parent)
          ? out.boxes.at(doc.nodes[id].parent).y : 0;
      const double parent_bottom = doc.nodes[id].parent >= 0 && out.boxes.count(doc.nodes[id].parent)
          ? out.boxes.at(doc.nodes[id].parent).y + out.boxes.at(doc.nodes[id].parent).h
          : std::max(out.height, viewport_height);
      double target_y = normal.y;
      if (style.has_top) target_y = std::max(target_y, scroll_y + style.top);
      if (style.has_bottom)
        target_y = std::min(target_y, scroll_y + viewport_height - style.bottom - normal.h);
      if (style.has_top || style.has_bottom)
        target_y = std::clamp(target_y, parent_top, std::max(parent_top, parent_bottom - normal.h));
      const double dy = target_y - normal.y;
      if (dy != 0) move_box(range->second.first, id, 0, dy);
    }
  }
  // I lay a box out for its height only, then throw the commands away.
  double measure_h(int id, double available, int depth) {
    auto saved_paint = std::move(out.paint);
    auto saved_boxes = std::move(out.boxes);
    auto saved_clips = std::move(out.clips);
    out.paint.clear();
    out.boxes.clear();
    out.clips.clear();
    double h = block(id, 0, 0, available, depth + 1, nullptr);
    out.paint = std::move(saved_paint);
    out.boxes = std::move(saved_boxes);
    out.clips = std::move(saved_clips);
    return h;
  }
  double measure_w(int id, double available, int depth) {
    if (id < 0 || id >= int(doc.nodes.size()) || depth > kMaxDomDepth) return 0;
    const Node &node = doc.nodes[id];
    Style style = resolve_box_style(styles[id], available);
    if (node.type == Node::Type::Text)
      return measure.text_width(node.text, style.font_size, style.bold, style.font_family);
    if (node.type != Node::Type::Element || style.display == "none") return 0;
    const double specified = resolved_width(style, available);
    double content = specified;
    if (specified >= 0 && style.box_sizing == "border-box")
      content = std::max(0.0, specified - style.padding[1] - style.padding[3] -
                                    style.border[1] - style.border[3]);
    if (content < 0 && node.name == "img") {
      auto url = resolve_url(doc.url, node.attribute("src"));
      auto image = url ? resources.find(url->href()) : resources.end();
      content = image != resources.end() && image->second.ok && image->second.image_width > 0
                    ? image->second.image_width : 120;
      const std::string attr = trim(node.attribute("width"));
      if (!attr.empty()) content = std::max(0.0, number_of(attr, content));
    }
    if (content < 0) {
      std::string text = doc.text_content(id);
      if (node.name == "input" || node.name == "textarea" || node.name == "select")
        text = node.attribute("value").empty() ? node.attribute("placeholder") : node.attribute("value");
      content = measure.text_width(text, style.font_size, style.bold, style.font_family);
      for (int child : node.children)
        if (doc.nodes[child].type == Node::Type::Element)
          content = std::max(content, measure_w(child, available, depth + 1));
    }
    return std::max(0.0, content) + outer_extra(style);
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
    int depth = 0;
    char quote = 0;
    bool escaped = false;
    for (char c : text) {
      if (quote) {
        current += c;
        if (escaped) escaped = false;
        else if (c == '\\') escaped = true;
        else if (c == quote) quote = 0;
        continue;
      }
      if (c == '\'' || c == '"') { quote = c; current += c; continue; }
      if (c == '(') ++depth;
      else if (c == ')' && depth > 0) --depth;
      if ((c == ' ' || c == '\t' || c == '\n' || c == '\r') && depth == 0) {
        if (!current.empty()) out.push_back(std::move(current));
        current.clear();
      } else current += c;
    }
    if (!current.empty()) out.push_back(std::move(current));
    return out;
  }
  // I resolve fixed, percentage, fractional, repeat(), and a focused minmax()
  // subset. Intrinsic track contributions are added by grid_children afterward.
  std::vector<double> track_sizes(const std::string &text, double available, double gap,
                                  size_t limit = 12) {
    struct Track { double minimum = 0, fixed = -1, fraction = 0; };
    std::vector<std::string> flat;
    for (const std::string &token : tokens(text)) {
      if (token.rfind("repeat(", 0) != 0) { flat.push_back(token); continue; }
      const size_t close = token.rfind(')');
      if (close == std::string::npos) { flat.push_back(token); continue; }
      std::string inner = token.substr(7, close - 7);
      const size_t comma = inner.find(',');
      if (comma == std::string::npos) { flat.push_back(token); continue; }
      const int count = int(std::clamp(number_of(trim(inner.substr(0, comma)), 0), 0.0, 12.0));
      const auto each = tokens(inner.substr(comma + 1));
      for (int i = 0; i < count && flat.size() < limit * 4; ++i)
        flat.insert(flat.end(), each.begin(), each.end());
    }
    auto length = [&](const std::string &value) -> double {
      if (value.empty()) return -1;
      if (value.back() == '%')
        return std::max(0.0, number_of(value.substr(0, value.size() - 1), 0) *
                                  std::max(0.0, available) / 100.0);
      if (value.size() > 2 && value.ends_with("px"))
        return std::max(0.0, number_of(value.substr(0, value.size() - 2), 0));
      const double parsed = number_of(value, -1);
      return parsed >= 0 ? parsed : -1;
    };
    std::vector<Track> tracks;
    for (const std::string &raw : flat) {
      if (tracks.size() >= limit) break;
      Track track;
      if (raw.rfind("minmax(", 0) == 0 && raw.back() == ')') {
        const std::string inside = raw.substr(7, raw.size() - 8);
        const size_t comma = inside.find(',');
        if (comma != std::string::npos) {
          const std::string minimum = trim(inside.substr(0, comma));
          const std::string maximum = trim(inside.substr(comma + 1));
          track.minimum = length(minimum);
          if (track.minimum < 0) track.minimum = 0;
          const size_t fr = maximum.find("fr");
          if (fr != std::string::npos && fr + 2 == maximum.size())
            track.fraction = std::max(0.0, number_of(maximum.substr(0, fr), 1));
          else if (maximum == "auto" || maximum == "max-content" || maximum == "min-content")
            track.fraction = 1;
          else
            track.fixed = std::max(track.minimum, length(maximum));
        }
      } else {
        const size_t fr = raw.find("fr");
        if (fr != std::string::npos && fr + 2 == raw.size())
          track.fraction = std::max(0.0, number_of(raw.substr(0, fr), 1));
        else if (raw == "auto" || raw == "min-content" || raw == "max-content")
          track.fraction = 1;
        else
          track.fixed = length(raw);
      }
      if (track.fixed < 0 && track.fraction <= 0) track.fraction = 1;
      tracks.push_back(track);
    }
    double used = gap * std::max(0.0, double(tracks.size()) - 1.0);
    double fractions = 0;
    for (const Track &track : tracks) {
      used += track.fixed >= 0 ? std::max(track.minimum, track.fixed) : track.minimum;
      fractions += track.fraction;
    }
    const double share = fractions > 0
        ? std::max(0.0, available - used) / fractions : 0;
    std::vector<double> sizes;
    sizes.reserve(tracks.size());
    for (const Track &track : tracks)
      sizes.push_back(track.fixed >= 0 ? std::max(track.minimum, track.fixed)
                                      : track.minimum + share * track.fraction);
    return sizes;
  }
  // I move everything a box painted, which is how a relative offset works.
  void move_box(size_t from, int id, double dx, double dy) {
    shift_range(from, dx, dy);
    shift_boxes(id, dx, dy);
  }
  // ---- flex -------------------------------------------------------------
  void flex_children(int id, const Style &s, double inner_x, double inner_y, double inner_w,
                     double inner_h, double &cursor, int depth, const Rect *cb) {
    const auto &node = doc.nodes[id];
    const bool row = s.flex_direction == "row" || s.flex_direction == "row-reverse";
    const bool reverse = s.flex_direction == "row-reverse" || s.flex_direction == "column-reverse";
    const bool wrap = s.flex_wrap == "wrap" || s.flex_wrap == "wrap-reverse";
    const bool wrap_reverse = s.flex_wrap == "wrap-reverse";
    const double main_origin = row ? inner_x : inner_y;
    const double main_extent = row ? inner_w : inner_h;
    const double cross_origin = row ? inner_y : inner_x;
    const double cross_extent = row ? inner_h : inner_w;
    const double main_gap = row ? s.gap : (s.row_gap >= 0 ? s.row_gap : s.gap);
    const double cross_gap = row ? (s.row_gap >= 0 ? s.row_gap : s.gap) : s.gap;
    struct Entry {
      int id;
      Style style;
      double base, minimum, maximum, grow, shrink;
    };
    std::vector<Entry> entries;
    for (int child : node.children) {
      const Style &source = styles[child];
      if (source.display == "none" || doc.nodes[child].type != Node::Type::Element ||
          positioned(source)) continue;
      Style cs = resolve_box_style(source, inner_w);
      const double margin_main = row ? cs.margin[1] + cs.margin[3] : cs.margin[0] + cs.margin[2];
      const double padding_border_main = row
          ? cs.padding[1] + cs.padding[3] + cs.border[1] + cs.border[3]
          : cs.padding[0] + cs.padding[2] + cs.border[0] + cs.border[2];
      const double sizing_extra = margin_main +
          (cs.box_sizing == "border-box" ? 0.0 : padding_border_main);
      double specified = row ? resolved_width(cs, inner_w) : resolved_height(cs, inner_h);
      if (!row && specified < 0 && cs.height_percent >= 0 && inner_h >= 0)
        specified = inner_h * cs.height_percent;
      double flex_basis = cs.flex_basis;
      if (!cs.flex_basis_expression.empty() && main_extent >= 0)
        flex_basis = css_length(cs.flex_basis_expression, main_extent, cs.font_size,
                               cs.root_font_size, cs.viewport_width, "flex-basis",
                               flex_basis, cs.viewport_height);
      double base = -1;
      if (flex_basis >= 0) base = flex_basis + sizing_extra;
      else if (specified >= 0) base = specified + sizing_extra;
      else if (row) base = measure_w(child, inner_w, depth + 1);
      else base = measure_h(child, std::max(0.0, inner_w - cs.margin[1] - cs.margin[3]), depth);

      double minimum = row
          ? resolved_bound(cs, cs.min_width_expression, cs.min_width, inner_w, 0, "width")
          : resolved_bound(cs, cs.min_height_expression, cs.min_height, inner_h, 0, "height");
      double maximum = row
          ? resolved_bound(cs, cs.max_width_expression, cs.max_width, inner_w, -1, "width")
          : resolved_bound(cs, cs.max_height_expression, cs.max_height, inner_h, -1, "height");
      minimum = std::max(0.0, minimum);
      if (cs.box_sizing == "border-box") {
        minimum = std::max(0.0, minimum - padding_border_main);
        if (maximum >= 0) maximum = std::max(0.0, maximum - padding_border_main);
      }
      const double edges = margin_main + padding_border_main;
      minimum += edges;
      if (maximum >= 0) maximum += edges;
      else maximum = 1e12;
      base = std::clamp(std::max(0.0, base), minimum, std::max(minimum, maximum));
      entries.push_back({child, std::move(cs), base, minimum, maximum,
                         styles[child].flex_grow, styles[child].flex_shrink});
    }
    if (entries.empty()) return;
    std::stable_sort(entries.begin(), entries.end(), [](const Entry &a, const Entry &b) {
      return a.style.order < b.style.order;
    });
    if (reverse) std::reverse(entries.begin(), entries.end());

    std::vector<std::vector<size_t>> lines;
    std::vector<size_t> current;
    double line_base = 0;
    for (size_t i = 0; i < entries.size(); ++i) {
      const double add = entries[i].base + (current.empty() ? 0 : main_gap);
      if (wrap && main_extent >= 0 && !current.empty() && line_base + add > main_extent + 0.5) {
        lines.push_back(std::move(current));
        current.clear();
        line_base = 0;
      }
      if (!current.empty()) line_base += main_gap;
      current.push_back(i);
      line_base += entries[i].base;
    }
    if (!current.empty()) lines.push_back(std::move(current));

    struct LineLayout {
      std::vector<size_t> items;
      std::vector<double> sizes, cross_sizes;
      double used_main = 0, cross = 0, baseline = 0;
    };
    std::vector<LineLayout> layouts;
    for (const auto &indices : lines) {
      LineLayout line;
      line.items = indices;
      line.sizes.reserve(indices.size());
      for (size_t index : indices) line.sizes.push_back(entries[index].base);
      auto used_main = [&]() {
        double total = main_gap * std::max(0.0, double(indices.size()) - 1.0);
        for (double size : line.sizes) total += size;
        return total;
      };
      if (main_extent >= 0) {
        for (size_t pass = 0; pass <= indices.size(); ++pass) {
          const double free = main_extent - used_main();
          if (free > 0.001) {
            double total_grow = 0;
            for (size_t k = 0; k < indices.size(); ++k)
              if (entries[indices[k]].grow > 0 && line.sizes[k] + 0.001 < entries[indices[k]].maximum)
                total_grow += entries[indices[k]].grow;
            if (total_grow <= 0) break;
            bool frozen = false;
            for (size_t k = 0; k < indices.size(); ++k) {
              const Entry &entry = entries[indices[k]];
              if (entry.grow <= 0 || line.sizes[k] + 0.001 >= entry.maximum) continue;
              const double next = line.sizes[k] + free * entry.grow / total_grow;
              if (next >= entry.maximum) { line.sizes[k] = entry.maximum; frozen = true; }
              else line.sizes[k] = next;
            }
            if (!frozen) break;
          } else if (free < -0.001) {
            double total_shrink = 0;
            for (size_t k = 0; k < indices.size(); ++k) {
              const Entry &entry = entries[indices[k]];
              if (entry.shrink > 0 && line.sizes[k] > entry.minimum + 0.001)
                total_shrink += entry.shrink * std::max(0.0, entry.base -
                    (row ? entry.style.margin[1] + entry.style.margin[3] + entry.style.padding[1] +
                               entry.style.padding[3] + entry.style.border[1] + entry.style.border[3]
                         : entry.style.margin[0] + entry.style.margin[2] + entry.style.padding[0] +
                               entry.style.padding[2] + entry.style.border[0] + entry.style.border[2]));
            }
            if (total_shrink <= 0) break;
            bool frozen = false;
            for (size_t k = 0; k < indices.size(); ++k) {
              const Entry &entry = entries[indices[k]];
              const double edges = row
                  ? entry.style.margin[1] + entry.style.margin[3] + entry.style.padding[1] +
                        entry.style.padding[3] + entry.style.border[1] + entry.style.border[3]
                  : entry.style.margin[0] + entry.style.margin[2] + entry.style.padding[0] +
                        entry.style.padding[2] + entry.style.border[0] + entry.style.border[2];
              const double weight = entry.shrink * std::max(0.0, entry.base - edges);
              if (weight <= 0 || line.sizes[k] <= entry.minimum + 0.001) continue;
              const double next = line.sizes[k] + free * weight / total_shrink;
              if (next <= entry.minimum) { line.sizes[k] = entry.minimum; frozen = true; }
              else line.sizes[k] = next;
            }
            if (!frozen) break;
          } else break;
        }
      }
      line.used_main = used_main();
      for (size_t k = 0; k < indices.size(); ++k) {
        const Entry &entry = entries[indices[k]];
        const double edges_main = row
            ? entry.style.margin[1] + entry.style.margin[3] + entry.style.padding[1] +
                  entry.style.padding[3] + entry.style.border[1] + entry.style.border[3]
            : entry.style.margin[0] + entry.style.margin[2] + entry.style.padding[0] +
                  entry.style.padding[2] + entry.style.border[0] + entry.style.border[2];
        const double content_main = std::max(0.0, line.sizes[k] - edges_main);
        const double cross_size = row
            ? measure_h(entry.id, content_main, depth)
            : measure_w(entry.id, inner_w, depth + 1);
        line.cross_sizes.push_back(cross_size);
        line.cross = std::max(line.cross, cross_size);
        if (row) {
          const double baseline = entry.style.margin[0] + entry.style.border[0] +
              entry.style.padding[0] + entry.style.font_size * entry.style.line_height * 0.8;
          line.baseline = std::max(line.baseline, baseline);
        }
      }
      layouts.push_back(std::move(line));
    }

    double total_cross = cross_gap * std::max(0.0, double(layouts.size()) - 1.0);
    for (const auto &line : layouts) total_cross += line.cross;
    double free_cross = cross_extent >= 0 ? cross_extent - total_cross : 0;
    double cross_offset = 0, extra_cross_gap = 0;
    if (free_cross > 0) {
      if (s.align_content == "stretch") {
        const double share = free_cross / layouts.size();
        for (auto &line : layouts) line.cross += share;
        free_cross = 0;
      } else if (s.align_content == "flex-end" || s.align_content == "end") cross_offset = free_cross;
      else if (s.align_content == "center") cross_offset = free_cross / 2;
      else if (s.align_content == "space-between" && layouts.size() > 1)
        extra_cross_gap = free_cross / (layouts.size() - 1);
      else if (s.align_content == "space-around" && !layouts.empty()) {
        extra_cross_gap = free_cross / layouts.size();
        cross_offset = extra_cross_gap / 2;
      } else if (s.align_content == "space-evenly" && !layouts.empty()) {
        extra_cross_gap = free_cross / (layouts.size() + 1);
        cross_offset = extra_cross_gap;
      }
    }

    double cross_cursor = cross_offset;
    double max_main_used = 0;
    for (const LineLayout &line : layouts) {
      const double cross_pos = wrap_reverse && cross_extent >= 0
          ? cross_origin + cross_extent - cross_cursor - line.cross
          : cross_origin + cross_cursor;
      double used = line.used_main;
      double remaining = main_extent >= 0 ? std::max(0.0, main_extent - used) : 0;
      size_t auto_slots = 0;
      for (size_t index : line.items) {
        const Style &cs = entries[index].style;
        auto_slots += row ? size_t(cs.auto_left) + size_t(cs.auto_right)
                          : size_t(cs.auto_top) + size_t(cs.auto_bottom);
      }
      const double auto_share = auto_slots ? remaining / auto_slots : 0;
      if (auto_slots) remaining = 0;
      double main_offset = 0, extra_main_gap = 0;
      if (remaining > 0) {
        if (s.justify_content == "flex-end" || s.justify_content == "end") main_offset = remaining;
        else if (s.justify_content == "center") main_offset = remaining / 2;
        else if (s.justify_content == "space-between" && line.items.size() > 1)
          extra_main_gap = remaining / (line.items.size() - 1);
        else if (s.justify_content == "space-around" && !line.items.empty()) {
          extra_main_gap = remaining / line.items.size();
          main_offset = extra_main_gap / 2;
        } else if (s.justify_content == "space-evenly" && !line.items.empty()) {
          extra_main_gap = remaining / (line.items.size() + 1);
          main_offset = extra_main_gap;
        }
      }
      double main_cursor = reverse
          ? (main_extent >= 0 ? main_origin + main_extent - main_offset : main_origin + used)
          : main_origin + main_offset;
      for (size_t k = 0; k < line.items.size(); ++k) {
        const Entry &entry = entries[line.items[k]];
        const Style &cs = entry.style;
        const double item_size = line.sizes[k];
        const double edges_main = row
            ? cs.margin[1] + cs.margin[3] + cs.padding[1] + cs.padding[3] + cs.border[1] + cs.border[3]
            : cs.margin[0] + cs.margin[2] + cs.padding[0] + cs.padding[2] + cs.border[0] + cs.border[2];
        const double content_main = std::max(0.0, item_size - edges_main);
        const bool auto_start = row ? (reverse ? cs.auto_right : cs.auto_left)
                                    : (reverse ? cs.auto_bottom : cs.auto_top);
        const bool auto_end = row ? (reverse ? cs.auto_left : cs.auto_right)
                                  : (reverse ? cs.auto_top : cs.auto_bottom);
        const double leading_auto = auto_start ? auto_share : 0;
        const double trailing_auto = auto_end ? auto_share : 0;
        double main_pos;
        if (reverse) {
          main_pos = main_cursor - leading_auto - item_size;
          main_cursor -= item_size + leading_auto + trailing_auto + main_gap + extra_main_gap;
        } else {
          main_pos = main_cursor + leading_auto;
          main_cursor += item_size + leading_auto + trailing_auto + main_gap + extra_main_gap;
        }

        double line_item_cross = line.cross_sizes[k];
        const std::string &align = cs.align_self == "auto" ? s.align_items : cs.align_self;
        const bool cross_auto_start = row ? cs.auto_top : cs.auto_left;
        const bool cross_auto_end = row ? cs.auto_bottom : cs.auto_right;
        const double cross_free = std::max(0.0, line.cross - line_item_cross);
        double cross_leading = 0;
        if (cross_auto_start || cross_auto_end) {
          const size_t slots = size_t(cross_auto_start) + size_t(cross_auto_end);
          if (slots) {
            if (cross_auto_start) cross_leading = cross_free / slots;

          }
        }
        double cross_shift = cross_leading;
        if (!cross_auto_start && !cross_auto_end) {
          if (align == "center") cross_shift = cross_free / 2;
          else if (align == "flex-end" || align == "end") cross_shift = cross_free;
          else if (row && align == "baseline") {
            const double baseline = cs.margin[0] + cs.border[0] + cs.padding[0] +
                                    cs.font_size * cs.line_height * 0.8;
            cross_shift = std::max(0.0, line.baseline - baseline);
          }
        }
        const double non_content_cross = row
            ? cs.margin[0] + cs.margin[2] + cs.padding[0] + cs.padding[2] + cs.border[0] + cs.border[2]
            : cs.margin[1] + cs.margin[3] + cs.padding[1] + cs.padding[3] + cs.border[1] + cs.border[3];
        double forced_cross = -1;
        if (align == "stretch" && !cross_auto_start && !cross_auto_end &&
            (row ? cs.height < 0 && cs.height_percent < 0 : resolved_width(cs, inner_w) < 0))
          forced_cross = std::max(0.0, line.cross - non_content_cross);

        if (row) {
          const double y = cross_pos + cross_shift;
          const double x = main_pos;
          block(entry.id, x, y, item_size, depth + 1, cb, forced_cross, content_main,
                inner_h);
        } else {
          const double x = cross_pos + cross_shift;
          const double y = main_pos;
          double forced_width = forced_cross;
          if (align != "stretch" && resolved_width(cs, inner_w) < 0 &&
              !cross_auto_start && !cross_auto_end) {
            const double outer_intrinsic = measure_w(entry.id, inner_w, depth + 1);
            forced_width = std::max(0.0, outer_intrinsic - non_content_cross);
          }
          block(entry.id, x, y, cross_extent, depth + 1, cb, content_main,
                forced_width, inner_h);
        }
        max_main_used = std::max(max_main_used, line.used_main);
      }
      cross_cursor += line.cross + cross_gap + extra_cross_gap;
    }
    if (row)
      cursor = std::max(cursor, cross_origin + std::max(0.0, cross_cursor - cross_gap - extra_cross_gap));
    else
      cursor = std::max(cursor, inner_y + max_main_used);
  }
  // ---- grid -------------------------------------------------------------
  void grid_children(int id, const Style &s, double inner_x, double inner_y, double inner_w,
                     double inner_h, double &cursor, int depth, const Rect *cb) {
    const auto &node = doc.nodes[id];
    const double row_gap = s.row_gap >= 0 ? s.row_gap : s.gap;
    std::vector<int> items;
    for (int child : node.children) {
      const Style &style = styles[child];
      if (style.display == "none" || doc.nodes[child].type != Node::Type::Element || positioned(style))
        continue;
      items.push_back(child);
    }
    if (items.empty()) return;

    struct Area { size_t row0, row1, col0, col1; };
    std::map<std::string, Area> areas;
    std::vector<std::vector<std::string>> area_rows;
    std::string area_row;
    char quote = 0;
    bool escaped = false;
    for (char ch : s.grid_template_areas) {
      if (quote) {
        if (escaped) { area_row += ch; escaped = false; }
        else if (ch == '\\') escaped = true;
        else if (ch == quote) {
          area_rows.push_back(tokens(area_row));
          area_row.clear();
          quote = 0;
        } else area_row += ch;
      } else if (ch == '\'' || ch == '"') quote = ch;
    }
    size_t area_columns = 0;
    if (!area_rows.empty()) {
      area_columns = area_rows.front().size();
      bool rectangular = area_columns > 0;
      for (const auto &row_values : area_rows)
        if (row_values.size() != area_columns) rectangular = false;
      if (rectangular) {
        for (size_t r = 0; r < area_rows.size(); ++r)
          for (size_t c = 0; c < area_columns; ++c) {
            const std::string &name = area_rows[r][c];
            if (name == ".") continue;
            auto found = areas.find(name);
            if (found == areas.end()) areas[name] = {r, r + 1, c, c + 1};
            else {
              found->second.row0 = std::min(found->second.row0, r);
              found->second.row1 = std::max(found->second.row1, r + 1);
              found->second.col0 = std::min(found->second.col0, c);
              found->second.col1 = std::max(found->second.col1, c + 1);
            }
          }
        for (auto it = areas.begin(); it != areas.end();) {
          const Area area = it->second;
          size_t cells = 0;
          for (size_t r = area.row0; r < area.row1; ++r)
            for (size_t c = area.col0; c < area.col1; ++c)
              if (area_rows[r][c] == it->first) ++cells;
          if (cells != (area.row1 - area.row0) * (area.col1 - area.col0)) it = areas.erase(it);
          else ++it;
        }
      } else {
        area_rows.clear();
        area_columns = 0;
      }
    }

    struct Placement {
      size_t row = 0, col = 0, row_span = 1, col_span = 1;
      bool fixed_row = false, fixed_col = false, by_area = false;
    };
    auto split_slash = [](const std::string &text) {
      std::vector<std::string> parts;
      size_t start = 0;
      for (size_t i = 0; i <= text.size(); ++i)
        if (i == text.size() || text[i] == '/') {
          parts.push_back(trim(text.substr(start, i - start)));
          start = i + 1;
        }
      return parts;
    };
    auto parse_axis = [&](const std::string &text, size_t &start, size_t &span) {
      const auto parts = split_slash(text);
      auto value = [](const std::string &raw, long &line, size_t &span_value, bool &is_span) {
        const auto words = tokens(lower(trim(raw)));
        if (words.empty() || words[0] == "auto") return false;
        if (words[0] == "span") {
          is_span = true;
          if (words.size() > 1)
            span_value = size_t(std::clamp(number_of(words[1], 1), 1.0, 12.0));
          return true;
        }
        try {
          size_t used = 0;
          line = std::stol(words[0], &used);
          return used == words[0].size() && line != 0;
        } catch (...) { return false; }
      };
      if (parts.empty()) return false;
      long first_line = 0, second_line = 0;
      size_t first_span = 1, second_span = 1;
      bool first_is_span = false, second_is_span = false;
      const bool first_valid = value(parts[0], first_line, first_span, first_is_span);
      const bool second_valid = parts.size() > 1 &&
          value(parts[1], second_line, second_span, second_is_span);
      if (first_valid && !first_is_span) {
        start = first_line > 0 ? size_t(first_line - 1) : size_t(std::max<long>(0, 1000 + first_line));
        if (second_valid) {
          if (second_is_span) span = second_span;
          else if (second_line > 0) span = size_t(std::clamp<long>(second_line - first_line, 1, 12));
        }
        return true;
      }
      if (first_is_span) span = first_span;
      if (second_valid && !second_is_span && second_line > 0) {
        start = size_t(second_line - 1);
        return true;
      }
      return false;
    };
    std::vector<Placement> placements(items.size());
    size_t explicit_rows = std::max<size_t>(1, area_rows.size());
    size_t explicit_columns = std::max<size_t>(1, area_columns);
    for (size_t i = 0; i < items.size(); ++i) {
      const Style &style = styles[items[i]];
      Placement &place = placements[i];
      if (!style.grid_area.empty() && style.grid_area != "auto") {
        const std::string area_name = trim(style.grid_area);
        if (auto found = areas.find(area_name); found != areas.end()) {
          place.row = found->second.row0; place.row_span = found->second.row1 - found->second.row0;
          place.col = found->second.col0; place.col_span = found->second.col1 - found->second.col0;
          place.fixed_row = place.fixed_col = place.by_area = true;
        } else {
          const auto area_parts = split_slash(area_name);
          if (area_parts.size() >= 4) {
            size_t row = 0, row_span = 1, col = 0, col_span = 1;
            const bool has_row = parse_axis(area_parts[0] + " / " + area_parts[2], row, row_span);
            const bool has_col = parse_axis(area_parts[1] + " / " + area_parts[3], col, col_span);
            if (has_row) { place.row = row; place.row_span = row_span; place.fixed_row = true; }
            if (has_col) { place.col = col; place.col_span = col_span; place.fixed_col = true; }
          }
        }
      }
      if (!style.grid_row.empty() && lower(style.grid_row) != "auto")
        place.fixed_row = parse_axis(style.grid_row, place.row, place.row_span);
      if (!style.grid_column.empty() && lower(style.grid_column) != "auto")
        place.fixed_col = parse_axis(style.grid_column, place.col, place.col_span);
      explicit_rows = std::max(explicit_rows, place.row + place.row_span);
      explicit_columns = std::max(explicit_columns, place.col + place.col_span);
    }
    auto rows = track_sizes(s.grid_rows, inner_h, row_gap);
    explicit_rows = std::max(explicit_rows, rows.size());
    const bool column_flow = s.grid_auto_flow.find("column") != std::string::npos;
    const bool dense = s.grid_auto_flow.find("dense") != std::string::npos;
    auto columns = track_sizes(s.grid_columns, inner_w, s.gap);
    if (s.grid_columns.empty() && area_columns > 1) columns.clear();
    size_t column_count = std::max(explicit_columns, columns.empty() ? size_t(1) : columns.size());
    if (column_flow) {
      const size_t flow_rows = std::max<size_t>(1, explicit_rows);
      column_count = std::max(column_count, (items.size() + flow_rows - 1) / flow_rows);
    }
    column_count = std::min<size_t>(column_count, 144);
    if (columns.empty()) {
      const double share = std::max(0.0, inner_w - s.gap * (column_count - 1)) / column_count;
      columns.assign(column_count, share);
    } else if (columns.size() < column_count) {
      double occupied = s.gap * std::max(0.0, double(column_count) - 1.0);
      for (double size : columns) occupied += size;
      const double implicit = std::max(0.0, inner_w - occupied) /
                              std::max<size_t>(1, column_count - columns.size());
      columns.resize(column_count, implicit);
    }

    const size_t row_capacity = std::min<size_t>(144, column_flow
        ? std::max<size_t>(1, explicit_rows)
        : std::max(explicit_rows, items.size() * 12 + area_rows.size() + 1));
    std::vector<std::vector<bool>> occupied(row_capacity, std::vector<bool>(column_count, false));
    auto fits = [&](size_t row, size_t col, size_t row_span, size_t col_span) {
      if (row + row_span > row_capacity || col + col_span > column_count) return false;
      for (size_t r = row; r < row + row_span; ++r)
        for (size_t c = col; c < col + col_span; ++c)
          if (occupied[r][c]) return false;
      return true;
    };
    auto mark = [&](const Placement &place) {
      for (size_t r = place.row; r < std::min(row_capacity, place.row + place.row_span); ++r)
        for (size_t c = place.col; c < std::min(column_count, place.col + place.col_span); ++c)
          occupied[r][c] = true;
    };
    // Explicit placements reserve their cells before the auto-placement cursor runs.
    for (Placement &place : placements)
      if (place.fixed_row && place.fixed_col) mark(place);
    size_t cursor_row = 0, cursor_col = 0;
    for (Placement &place : placements) {
      if (place.fixed_row && place.fixed_col) continue;
      size_t begin_row = dense ? 0 : cursor_row;
      size_t begin_col = dense ? 0 : cursor_col;
      bool placed = false;
      if (place.fixed_row) {
        const size_t row = std::min(place.row, row_capacity - 1);
        for (size_t col = 0; col + place.col_span <= column_count; ++col)
          if (fits(row, col, place.row_span, place.col_span)) {
            place.row = row; place.col = col; placed = true; break;
          }
      } else if (place.fixed_col) {
        const size_t col = std::min(place.col, column_count - 1);
        for (size_t row = 0; row + place.row_span <= row_capacity; ++row)
          if (fits(row, col, place.row_span, place.col_span)) {
            place.col = col; place.row = row; placed = true; break;
          }
      } else {
        for (size_t step = 0; step < row_capacity * column_count && !placed; ++step) {
          size_t row, col;
          if (column_flow) {
            const size_t start = begin_col * row_capacity + begin_row + step;
            col = start / row_capacity; row = start % row_capacity;
          } else {
            const size_t start = begin_row * column_count + begin_col + step;
            row = start / column_count; col = start % column_count;
          }
          if (row >= row_capacity || col >= column_count) break;
          if (fits(row, col, place.row_span, place.col_span)) {
            place.row = row; place.col = col; placed = true;
          }
        }
      }
      if (!placed) { place.row = 0; place.col = 0; }
      mark(place);
      if (!dense) {
        if (column_flow) {
          cursor_row = place.row + place.row_span;
          cursor_col = place.col;
          if (cursor_row >= row_capacity) { cursor_row = 0; ++cursor_col; }
        } else {
          cursor_row = place.row;
          cursor_col = place.col + place.col_span;
          if (cursor_col >= column_count) { cursor_col = 0; ++cursor_row; }
        }
      }
    }
    size_t row_count = std::max<size_t>(area_rows.size(), 1);
    for (const Placement &place : placements)
      row_count = std::max(row_count, place.row + place.row_span);
    row_count = std::min(row_count, row_capacity);
    if (rows.size() > row_count) row_count = std::min(rows.size(), row_capacity);
    rows.resize(row_count, 0);
    if (rows.empty()) rows.push_back(0);
    struct Cell { int id; size_t row, col, row_span, col_span; double width, height; };
    std::vector<Cell> cells;
    for (size_t i = 0; i < items.size(); ++i) {
      const Placement &place = placements[i];
      double width = s.gap * std::max(0.0, double(place.col_span) - 1.0);
      for (size_t c = place.col; c < std::min(column_count, place.col + place.col_span); ++c)
        width += columns[c];
      const double height = measure_h(items[i], std::max(0.0, width), depth);
      cells.push_back({items[i], place.row, place.col, place.row_span, place.col_span, width, height});
      if (place.row_span == 1 && place.row < rows.size())
        rows[place.row] = std::max(rows[place.row], height);
    }
    for (const Cell &cell : cells) {
      if (cell.row_span <= 1 || cell.row + cell.row_span > rows.size()) continue;
      double current = row_gap * (cell.row_span - 1);
      for (size_t r = cell.row; r < cell.row + cell.row_span; ++r) current += rows[r];
      const double share = std::max(0.0, cell.height - current) / cell.row_span;
      for (size_t r = cell.row; r < cell.row + cell.row_span; ++r) rows[r] += share;
    }
    auto align_tracks = [](std::vector<double> &sizes, double origin, double extent,
                           double gap, const std::string &align) {
      double used = gap * std::max(0.0, double(sizes.size()) - 1.0);
      for (double size : sizes) used += size;
      double free = extent >= 0 ? extent - used : 0;
      double offset = 0, extra_gap = 0;
      if (free > 0) {
        if (align == "stretch" && !sizes.empty()) {
          const double share = free / sizes.size();
          for (double &size : sizes) size += share;
        } else if (align == "center") offset = free / 2;
        else if (align == "end" || align == "flex-end") offset = free;
        else if (align == "space-between" && sizes.size() > 1)
          extra_gap = free / (sizes.size() - 1);
        else if (align == "space-around" && !sizes.empty()) {
          extra_gap = free / sizes.size(); offset = extra_gap / 2;
        } else if (align == "space-evenly" && !sizes.empty()) {
          extra_gap = free / (sizes.size() + 1); offset = extra_gap;
        }
      }
      std::vector<double> positions(sizes.size());
      double at = origin + offset;
      for (size_t i = 0; i < sizes.size(); ++i) {
        positions[i] = at;
        at += sizes[i] + gap + extra_gap;
      }
      return positions;
    };
    const auto column_x = align_tracks(columns, inner_x, inner_w, s.gap, s.justify_content);
    const auto row_y = align_tracks(rows, inner_y, inner_h, row_gap, s.align_content);
    for (const Cell &cell : cells) {
      if (cell.row >= rows.size() || cell.col >= columns.size()) continue;
      double width = s.gap * std::max(0.0, double(cell.col_span) - 1.0);
      for (size_t c = cell.col; c < std::min(columns.size(), cell.col + cell.col_span); ++c)
        width += columns[c];
      double height = row_gap * std::max(0.0, double(cell.row_span) - 1.0);
      for (size_t r = cell.row; r < std::min(rows.size(), cell.row + cell.row_span); ++r)
        height += rows[r];
      const Style &style = styles[cell.id];
      const std::string justify = style.justify_self == "auto" ? s.justify_items : style.justify_self;
      const std::string align = style.align_self == "auto" ? s.align_items : style.align_self;
      const double measured_width = measure_w(cell.id, width, depth + 1);
      const double measured_height = measure_h(cell.id, width, depth);
      const double horizontal_edges = style.margin[1] + style.margin[3] + style.padding[1] +
                                      style.padding[3] + style.border[1] + style.border[3];
      const double vertical_edges = style.margin[0] + style.margin[2] + style.padding[0] +
                                    style.padding[2] + style.border[0] + style.border[2];
      const bool width_auto = resolved_width(style, width) < 0;
      const bool height_auto = resolved_height(style, height) < 0 && style.height_percent < 0;
      double forced_width = -1, forced_height = -1;
      double x_offset = 0, y_offset = 0;
      if (justify != "stretch" && width_auto) {
        const double outer = std::min(width, measured_width);
        forced_width = std::max(0.0, outer - horizontal_edges);
        if (justify == "center") x_offset = std::max(0.0, (width - outer) / 2);
        else if (justify == "end" || justify == "flex-end") x_offset = std::max(0.0, width - outer);
      } else if (!width_auto && (justify == "center" || justify == "end" || justify == "flex-end")) {
        const double outer = std::min(width, measured_width);
        x_offset = justify == "center" ? std::max(0.0, (width - outer) / 2)
                                        : std::max(0.0, width - outer);
      }
      if (height_auto && align == "stretch")
        forced_height = std::max(0.0, height - vertical_edges);
      else if (height_auto) {
        const double outer = std::min(height, measured_height);
        forced_height = std::max(0.0, outer - vertical_edges);
        if (align == "center") y_offset = std::max(0.0, (height - outer) / 2);
        else if (align == "end" || align == "flex-end") y_offset = std::max(0.0, height - outer);
      } else if (align == "center" || align == "end" || align == "flex-end") {
        const double outer = std::min(height, measured_height);
        y_offset = align == "center" ? std::max(0.0, (height - outer) / 2)
                                      : std::max(0.0, height - outer);
      }
      block(cell.id, column_x[cell.col] + x_offset, row_y[cell.row] + y_offset, width,
            depth + 1, cb, forced_height, forced_width, height);
    }
    double total_height = row_gap * std::max(0.0, double(rows.size()) - 1.0);
    for (double h : rows) total_height += h;
    cursor = std::max(cursor, inner_y + total_height);
  }
  // ---- positioning ------------------------------------------------------
  void place_positioned(int id, const Style &s, const Rect &cb, double left, double top,
                        double border_w, double border_h, int depth) {
    if (id < 0 || id >= int(doc.nodes.size())) return;
    Rect self{left + s.border[3], top + s.border[0],
              std::max(0.0, border_w - s.border[1] - s.border[3]),
              std::max(0.0, border_h - s.border[0] - s.border[2])};
    const bool layout_container = s.display == "flex" || s.display == "inline-flex" ||
                                  s.display == "grid" || s.display == "inline-grid";
    const bool establishes_containing_block = s.position != "static" || layout_container;
    Rect containing = establishes_containing_block ? self : cb;
    std::vector<int> children;
    for (int child : doc.nodes[id].children) {
      const Style &style = styles[child];
      if (style.display != "none" && doc.nodes[child].type == Node::Type::Element && positioned(style))
        children.push_back(child);
    }
    // Positioned siblings are painted in stacking order, retaining DOM order at
    // equal z-index. Each child's commands stay together as its stacking group.
    std::stable_sort(children.begin(), children.end(), [&](int a, int b) {
      return styles[a].z_index < styles[b].z_index;
    });
    for (int child : children) {
      const Style &style = styles[child];
      Rect used = style.position == "fixed" ? Rect{0, 0, viewport_width, viewport_height} : containing;
      double x = used.x + (style.has_left ? style.left : 0);
      double y = used.y + (style.has_top ? style.top : 0);
      const double extra_w = outer_extra(style);
      const double specified_width = resolved_width(style, used.w);
      if (style.has_right && !style.has_left)
        x = used.x + used.w - style.right - (specified_width >= 0 ? specified_width + extra_w : 0);
      const double extra_h = style.margin[0] + style.margin[2] + style.padding[0] +
                             style.padding[2] + style.border[0] + style.border[2];
      const double specified_height = resolved_height(style, used.h);
      if (style.has_bottom && !style.has_top)
        y = used.y + used.h - style.bottom -
            (specified_height >= 0 ? specified_height + extra_h : 0);
      const size_t from = out.paint.size();
      block(child, x, y, std::max(0.0, used.w - (style.has_left ? style.left : 0.0)),
            depth + 1, &used, -1, -1, used.h);
      if (style.position == "fixed") mark_fixed(from, child);
    }
  }
  // ---- transforms -------------------------------------------------------
  double transform_rotation(const std::string &text) const {
    double rotation = 0;
    size_t at = 0;
    while ((at = text.find('(', at)) != std::string::npos) {
      const size_t close = text.find(')', at);
      if (close == std::string::npos) break;
      std::string name = lower(trim(text.substr(0, at)));
      const size_t space = name.rfind(' ');
      if (space != std::string::npos) name = name.substr(space + 1);
      if (name == "rotate" || name == "rotatez") {
        const std::string angle = lower(trim(text.substr(at + 1, close - at - 1)));
        try {
          double value = std::stod(angle);
          if (angle.ends_with("rad")) value *= 180 / 3.14159265358979323846;
          else if (angle.ends_with("turn")) value *= 360;
          rotation += value;
        } catch (...) {}
      }
      at = close + 1;
    }
    return std::fmod(rotation, 360.0);
  }
  void apply_transform(size_t from, size_t to, const Style &s, const Rect &box, int id) {
    if (s.transform.empty() || s.transform == "none") return;
    double tx = 0, ty = 0, sx = 1, sy = 1;
    const double rotation = transform_rotation(s.transform);
    size_t p = 0;
    while ((p = s.transform.find('(', p)) != std::string::npos) {
      const size_t close = s.transform.find(')', p);
      if (close == std::string::npos) break;
      std::string name = lower(trim(s.transform.substr(0, p)));
      const size_t space = name.rfind(' ');
      if (space != std::string::npos) name = name.substr(space + 1);
      std::string args_text = s.transform.substr(p + 1, close - p - 1);
      for (char &ch : args_text) if (ch == ',') ch = ' ';
      const auto args = tokens(args_text);
      p = close + 1;
      if (name == "translate" || name == "translate3d") {
        if (!args.empty()) tx += number_of(args[0], 0);
        if (args.size() > 1) ty += number_of(args[1], 0);
      } else if (name == "translatex") {
        if (!args.empty()) tx += number_of(args[0], 0);
      } else if (name == "translatey") {
        if (!args.empty()) ty += number_of(args[0], 0);
      } else if (name == "scale") {
        const double x = args.empty() ? 1 : number_of(args[0], 1);
        sx *= x;
        sy *= args.size() > 1 ? number_of(args[1], x) : x;
      } else if (name == "scalex") {
        if (!args.empty()) sx *= number_of(args[0], 1);
      } else if (name == "scaley") {
        if (!args.empty()) sy *= number_of(args[0], 1);
      }
    }
    if (tx == 0 && ty == 0 && sx == 1 && sy == 1 && std::abs(rotation) < 0.001) return;
    const double ox = box.x + box.w / 2, oy = box.y + box.h / 2;
    const double rotate_x = tx + ox, rotate_y = ty + oy;
    auto scale_rect = [&](Rect &r) {
      const double x0 = tx + ox + (r.x - ox) * sx;
      const double y0 = ty + oy + (r.y - oy) * sy;
      r.x = x0; r.y = y0; r.w *= sx; r.h *= sy;
    };
    auto rotate_bounds = [&](Rect &r) {
      if (std::abs(rotation) < 0.001) return;
      const double radians = rotation * 3.14159265358979323846 / 180.0;
      const double cosine = std::cos(radians), sine = std::sin(radians);
      double min_x = std::numeric_limits<double>::max(), min_y = min_x;
      double max_x = std::numeric_limits<double>::lowest(), max_y = max_x;
      for (const Point corner : {Point{r.x, r.y}, Point{r.x + r.w, r.y},
                                  Point{r.x, r.y + r.h}, Point{r.x + r.w, r.y + r.h}}) {
        const double dx = corner.x - rotate_x, dy = corner.y - rotate_y;
        const double x = rotate_x + cosine * dx - sine * dy;
        const double y = rotate_y + sine * dx + cosine * dy;
        min_x = std::min(min_x, x); min_y = std::min(min_y, y);
        max_x = std::max(max_x, x); max_y = std::max(max_y, y);
      }
      r = {min_x, min_y, max_x - min_x, max_y - min_y};
    };
    std::vector<int> moved{id};
    for (size_t i = from; i < to && i < out.paint.size(); ++i) {
      PaintCommand &command = out.paint[i];
      scale_rect(command.rect);
      command.radius *= std::sqrt(std::abs(sx * sy));
      if (command.kind == PaintCommand::Kind::Text) command.size *= std::abs(sy);
      if (command.node >= 0 && std::find(moved.begin(), moved.end(), command.node) == moved.end())
        moved.push_back(command.node);
    }
    for (int node : moved) {
      if (auto found = out.boxes.find(node); found != out.boxes.end()) {
        scale_rect(found->second);
        rotate_bounds(found->second);
      }
      if (auto found = out.clips.find(node); found != out.clips.end()) {
        scale_rect(found->second);
        rotate_bounds(found->second);
      }
      if (auto found = out.scroll_containers.find(node); found != out.scroll_containers.end()) {
        scale_rect(found->second.viewport); rotate_bounds(found->second.viewport);
        scale_rect(found->second.track); rotate_bounds(found->second.track);
        scale_rect(found->second.thumb); rotate_bounds(found->second.thumb);
      }
    }
  }
  // In a column the cross axis is horizontal, so align-items only moves the box
  // when it has a width of its own.
  static double collapse_margins(double first, double second) {
    return std::max({0.0, first, second}) + std::min({0.0, first, second});
  }
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
    Style used_style = resolve_box_style(styles[id], available);
    const auto &s = used_style;
    if (s.display == "none" || n.type == Node::Type::Comment)
      return 0;
    if (n.type == Node::Type::Text)
      return 0;
    double mw = std::max(0.0, available - s.margin[1] - s.margin[3]);
    double sideways = s.padding[1] + s.padding[3] + s.border[1] + s.border[3];
    const double specified_width = resolved_width(s, available);
    const double specified_height = resolved_height(s, avail_h);
    double min_width = std::max(0.0, resolved_bound(s, s.min_width_expression, s.min_width,
                                                    available, 0, "width"));
    double max_width = resolved_bound(s, s.max_width_expression, s.max_width,
                                      available, -1, "width");
    if (max_width < 0) max_width = -1;
    if (s.box_sizing == "border-box") {
      min_width = std::max(0.0, min_width - sideways);
      if (max_width >= 0) max_width = std::max(0.0, max_width - sideways);
    }
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
    if (max_width >= 0) w = std::min(w, max_width);
    w = std::max({w, min_width, 0.0});
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
    double inner_h = specified_height >= 0 ? specified_height
                      : (s.height_percent >= 0 && avail_h > 0 ? avail_h * s.height_percent : -1);
    if (inner_h >= 0 && s.box_sizing == "border-box")
      inner_h = std::max(0.0, inner_h - s.padding[0] - s.padding[2] -
                                      s.border[0] - s.border[2]);
    const double border_box_w = w + s.padding[1] + s.padding[3] + s.border[1] + s.border[3];
    const double rotation = transform_rotation(s.transform);
    const bool effect_layer = s.opacity < 0.999999 || !s.filter.empty() || std::abs(rotation) > 0.001;
    size_t effect_layer_at = kNoPaint;
    size_t shadow_at = kNoPaint;
    if (effect_layer) {
      PaintCommand layer;
      layer.kind = PaintCommand::Kind::PushLayer;
      layer.rect = {left, top, border_box_w, 0};
      layer.node = id;
      out.paint.push_back(std::move(layer));
      effect_layer_at = out.paint.size() - 1;
    }
    if (!s.box_shadow.empty()) {
      std::string shadow_text = s.box_shadow;
      int shadow_depth = 0;
      char shadow_quote = 0;
      for (size_t i = 0; i < shadow_text.size(); ++i) {
        const char ch = shadow_text[i];
        if (shadow_quote) { if (ch == shadow_quote) shadow_quote = 0; continue; }
        if (ch == '\'' || ch == '"') { shadow_quote = ch; continue; }
        if (ch == '(') ++shadow_depth;
        else if (ch == ')') --shadow_depth;
        else if (ch == ',' && shadow_depth == 0) { shadow_text.resize(i); break; }
      }
      auto parts = tokens(shadow_text);
      double lengths[4] = {0, 0, 0, 0};
      size_t length_count = 0;
      uint32_t shadow_color = s.color.bgra();
      for (const std::string &part : parts) {
        const std::string low = lower(part);
        if (low == "inset") continue;
        const Color parsed = parse_color(part);
        if (!parsed.transparent || low == "transparent" || low == "currentcolor") {
          shadow_color = lower(part) == "currentcolor" ? s.color.bgra() : parsed.bgra();
        } else if (length_count < 4) {
          lengths[length_count++] = css_length(part, inner_w, s.font_size, s.root_font_size,
                                                s.viewport_width, "box-shadow", 0,
                                                s.viewport_height);
        }
      }
      if (length_count >= 2) {
        PaintCommand shadow;
        shadow.kind = PaintCommand::Kind::Shadow;
        shadow.rect = {left, top, border_box_w, 0};
        shadow.color = shadow_color;
        shadow.node = id;
        shadow.radius = s.radius;
        shadow.offset_x = lengths[0];
        shadow.offset_y = lengths[1];
        shadow.blur = length_count > 2 ? std::max(0.0, lengths[2]) : 0;
        shadow.spread = length_count > 3 ? lengths[3] : 0;
        out.paint.push_back(std::move(shadow));
        shadow_at = out.paint.size() - 1;
      }
    }
    // Paint the solid color underneath image/gradient layers, back to front.
    std::vector<size_t> background_commands;
    if (!s.background.transparent) {
      PaintCommand background;
      background.kind = PaintCommand::Kind::Rect;
      background.rect = {left, top, border_box_w, 0};
      background.color = s.background.bgra();
      background.node = id;
      background.radius = s.radius;
      out.paint.push_back(std::move(background));
      background_commands.push_back(out.paint.size() - 1);
    }
    std::vector<std::string> layers = s.background_layers;
    if (layers.empty() && !s.background_image.empty()) layers.push_back(s.background_image);
    for (auto it = layers.rbegin(); it != layers.rend(); ++it) {
      const std::string lowered_layer = lower(*it);
      if (lowered_layer.find("gradient(") != std::string::npos) {
        PaintCommand image_layer;
        image_layer.kind = PaintCommand::Kind::Rect;
        image_layer.rect = {left, top, border_box_w, 0};
        image_layer.node = id;
        image_layer.gradient = *it;
        image_layer.radius = s.radius;
        out.paint.push_back(std::move(image_layer));
        background_commands.push_back(out.paint.size() - 1);
      } else if (lowered_layer.find("url(") != std::string::npos) {
        const size_t open = lowered_layer.find("url(");
        const size_t close = it->find(')', open + 4);
        if (close == std::string::npos) continue;
        std::string source = trim(it->substr(open + 4, close - open - 4));
        if (source.size() >= 2 && ((source.front() == '\'' && source.back() == '\'') ||
                                   (source.front() == '"' && source.back() == '"')))
          source = source.substr(1, source.size() - 2);
        auto url = resolve_url(doc.url, source);
        if (!url) continue;
        PaintCommand image_layer;
        image_layer.kind = PaintCommand::Kind::Image;
        image_layer.rect = {left, top, border_box_w, 0};
        image_layer.node = id;
        image_layer.url = url->href();
        image_layer.radius = s.radius;
        out.paint.push_back(std::move(image_layer));
        background_commands.push_back(out.paint.size() - 1);
      }
    }
    // Overflow hides what spills out, so I open a clip here and close it after
    // the last child has painted.
    bool clips_children = s.overflow == "hidden" || s.overflow == "clip" || s.overflow == "auto" ||
                          s.overflow == "scroll";
    size_t clip_at = 0;
    if (clips_children) {
      out.paint.push_back({PaintCommand::Kind::PushClip,
                           {left + s.border[3], top + s.border[0],
                            w + s.padding[1] + s.padding[3], 0},
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
      out.paint[clip_at].radius = s.radius;
    }
    const size_t children_at = out.paint.size();
    double cursor = inner_y;
    if (s.display == "flex" || s.display == "inline-flex") {
      flex_children(id, s, inner_x, inner_y, inner_w, inner_h, cursor, depth, cb);
    } else if (s.display == "grid" || s.display == "inline-grid") {
      grid_children(id, s, inner_x, inner_y, inner_w, inner_h, cursor, depth, cb);
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
        Style child_style = resolve_box_style(styles[c], inner_w);
        const auto &cs = child_style;
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
          double specified_height = resolved_height(cs, avail_h);
          if (specified_height < 0 && cs.height_percent >= 0 && avail_h > 0)
            specified_height = avail_h * cs.height_percent;
          double iw = specified_width >= 0 ? specified_width : aw >= 0 ? aw : natural_w > 0 ? natural_w : 120;
          double ih = specified_height >= 0 ? specified_height
                      : ah >= 0 ? ah
                      : natural_w > 0 && natural_h > 0 ? iw * natural_h / natural_w
                      : aw >= 0 && natural_w > 0 ? aw * natural_h / natural_w : 80;
          if (specified_width < 0 && cs.height >= 0 && natural_w > 0 && natural_h > 0)
            iw = ih * natural_w / natural_h;
          const double horizontal_extra = cs.padding[1] + cs.padding[3] + cs.border[1] + cs.border[3];
          const double vertical_extra = cs.padding[0] + cs.padding[2] + cs.border[0] + cs.border[2];
          if (cs.box_sizing == "border-box") {
            if (specified_width >= 0) iw = std::max(0.0, iw - horizontal_extra);
            if (specified_height >= 0) ih = std::max(0.0, ih - vertical_extra);
          }
          double minw = std::max(0.0, resolved_bound(cs, cs.min_width_expression, cs.min_width,
                                                     inner_w, 0, "width"));
          double maxw = resolved_bound(cs, cs.max_width_expression, cs.max_width, inner_w, -1, "width");
          double minh = std::max(0.0, resolved_bound(cs, cs.min_height_expression, cs.min_height,
                                                     avail_h, 0, "height"));
          double maxh = resolved_bound(cs, cs.max_height_expression, cs.max_height, avail_h, -1, "height");
          if (cs.box_sizing == "border-box") {
            minw = std::max(0.0, minw - horizontal_extra);
            if (maxw >= 0) maxw = std::max(0.0, maxw - horizontal_extra);
            minh = std::max(0.0, minh - vertical_extra);
            if (maxh >= 0) maxh = std::max(0.0, maxh - vertical_extra);
          }
          const bool auto_width = specified_width < 0 && aw < 0;
          const bool auto_height = specified_height < 0 && ah < 0;
          if (auto_width && auto_height && iw > 0 && ih > 0) {
            const double min_scale = std::max({0.0, minw / iw, minh / ih});
            const double max_scale = std::min(maxw >= 0 ? maxw / iw : 1e9,
                                              maxh >= 0 ? maxh / ih : 1e9);
            const double scale = max_scale >= min_scale
                                     ? std::clamp(1.0, min_scale, max_scale)
                                     : min_scale;
            iw *= scale;
            ih *= scale;
          } else {
            const double ratio = ih > 0 ? iw / ih : 0;
            if (maxw >= 0) iw = std::min(iw, maxw);
            iw = std::max(iw, minw);
            if (maxh >= 0) ih = std::min(ih, maxh);
            ih = std::max(ih, minh);
            if (auto_height && ratio > 0) ih = iw / ratio;
            if (auto_width && ratio > 0) iw = ih * ratio;
            if (maxw >= 0) iw = std::min(iw, maxw);
            iw = std::max(iw, minw);
            if (maxh >= 0) ih = std::min(ih, maxh);
            ih = std::max(ih, minh);
          }
          iw = std::clamp(iw, 0.0, 16384.0);
          ih = std::clamp(ih, 0.0, 16384.0);
          const double outer_x = inner_x + cs.margin[3];
          const double outer_y = cursor + cs.margin[0];
          const double outer_w = iw + horizontal_extra;
          const double outer_h = ih + vertical_extra;
          out.paint.push_back({PaintCommand::Kind::Image,
                               {outer_x + cs.border[3] + cs.padding[3],
                                outer_y + cs.border[0] + cs.padding[0], iw, ih},
                               0,
                               child.attribute("alt"),
                               cs.font_size,
                               0,
                               false,
                               c,
                               key, {}, {}});
          out.boxes[c] = {outer_x, outer_y, outer_w, outer_h};
          cursor += outer_h + cs.margin[0] + cs.margin[2];          return;
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
          std::string label = child.name == "button" || child.name == "textarea"
              ? doc.text_content(c) : child.attribute("value");
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
      if (child_height >= 0 && s.box_sizing == "border-box")
        child_height = std::max(0.0, child_height - s.padding[0] - s.padding[2] -
                                      s.border[0] - s.border[2]);
      for (int c : n.children) {
        Style child_style = resolve_box_style(styles[c], inner_w);
        const auto &cs = child_style;
        if (cs.display == "none" || positioned(cs))
          continue;
        bool is_block = doc.nodes[c].type == Node::Type::Element && cs.display != "inline" &&
                        cs.display != "inline-block" && c != 0;
        if (is_block) {
          flush();
          // Positive margins collapse to the largest; negative margins add to
          // the largest positive (or the least-negative margin if both are negative).
          double gap = collapse_margins(last_margin, cs.margin[0]);
          double y = cursor - last_margin + gap - cs.margin[0];
          double h = block(c, inner_x, y, inner_w, depth + 1, cb, -1, -1, child_height);
          cursor = y + h;
          last_margin = cs.margin[2];
        } else {
          inline_child(c, 0);
          // In-flow inline content breaks vertical margin adjacency.
          last_margin = 0;
        }
      }
      flush();
    }
    double content_h = cursor - inner_y;
    const double vertical_extra = s.padding[0] + s.padding[2] + s.border[0] + s.border[2];
    double min_height = std::max(0.0, resolved_bound(s, s.min_height_expression, s.min_height,
                                                    avail_h, 0, "height"));
    double max_height = resolved_bound(s, s.max_height_expression, s.max_height,
                                       avail_h, -1, "height");
    if (max_height < 0) max_height = -1;
    if (s.box_sizing == "border-box") {
      min_height = std::max(0.0, min_height - vertical_extra);
      if (max_height >= 0) max_height = std::max(0.0, max_height - vertical_extra);
    }
    double h = forced_h >= 0 ? forced_h : (specified_height >= 0 ? specified_height : content_h);
    const bool percent_height_used = forced_h < 0 && specified_height < 0 &&
                                     s.height_percent >= 0 && avail_h > 0;
    if (percent_height_used) h = avail_h * s.height_percent;
    // CSS height/min/max on a border-box include padding and border; layout
    // stores the content size, so convert all constraints into that space.
    if (forced_h < 0 && s.box_sizing == "border-box" &&
        (specified_height >= 0 || percent_height_used)) h = std::max(0.0, h - vertical_extra);
    if (max_height >= 0) h = std::min(h, max_height);
    h = std::max({h, min_height, 0.0});
    double border_box = h + vertical_extra;
    double local_scroll = 0;
    double local_scroll_max = 0;
    const bool scrollable = s.overflow == "scroll" ||
                            (s.overflow == "auto" && content_h > h + 0.5);
    if (scrollable && h > 0) {
      local_scroll_max = std::max(0.0, content_h - h);
      if (auto stored = scroll_offsets.find(id); stored != scroll_offsets.end())
        local_scroll = std::clamp(stored->second, 0.0, local_scroll_max);
      if (local_scroll > 0) {
        shift_range(children_at, 0, -local_scroll);
        for (int child : doc.nodes[id].children) shift_boxes(child, 0, -local_scroll);
      }
    }
    for (size_t command : background_commands)
      if (command < out.paint.size()) out.paint[command].rect.h = border_box;
    if (shadow_at != kNoPaint && shadow_at < out.paint.size())
      out.paint[shadow_at].rect.h = border_box;
    if (effect_layer_at != kNoPaint && effect_layer_at < out.paint.size())
      out.paint[effect_layer_at].rect.h = border_box;
    out.boxes[id] = {left, top, border_box_w, border_box};
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
      const Rect clip_rect{left + s.border[3], top + s.border[0],
                           w + s.padding[1] + s.padding[3], h + s.padding[0] + s.padding[2]};
      out.paint[clip_at].rect = clip_rect;
      out.clips[id] = clip_rect;
      out.paint.push_back({PaintCommand::Kind::PopClip,
                           clip_rect,
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
    if (*std::max_element(std::begin(s.border), std::end(s.border)) > 0) {
      PaintCommand border;
      border.kind = PaintCommand::Kind::Border;
      border.rect = {left, top, out.boxes[id].w, border_box};
      border.color = s.border_color.bgra();
      border.thickness = *std::max_element(std::begin(s.border), std::end(s.border));
      border.node = id;
      border.radius = s.radius;
      out.paint.push_back(std::move(border));
    }
    if (s.outline_width > 0) {
      const double expansion = s.outline_offset + s.outline_width / 2;
      PaintCommand outline;
      outline.kind = PaintCommand::Kind::Outline;
      outline.rect = {left - expansion, top - expansion,
                      out.boxes[id].w + expansion * 2, border_box + expansion * 2};
      outline.color = s.outline_color.bgra();
      outline.thickness = s.outline_width;
      outline.radius = std::max(0.0, s.radius + expansion);
      outline.node = id;
      out.paint.push_back(std::move(outline));
    }
    if (scrollable && h > 0) {
      const double track_width = 7;
      const Rect viewport{left + s.border[3], top + s.border[0],
                          w + s.padding[1] + s.padding[3],
                          h + s.padding[0] + s.padding[2]};
      const Rect track{viewport.x + viewport.w - track_width, viewport.y,
                       track_width, viewport.h};
      const double thumb_height = std::min(track.h,
          std::max(14.0, track.h * track.h / std::max(content_h, track.h)));
      const double thumb_travel = std::max(0.0, track.h - thumb_height);
      const double thumb_y = track.y + (local_scroll_max > 0
          ? thumb_travel * local_scroll / local_scroll_max : 0);
      const Rect thumb{track.x, thumb_y, track.w, thumb_height};
      out.scroll_containers[id] = {viewport, track, thumb, local_scroll, local_scroll_max,
                                   content_h, s.overscroll_behavior};
      out.paint.push_back({PaintCommand::Kind::Rect, track,
                           0x30607080u, {}, 0, 0, false, id, {}, {}, {}});
      out.paint.push_back({PaintCommand::Kind::Rect, thumb,
                           0xff71869au, {}, 0, 0, false, id, {}, {}, {}});
    }
    if (effect_layer) {
      PaintCommand layer;
      layer.kind = PaintCommand::Kind::PopLayer;
      layer.rect = out.boxes[id];
      layer.node = id;
      layer.opacity = s.opacity;
      layer.filter = s.filter;
      layer.rotation = rotation;
      out.paint.push_back(std::move(layer));
    }
    if (s.position == "relative") {
      const double dx = s.has_left ? s.left : (s.has_right ? -s.right : 0);
      const double dy = s.has_top ? s.top : (s.has_bottom ? -s.bottom : 0);
      move_box(start, id, dx, dy);
    }
    if (!s.transform.empty())
      apply_transform(start, out.paint.size(), s, out.boxes[id], id);
    out.paint_ranges[id] = {start, out.paint.size()};
    return border_box + s.margin[0] + s.margin[2];
  }
};
LayoutResult layout_document(const Document &doc, const std::vector<Style> &styles, double width,
                             double height, const std::map<std::string, Resource> &resources,
                             Canvas &measure, double scroll_y,
                             const std::unordered_map<int, double> &scroll_offsets) {
  Layout l(doc, styles, measure, resources, width, scroll_y, scroll_offsets);
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
  l.apply_sticky_positions();
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
  struct ClipState { Canvas *canvas; Rect clip; size_t rounded_count; };
  struct LayerState {
    std::unique_ptr<Canvas> scratch;
    Canvas *parent = nullptr;
    size_t clip_depth = 0;
  };
  Canvas *target = &canvas;
  std::vector<ClipState> clips;
  std::vector<LayerState> layers;
  auto get_number = [](const std::string &text, double fallback) {
    try {
      size_t used = 0;
      const double value = std::stod(text, &used);
      return used ? value : fallback;
    } catch (...) { return fallback; }
  };
  auto apply_filter = [&](Canvas &surface, const std::string &filter) {
    size_t at = 0;
    while ((at = filter.find('(', at)) != std::string::npos) {
      const size_t close = filter.find(')', at);
      if (close == std::string::npos) break;
      size_t name_start = at;
      while (name_start > 0 && (std::isalnum(static_cast<unsigned char>(filter[name_start - 1])) ||
                                filter[name_start - 1] == '-')) --name_start;
      const std::string name = lower(filter.substr(name_start, at - name_start));
      std::string argument = trim(filter.substr(at + 1, close - at - 1));
      double amount = get_number(argument, 1);
      if (lower(argument).ends_with("%")) amount /= 100;
      amount = std::clamp(amount, 0.0, name == "brightness" || name == "contrast" ||
                                           name == "saturate" ? 8.0 : 1.0);
      for (size_t i = 0; i + 3 < surface.pixels.size(); i += 4) {
        double b = surface.pixels[i], g = surface.pixels[i + 1], r = surface.pixels[i + 2];
        if (name == "brightness") { r *= amount; g *= amount; b *= amount; }
        else if (name == "contrast") {
          r = (r - 128) * amount + 128; g = (g - 128) * amount + 128; b = (b - 128) * amount + 128;
        } else if (name == "grayscale" || name == "saturate") {
          const double luminance = 0.2126 * r + 0.7152 * g + 0.0722 * b;
          const double saturation = name == "grayscale" ? 1 - amount : amount;
          r = luminance + (r - luminance) * saturation;
          g = luminance + (g - luminance) * saturation;
          b = luminance + (b - luminance) * saturation;
        } else if (name == "invert") {
          r = r * (1 - amount) + (255 - r) * amount;
          g = g * (1 - amount) + (255 - g) * amount;
          b = b * (1 - amount) + (255 - b) * amount;
        } else if (name == "sepia") {
          const double nr = std::min(255.0, r * (1 - 0.607 * amount) + g * 0.769 * amount + b * 0.189 * amount);
          const double ng = std::min(255.0, r * 0.349 * amount + g * (1 - 0.314 * amount) + b * 0.168 * amount);
          const double nb = std::min(255.0, r * 0.272 * amount + g * 0.534 * amount + b * (1 - 0.869 * amount));
          r = nr; g = ng; b = nb;
        } else if (name == "opacity") {
          surface.pixels[i + 3] = uint8_t(std::lround(surface.pixels[i + 3] * amount));
        }
        surface.pixels[i] = uint8_t(std::clamp(std::lround(b), 0l, 255l));
        surface.pixels[i + 1] = uint8_t(std::clamp(std::lround(g), 0l, 255l));
        surface.pixels[i + 2] = uint8_t(std::clamp(std::lround(r), 0l, 255l));
      }
      at = close + 1;
    }
  };
  auto composite = [&](LayerState &layer, const PaintCommand &command, Rect pivot) {
    Canvas &source = *layer.scratch;
    Canvas &destination = *layer.parent;
    apply_filter(source, command.filter);
    const double opacity = std::clamp(command.opacity, 0.0, 1.0);
    const double angle = command.rotation * 3.14159265358979323846 / 180.0;
    const double cosine = std::cos(angle), sine = std::sin(angle);
    const double cx = pivot.x + pivot.w / 2, cy = pivot.y + pivot.h / 2;
    const double radius = std::sqrt(pivot.w * pivot.w + pivot.h * pivot.h) / 2;
    const int x0 = std::clamp(int(std::floor(command.rotation == 0 ? 0 : cx - radius)), 0, destination.width);
    const int y0 = std::clamp(int(std::floor(command.rotation == 0 ? 0 : cy - radius)), 0, destination.height);
    const int x1 = std::clamp(int(std::ceil(command.rotation == 0 ? source.width : cx + radius)), 0, destination.width);
    const int y1 = std::clamp(int(std::ceil(command.rotation == 0 ? source.height : cy + radius)), 0, destination.height);
    for (int y = y0; y < y1; ++y)
      for (int x = x0; x < x1; ++x) {
        double sx = x + 0.5, sy = y + 0.5;
        if (std::abs(command.rotation) > 0.001) {
          const double dx = sx - cx, dy = sy - cy;
          sx = cx + cosine * dx + sine * dy;
          sy = cy - sine * dx + cosine * dy;
        }
        const int ix = int(std::floor(sx)), iy = int(std::floor(sy));
        if (ix < 0 || iy < 0 || ix >= source.width || iy >= source.height) continue;
        const size_t offset = (size_t(iy) * source.width + ix) * 4;
        uint32_t alpha = source.pixels[offset + 3];
        if (!alpha) continue;
        alpha = uint32_t(std::lround(alpha * opacity));
        const uint32_t color = (alpha << 24) | (uint32_t(source.pixels[offset + 2]) << 16) |
                               (uint32_t(source.pixels[offset + 1]) << 8) | source.pixels[offset];
        destination.pixel(x, y, color);
      }
  };
  for (const PaintCommand &command : commands) {
    Rect r = command.rect;
    if (!command.fixed) r.y -= scroll_y;
    if (command.kind == PaintCommand::Kind::PushLayer) {
      LayerState layer;
      layer.parent = target;
      layer.clip_depth = clips.size();
      layer.scratch = std::make_unique<Canvas>(target->width, target->height, 0x00000000u);
      // Parent clips apply when the completed group is composited, so transforms
      // can move the whole group through the clip without clipping its source.
      target = layer.scratch.get();
      layers.push_back(std::move(layer));
      continue;
    }
    if (command.kind == PaintCommand::Kind::PopLayer) {
      if (layers.empty()) continue;
      LayerState layer = std::move(layers.back());
      layers.pop_back();
      while (clips.size() > layer.clip_depth) {
        ClipState state = clips.back(); clips.pop_back();
        state.canvas->clip = state.clip;
        state.canvas->rounded_clips.resize(state.rounded_count);
      }
      target = layer.parent;
      composite(layer, command, r);
      continue;
    }
    if (command.kind == PaintCommand::Kind::PushClip) {
      Rect clipped{std::max(r.x, target->clip.x), std::max(r.y, target->clip.y), 0, 0};
      clipped.w = std::max(0.0, std::min(r.x + r.w, target->clip.x + target->clip.w) - clipped.x);
      clipped.h = std::max(0.0, std::min(r.y + r.h, target->clip.y + target->clip.h) - clipped.y);
      clips.push_back({target, target->clip, target->rounded_clips.size()});
      target->clip = clipped;
      if (command.radius > 0) target->rounded_clips.emplace_back(r, command.radius);
      continue;
    }
    if (command.kind == PaintCommand::Kind::PopClip) {
      if (!clips.empty()) {
        ClipState state = clips.back(); clips.pop_back();
        state.canvas->clip = state.clip;
        state.canvas->rounded_clips.resize(state.rounded_count);
      }
      continue;
    }
    if (r.y + r.h < 0 || r.y > target->height) continue;
    switch (command.kind) {
    case PaintCommand::Kind::Rect: {
      Gradient gradient;
      if (!command.gradient.empty() && parse_gradient(command.gradient, r, gradient))
        target->fill_gradient(r, gradient, command.radius);
      else if (command.radius > 0) target->fill_rounded(r, command.radius, command.color);
      else target->fill(r, command.color);
      break;
    }
    case PaintCommand::Kind::Border:
      if (command.radius > 0) target->stroke_rounded(r, command.radius, command.color, command.thickness);
      else target->stroke(r, command.color, command.thickness);
      break;
    case PaintCommand::Kind::Outline:
      if (command.radius > 0) target->stroke_rounded(r, command.radius, command.color, command.thickness);
      else target->stroke(r, command.color, command.thickness);
      break;
    case PaintCommand::Kind::Shadow:
      target->box_shadow(r, command.radius, command.offset_x, command.offset_y,
                         command.blur, command.spread, command.color);
      break;
    case PaintCommand::Kind::Line:
      target->line(r.x, r.y, r.x + r.w, r.y, command.color, command.thickness);
      break;
    case PaintCommand::Kind::Text:
      target->text(r.x, r.y, command.text, command.size, command.color, command.bold);
      break;
    case PaintCommand::Kind::Canvas: {
      auto found = images.find(command.url);
      if (found != images.end() && found->second.valid()) target->image(found->second, r, command.radius);
      break;
    }
    case PaintCommand::Kind::Image: {
      auto found = images.find(command.url);
      if (found != images.end() && found->second.valid())
        target->image(found->second, r, command.radius);
      else {
        target->fill_rounded(r, command.radius, 0xff202833);
        target->stroke_rounded(r, command.radius, 0xff506070);
        target->text(r.x + 4, r.y + 4, command.text.empty() ? "[image unavailable]" : command.text,
                     12, 0xffb8c4d0);
      }
      break;
    }
    case PaintCommand::Kind::PushLayer:
    case PaintCommand::Kind::PopLayer:
    case PaintCommand::Kind::PushClip:
    case PaintCommand::Kind::PopClip:
      break;
    }
  }
  while (!clips.empty()) {
    ClipState state = clips.back(); clips.pop_back();
    state.canvas->clip = state.clip;
    state.canvas->rounded_clips.resize(state.rounded_count);
  }
  while (!layers.empty()) {
    LayerState layer = std::move(layers.back()); layers.pop_back();
    target = layer.parent;
  }
}

} // namespace pv
