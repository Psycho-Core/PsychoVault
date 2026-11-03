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
  Layout(const Document &d, const std::vector<Style> &s, Canvas &c,
         const std::map<std::string, Resource> &r, double w)
      : doc(d), styles(s), measure(c), resources(r), viewport_width(w) {}
  double outer_extra(const Style &s) {
    return s.margin[1] + s.margin[3] + s.padding[1] + s.padding[3] + s.border[1] + s.border[3];
  }
  double block(int id, double x, double y, double available, int depth = 0) {
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
    if (s.display == "flex" && s.flex_direction == "row") {
      std::vector<int> children;
      for (int c : n.children)
        if (styles[c].display != "none" && doc.nodes[c].type == Node::Type::Element)
          children.push_back(c);
      double used = 0;
      int autos = 0;
      for (int c : children) {
        const auto &cs = styles[c];
        if (cs.width >= 0)
          used += cs.width + outer_extra(cs);
        else
          ++autos;
      }
      double share =
          autos ? std::max(0.0,
                           (inner_w - used - s.gap * std::max(0, int(children.size() - 1))) / autos)
                : 0;
      double cx = inner_x;
      double maxh = 0;
      for (int c : children) {
        double cw = styles[c].width >= 0 ? styles[c].width : share;
        double h = block(c, cx, inner_y, cw + outer_extra(styles[c]), depth + 1);
        cx += cw + outer_extra(styles[c]) + s.gap;
        maxh = std::max(maxh, h);
      }
      cursor += maxh;
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
        if (cs.display == "none")
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
    double h = s.height >= 0 ? s.height : content_h;
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
  double used = l.block(doc.body, 0, 0, width);
  l.out.height = std::max(height, used);
  l.out.paint[0].rect.h = l.out.height;
  return l.out;
}
void paint_commands(Canvas &canvas, const std::vector<PaintCommand> &commands, double scroll_y,
                    const std::map<std::string, Image> &images) {
  for (auto &c : commands) {
    Rect r = c.rect;
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
