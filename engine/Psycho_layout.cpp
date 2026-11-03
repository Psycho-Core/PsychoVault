#include "Psycho_image.hpp"
#include "Psycho_layout.hpp"
namespace pv {
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
        if (cs.width >= 0)
          base = cs.width + extra;
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
    double main = row ? inner_x : inner_y;
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
        size_t from = out.paint.size();
        if (row) {
          double forced = -1;
          if (s.align_items == "stretch" && cs.height < 0)
            forced = std::max(0.0, line_cross - cs.margin[0] - cs.margin[2]);
          block(c, pos, main, sizes[k], depth + 1, cb, forced);
          if (cross[k] < line_cross) {
            double dy = 0;
            if (s.align_items == "center")
              dy = (line_cross - cross[k]) / 2;
            else if (s.align_items == "flex-end" || s.align_items == "end")
              dy = line_cross - cross[k];
            shift_range(from, 0, dy);
            shift_boxes(c, 0, dy);
          }
        } else {
          double forced = std::max(0.0, sizes[k] - cs.margin[0] - cs.margin[2]);
          block(c, inner_x, pos, inner_w, depth + 1, cb, forced);
        }
        pos += sizes[k] + s.gap + extra_gap;
      }
      main += (row ? line_cross : span) + s.gap;
    }
    cursor = std::max(cursor, main);
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
    auto sizes = track_sizes(s.grid_columns, inner_w, s.gap);
    if (sizes.empty())
      sizes.push_back(inner_w);
    double y = inner_y, row_h = 0, bottom = inner_y;
    size_t col = 0;
    for (int c : items) {
      const auto &cs = styles[c];
      size_t span = size_t(std::clamp(cs.grid_span, 1, int(sizes.size())));
      if (col + span > sizes.size()) {
        col = 0;
        y += row_h + s.gap;
        row_h = 0;
      }
      double x = inner_x, w = 0;
      for (size_t i = 0; i < col; ++i)
        x += sizes[i] + s.gap;
      for (size_t i = col; i < col + span && i < sizes.size(); ++i)
        w += sizes[i] + (i > col ? s.gap : 0);
      double h = measure_h(c, w, depth);
      block(c, x, y, w, depth + 1, cb);
      row_h = std::max(row_h, h);
      bottom = std::max(bottom, y + h);
      col += span;
      if (col >= sizes.size()) {
        col = 0;
        y += row_h + s.gap;
        row_h = 0;
      }
    }
    cursor = std::max(cursor, bottom);
  }
  // ---- positioning ------------------------------------------------------
  void place_positioned(int id, const Style &s, const Rect &cb, double left, double top,
                        double border_w, double border_h, int depth) {
    if (id < 0 || id >= int(doc.nodes.size()))
      return;
    Rect self{left, top, border_w, border_h};
    Rect box = s.position == "static" ? cb : self;
    for (int c : doc.nodes[id].children) {
      const auto &cs = styles[c];
      if (cs.display == "none" || doc.nodes[c].type != Node::Type::Element || !positioned(cs))
        continue;
      Rect used = cs.position == "fixed" ? Rect{0, 0, viewport_width, viewport_height} : box;
      double x = used.x + (cs.has_left ? cs.left : 0);
      double y = used.y + (cs.has_top ? cs.top : 0);
      double extra_w = outer_extra(cs);
      if (cs.has_right && !cs.has_left)
        x = used.x + used.w - cs.right - (cs.width >= 0 ? cs.width + extra_w : 0);
      double extra_h = cs.margin[0] + cs.margin[2] + cs.padding[0] + cs.padding[2] +
                       cs.border[0] + cs.border[2];
      if (cs.has_bottom && !cs.has_top)
        y = used.y + used.h - cs.bottom - (cs.height >= 0 ? cs.height + extra_h : 0);
      size_t from = out.paint.size();
      block(c, x, y, std::max(0.0, used.w - (cs.has_left ? cs.left : 0.0)), depth + 1, &used);
      if (cs.position == "fixed")
        mark_fixed(from);
    }
  }
  // ---- transforms -------------------------------------------------------
  void apply_transform(size_t from, size_t to, const Style &s, const Rect &box) {
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
    for (size_t i = from; i < to && i < out.paint.size(); ++i) {
      auto &r = out.paint[i].rect;
      r.x = tx + ox + (r.x - ox) * sx;
      r.y = ty + oy + (r.y - oy) * sy;
      r.w *= sx;
      r.h *= sy;
      if (out.paint[i].kind == PaintCommand::Kind::Text)
        out.paint[i].size *= sy;
    }
  }

  double block(int id, double x, double y, double available, int depth = 0,
               const Rect *cb = nullptr, double forced_h = -1) {
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
    double w = s.width >= 0 ? s.width : mw;
    if (s.box_sizing == "border-box")
      w -= s.padding[1] + s.padding[3] + s.border[1] + s.border[3];
    if (s.max_width >= 0)
      w = std::min(w, s.max_width);
    w = std::max({w, s.min_width, 0.0});
    double left = x + s.margin[3];
    if (s.margin[1] < 0 && s.margin[3] < 0 && s.width >= 0)
      left = x + std::max(0.0, (available - w - outer_extra(s)) / 2);
    double top = y + s.margin[0];
    double inner_x = left + s.border[3] + s.padding[3], inner_y = top + s.border[0] + s.padding[0];
    double inner_w = std::max(0.0, w);
    out.paint.push_back(
        {PaintCommand::Kind::Rect,
         {left, top, w + s.padding[1] + s.padding[3] + s.border[1] + s.border[3], 0},
         s.background.bgra(),
         {},
         0,
         0,
         false,
         id,
         {}});
    size_t bg = out.paint.size() - 1;
    double cursor = inner_y;
    if (s.display == "flex" || s.display == "inline-flex") {
      flex_children(id, s, inner_x, inner_y, inner_w, cursor, depth, cb);
    } else if (s.display == "grid" || s.display == "inline-grid") {
      grid_children(id, s, inner_x, inner_y, inner_w, cursor, depth, cb);
    } else {
      std::vector<Run> line;
      double line_width = 0, line_height = 0;
      auto flush = [&]() {
        if (line.empty())
          return;
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
                               {}});
          if (run.style.underline)
            out.paint.push_back({PaintCommand::Kind::Line,
                                 {tx, cursor + run.style.font_size + 2, run.width, 1},
                                 run.style.color.bgra(),
                                 {},
                                 0,
                                 1,
                                 false,
                                 run.node,
                                 {}});
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
          if (line_width + ww > inner_w && !line.empty() && word != " ")
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
          double iw = cs.width >= 0 ? cs.width : aw >= 0 ? aw : natural_w > 0 ? natural_w : 120;
          double ih = cs.height >= 0                   ? cs.height
                      : ah >= 0                        ? ah
                      : natural_w > 0 && natural_h > 0 ? iw * natural_h / natural_w
                      : aw >= 0 && natural_w > 0       ? aw * natural_h / natural_w
                                                       : 80;
          if (cs.width < 0 && cs.height >= 0 && natural_w > 0 && natural_h > 0)
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
                               key});
          out.boxes[c] = {inner_x + cs.margin[3], cursor + cs.margin[0], iw, ih};
          cursor += ih + cs.margin[0] + cs.margin[2];
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
          double cw = cs.width >= 0 ? cs.width
                                    : std::max(80.0, measure.text_width(label, cs.font_size) +
                                                         cs.padding[1] + cs.padding[3] + 16),
                 ch = cs.height >= 0             ? cs.height
                      : child.name == "textarea" ? 80
                                                 : cs.font_size + cs.padding[0] + cs.padding[2] +
                                                       cs.border[0] + cs.border[2];
          out.paint.push_back({PaintCommand::Kind::Rect,
                               {inner_x, cursor, cw, ch},
                               cs.background.bgra(),
                               {},
                               0,
                               0,
                               false,
                               c,
                               {}});
          out.paint.push_back({PaintCommand::Kind::Border,
                               {inner_x, cursor, cw, ch},
                               cs.border_color.bgra(),
                               {},
                               0,
                               1,
                               false,
                               c,
                               {}});
          out.paint.push_back(
              {PaintCommand::Kind::Text,
               {inner_x + cs.padding[3] + 4, cursor + cs.padding[0] + 2, cw - 8, ch - 4},
               cs.color.bgra(),
               label,
               cs.font_size,
               0,
               cs.bold,
               c,
               {}});
          out.boxes[c] = {inner_x, cursor, cw, ch};
          cursor += ch + cs.margin[0] + cs.margin[2];
          return;
        }
        for (int gc : child.children)
          inline_child(gc, dep + 1);
      };
      for (int c : n.children) {
        const auto &cs = styles[c];
        if (cs.display == "none" || positioned(cs))
          continue;
        bool is_block = doc.nodes[c].type == Node::Type::Element && cs.display != "inline" &&
                        cs.display != "inline-block" && c != 0;
        if (is_block) {
          flush();
          if (s.list_item && out.boxes.count(c) == 0) {
          }
          double h = block(c, inner_x, cursor, inner_w, depth + 1);
          cursor += h;
        } else
          inline_child(c, 0);
      }
      flush();
    }
    double content_h = cursor - inner_y;
    double h = forced_h >= 0 ? forced_h : (s.height >= 0 ? s.height : content_h);
    if (s.max_height >= 0)
      h = std::min(h, s.max_height);
    h = std::max(h, s.min_height);
    double border_box = h + s.padding[0] + s.padding[2] + s.border[0] + s.border[2];
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
                           {}});
    if (s.list_item)
      out.paint.push_back({PaintCommand::Kind::Text,
                           {left - 16, top, 12, s.font_size * 1.4},
                           s.color.bgra(),
                           "-",
                           s.font_size,
                           0,
                           false,
                           id,
                           {}});
    // Anything positioned is measured against the nearest positioned box.
    double box_w = w + s.padding[1] + s.padding[3] + s.border[1] + s.border[3];
    place_positioned(id, s, cb ? *cb : Rect{0, 0, viewport_width, viewport_height}, left, top,
                     box_w, border_box, depth);
    // A relative box keeps its place in the flow but paints where it is told.
    if (s.position == "relative" || s.position == "sticky")
      move_box(start, id, s.left, s.top);
    if (!s.transform.empty())
      apply_transform(start, out.paint.size(), s, out.boxes[id]);
    return border_box + s.margin[0] + s.margin[2];
  }
};
LayoutResult layout_document(const Document &doc, const std::vector<Style> &styles, double width,
                             double height, const std::map<std::string, Resource> &resources,
                             Canvas &measure) {
  Layout l(doc, styles, measure, resources, width);
  uint32_t bg = styles[doc.body].background.bgra();
  l.out.paint.push_back({PaintCommand::Kind::Rect,
                         {0, 0, width, std::max(height, 1.0)},
                         bg,
                         {},
                         0,
                         0,
                         false,
                         doc.body,
                         {}});
  l.viewport_height = height;
  Rect viewport{0, 0, width, height};
  double used = l.block(doc.body, 0, 0, width, 0, &viewport);
  l.out.height = std::max(height, used);
  l.out.paint[0].rect.h = l.out.height;
  return l.out;
}
void paint_commands(Canvas &canvas, const std::vector<PaintCommand> &commands, double scroll_y,
                    const std::map<std::string, Image> &images) {
  for (auto &c : commands) {
    Rect r = c.rect;
    if (!c.fixed)
      r.y -= scroll_y;
    if (r.y + r.h < 0 || r.y > canvas.height)
      continue;
    switch (c.kind) {
    case PaintCommand::Kind::Rect:
      canvas.fill(r, c.color);
      break;
    case PaintCommand::Kind::Border:
      canvas.stroke(r, c.color, c.thickness);
      break;
    case PaintCommand::Kind::Line:
      canvas.line(r.x, r.y, r.x + r.w, r.y, c.color, c.thickness);
      break;
    case PaintCommand::Kind::Text:
      canvas.text(r.x, r.y, c.text, c.size, c.color, c.bold);
      break;
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
