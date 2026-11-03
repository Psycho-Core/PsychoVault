#include "Psycho_css.hpp"
namespace pv {
Color parse_color(std::string_view v) {
  auto s = lower(trim(v));
  Color c;
  static const std::map<std::string, uint32_t> names = {
      {"black", 0x000000},  {"white", 0xffffff},   {"red", 0xff0000},     {"green", 0x008000},
      {"blue", 0x0000ff},   {"gray", 0x808080},    {"grey", 0x808080},    {"silver", 0xc0c0c0},
      {"navy", 0x000080},   {"teal", 0x008080},    {"aqua", 0x00ffff},    {"cyan", 0x00ffff},
      {"purple", 0x800080}, {"magenta", 0xff00ff}, {"fuchsia", 0xff00ff}, {"yellow", 0xffff00},
      {"orange", 0xffa500}, {"pink", 0xffc0cb},    {"brown", 0xa52a2a},   {"transparent", 0}};
  if (s == "transparent" || s.empty() || s == "initial" || s == "unset")
    return c;
  c.transparent = false;
  if (s[0] == '#') {
    std::string h = s.substr(1);
    if (h.size() == 3 || h.size() == 4) {
      std::string x;
      for (char ch : h)
        x += std::string(2, ch);
      h = x;
    }
    if (h.size() != 6 && h.size() != 8)
      return c;
    uint32_t n = 0;
    for (char ch : h) {
      int d = ch >= '0' && ch <= '9' ? ch - '0' : ch >= 'a' && ch <= 'f' ? ch - 'a' + 10 : -1;
      if (d < 0)
        return c;
      n = n * 16 + d;
    }
    if (h.size() == 6) {
      c.r = n >> 16;
      c.g = n >> 8;
      c.b = n;
    } else {
      c.r = n >> 24;
      c.g = n >> 16;
      c.b = n >> 8;
      c.a = n;
    }
    return c;
  }
  auto it = names.find(s);
  if (it != names.end()) {
    c.r = it->second >> 16;
    c.g = it->second >> 8;
    c.b = it->second;
    return c;
  }
  if (starts(s, "rgb")) {
    size_t a = s.find('('), b = s.rfind(')');
    if (a == std::string::npos || b == std::string::npos)
      return c;
    auto parts = split(s.substr(a + 1, b - a - 1), ',');
    if (parts.size() < 3)
      return c;
    auto channel = [](std::string x) {
      x = trim(x);
      double n = ends(x, "%") ? std::stod(x.substr(0, x.size() - 1)) * 2.55 : std::stod(x);
      return uint8_t(std::clamp(int(std::round(n)), 0, 255));
    };
    try {
      c.r = channel(parts[0]);
      c.g = channel(parts[1]);
      c.b = channel(parts[2]);
      if (parts.size() > 3)
        c.a = uint8_t(std::clamp(std::stod(trim(parts[3])), 0.0, 1.0) * 255);
    } catch (...) {
    }
    return c;
  }
  return c;
}
static std::string strip_comments(std::string_view s) {
  std::string out;
  for (size_t i = 0; i < s.size();) {
    if (s.substr(i, 2) == "/*") {
      auto e = s.find("*/", i + 2);
      i = e == std::string_view::npos ? s.size() : e + 2;
    } else
      out += s[i++];
  }
  return out;
}
static std::map<std::string, std::string> declarations(std::string_view text,
                                                       std::set<std::string> *important = nullptr) {
  std::map<std::string, std::string> out;
  for (auto &part : split(text, ';')) {
    auto p = part.find(':');
    if (p == std::string::npos)
      continue;
    auto key = lower(trim(std::string_view(part).substr(0, p)));
    auto value = trim(std::string_view(part).substr(p + 1));
    auto bang = value.find('!');
    if (bang != std::string::npos) {
      if (lower(trim(std::string_view(value).substr(bang + 1))) == "important" && important)
        important->insert(key);
      value = trim(std::string_view(value).substr(0, bang));
    }
    if (!key.empty() && key.size() < 128 && value.size() < 8192)
      out[key] = value;
  }
  return out;
}
void Stylesheet::parse(std::string_view input, int &order) {
  auto s = strip_comments(input);
  size_t sPos = 0;
  while (sPos < s.size()) {
    while (sPos < s.size() && std::isspace(static_cast<unsigned char>(s[sPos])))
      ++sPos;
    size_t brace = s.find('{', sPos);
    if (brace == std::string::npos)
      break;
    auto selector = trim(std::string_view(s).substr(sPos, brace - sPos));
    size_t depth = 1, end = brace + 1;
    bool quote = false;
    char q = 0;
    while (end < s.size() && depth) {
      char c = s[end];
      if (quote) {
        if (c == '\\' && end + 1 < s.size())
          ++end;
        else if (c == q)
          quote = false;
      } else if (c == '"' || c == '\'') {
        quote = true;
        q = c;
      } else if (c == '{')
        ++depth;
      else if (c == '}')
        --depth;
      ++end;
    }
    auto body = std::string_view(s).substr(brace + 1, end - brace - 2);
    if (starts(selector, "@media")) {
      Stylesheet nested;
      nested.parse(body, order);
      for (auto &r : nested.rules) {
        r.media = selector;
        rules.push_back(std::move(r));
      }
      diagnostics.insert(diagnostics.end(), nested.diagnostics.begin(), nested.diagnostics.end());
    } else if (starts(selector, "@")) {
      if (diagnostics.size() < 100)
        diagnostics.push_back(
            {"css",
             "unsupported at-rule " + selector.substr(0, std::min(size_t(40), selector.size()))});
    } else {
      CssRule r;
      r.selector = selector;
      r.declarations = declarations(body, &r.important);
      r.order = order++;
      if (rules.size() < 10000)
        rules.push_back(std::move(r));
    }
    sPos = end;
  }
}
double css_length(const std::string &v, double parent, double font, double root, double viewport,
                  const std::string &prop, double fallback) {
  (void)prop;
  auto s = lower(trim(v));
  if (s.empty() || s == "auto" || s == "none" || s == "normal" || s == "initial" ||
      s == "inherit" || s == "unset")
    return fallback;
  try {
    if (ends(s, "px"))
      return std::stod(s.substr(0, s.size() - 2));
    if (ends(s, "rem"))
      return std::stod(s.substr(0, s.size() - 3)) * root;
    if (ends(s, "em"))
      return std::stod(s.substr(0, s.size() - 2)) * font;
    if (ends(s, "vw"))
      return std::stod(s.substr(0, s.size() - 2)) * viewport / 100;
    if (ends(s, "vh"))
      return std::stod(s.substr(0, s.size() - 2)) * viewport / 100;
    if (ends(s, "%"))
      return std::stod(s.substr(0, s.size() - 1)) * parent / 100;
    if (ends(s, "pt"))
      return std::stod(s.substr(0, s.size() - 2)) * 96 / 72;
    return std::stod(s);
  } catch (...) {
    return fallback;
  }
}
int specificity(std::string_view selector) {
  int best = 0;
  for (auto &group : split(selector, ',')) {
    int a = 0, b = 0, c = 0;
    bool ident = false;
    for (char ch : group) {
      if (ch == '#')
        ++a;
      else if (ch == '.' || ch == '[' || ch == ':')
        ++b;
      else if (std::isalpha(static_cast<unsigned char>(ch)) && !ident) {
        ++c;
        ident = true;
      } else if (!std::isalnum(static_cast<unsigned char>(ch)) && ch != '-' && ch != '_')
        ident = false;
    }
    best = std::max(best, a * 10000 + b * 100 + c);
  }
  return best;
}
static std::string ua_css() {
  return R"CSS(
html,body{margin:0;padding:0}body{font-family:Segoe UI,Arial,sans-serif;font-size:16px;line-height:1.45;color:#111;background:#fff}head,style,script,meta,link,title{display:none}p{margin:1em 0}h1{font-size:2em;font-weight:bold;margin:.67em 0}h2{font-size:1.5em;font-weight:bold;margin:.83em 0}h3{font-size:1.17em;font-weight:bold;margin:1em 0}h4{font-weight:bold;margin:1.33em 0}h5{font-size:.83em;font-weight:bold;margin:1.67em 0}h6{font-size:.67em;font-weight:bold;margin:2.33em 0}a{color:#06c;text-decoration:underline}strong,b{font-weight:bold}em,i{font-style:italic}small{font-size:.83em}code,pre,kbd,samp{font-family:Consolas,monospace}pre{white-space:pre;margin:1em 0}ul,ol{margin:1em 0;padding-left:40px}li{display:list-item}blockquote{margin:1em 40px}hr{border-top:1px solid #888;margin:.5em 0}img,canvas{display:inline-block}input,button,select,textarea{display:inline-block;font-size:14px;padding:4px 8px;border:1px solid #777;background:#fff;color:#111}button{text-align:center}table{display:block;border-collapse:collapse}tr{display:block}td,th{display:inline-block;padding:2px}th{font-weight:bold}
)CSS";
}
static void expand_four(const std::string &value, double *out, double parent, double font,
                        double root, double viewport, const char *prop) {
  auto parts = split(value, ' ');
  std::erase_if(parts, [](const std::string &p) { return p.empty(); });
  if (parts.empty())
    return;
  size_t indexes[4] = {0, 0, 0, 0};
  if (parts.size() == 2) {
    indexes[1] = indexes[3] = 1;
  } else if (parts.size() == 3) {
    indexes[1] = 1;
    indexes[3] = 1;
    indexes[2] = 2;
  } else if (parts.size() >= 4) {
    indexes[1] = 1;
    indexes[2] = 2;
    indexes[3] = 3;
  }
  for (int i = 0; i < 4; ++i)
    out[i] = css_length(parts[indexes[i]], parent, font, root, viewport, prop, out[i]);
}
// A border shorthand mixes a length, a style word and a colour, and my colour
// parser answers black for anything it does not know, so I classify first.
static bool looks_like_length(const std::string &text) {
  if (text.empty())
    return false;
  size_t i = 0;
  if (text[0] == '-' || text[0] == '+')
    i = 1;
  bool digit = false;
  for (; i < text.size(); ++i) {
    if (std::isdigit(static_cast<unsigned char>(text[i])) || text[i] == '.') {
      digit = true;
      continue;
    }
    break;
  }
  return digit;
}
static bool border_style_word(const std::string &text) {
  static const std::set<std::string> words = {"solid", "dashed", "dotted", "double", "none",
                                              "hidden", "groove", "ridge",   "inset",  "outset"};
  return words.count(text) != 0;
}
static bool colour_word(const std::string &text) {
  if (text.empty())
    return false;
  return text[0] == '#' || text.rfind("rgb", 0) == 0 || text.rfind("hsl", 0) == 0 ||
         text == "black" || text == "white" || text == "red" || text == "green" ||
         text == "blue" || text == "gray" || text == "grey" || text == "silver" ||
         text == "navy" || text == "teal" || text == "aqua" || text == "cyan" ||
         text == "purple" || text == "magenta" || text == "fuchsia" || text == "yellow" ||
         text == "orange" || text == "pink" || text == "brown" || text == "transparent";
}
static void apply(Style &s, const std::map<std::string, std::string> &d, double parent_width,
                  double root, double viewport) {
  auto get = [&](const char *k) -> const std::string * {
    auto it = d.find(k);
    return it == d.end() ? nullptr : &it->second;
  };
  for (auto &p : d)
    s.raw[p.first] = p.second;
  if (auto v = get("display"))
    s.display = lower(*v);
  if (auto v = get("color"))
    s.color = parse_color(*v);
  if (auto v = get("background-color"))
    s.background = parse_color(*v);
  if (auto v = get("background")) {
    auto parts = split(*v, ' ');
    // A gradient is not a colour, so I keep it for my paint pass.
    if (v->find("gradient(") != std::string::npos) {
      s.background_image = trim(*v);
      s.background = Color{};
    } else if (!parts.empty())
      s.background = parse_color(parts[0]);
  }
  if (auto v = get("background-image")) {
    if (v->find("gradient(") != std::string::npos)
      s.background_image = trim(*v);
  }
  if (auto v = get("font-size"))
    s.font_size = std::clamp(
        css_length(*v, parent_width, s.font_size, root, viewport, "font-size", s.font_size), 6.0,
        128.0);
  if (auto v = get("font-family"))
    s.font_family = trim(split(*v, ',')[0]);
  if (auto v = get("font-weight")) {
    s.weight = *v;
    s.bold =
        *v == "bold" || *v == "bolder" ||
        (!v->empty() && std::isdigit(static_cast<unsigned char>((*v)[0])) && std::stoi(*v) >= 600);
  }
  if (auto v = get("font-style"))
    s.italic = v->find("italic") != std::string::npos;
  if (auto v = get("text-align"))
    s.text_align = lower(*v);
  if (auto v = get("text-decoration"))
    s.underline = v->find("underline") != std::string::npos;
  if (auto v = get("line-height")) {
    try {
      s.line_height = std::stod(*v);
    } catch (...) {
      s.line_height = css_length(*v, s.font_size, s.font_size, root, viewport, "line-height",
                                 s.font_size * 1.45) /
                      s.font_size;
    }
    s.line_height = std::clamp(s.line_height, 0.8, 4.0);
  }
  const char *sides[4] = {"top", "right", "bottom", "left"};
  auto note_auto = [&](int index, const std::string &value) {
    if (lower(trim(value)) != "auto")
      return;
    if (index == 1)
      s.auto_right = true;
    else if (index == 3)
      s.auto_left = true;
  };
  for (int i = 0; i < 4; ++i) {
    if (auto v = get((std::string("margin-") + sides[i]).c_str())) {
      note_auto(i, *v);
      s.margin[i] = css_length(*v, parent_width, s.font_size, root, viewport, "margin", 0);
    }
    if (auto v = get((std::string("padding-") + sides[i]).c_str()))
      s.padding[i] =
          std::max(0.0, css_length(*v, parent_width, s.font_size, root, viewport, "padding", 0));
    if (auto v = get((std::string("border-") + sides[i] + "-width").c_str()))
      s.border[i] =
          std::max(0.0, css_length(*v, parent_width, s.font_size, root, viewport, "border", 0));
    // `border-bottom: 2px solid #ccc` is how most pages draw a divider.
    if (auto v = get((std::string("border-") + sides[i]).c_str())) {
      auto parts = split(*v, ' ');
      for (auto &part : parts) {
        auto low = lower(trim(part));
        if (low.empty())
          continue;
        if (low == "none" || low == "hidden") {
          s.border[i] = 0;
          continue;
        }
        if (border_style_word(low))
          continue;
        if (looks_like_length(low)) {
          s.border[i] = std::max(
              0.0, css_length(low, parent_width, s.font_size, root, viewport, "border", s.border[i]));
          continue;
        }
        if (colour_word(low))
          s.border_color = parse_color(low);
      }
    }
  }
  if (auto v = get("margin")) {
    expand_four(*v, s.margin, parent_width, s.font_size, root, viewport, "margin");
    // The shorthand repeats the same value across the sides it covers.
    auto parts = split(*v, ' ');
    std::erase_if(parts, [](const std::string &part) { return part.empty(); });
    if (!parts.empty()) {
      auto pick = [&](size_t index) { return parts[std::min(index, parts.size() - 1)]; };
      note_auto(1, pick(parts.size() >= 2 ? 1 : 0));
      note_auto(3, pick(parts.size() >= 4 ? 3 : parts.size() >= 2 ? 1 : 0));
    }
  }
  if (auto v = get("padding")) {
    expand_four(*v, s.padding, parent_width, s.font_size, root, viewport, "padding");
    for (double &x : s.padding)
      x = std::max(0.0, x);
  }
  if (auto v = get("border")) {
    auto parts = split(*v, ' ');
    for (auto &part : parts) {
      auto low = lower(trim(part));
      if (low.empty() || border_style_word(low))
        continue;
      if (low == "none" || low == "hidden") {
        for (double &width : s.border)
          width = 0;
        continue;
      }
      if (looks_like_length(low)) {
        double width =
            std::max(0.0, css_length(low, parent_width, s.font_size, root, viewport, "border", 0));
        for (int i = 0; i < 4; ++i)
          s.border[i] = width;
        continue;
      }
      if (colour_word(low))
        s.border_color = parse_color(low);
    }
  }
  if (auto v = get("border-color"))
    s.border_color = parse_color(*v);
  if (auto v = get("border-radius"))
    s.radius =
        std::max(0.0, css_length(*v, parent_width, s.font_size, root, viewport, "radius", 0));
  if (auto v = get("width"))
    s.width = css_length(*v, parent_width, s.font_size, root, viewport, "width", -1);
  if (auto v = get("height")) {
    auto text = trim(*v);
    if (!text.empty() && text.back() == '%') {
      try {
        s.height_percent = std::clamp(std::stod(text) / 100.0, 0.0, 10.0);
        s.height = -1;
      } catch (...) {
        s.height = css_length(*v, parent_width, s.font_size, root, viewport, "height", -1);
      }
    } else
      s.height = css_length(*v, parent_width, s.font_size, root, viewport, "height", -1);
  }
  if (auto v = get("min-width"))
    s.min_width = css_length(*v, parent_width, s.font_size, root, viewport, "width", 0);
  if (auto v = get("max-width"))
    s.max_width = css_length(*v, parent_width, s.font_size, root, viewport, "width", -1);
  if (auto v = get("min-height"))
    s.min_height = css_length(*v, parent_width, s.font_size, root, viewport, "height", 0);
  if (auto v = get("max-height"))
    s.max_height = css_length(*v, parent_width, s.font_size, root, viewport, "height", -1);
  if (auto v = get("box-sizing"))
    s.box_sizing = *v;
  if (auto v = get("white-space"))
    s.white_space = *v;
  if (auto v = get("position"))
    s.position = *v;
  if (auto v = get("overflow"))
    s.overflow = *v;
  if (auto v = get("opacity")) {
    try {
      s.opacity = std::clamp(std::stod(*v), 0.0, 1.0);
    } catch (...) {
    }
  }
  if (auto v = get("flex-direction"))
    s.flex_direction = *v;
  if (auto v = get("justify-content"))
    s.justify_content = *v;
  if (auto v = get("align-items"))
    s.align_items = *v;
  if (auto v = get("gap"))
    s.gap = css_length(*v, parent_width, s.font_size, root, viewport, "gap", 0);
  if (auto v = get("top")) {
    s.top = css_length(*v, parent_width, s.font_size, root, viewport, "top", 0);
    s.has_top = lower(*v) != "auto";
  }
  if (auto v = get("left")) {
    s.left = css_length(*v, parent_width, s.font_size, root, viewport, "left", 0);
    s.has_left = lower(*v) != "auto";
  }
  if (auto v = get("right")) {
    s.right = css_length(*v, parent_width, s.font_size, root, viewport, "right", 0);
    s.has_right = lower(*v) != "auto";
  }
  if (auto v = get("bottom")) {
    s.bottom = css_length(*v, parent_width, s.font_size, root, viewport, "bottom", 0);
    s.has_bottom = lower(*v) != "auto";
  }
  if (auto v = get("flex-wrap"))
    s.flex_wrap = lower(*v);
  if (auto v = get("flex-grow")) {
    try {
      s.flex_grow = std::max(0.0, std::stod(*v));
    } catch (...) {
    }
  }
  if (auto v = get("flex-shrink")) {
    try {
      s.flex_shrink = std::max(0.0, std::stod(*v));
    } catch (...) {
    }
  }
  if (auto v = get("flex-basis"))
    s.flex_basis = lower(*v) == "auto"
                       ? -1
                       : css_length(*v, parent_width, s.font_size, root, viewport, "flex-basis", -1);
  if (auto v = get("flex")) {
    // I read the one, two and three value forms: grow, grow shrink, grow shrink basis.
    auto parts = split(*v, ' ');
    std::vector<std::string> numbers;
    for (auto &part : parts)
      if (!part.empty())
        numbers.push_back(part);
    auto number = [](const std::string &text, double fallback) {
      try {
        return std::stod(text);
      } catch (...) {
        return fallback;
      }
    };
    if (!numbers.empty() && numbers[0] != "none" && numbers[0] != "auto") {
      s.flex_grow = std::max(0.0, number(numbers[0], s.flex_grow));
      if (numbers.size() > 1 && numbers[1] != "auto")
        s.flex_shrink = std::max(0.0, number(numbers[1], s.flex_shrink));
      if (!numbers.empty() && numbers.back() != numbers[0] && numbers.back() != "auto" &&
          !numbers.back().empty() && !std::isdigit(static_cast<unsigned char>(numbers.back()[0])))
        s.flex_basis = css_length(numbers.back(), parent_width, s.font_size, root, viewport,
                                  "flex-basis", -1);
      if (numbers.size() >= 3 && numbers[2] == "auto")
        s.flex_basis = -1;
    } else if (!numbers.empty() && numbers[0] == "auto") {
      s.flex_grow = 1;
      s.flex_basis = -1;
    }
  }
  if (auto v = get("flex-flow")) {
    for (auto &part : split(*v, ' ')) {
      auto low = lower(trim(part));
      if (low == "row" || low == "row-reverse" || low == "column" || low == "column-reverse")
        s.flex_direction = low;
      else if (low == "wrap" || low == "nowrap" || low == "wrap-reverse")
        s.flex_wrap = low;
    }
  }
  if (auto v = get("order")) {
    try {
      s.order = int(std::clamp(std::stod(*v), -100000.0, 100000.0));
    } catch (...) {
    }
  }
  if (auto v = get("align-self"))
    s.align_self = lower(*v);
  if (auto v = get("row-gap") ? get("row-gap") : get("grid-row-gap")) {
    s.row_gap =
        std::max(0.0, css_length(*v, parent_width, s.font_size, root, viewport, "gap", 0));
  }
  if (auto v = get("grid-template-rows"))
    s.grid_rows = trim(*v);
  if (auto v = get("grid-template-columns"))
    s.grid_columns = *v;
  if (auto v = get("grid-column")) {
    s.grid_column = *v;
    auto parts = split(lower(*v), ' ');
    if (parts.size() == 2 && parts[0] == "span") {
      try {
        s.grid_span = std::clamp(std::stoi(parts[1]), 1, 12);
      } catch (...) {
      }
    }
  }
  if (auto v = get("transform"))
    s.transform = *v;
}
static bool media_matches(const std::string &media, double viewport) {
  if (media.empty())
    return true;
  auto low = lower(media);
  bool apply = true;
  auto min = low.find("min-width:");
  auto max = low.find("max-width:");
  try {
    if (min != std::string::npos)
      apply = apply && viewport >= std::stod(low.substr(min + 10));
    if (max != std::string::npos)
      apply = apply && viewport <= std::stod(low.substr(max + 9));
  } catch (...) {
    apply = false;
  }
  if (low.find("print") != std::string::npos)
    apply = false;
  return apply;
}
std::vector<Style> compute_styles(const Document &doc, const std::vector<Stylesheet> &sheets,
                                  double viewport) {
  std::vector<Style> styles(doc.nodes.size());
  Stylesheet ua;
  int ua_order = -1000000;
  ua.parse(ua_css(), ua_order);
  std::vector<int> stack{doc.root};
  while (!stack.empty()) {
    int id = stack.back();
    stack.pop_back();
    const auto &n = doc.nodes[id];
    Style s;
    if (n.parent >= 0) {
      s = styles[n.parent];
      // Only some properties inherit. Everything here is non-inherited, so a
      // child starts from its own defaults rather than borrowing the parent's.
      s.display = "block";
      s.background = Color{};
      s.background_image.clear();
      s.border_color = Color{70, 80, 95, 255, false};
      std::fill(std::begin(s.margin), std::end(s.margin), 0.0);
      std::fill(std::begin(s.padding), std::end(s.padding), 0.0);
      std::fill(std::begin(s.border), std::end(s.border), 0.0);
      s.auto_left = s.auto_right = false;
      s.width = s.height = -1;
      s.height_percent = -1;
      s.min_width = s.min_height = 0;
      s.max_width = s.max_height = -1;
      s.radius = 0;
      s.underline = false;
      s.list_item = false;
      s.box_sizing = "content-box";
      s.position = "static";
      s.top = s.left = s.right = s.bottom = 0;
      s.has_top = s.has_left = s.has_right = s.has_bottom = false;
      s.overflow = "visible";
      s.opacity = 1;
      s.flex_direction = "row";
      s.justify_content = "flex-start";
      s.align_items = "stretch";
      s.align_self = "auto";
      s.flex_wrap = "nowrap";
      s.flex_grow = 0;
      s.flex_shrink = 1;
      s.flex_basis = -1;
      s.order = 0;
      s.gap = 0;
      s.row_gap = -1;
      s.grid_columns.clear();
      s.grid_rows.clear();
      s.grid_column.clear();
      s.grid_span = 1;
      s.transform.clear();
    }
    double parent_width = viewport; // Width inheritance is resolved against the
                                    // containing width during layout.
    apply(s, {}, parent_width, 16, viewport);
    std::vector<const CssRule *> matched;
    for (auto &r : ua.rules)
      if (matches_selector(doc, id, r.selector))
        matched.push_back(&r);
    for (auto &sheet : sheets)
      for (auto &r : sheet.rules)
        if (media_matches(r.media, viewport) && matches_selector(doc, id, r.selector))
          matched.push_back(&r);
    std::stable_sort(matched.begin(), matched.end(), [](auto a, auto b) {
      return specificity(a->selector) < specificity(b->selector);
    });
    // Important declarations win after normal declarations without changing
    // origin order.
    for (bool important : {false, true})
      for (auto r : matched) {
        std::map<std::string, std::string> d;
        for (auto &p : r->declarations)
          if ((r->important.count(p.first) != 0) == important)
            d.insert(p);
        apply(s, d, parent_width, 16, viewport);
      }
    if (n.type == Node::Type::Element)
      apply(s, declarations(n.attribute("style")), parent_width, 16, viewport);
    if (n.type == Node::Type::Text)
      s.display = "inline";
    if (n.name == "li")
      s.list_item = true;
    if (n.name == "a")
      s.underline = true;
    if (n.name == "input" || n.name == "button" || n.name == "select" || n.name == "textarea")
      s.display = "inline-block";
    styles[id] = s;
    for (auto it = n.children.rbegin(); it != n.children.rend(); ++it)
      stack.push_back(*it);
  }
  return styles;
}
} // namespace pv
