#include "Psycho_css.hpp"
namespace pv {
Color parse_color(std::string_view v) {
  const std::string s = lower(trim(v));
  Color color;
  if (s.empty() || s == "transparent" || s == "initial" || s == "unset" ||
      s == "inherit" || s == "currentcolor") return color;
  static const std::map<std::string, uint32_t> names = {
      {"black",0x000000},{"white",0xffffff},{"red",0xff0000},{"green",0x008000},
      {"blue",0x0000ff},{"gray",0x808080},{"grey",0x808080},{"silver",0xc0c0c0},
      {"navy",0x000080},{"teal",0x008080},{"aqua",0x00ffff},{"cyan",0x00ffff},
      {"purple",0x800080},{"magenta",0xff00ff},{"fuchsia",0xff00ff},{"yellow",0xffff00},
      {"orange",0xffa500},{"pink",0xffc0cb},{"brown",0xa52a2a},{"rebeccapurple",0x663399},
      {"lime",0x00ff00},{"olive",0x808000},{"maroon",0x800000},{"indigo",0x4b0082},
      {"violet",0xee82ee},{"gold",0xffd700},{"coral",0xff7f50},{"tomato",0xff6347},
      {"whitesmoke",0xf5f5f5},{"lightgray",0xd3d3d3},{"lightgrey",0xd3d3d3},
      {"darkgray",0xa9a9a9},{"darkgrey",0xa9a9a9},{"darkslategray",0x2f4f4f},
      {"slategray",0x708090},{"steelblue",0x4682b4},{"dodgerblue",0x1e90ff},
      {"deepskyblue",0x00bfff},{"skyblue",0x87ceeb},{"cornflowerblue",0x6495ed},
      {"forestgreen",0x228b22},{"seagreen",0x2e8b57},{"limegreen",0x32cd32},
      {"darkgreen",0x006400},{"crimson",0xdc143c},{"firebrick",0xb22222},
      {"salmon",0xfa8072},{"khaki",0xf0e68c},{"beige",0xf5f5dc},
      {"ivory",0xfffff0},{"lavender",0xe6e6fa},{"plum",0xdda0dd},
      {"orchid",0xda70d6},{"hotpink",0xff69b4},{"deeppink",0xff1493},
      {"chocolate",0xd2691e},{"sienna",0xa0522d},{"peru",0xcd853f},
      {"tan",0xd2b48c},{"wheat",0xf5deb3},{"aliceblue",0xf0f8ff},
      {"antiquewhite",0xfaebd7},{"azure",0xf0ffff},{"bisque",0xffe4c4},
      {"blanchedalmond",0xffebcd},{"blueviolet",0x8a2be2},{"burlywood",0xdeb887},
      {"cadetblue",0x5f9ea0},{"chartreuse",0x7fff00},{"cornsilk",0xfff8dc},
      {"darkblue",0x00008b},{"darkcyan",0x008b8b},{"darkmagenta",0x8b008b},
      {"darkorange",0xff8c00},{"darkred",0x8b0000},{"goldenrod",0xdaa520},
      {"honeydew",0xf0fff0},{"linen",0xfaf0e6},{"mintcream",0xf5fffa},
      {"oldlace",0xfdf5e6},{"palegreen",0x98fb98},{"seashell",0xfff5ee},
      {"snow",0xfffafa},{"thistle",0xd8bfd8},{"turquoise",0x40e0d0}};
  if (s[0] == '#') {
    std::string hex = s.substr(1);
    if (hex.size() == 3 || hex.size() == 4) {
      std::string expanded;
      for (char c : hex) { expanded += c; expanded += c; }
      hex = std::move(expanded);
    }
    if (hex.size() != 6 && hex.size() != 8) return color;
    uint32_t value = 0;
    for (char c : hex) {
      const int digit = c >= '0' && c <= '9' ? c - '0' :
                        c >= 'a' && c <= 'f' ? c - 'a' + 10 : -1;
      if (digit < 0) return color;
      value = value * 16 + uint32_t(digit);
    }
    color.transparent = false;
    if (hex.size() == 6) {
      color.r = uint8_t(value >> 16); color.g = uint8_t(value >> 8); color.b = uint8_t(value);
    } else {
      color.r = uint8_t(value >> 24); color.g = uint8_t(value >> 16);
      color.b = uint8_t(value >> 8); color.a = uint8_t(value);
    }
    return color;
  }
  if (auto it = names.find(s); it != names.end()) {
    color.transparent = false;
    color.r = uint8_t(it->second >> 16); color.g = uint8_t(it->second >> 8);
    color.b = uint8_t(it->second);
    return color;
  }
  const size_t open = s.find('('), close = s.rfind(')');
  if (open == std::string::npos || close != s.size() - 1 || close <= open) return color;
  const std::string function = trim(std::string_view(s).substr(0, open));
  const std::string body = s.substr(open + 1, close - open - 1);
  std::vector<std::string> tokens;
  std::string token;
  for (char c : body) {
    if (c == ',' || c == '/' || std::isspace(static_cast<unsigned char>(c))) {
      if (!token.empty()) { tokens.push_back(std::move(token)); token.clear(); }
    } else token += c;
  }
  if (!token.empty()) tokens.push_back(std::move(token));
  if ((function != "rgb" && function != "rgba" && function != "hsl" && function != "hsla") ||
      tokens.size() < 3 || tokens.size() > 4) return color;
  auto number = [](std::string text, bool percent, double &out) {
    try {
      size_t used = 0;
      out = std::stod(text, &used);
      if (used != text.size() - (percent ? 1u : 0u) || !std::isfinite(out)) return false;
      return true;
    } catch (...) { return false; }
  };
  double channels[3] = {}, alpha = 1;
  if (function == "rgb" || function == "rgba") {
    for (size_t i = 0; i < 3; ++i) {
      const bool percent = ends(tokens[i], "%");
      std::string raw = percent ? tokens[i].substr(0, tokens[i].size() - 1) : tokens[i];
      if (!number(raw, false, channels[i])) return color;
      channels[i] = percent ? channels[i] * 2.55 : channels[i];
      channels[i] = std::clamp(channels[i], 0.0, 255.0);
    }
  } else {
    std::string hue_text = tokens[0];
    double factor = 1;
    if (ends(hue_text, "deg")) hue_text.resize(hue_text.size() - 3);
    else if (ends(hue_text, "turn")) { hue_text.resize(hue_text.size() - 4); factor = 360; }
    else if (ends(hue_text, "rad")) { hue_text.resize(hue_text.size() - 3); factor = 180 / 3.14159265358979323846; }
    else if (ends(hue_text, "grad")) { hue_text.resize(hue_text.size() - 4); factor = 0.9; }
    double hue = 0, saturation = 0, lightness = 0;
    if (!number(hue_text, false, hue) || !ends(tokens[1], "%") || !ends(tokens[2], "%")) return color;
    std::string sat_text = tokens[1].substr(0, tokens[1].size() - 1);
    std::string light_text = tokens[2].substr(0, tokens[2].size() - 1);
    if (!number(sat_text, false, saturation) || !number(light_text, false, lightness)) return color;
    hue = std::fmod(hue * factor, 360.0); if (hue < 0) hue += 360;
    saturation = std::clamp(saturation / 100, 0.0, 1.0);
    lightness = std::clamp(lightness / 100, 0.0, 1.0);
    const double chroma = (1 - std::abs(2 * lightness - 1)) * saturation;
    const double x = chroma * (1 - std::abs(std::fmod(hue / 60, 2.0) - 1));
    double r = 0, g = 0, b = 0;
    if (hue < 60) { r=chroma; g=x; } else if (hue < 120) { r=x; g=chroma; }
    else if (hue < 180) { g=chroma; b=x; } else if (hue < 240) { g=x; b=chroma; }
    else if (hue < 300) { r=x; b=chroma; } else { r=chroma; b=x; }
    const double m = lightness - chroma / 2;
    channels[0] = (r + m) * 255; channels[1] = (g + m) * 255; channels[2] = (b + m) * 255;
  }
  if (tokens.size() == 4) {
    const bool percent = ends(tokens[3], "%");
    std::string raw = percent ? tokens[3].substr(0, tokens[3].size() - 1) : tokens[3];
    if (!number(raw, false, alpha)) return color;
    alpha = std::clamp(percent ? alpha / 100 : alpha, 0.0, 1.0);
  }
  color.transparent = false;
  color.r = uint8_t(std::round(channels[0])); color.g = uint8_t(std::round(channels[1]));
  color.b = uint8_t(std::round(channels[2])); color.a = uint8_t(std::round(alpha * 255));
  return color;
}
static std::vector<std::string> split_css_top_level(std::string_view text, char delimiter) {
  std::vector<std::string> parts;
  size_t start = 0;
  int parentheses = 0, brackets = 0;
  char quote = 0;
  bool escaped = false;
  for (size_t i = 0; i < text.size(); ++i) {
    const char c = text[i];
    if (quote) {
      if (escaped) escaped = false;
      else if (c == '\\') escaped = true;
      else if (c == quote) quote = 0;
      continue;
    }
    if (c == '\'' || c == '"') { quote = c; continue; }
    if (c == '(') ++parentheses;
    else if (c == ')' && parentheses > 0) --parentheses;
    else if (c == '[') ++brackets;
    else if (c == ']' && brackets > 0) --brackets;
    else if (c == delimiter && parentheses == 0 && brackets == 0) {
      parts.emplace_back(text.substr(start, i - start));
      start = i + 1;
    }
  }
  parts.emplace_back(text.substr(start));
  return parts;
}
static std::vector<std::string> split_css_components(std::string_view text) {
  std::vector<std::string> parts;
  size_t start = std::string_view::npos;
  int parentheses = 0, brackets = 0;
  char quote = 0;
  bool escaped = false;
  for (size_t i = 0; i <= text.size(); ++i) {
    const bool end = i == text.size();
    const char c = end ? '\0' : text[i];
    if (!end && quote) {
      if (escaped) escaped = false;
      else if (c == '\\') escaped = true;
      else if (c == quote) quote = 0;
      continue;
    }
    if (!end && (c == '\'' || c == '"')) { quote = c; if (start == std::string_view::npos) start = i; continue; }
    if (!end && c == '(') ++parentheses;
    else if (!end && c == ')' && parentheses > 0) --parentheses;
    else if (!end && c == '[') ++brackets;
    else if (!end && c == ']' && brackets > 0) --brackets;
    const bool separator = end || (std::isspace(static_cast<unsigned char>(c)) &&
                                   parentheses == 0 && brackets == 0);
    if (separator) {
      if (start != std::string_view::npos) {
        parts.emplace_back(text.substr(start, i - start));
        start = std::string_view::npos;
      }
    } else if (start == std::string_view::npos) start = i;
  }
  return parts;
}
static std::string strip_comments(std::string_view s) {
  std::string out;
  char quote = 0;
  for (size_t i = 0; i < s.size();) {
    if (quote) {
      out += s[i];
      if (s[i] == '\\' && i + 1 < s.size()) out += s[++i];
      else if (s[i] == quote) quote = 0;
      ++i;
    } else if (s[i] == '\'' || s[i] == '"') {
      quote = s[i];
      out += s[i++];
    } else if (s.substr(i, 2) == "/*") {
      auto e = s.find("*/", i + 2);
      i = e == std::string_view::npos ? s.size() : e + 2;
    } else out += s[i++];
  }
  return out;
}
std::vector<CssDeclaration> parse_css_declarations(std::string_view text) {
  std::vector<CssDeclaration> out;
  const std::string uncommented = strip_comments(text);
  for (const std::string &part : split_css_top_level(uncommented, ';')) {
    const auto colons = split_css_top_level(part, ':');
    if (colons.size() < 2) continue;
    std::string key = trim(colons.front());
    std::string value;
    for (size_t i = 1; i < colons.size(); ++i) {
      if (i > 1) value += ':';
      value += colons[i];
    }
    if (key.empty() || key.size() >= 128) continue;
    key = starts(key, "--") ? key : lower(key);
    value = trim(value);
    bool important = false;
    constexpr std::string_view important_suffix = "!important";
    if (value.size() >= important_suffix.size() &&
        lower(std::string_view(value).substr(value.size() - important_suffix.size())) ==
            important_suffix) {
      value = trim(std::string_view(value).substr(0, value.size() - important_suffix.size()));
      important = true;
    }
    if (value.size() < 8192) out.push_back({std::move(key), std::move(value), important});
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
    } else if (starts(selector, "@keyframes") || starts(selector, "@-webkit-keyframes")) {
      const size_t prefix = starts(selector, "@-webkit-keyframes") ? 18 : 10;
      CssKeyframes animation;
      animation.name = trim(selector.substr(prefix));
      Stylesheet nested;
      nested.parse(body, order);
      for (const CssRule &rule : nested.rules) {
        for (const std::string &part : split_css_top_level(rule.selector, ',')) {
          const std::string key = lower(trim(part));
          double offset = -1;
          if (key == "from") offset = 0;
          else if (key == "to") offset = 1;
          else if (!key.empty() && key.back() == '%') {
            try { offset = std::clamp(std::stod(key.substr(0, key.size() - 1)) / 100.0, 0.0, 1.0); }
            catch (...) { offset = -1; }
          }
          if (offset >= 0 && animation.frames.size() < 64)
            animation.frames.push_back({offset, rule.declarations});
        }
      }
      std::stable_sort(animation.frames.begin(), animation.frames.end(),
                       [](const CssKeyframe &a, const CssKeyframe &b) { return a.offset < b.offset; });
      if (!animation.name.empty() && !animation.frames.empty() && keyframes.size() < 128)
        keyframes.push_back(std::move(animation));
      diagnostics.insert(diagnostics.end(), nested.diagnostics.begin(), nested.diagnostics.end());
    } else if (starts(selector, "@")) {
      if (diagnostics.size() < 100)
        diagnostics.push_back(
            {"css",
             "unsupported at-rule " + selector.substr(0, std::min(size_t(40), selector.size()))});
    } else {
      CssRule r;
      r.selector = selector;
      r.declaration_order = parse_css_declarations(body);
      for (const CssDeclaration &declaration : r.declaration_order) {
        r.declarations[declaration.property] = declaration.value;
        if (declaration.important) r.important.insert(declaration.property);
      }
      r.order = order++;
      if (rules.size() < 10000)
        rules.push_back(std::move(r));
    }
    sPos = end;
  }
}
namespace {
class CssMathParser {
public:
  CssMathParser(std::string_view input, double parent, double font, double root,
                double viewport_width, double viewport_height)
      : input_(lower(input)), parent_(parent), font_(font), root_(root),
        viewport_width_(viewport_width),
        viewport_height_(viewport_height >= 0 ? viewport_height : viewport_width) {}

  bool evaluate(double &result) {
    result = sum();
    whitespace();
    return valid_ && position_ == input_.size() && std::isfinite(result) &&
           std::abs(result) <= 1.0e9;
  }

private:
  std::string input_;
  size_t position_ = 0;
  double parent_, font_, root_, viewport_width_, viewport_height_;
  bool valid_ = true;

  void whitespace() {
    while (position_ < input_.size() &&
           std::isspace(static_cast<unsigned char>(input_[position_]))) ++position_;
  }
  bool take(char c) {
    whitespace();
    if (position_ >= input_.size() || input_[position_] != c) return false;
    ++position_;
    return true;
  }
  double sum() {
    double value = product();
    for (;;) {
      whitespace();
      if (position_ >= input_.size() || (input_[position_] != '+' && input_[position_] != '-'))
        return value;
      const char op = input_[position_++];
      const double rhs = product();
      value = op == '+' ? value + rhs : value - rhs;
    }
  }
  double product() {
    double value = unary();
    for (;;) {
      whitespace();
      if (position_ >= input_.size() || (input_[position_] != '*' && input_[position_] != '/'))
        return value;
      const char op = input_[position_++];
      const double rhs = unary();
      if (op == '/' && rhs == 0) { valid_ = false; return 0; }
      value = op == '*' ? value * rhs : value / rhs;
    }
  }
  double unary() {
    whitespace();
    if (take('+')) return unary();
    if (take('-')) return -unary();
    return primary();
  }
  double primary() {
    whitespace();
    if (take('(')) {
      double value = sum();
      if (!take(')')) valid_ = false;
      return value;
    }
    const size_t start = position_;
    while (position_ < input_.size() &&
           (std::isalpha(static_cast<unsigned char>(input_[position_])) || input_[position_] == '-'))
      ++position_;
    if (position_ > start && take('(')) {
      const std::string function = input_.substr(start, position_ - start - 1);
      if (function == "calc") {
        double value = sum();
        if (!take(')')) valid_ = false;
        return value;
      }
      if (function == "min" || function == "max" || function == "clamp") {
        std::vector<double> args;
        args.push_back(sum());
        while (take(',')) args.push_back(sum());
        if (!take(')')) valid_ = false;
        if ((function == "clamp" && args.size() != 3) ||
            (function != "clamp" && args.empty())) { valid_ = false; return 0; }
        if (function == "min") return *std::min_element(args.begin(), args.end());
        if (function == "max") return *std::max_element(args.begin(), args.end());
        return std::max(args[0], std::min(args[1], args[2]));
      }
      valid_ = false;
      return 0;
    }
    position_ = start;
    if (position_ >= input_.size()) { valid_ = false; return 0; }
    const char *begin = input_.c_str() + position_;
    char *end = nullptr;
    const double number = std::strtod(begin, &end);
    if (end == begin || !std::isfinite(number)) { valid_ = false; return 0; }
    position_ += size_t(end - begin);
    const size_t unit_start = position_;
    while (position_ < input_.size() &&
           (std::isalpha(static_cast<unsigned char>(input_[position_])) || input_[position_] == '%'))
      ++position_;
    const std::string unit = input_.substr(unit_start, position_ - unit_start);
    if (unit.empty() || unit == "px") return number;
    if (unit == "%") return number * parent_ / 100;
    if (unit == "em") return number * font_;
    if (unit == "rem") return number * root_;
    if (unit == "vw") return number * viewport_width_ / 100;
    if (unit == "vh") return number * viewport_height_ / 100;
    if (unit == "vmin") return number * std::min(viewport_width_, viewport_height_) / 100;
    if (unit == "vmax") return number * std::max(viewport_width_, viewport_height_) / 100;
    if (unit == "pt") return number * 96 / 72;
    if (unit == "pc") return number * 16;
    if (unit == "in") return number * 96;
    if (unit == "cm") return number * 96 / 2.54;
    if (unit == "mm") return number * 96 / 25.4;
    if (unit == "q") return number * 96 / 101.6;
    if (unit == "ex" || unit == "ch") return number * font_ * 0.5;
    valid_ = false;
    return 0;
  }
};
} // namespace

double css_length(const std::string &v, double parent, double font, double root,
                  double viewport_width, const std::string &prop, double fallback,
                  double viewport_height) {
  (void)prop;
  auto s = lower(trim(v));
  if (s.empty() || s == "auto" || s == "none" || s == "normal" || s == "initial" ||
      s == "inherit" || s == "unset") return fallback;
  double result = fallback;
  CssMathParser parser(s, parent, font, root, viewport_width, viewport_height);
  return parser.evaluate(result) ? result : fallback;
}
int specificity(std::string_view selector) {
  std::function<std::array<int, 3>(std::string_view)> one;
  int nesting = 0;
  one = [&](std::string_view text) -> std::array<int, 3> {
    std::array<int, 3> result{};
    if (++nesting > 64) { --nesting; return result; }
    char quote = 0;
    bool escaped = false;
    for (size_t i = 0; i < text.size();) {
      const char c = text[i];
      if (quote) {
        if (escaped) escaped = false;
        else if (c == '\\') escaped = true;
        else if (c == quote) quote = 0;
        ++i;
        continue;
      }
      if (c == '\'' || c == '"') { quote = c; ++i; continue; }
      if (c == '[') {
        ++result[1];
        int depth = 1;
        for (++i; i < text.size() && depth; ++i) {
          if (text[i] == '[') ++depth;
          else if (text[i] == ']') --depth;
        }
        continue;
      }
      if (c == '#') {
        ++result[0];
        ++i;
        while (i < text.size() && (std::isalnum(static_cast<unsigned char>(text[i])) ||
                                   text[i] == '-' || text[i] == '_')) ++i;
        continue;
      }
      if (c == '.') {
        ++result[1];
        ++i;
        while (i < text.size() && (std::isalnum(static_cast<unsigned char>(text[i])) ||
                                   text[i] == '-' || text[i] == '_')) ++i;
        continue;
      }
      if (c == ':') {
        const bool element = i + 1 < text.size() && text[i + 1] == ':';
        i += element ? 2 : 1;
        const size_t begin = i;
        while (i < text.size() && (std::isalnum(static_cast<unsigned char>(text[i])) || text[i] == '-')) ++i;
        const std::string name = lower(text.substr(begin, i - begin));
        if (element) ++result[2];
        if (i < text.size() && text[i] == '(') {
          const size_t open = i++;
          int depth = 1;
          char q = 0;
          for (; i < text.size() && depth; ++i) {
            const char inner = text[i];
            if (q) { if (inner == q && (i == 0 || text[i - 1] != '\\')) q = 0; }
            else if (inner == '\'' || inner == '"') q = inner;
            else if (inner == '(') ++depth;
            else if (inner == ')') --depth;
          }
          const std::string_view args = text.substr(open + 1, i - open - 2);
          if (name == "not" || name == "is" || name == "has") {
            std::array<int, 3> best{};
            for (const std::string &group : split_css_top_level(args, ',')) {
              const auto value = one(group);
              if (value > best) best = value;
            }
            for (size_t k = 0; k < 3; ++k) result[k] += best[k];
          } else if (name != "where" && !element) ++result[1];
        } else if (!element && name != "where") ++result[1];
        continue;
      }
      const bool type_start = std::isalpha(static_cast<unsigned char>(c)) || c == '*';
      if (type_start) {
        const bool boundary = i == 0 || std::isspace(static_cast<unsigned char>(text[i - 1])) ||
                              text[i - 1] == '>' || text[i - 1] == '+' || text[i - 1] == '~' ||
                              text[i - 1] == ',';
        size_t end = i + 1;
        while (end < text.size() && (std::isalnum(static_cast<unsigned char>(text[end])) ||
                                     text[end] == '-' || text[end] == '_')) ++end;
        if (boundary && c != '*') ++result[2];
        i = end;
        continue;
      }
      ++i;
    }
    --nesting;
    return result;
  };
  int best = 0;
  for (const std::string &group : split_css_top_level(selector, ',')) {
    auto value = one(group);
    best = std::max(best, value[0] * 10000 + value[1] * 100 + value[2]);
  }
  return best;
}
static std::string ua_css() {
  return R"CSS(
html,body{margin:0;padding:0}body{font-family:Segoe UI,Arial,sans-serif;font-size:16px;line-height:1.45;color:#111;background:#fff}head,style,script,meta,link,title{display:none}p{margin:1em 0}h1{font-size:2em;font-weight:bold;margin:.67em 0}h2{font-size:1.5em;font-weight:bold;margin:.83em 0}h3{font-size:1.17em;font-weight:bold;margin:1em 0}h4{font-weight:bold;margin:1.33em 0}h5{font-size:.83em;font-weight:bold;margin:1.67em 0}h6{font-size:.67em;font-weight:bold;margin:2.33em 0}a{color:#06c;text-decoration:underline}strong,b{font-weight:bold}em,i{font-style:italic}small{font-size:.83em}code,pre,kbd,samp{font-family:Consolas,monospace}pre{white-space:pre;margin:1em 0}ul,ol{margin:1em 0;padding-left:40px}li{display:list-item}blockquote{margin:1em 40px}hr{border-top:1px solid #888;margin:.5em 0}img,canvas,video,audio{display:inline-block}input,button,select,textarea{display:inline-block;font-size:14px;padding:4px 8px;border:1px solid #777;background:#fff;color:#111}button{text-align:center}table{display:block;border-collapse:collapse}tr{display:block}td,th{display:inline-block;padding:2px}th{font-weight:bold}
)CSS";
}
static void expand_four(const std::string &value, double *out, double parent, double font,
                        double root, double viewport, double viewport_height, const char *prop) {
  auto parts = split_css_components(value);
  if (parts.empty() || parts.size() > 4)
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
    out[i] = css_length(parts[indexes[i]], parent, font, root, viewport, prop, out[i],
                        viewport_height);
}
// A border shorthand mixes a length, a style word and a colour, and my colour
// parser answers black for anything it does not know, so I classify first.
static bool looks_like_length(const std::string &text) {
  if (text.empty())
    return false;
  const std::string low = lower(text);
  if (starts(low, "calc(") || starts(low, "min(") || starts(low, "max(") ||
      starts(low, "clamp(")) return true;
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
static std::vector<std::string> expand_four_values(const std::string &value) {
  const auto parts = split_css_components(value);
  if (parts.empty() || parts.size() > 4) return {};
  std::vector<std::string> out(4);
  out[0] = parts[0];
  out[1] = parts.size() > 1 ? parts[1] : parts[0];
  out[2] = parts.size() > 2 ? parts[2] : parts[0];
  out[3] = parts.size() > 3 ? parts[3] : out[1];
  return out;
}
static std::vector<std::pair<std::string, std::string>> expand_css_declaration(
    const std::string &property, const std::string &value) {
  std::vector<std::pair<std::string, std::string>> out;
  const std::string wide = lower(trim(value));
  if (wide == "inherit" || wide == "initial" || wide == "unset" || wide == "revert") {
    if (property == "margin" || property == "padding" || property == "border-width") {
      const char *prefix = property == "margin" ? "margin-" :
                           property == "padding" ? "padding-" : "border-";
      for (const char *side : {"top", "right", "bottom", "left"})
        out.emplace_back(std::string(prefix) + side +
                         (property == "border-width" ? "-width" : ""), wide);
      return out;
    }
    if (property == "border" || property == "border-top" || property == "border-right" ||
        property == "border-bottom" || property == "border-left") {
      if (property == "border") {
        for (const char *side : {"top", "right", "bottom", "left"})
          out.emplace_back(std::string("border-") + side + "-width", wide);
      } else out.emplace_back(property + "-width", wide);
      out.emplace_back("border-color", wide);
      return out;
    }
    if (property == "font") {
      for (const char *sub : {"font-family", "font-size", "font-style", "font-weight", "line-height"})
        out.emplace_back(sub, wide);
      return out;
    }
    if (property == "flex") {
      for (const char *sub : {"flex-grow", "flex-shrink", "flex-basis"})
        out.emplace_back(sub, wide);
      return out;
    }
    if (property == "flex-flow") return {{"flex-direction", wide}, {"flex-wrap", wide}};
    if (property == "place-items") return {{"align-items", wide}, {"justify-items", wide}};
    if (property == "place-self") return {{"align-self", wide}, {"justify-self", wide}};
    if (property == "place-content") return {{"align-content", wide}, {"justify-content", wide}};
    if (property == "gap") return {{"row-gap", wide}, {"column-gap", wide}};
    return {{property, wide}};
  }
  auto four = [&](const std::string &prefix) {
    const auto values = expand_four_values(value);
    if (!values.empty())
      for (size_t i = 0; i < values.size(); ++i)
        out.emplace_back(prefix + std::array<const char *, 4>{"top", "right", "bottom", "left"}[i],
                         values[i]);
  };
  if (property == "margin" || property == "padding" || property == "border-width") {
    four(property == "border-width" ? "border-" : property + "-");
    if (property == "border-width")
      for (auto &entry : out) entry.first += "-width";
    return out.empty() ? std::vector<std::pair<std::string, std::string>>{{property, value}}
                       : out;
  }
  if (property == "gap") {
    const auto parts = split_css_components(value);
    if (parts.size() == 1) return {{"row-gap", parts[0]}, {"column-gap", parts[0]}};
    if (parts.size() == 2) return {{"row-gap", parts[0]}, {"column-gap", parts[1]}};
  }
  if (property == "place-items" || property == "place-self" || property == "place-content") {
    const auto parts = split_css_components(value);
    if (parts.size() == 1) {
      const std::string first = property == "place-items" ? "align-items" :
                                property == "place-self" ? "align-self" : "align-content";
      const std::string second = property == "place-items" ? "justify-items" :
                                 property == "place-self" ? "justify-self" : "justify-content";
      return {{first, parts[0]}, {second, parts[0]}};
    }
    if (parts.size() == 2) {
      const std::string first = property == "place-items" ? "align-items" :
                                property == "place-self" ? "align-self" : "align-content";
      const std::string second = property == "place-items" ? "justify-items" :
                                 property == "place-self" ? "justify-self" : "justify-content";
      return {{first, parts[0]}, {second, parts[1]}};
    }
  }
  if (property == "font") {
    const auto parts = split_css_components(value);
    size_t size_index = std::string::npos;
    std::string size_value, line_height;
    for (size_t i = 0; i < parts.size(); ++i) {
      const size_t slash = parts[i].find('/');
      const std::string candidate = parts[i].substr(0, slash);
      static const std::map<std::string, std::string> sizes = {
          {"xx-small", "9px"}, {"x-small", "10px"}, {"small", "13px"},
          {"medium", "16px"}, {"large", "18px"}, {"x-large", "24px"},
          {"xx-large", "32px"}};
      const bool has_unit = std::any_of(candidate.begin(), candidate.end(), [](unsigned char c) {
        return std::isalpha(c) || c == '%';
      });
      if ((has_unit && looks_like_length(candidate)) || sizes.count(lower(candidate))) {
        size_index = i;
        size_value = sizes.count(lower(candidate)) ? sizes.at(lower(candidate)) : candidate;
        if (slash != std::string::npos) line_height = parts[i].substr(slash + 1);
        break;
      }
    }
    if (size_index != std::string::npos && size_index + 1 < parts.size()) {
      out.emplace_back("font-style", "normal");
      out.emplace_back("font-weight", "normal");
      for (size_t i = 0; i < size_index; ++i) {
        const std::string word = lower(parts[i]);
        if (word == "italic" || word == "oblique") out.emplace_back("font-style", word);
        else if (word == "bold" || word == "bolder" || word == "lighter" ||
                 (!word.empty() && std::isdigit(static_cast<unsigned char>(word[0]))))
          out.emplace_back("font-weight", word);
      }
      out.emplace_back("font-size", size_value);
      if (!line_height.empty()) out.emplace_back("line-height", line_height);
      size_t at = value.find(parts[size_index]);
      if (at != std::string::npos) {
        at += parts[size_index].size();
        std::string family = trim(std::string_view(value).substr(at));
        if (!family.empty()) out.emplace_back("font-family", family);
      }
      if (out.size() >= 4) return out;
    }
  }
  static const std::array<const char *, 4> sides = {"top", "right", "bottom", "left"};
  bool all_sides = property == "border";
  std::string single_side;
  if (!all_sides)
    for (const char *side : sides)
      if (property == std::string("border-") + side) single_side = side;
  if (all_sides || !single_side.empty()) {
    std::string width, border_color;
    for (const std::string &part : split_css_components(value)) {
      const std::string word = lower(part);
      if (word == "none" || word == "hidden") width = "0";
      else if (border_style_word(word)) continue;
      else if (looks_like_length(word) || word == "thin" || word == "medium" || word == "thick")
        width = word == "thin" ? "1px" : word == "medium" ? "3px" :
                word == "thick" ? "5px" : part;
      else if (colour_word(word) || parse_color(word).transparent == false) border_color = part;
    }
    if (!width.empty()) {
      if (all_sides) {
        for (const char *side : sides) out.emplace_back(std::string("border-") + side + "-width", width);
      } else out.emplace_back(std::string("border-") + single_side + "-width", width);
    }
    if (!border_color.empty()) out.emplace_back("border-color", border_color);
    if (!out.empty()) return out;
  }
  return {{property, value}};
}
static bool substitute_css_vars(const std::string &input,
                                const std::map<std::string, std::string> &custom,
                                std::set<std::string> &stack, std::string &output,
                                unsigned depth = 0) {
  if (depth > 32) return false;
  output.clear();
  for (size_t i = 0; i < input.size();) {
    if (input[i] == '\'' || input[i] == '"') {
      const char quote = input[i];
      output += input[i++];
      while (i < input.size()) {
        const char c = input[i++];
        output += c;
        if (c == '\\' && i < input.size()) output += input[i++];
        else if (c == quote) break;
      }
    } else if (i + 4 <= input.size() && lower(std::string_view(input).substr(i, 4)) == "var(") {
      size_t open = i + 3, end = open + 1;
      int nesting = 1;
      char quote = 0;
      bool escaped = false;
      for (; end < input.size() && nesting; ++end) {
        const char c = input[end];
        if (quote) {
          if (escaped) escaped = false;
          else if (c == '\\') escaped = true;
          else if (c == quote) quote = 0;
        } else if (c == '\'' || c == '"') quote = c;
        else if (c == '(') ++nesting;
        else if (c == ')') --nesting;
      }
      if (nesting != 0) return false;
      const std::string body = input.substr(open + 1, end - open - 2);
      const auto args = split_css_top_level(body, ',');
      const std::string name = trim(args.front());
      if (!starts(name, "--")) return false;
      std::string replacement;
      auto found = custom.find(name);
      bool resolved = false;
      if (found != custom.end() && !stack.count(name)) {
        stack.insert(name);
        resolved = substitute_css_vars(found->second, custom, stack, replacement, depth + 1);
        stack.erase(name);
      }
      if (!resolved && args.size() > 1)
        resolved = substitute_css_vars(trim(std::string_view(body).substr(
                                                body.find(',') + 1)), custom, stack,
                                       replacement, depth + 1);
      if (!resolved) return false;
      output += replacement;
      i = end;
    } else output += input[i++];
  }
  return true;
}
static Color resolved_color(const std::string &value, const Color &current) {
  return lower(trim(value)) == "currentcolor" ? current : parse_color(value);
}
static void apply(Style &s, const std::map<std::string, std::string> &d, double parent_width,
                  double root, double viewport, double viewport_height) {
  auto get = [&](const char *k) -> const std::string * {
    auto it = d.find(k);
    return it == d.end() ? nullptr : &it->second;
  };
  for (auto &p : d)
    s.raw[p.first] = p.second;
  if (auto v = get("display"))
    s.display = lower(*v);
  if (auto v = get("color"))
    s.color = resolved_color(*v, s.color);
  if (auto v = get("background-color"))
    s.background = resolved_color(*v, s.color);
  auto parse_background_layers = [&](const std::string &value) {
    std::vector<std::string> layers;
    for (const std::string &part : split_css_top_level(value, ',')) {
      const std::string layer = trim(part);
      const std::string lowered = lower(layer);
      if (lowered.find("gradient(") != std::string::npos || lowered.find("url(") != std::string::npos)
        layers.push_back(layer);
    }
    s.background_layers = std::move(layers);
    s.background_image = s.background_layers.empty() ? std::string() : s.background_layers.front();
  };
  if (auto v = get("background")) {
    parse_background_layers(*v);
    for (const std::string &part : split_css_top_level(*v, ','))
      for (const std::string &component : split_css_components(part)) {
        Color candidate = resolved_color(component, s.color);
        if (!candidate.transparent) { s.background = candidate; break; }
      }
  }
  if (auto v = get("background-image")) {
    if (lower(trim(*v)) == "none") parse_background_layers(std::string());
    else parse_background_layers(*v);
  }
  if (auto v = get("font-size"))
    s.font_size = std::clamp(
        css_length(*v, parent_width, s.font_size, root, viewport, "font-size", s.font_size, viewport_height), 6.0,
        128.0);
  if (auto v = get("font-family")) {
    auto families = split_css_top_level(*v, ',');
    if (!families.empty()) {
      s.font_family = trim(families.front());
      if (s.font_family.size() >= 2 &&
          ((s.font_family.front() == '\'' && s.font_family.back() == '\'') ||
           (s.font_family.front() == '"' && s.font_family.back() == '"')))
        s.font_family = s.font_family.substr(1, s.font_family.size() - 2);
    }
  }
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
    const std::string value = trim(*v);
    char *end = nullptr;
    const double unitless = std::strtod(value.c_str(), &end);
    if (end != value.c_str() && end == value.c_str() + value.size() && std::isfinite(unitless))
      s.line_height = unitless;
    else
      s.line_height = css_length(value, s.font_size, s.font_size, root, viewport,
                                 "line-height", s.font_size * 1.45, viewport_height) /
                      s.font_size;
    s.line_height = std::clamp(s.line_height, 0.8, 4.0);
  }
  const char *sides[4] = {"top", "right", "bottom", "left"};
  auto note_auto = [&](int index, const std::string &value) {
    if (lower(trim(value)) != "auto")
      return;
    if (index == 0)
      s.auto_top = true;
    else if (index == 1)
      s.auto_right = true;
    else if (index == 2)
      s.auto_bottom = true;
    else if (index == 3)
      s.auto_left = true;
  };
  for (int i = 0; i < 4; ++i) {
    if (auto v = get((std::string("margin-") + sides[i]).c_str())) {
      note_auto(i, *v);
      s.margin[i] = css_length(*v, parent_width, s.font_size, root, viewport, "margin", 0, viewport_height);
    }
    if (auto v = get((std::string("padding-") + sides[i]).c_str()))
      s.padding[i] =
          std::max(0.0, css_length(*v, parent_width, s.font_size, root, viewport, "padding", 0, viewport_height));
    if (auto v = get((std::string("border-") + sides[i] + "-width").c_str()))
      s.border[i] =
          std::max(0.0, css_length(*v, parent_width, s.font_size, root, viewport, "border", 0, viewport_height));
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
              0.0, css_length(low, parent_width, s.font_size, root, viewport, "border", s.border[i], viewport_height));
          continue;
        }
        if (colour_word(low))
          s.border_color = resolved_color(low, s.color);
      }
    }
  }
  if (auto v = get("margin")) {
    expand_four(*v, s.margin, parent_width, s.font_size, root, viewport,
                viewport_height, "margin");
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
    expand_four(*v, s.padding, parent_width, s.font_size, root, viewport,
                viewport_height, "padding");
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
            std::max(0.0, css_length(low, parent_width, s.font_size, root, viewport, "border", 0, viewport_height));
        for (int i = 0; i < 4; ++i)
          s.border[i] = width;
        continue;
      }
      if (colour_word(low))
        s.border_color = resolved_color(low, s.color);
    }
  }
  if (auto v = get("border-color"))
    s.border_color = resolved_color(*v, s.color);
  if (auto v = get("border-radius"))
    s.radius =
        std::max(0.0, css_length(*v, parent_width, s.font_size, root, viewport, "radius", 0, viewport_height));
  if (auto v = get("outline-width"))
    s.outline_width = std::max(0.0, css_length(*v, parent_width, s.font_size, root, viewport,
                                               "outline", 0, viewport_height));
  if (auto v = get("outline-color")) s.outline_color = resolved_color(*v, s.color);
  if (auto v = get("outline-offset"))
    s.outline_offset = css_length(*v, parent_width, s.font_size, root, viewport,
                                  "outline-offset", 0, viewport_height);
  if (auto v = get("outline")) {
    for (const std::string &part : split_css_components(*v)) {
      const std::string low = lower(trim(part));
      if (low == "none" || low == "hidden") s.outline_width = 0;
      else if (colour_word(low) || !parse_color(low).transparent)
        s.outline_color = resolved_color(low, s.color);
      else if (looks_like_length(low))
        s.outline_width = std::max(0.0, css_length(low, parent_width, s.font_size, root,
                                                    viewport, "outline", 0, viewport_height));
      else if (low == "thin") s.outline_width = 1;
      else if (low == "medium") s.outline_width = 3;
      else if (low == "thick") s.outline_width = 5;
    }
  }
  if (auto v = get("box-shadow")) s.box_shadow = lower(trim(*v)) == "none" ? std::string() : trim(*v);
  if (auto v = get("filter")) s.filter = lower(trim(*v)) == "none" ? std::string() : trim(*v);
  if (auto v = get("width")) {
    if (v->find('%') != std::string::npos) {
      s.width = -1;
      s.width_expression = *v;
    } else {
      s.width_expression.clear();
      s.width = css_length(*v, parent_width, s.font_size, root, viewport,
                           "width", -1, viewport_height);
    }
  }
  if (auto v = get("height")) {
    const std::string text = trim(*v);
    s.height_expression.clear();
    s.height_percent = -1;
    if (text.find('%') != std::string::npos && !text.empty() && text.back() == '%') {
      char *end = nullptr;
      const double percent = std::strtod(text.c_str(), &end);
      if (end == text.c_str() + text.size() - 1 && std::isfinite(percent)) {
        s.height_percent = std::clamp(percent / 100.0, 0.0, 10.0);
        s.height = -1;
      } else {
        s.height = -1;
        s.height_expression = text;
      }
    } else if (text.find('%') != std::string::npos) {
      s.height = -1;
      s.height_expression = text;
    } else {
      s.height = css_length(text, parent_width, s.font_size, root, viewport,
                            "height", -1, viewport_height);
    }
  }
  auto set_bound = [&](const char *property, double &number, std::string &expression,
                       double fallback, const char *axis) {
    if (const auto *value = get(property)) {
      if (value->find('%') != std::string::npos) {
        expression = *value;
        number = fallback;
      } else {
        expression.clear();
        number = css_length(*value, parent_width, s.font_size, root, viewport, axis,
                            fallback, viewport_height);
      }
    }
  };
  set_bound("min-width", s.min_width, s.min_width_expression, 0, "width");
  set_bound("max-width", s.max_width, s.max_width_expression, -1, "width");
  set_bound("min-height", s.min_height, s.min_height_expression, 0, "height");
  set_bound("max-height", s.max_height, s.max_height_expression, -1, "height");
  if (auto v = get("box-sizing"))
    s.box_sizing = *v;
  if (auto v = get("white-space"))
    s.white_space = *v;
  if (auto v = get("text-overflow"))
    s.text_overflow = *v;
  if (auto v = get("position"))
    s.position = lower(*v);
  if (auto v = get("z-index")) {
    const std::string value = lower(trim(*v));
    if (value == "auto") {
      s.z_index = 0;
      s.z_index_auto = true;
    } else {
      try {
        s.z_index = int(std::clamp(std::stol(value), -1000000L, 1000000L));
        s.z_index_auto = false;
      } catch (...) {
      }
    }
  }
  if (auto v = get("overflow"))
    s.overflow = lower(trim(*v));
  if (auto v = get("scrollbar-gutter"))
    s.scrollbar_gutter = lower(trim(*v));
  if (auto v = get("overscroll-behavior"))
    s.overscroll_behavior = lower(trim(*v));
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
    s.align_items = lower(*v);
  if (auto v = get("align-content"))
    s.align_content = lower(*v);
  if (auto v = get("justify-items"))
    s.justify_items = lower(*v);
  if (auto v = get("justify-self"))
    s.justify_self = lower(*v);
  if (auto v = get("gap") ? get("gap") : get("column-gap"))
    s.gap = css_length(*v, parent_width, s.font_size, root, viewport, "gap", 0, viewport_height);
  if (auto v = get("top")) {
    s.top = css_length(*v, parent_width, s.font_size, root, viewport, "top", 0, viewport_height);
    s.has_top = lower(*v) != "auto";
  }
  if (auto v = get("left")) {
    s.left = css_length(*v, parent_width, s.font_size, root, viewport, "left", 0, viewport_height);
    s.has_left = lower(*v) != "auto";
  }
  if (auto v = get("right")) {
    s.right = css_length(*v, parent_width, s.font_size, root, viewport, "right", 0, viewport_height);
    s.has_right = lower(*v) != "auto";
  }
  if (auto v = get("bottom")) {
    s.bottom = css_length(*v, parent_width, s.font_size, root, viewport, "bottom", 0, viewport_height);
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
  if (auto v = get("flex-basis")) {
    s.flex_basis_expression = lower(*v) == "auto" ? std::string() : *v;
    s.flex_basis = lower(*v) == "auto"
                       ? -1
                       : css_length(*v, parent_width, s.font_size, root, viewport, "flex-basis", -1, viewport_height);
  }
  if (auto v = get("flex")) {
    // A one-number shorthand has a zero basis; auto/none retain their named
    // semantics rather than being parsed as invalid numeric values.
    auto parts = split(*v, ' ');
    std::vector<std::string> values;
    for (const std::string &part : parts)
      if (!part.empty()) values.push_back(lower(part));
    if (!values.empty() && values[0] == "none") {
      s.flex_grow = 0;
      s.flex_shrink = 0;
      s.flex_basis = -1;
      s.flex_basis_expression.clear();
    } else if (!values.empty() && values[0] == "auto") {
      s.flex_grow = 1;
      s.flex_shrink = 1;
      s.flex_basis = -1;
      s.flex_basis_expression.clear();
    } else if (!values.empty()) {
      auto number = [](const std::string &text, double fallback) {
        try { return std::max(0.0, std::stod(text)); }
        catch (...) { return fallback; }
      };
      s.flex_grow = number(values[0], s.flex_grow);
      s.flex_shrink = values.size() > 1 ? number(values[1], s.flex_shrink) : 1;
      if (values.size() > 2) {
        s.flex_basis_expression = lower(values[2]) == "auto" ? std::string() : values[2];
        s.flex_basis = lower(values[2]) == "auto" ? -1 :
            css_length(values[2], parent_width, s.font_size, root, viewport,
                       "flex-basis", -1, viewport_height);
      } else {
        s.flex_basis = 0;
        s.flex_basis_expression.clear();
      }
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
        std::max(0.0, css_length(*v, parent_width, s.font_size, root, viewport, "gap", 0, viewport_height));
  }
  if (auto v = get("grid-template-rows"))
    s.grid_rows = trim(*v);
  if (auto v = get("grid-template-columns"))
    s.grid_columns = *v;
  if (auto v = get("grid-template-areas"))
    s.grid_template_areas = *v;
  if (auto v = get("grid-auto-flow"))
    s.grid_auto_flow = lower(*v);
  if (auto v = get("grid-column"))
    s.grid_column = trim(*v);
  if (auto v = get("grid-row"))
    s.grid_row = trim(*v);
  if (auto v = get("grid-area"))
    s.grid_area = trim(*v);
  if (auto v = get("transform"))
    s.transform = *v;
}
void apply_style_overrides(Style &style,
                          const std::map<std::string, std::string> &declarations,
                          double viewport_width, double viewport_height) {
  apply(style, declarations, viewport_width, style.root_font_size, viewport_width,
        viewport_height);
}
static bool inherited_css_property(const std::string &property) {
  static const std::set<std::string> inherited = {
      "color", "font-family", "font-size", "font-style", "font-weight", "line-height",
      "text-align", "white-space"};
  return inherited.count(property) != 0;
}
static std::optional<std::string> initial_css_value(const std::string &property) {
  static const std::map<std::string, std::string> values = {
      {"display", "inline"}, {"color", "#000"}, {"background", "transparent"},
      {"background-color", "transparent"}, {"background-image", "none"},
      {"font-family", "serif"}, {"font-size", "16px"}, {"font-style", "normal"},
      {"font-weight", "normal"}, {"line-height", "normal"}, {"text-align", "left"},
      {"text-decoration", "none"}, {"white-space", "normal"}, {"width", "auto"},
      {"height", "auto"}, {"min-width", "0"}, {"max-width", "none"},
      {"min-height", "0"}, {"max-height", "none"}, {"box-sizing", "content-box"},
      {"position", "static"}, {"z-index", "auto"}, {"overflow", "visible"},
      {"overscroll-behavior", "auto"}, {"opacity", "1"},
      {"top", "auto"}, {"right", "auto"}, {"bottom", "auto"}, {"left", "auto"},
      {"border-color", "currentColor"}, {"border-radius", "0"},
      {"outline", "none"}, {"outline-width", "medium"}, {"outline-color", "currentColor"},
      {"outline-offset", "0"}, {"box-shadow", "none"}, {"filter", "none"}, {"gap", "normal"},
      {"row-gap", "normal"}, {"column-gap", "normal"}, {"flex-direction", "row"},
      {"flex-wrap", "nowrap"}, {"flex-grow", "0"}, {"flex-shrink", "1"},
      {"flex-basis", "auto"}, {"justify-content", "flex-start"},
      {"align-items", "stretch"}, {"align-self", "auto"}, {"align-content", "stretch"},
      {"justify-items", "stretch"}, {"justify-self", "auto"},
      {"grid-template-columns", "none"}, {"grid-template-rows", "none"},
      {"grid-template-areas", "none"}, {"grid-auto-flow", "row"},
      {"grid-column", "auto"}, {"grid-row", "auto"}, {"grid-area", "auto"},
      {"order", "0"}};
  if (property == "margin" || property.rfind("margin-", 0) == 0 ||
      property == "padding" || property.rfind("padding-", 0) == 0 ||
      (property.rfind("border-", 0) == 0 && property.ends_with("-width"))) return "0";
  auto found = values.find(property);
  return found == values.end() ? std::nullopt : std::optional<std::string>(found->second);
}
static void inherit_style_property(Style &to, const Style &from, const std::string &property) {
  if (property == "color") to.color = from.color;
  else if (property == "background" || property == "background-color") {
    to.background = from.background;
    if (property == "background") {
      to.background_image = from.background_image;
      to.background_layers = from.background_layers;
    }
  } else if (property == "background-image") {
    to.background_image = from.background_image;
    to.background_layers = from.background_layers;
  }
  else if (property == "display") to.display = from.display;
  else if (property == "font-family") to.font_family = from.font_family;
  else if (property == "font-size") to.font_size = from.font_size;
  else if (property == "font-style") to.italic = from.italic;
  else if (property == "font-weight") { to.weight = from.weight; to.bold = from.bold; }
  else if (property == "line-height") to.line_height = from.line_height;
  else if (property == "text-align") to.text_align = from.text_align;
  else if (property == "text-decoration") to.underline = from.underline;
  else if (property == "white-space") to.white_space = from.white_space;
  else if (property == "width") { to.width = from.width; to.width_expression = from.width_expression; }
  else if (property == "height") { to.height = from.height; to.height_expression = from.height_expression; to.height_percent = from.height_percent; }
  else if (property == "min-width") { to.min_width = from.min_width; to.min_width_expression = from.min_width_expression; }
  else if (property == "max-width") { to.max_width = from.max_width; to.max_width_expression = from.max_width_expression; }
  else if (property == "min-height") { to.min_height = from.min_height; to.min_height_expression = from.min_height_expression; }
  else if (property == "max-height") { to.max_height = from.max_height; to.max_height_expression = from.max_height_expression; }
  else if (property == "border-color") to.border_color = from.border_color;
  else if (property == "border-radius") to.radius = from.radius;
  else if (property == "box-sizing") to.box_sizing = from.box_sizing;
  else if (property == "position") to.position = from.position;
  else if (property == "z-index") { to.z_index = from.z_index; to.z_index_auto = from.z_index_auto; }
  else if (property == "overflow") to.overflow = from.overflow;
  else if (property == "scrollbar-gutter") to.scrollbar_gutter = from.scrollbar_gutter;
  else if (property == "overscroll-behavior") to.overscroll_behavior = from.overscroll_behavior;
  else if (property == "opacity") to.opacity = from.opacity;
  else if (property == "filter") to.filter = from.filter;
  else if (property == "box-shadow") to.box_shadow = from.box_shadow;
  else if (property == "outline-width") to.outline_width = from.outline_width;
  else if (property == "outline-color") to.outline_color = from.outline_color;
  else if (property == "outline-offset") to.outline_offset = from.outline_offset;
  else if (property == "outline") {
    to.outline_width = from.outline_width;
    to.outline_color = from.outline_color;
    to.outline_offset = from.outline_offset;
  }
  else if (property == "transform") to.transform = from.transform;
  else if (property == "top") { to.top=from.top; to.has_top=from.has_top; }
  else if (property == "left") { to.left=from.left; to.has_left=from.has_left; }
  else if (property == "right") { to.right=from.right; to.has_right=from.has_right; }
  else if (property == "bottom") { to.bottom=from.bottom; to.has_bottom=from.has_bottom; }
  else if (property == "gap" || property == "column-gap") to.gap = from.gap;
  else if (property == "row-gap") to.row_gap = from.row_gap;
  else if (property == "flex-direction") to.flex_direction = from.flex_direction;
  else if (property == "flex-wrap") to.flex_wrap = from.flex_wrap;
  else if (property == "flex-grow") to.flex_grow = from.flex_grow;
  else if (property == "flex-shrink") to.flex_shrink = from.flex_shrink;
  else if (property == "flex-basis") {
    to.flex_basis = from.flex_basis;
    to.flex_basis_expression = from.flex_basis_expression;
  }
  else if (property == "justify-content") to.justify_content = from.justify_content;
  else if (property == "align-items") to.align_items = from.align_items;
  else if (property == "align-self") to.align_self = from.align_self;
  else if (property == "align-content") to.align_content = from.align_content;
  else if (property == "justify-items") to.justify_items = from.justify_items;
  else if (property == "justify-self") to.justify_self = from.justify_self;
  else if (property == "grid-template-columns") to.grid_columns = from.grid_columns;
  else if (property == "grid-template-rows") to.grid_rows = from.grid_rows;
  else if (property == "grid-template-areas") to.grid_template_areas = from.grid_template_areas;
  else if (property == "grid-auto-flow") to.grid_auto_flow = from.grid_auto_flow;
  else if (property == "grid-column") to.grid_column = from.grid_column;
  else if (property == "grid-row") to.grid_row = from.grid_row;
  else if (property == "grid-area") to.grid_area = from.grid_area;
  else if (property == "order") to.order = from.order;
  else if (property.rfind("margin-", 0) == 0) {
    static const char *side_names[] = {"top", "right", "bottom", "left"};
    for (int i=0;i<4;++i) if (property == std::string("margin-")+side_names[i]) to.margin[i]=from.margin[i];
    if (property == "margin-top") to.auto_top = from.auto_top;
    if (property == "margin-right") to.auto_right = from.auto_right;
    if (property == "margin-bottom") to.auto_bottom = from.auto_bottom;
    if (property == "margin-left") to.auto_left = from.auto_left;
  } else if (property.rfind("padding-", 0) == 0) {
    static const char *side_names[] = {"top", "right", "bottom", "left"};
    for (int i=0;i<4;++i) if (property == std::string("padding-")+side_names[i]) to.padding[i]=from.padding[i];
  } else if (property.rfind("border-", 0) == 0 && property.ends_with("-width")) {
    static const char *side_names[] = {"top", "right", "bottom", "left"};
    for (int i=0;i<4;++i) if (property == std::string("border-")+side_names[i]+"-width") to.border[i]=from.border[i];
  } else if (auto found = from.raw.find(property); found != from.raw.end()) to.raw[property] = found->second;
}
static bool media_matches(const std::string &media, double viewport_width,
                          double viewport_height, const MediaEnvironment &environment) {
  if (media.empty())
    return true;
  const std::string source = lower(trim(media));
  const size_t at = source.find("@media");
  std::string query = trim(at == std::string::npos ? source : source.substr(at + 6));
  auto number = [](std::string value, double &out) {
    value = trim(value);
    try {
      size_t used = 0;
      out = std::stod(value, &used);
      const std::string unit = trim(value.substr(used));
      return unit.empty() || unit == "px";
    } catch (...) {
      return false;
    }
  };
  auto ratio = [&](const std::string &value, double &out) {
    const size_t slash = value.find('/');
    if (slash == std::string::npos) return number(value, out) && out > 0;
    double numerator = 0, denominator = 0;
    if (!number(value.substr(0, slash), numerator) ||
        !number(value.substr(slash + 1), denominator) || denominator == 0)
      return false;
    out = numerator / denominator;
    return out > 0;
  };
  auto resolution = [&](const std::string &value, double &out) {
    try {
      size_t used = 0;
      const double amount = std::stod(value, &used);
      const std::string unit = trim(value.substr(used));
      if (amount <= 0) return false;
      if (unit == "dppx") out = amount;
      else if (unit == "dpi") out = amount / 96.0;
      else if (unit == "dpcm") out = amount * 2.54 / 96.0;
      else return false;
      return true;
    } catch (...) {
      return false;
    }
  };
  // CSS media query lists are ORed. For each query, all `and` conditions must
  // hold. This deliberately implements a bounded feature set, not full MQ4.
  size_t query_begin = 0;
  while (query_begin <= query.size()) {
    size_t query_end = query.find(',', query_begin);
    if (query_end == std::string::npos) query_end = query.size();
    std::string clause = trim(query.substr(query_begin, query_end - query_begin));
    bool matches = true, negate = false;
    if (clause.rfind("not ", 0) == 0) {
      negate = true;
      clause = trim(clause.substr(4));
    }
    if (clause.rfind("only ", 0) == 0) clause = trim(clause.substr(5));
    size_t condition_begin = 0;
    while (condition_begin < clause.size()) {
      size_t condition_end = condition_begin;
      int depth = 0;
      for (; condition_end < clause.size(); ++condition_end) {
        if (clause[condition_end] == '(') ++depth;
        else if (clause[condition_end] == ')') --depth;
        else if (depth == 0 && clause.compare(condition_end, 5, " and ") == 0) break;
      }
      std::string condition = trim(clause.substr(condition_begin, condition_end - condition_begin));
      if (!condition.empty() && condition.front() == '(' && condition.back() == ')')
        condition = trim(condition.substr(1, condition.size() - 2));
      if (condition == "screen" || condition == "all" || condition.empty()) {
        // These media types describe the engine's current presentation mode.
      } else if (condition == "print") {
        matches = false;
      } else {
        const size_t colon = condition.find(':');
        if (colon == std::string::npos) {
          matches = false;
        } else {
          const std::string feature = trim(condition.substr(0, colon));
          const std::string value = trim(condition.substr(colon + 1));
          double actual = 0, expected = 0;
          if (feature == "width" || feature == "min-width" || feature == "max-width" ||
              feature == "height" || feature == "min-height" || feature == "max-height") {
            const bool is_width = feature.find("width") != std::string::npos;
            actual = is_width ? viewport_width : viewport_height;
            const bool valid = number(value, expected);
            if (!valid) matches = false;
            else if (feature.rfind("min-", 0) == 0) matches = matches && actual >= expected;
            else if (feature.rfind("max-", 0) == 0) matches = matches && actual <= expected;
            else matches = matches && std::fabs(actual - expected) < 0.01;
          } else if (feature == "orientation") {
            matches = matches && (value == (viewport_width >= viewport_height ? "landscape" : "portrait"));
          } else if (feature == "aspect-ratio" || feature == "min-aspect-ratio" ||
                     feature == "max-aspect-ratio") {
            const double actual_ratio = viewport_height > 0 ? viewport_width / viewport_height : 0;
            const bool valid = ratio(value, expected);
            if (!valid || actual_ratio <= 0) matches = false;
            else if (feature.rfind("min-", 0) == 0) matches = matches && actual_ratio >= expected;
            else if (feature.rfind("max-", 0) == 0) matches = matches && actual_ratio <= expected;
            else matches = matches && std::fabs(actual_ratio - expected) < 0.01;
          } else if (feature == "resolution" || feature == "min-resolution" ||
                     feature == "max-resolution") {
            const bool valid = resolution(value, expected);
            actual = environment.device_pixel_ratio;
            if (!valid) matches = false;
            else if (feature.rfind("min-", 0) == 0) matches = matches && actual >= expected;
            else if (feature.rfind("max-", 0) == 0) matches = matches && actual <= expected;
            else matches = matches && std::fabs(actual - expected) < 0.001;
          } else if (feature == "prefers-reduced-motion") {
            matches = matches && value == environment.prefers_reduced_motion;
          } else if (feature == "prefers-color-scheme") {
            matches = matches && value == environment.prefers_color_scheme;
          } else if (feature == "prefers-contrast") {
            matches = matches && value == environment.prefers_contrast;
          } else if (feature == "forced-colors") {
            matches = matches && value == environment.forced_colors;
          } else {
            matches = false;
          }
        }
      }
      if (condition_end == clause.size()) break;
      condition_begin = condition_end + 5;
    }
    if (negate) matches = !matches;
    if (matches) return true;
    if (query_end == query.size()) break;
    query_begin = query_end + 1;
  }
  return false;
}
std::vector<Style> compute_styles(const Document &doc, const std::vector<Stylesheet> &sheets,
                                  double viewport, double viewport_height,
                                  const SelectorState *selector_state,
                                  const MediaEnvironment *media_environment) {
  if (viewport_height < 0) viewport_height = viewport;
  const MediaEnvironment default_media_environment;
  const MediaEnvironment &environment = media_environment ? *media_environment : default_media_environment;
  std::vector<Style> styles(doc.nodes.size());
  Stylesheet ua;
  int ua_order = -1000000;
  ua.parse(ua_css(), ua_order);
  auto declarations_for = [](const CssRule &rule) {
    if (!rule.declaration_order.empty()) return rule.declaration_order;
    std::vector<CssDeclaration> result;
    for (const auto &entry : rule.declarations)
      result.push_back({entry.first, entry.second, rule.important.count(entry.first) != 0});
    return result;
  };
  std::vector<int> stack{doc.root};
  double root_font = 16;
  while (!stack.empty()) {
    const int id = stack.back();
    stack.pop_back();
    const Node &node = doc.nodes[id];
    Style style;
    const Style *parent_style = node.parent >= 0 ? &styles[node.parent] : nullptr;
    if (parent_style) {
      style = *parent_style;
      // Preserve inherited text properties and custom properties; reset the
      // layout/paint properties that this engine treats as non-inherited.
      style.display = "block";
      style.background = Color{};
      style.background_image.clear();
      style.border_color = Color{70, 80, 95, 255, false};
      std::fill(std::begin(style.margin), std::end(style.margin), 0.0);
      std::fill(std::begin(style.padding), std::end(style.padding), 0.0);
      std::fill(std::begin(style.border), std::end(style.border), 0.0);
      style.auto_left = style.auto_right = style.auto_top = style.auto_bottom = false;
      style.width = style.height = -1;
      style.width_expression.clear();
      style.height_expression.clear();
      style.min_width_expression.clear();
      style.max_width_expression.clear();
      style.min_height_expression.clear();
      style.max_height_expression.clear();
      style.height_percent = -1;
      style.min_width = style.min_height = 0;
      style.max_width = style.max_height = -1;
      style.radius = 0;
      style.outline_width = 0;
      style.outline_color = Color{230, 237, 243, 255, false};
      style.outline_offset = 0;
      style.box_shadow.clear();
      style.filter.clear();
      style.background_layers.clear();
      style.underline = false;
      style.text_overflow = "clip";
      style.list_item = false;
      style.box_sizing = "content-box";
      style.position = "static";
      style.z_index = 0;
      style.z_index_auto = true;
      style.top = style.left = style.right = style.bottom = 0;
      style.has_top = style.has_left = style.has_right = style.has_bottom = false;
      style.overflow = "visible";
      style.scrollbar_gutter = "auto";
      style.overscroll_behavior = "auto";
      style.opacity = 1;
      style.flex_direction = "row";
      style.justify_content = "flex-start";
      style.align_items = "stretch";
      style.align_self = "auto";
      style.align_content = "stretch";
      style.justify_items = "stretch";
      style.justify_self = "auto";
      style.flex_wrap = "nowrap";
      style.flex_grow = 0;
      style.flex_shrink = 1;
      style.flex_basis = -1;
      style.flex_basis_expression.clear();
      style.order = 0;
      style.gap = 0;
      style.row_gap = -1;
      style.grid_columns.clear();
      style.grid_rows.clear();
      style.grid_template_areas.clear();
      style.grid_column.clear();
      style.grid_row.clear();
      style.grid_area.clear();
      style.grid_auto_flow = "row";
      style.grid_span = 1;
      style.transform.clear();
    }
    style.raw.clear();
    const double parent_width = viewport; // Containing-block percentages are finalized by layout.
    std::vector<const CssRule *> ua_matched, author_matched;
    for (const CssRule &rule : ua.rules)
      if (matches_selector(doc, id, rule.selector, selector_state)) ua_matched.push_back(&rule);
    for (const Stylesheet &sheet : sheets)
      for (const CssRule &rule : sheet.rules)
        if (media_matches(rule.media, viewport, viewport_height, environment) &&
            matches_selector(doc, id, rule.selector, selector_state)) author_matched.push_back(&rule);
    auto matched_specificity = [&](const CssRule *rule) {
      int best = 0;
      for (const std::string &group : split_css_top_level(rule->selector, ','))
        if (matches_selector(doc, id, group, selector_state)) best = std::max(best, specificity(group));
      return best;
    };
    auto sort_by_specificity = [&](std::vector<const CssRule *> &rules) {
      std::stable_sort(rules.begin(), rules.end(), [&](const CssRule *a, const CssRule *b) {
        return matched_specificity(a) < matched_specificity(b);
      });
    };
    sort_by_specificity(ua_matched);
    sort_by_specificity(author_matched);
    const std::vector<CssDeclaration> inline_declarations = node.type == Node::Type::Element
        ? parse_css_declarations(node.attribute("style")) : std::vector<CssDeclaration>{};

    // Custom properties are an author-origin cascade and inherit lazily.
    const auto inherited_custom = style.custom_properties;
    auto custom_pass = [&](bool important) {
      auto consume = [&](const CssDeclaration &declaration) {
        if (!starts(declaration.property, "--") || declaration.important != important) return;
        const std::string keyword = lower(trim(declaration.value));
        if (keyword == "initial") style.custom_properties.erase(declaration.property);
        else if (keyword == "inherit" || keyword == "unset" || keyword == "revert") {
          auto inherited = inherited_custom.find(declaration.property);
          if (inherited == inherited_custom.end()) style.custom_properties.erase(declaration.property);
          else style.custom_properties[declaration.property] = inherited->second;
        } else style.custom_properties[declaration.property] = declaration.value;
      };
      for (const CssRule *rule : author_matched)
        for (const CssDeclaration &declaration : declarations_for(*rule)) consume(declaration);
      for (const CssDeclaration &declaration : inline_declarations) consume(declaration);
    };
    custom_pass(false);
    custom_pass(true);

    using Properties = std::map<std::string, std::string>;
    auto collect = [&](const std::vector<const CssRule *> &rules,
                       const std::vector<CssDeclaration> *inline_style,
                       bool important) {
      Properties winners;
      auto consume = [&](const CssDeclaration &declaration) {
        if (starts(declaration.property, "--") || declaration.important != important) return;
        std::string resolved;
        std::set<std::string> stack_names;
        if (!substitute_css_vars(declaration.value, style.custom_properties, stack_names, resolved))
          return;
        for (auto &expanded : expand_css_declaration(declaration.property, resolved))
          winners[expanded.first] = std::move(expanded.second);
      };
      for (const CssRule *rule : rules)
        for (const CssDeclaration &declaration : declarations_for(*rule)) consume(declaration);
      if (inline_style)
        for (const CssDeclaration &declaration : *inline_style) consume(declaration);
      return winners;
    };
    const Properties ua_normal = collect(ua_matched, nullptr, false);
    const Properties ua_important = collect(ua_matched, nullptr, true);
    const Properties author_normal = collect(author_matched, &inline_declarations, false);
    const Properties author_important = collect(author_matched, &inline_declarations, true);
    std::set<std::string> properties;
    for (const auto *set : {&ua_normal, &ua_important, &author_normal, &author_important})
      for (const auto &entry : *set) properties.insert(entry.first);
    Properties computed;
    for (const std::string &property : properties) {
      const std::string *ua_value = nullptr;
      const std::string *author_value = nullptr;
      if (auto it = ua_normal.find(property); it != ua_normal.end()) ua_value = &it->second;
      if (auto it = ua_important.find(property); it != ua_important.end()) ua_value = &it->second;
      if (auto it = author_normal.find(property); it != author_normal.end()) author_value = &it->second;
      if (auto it = author_important.find(property); it != author_important.end()) author_value = &it->second;
      // Important UA declarations outrank author-important; otherwise author
      // origin outranks UA, and author-important outranks author-normal.
      const std::string *value = nullptr;
      if (ua_important.count(property)) value = &ua_important.at(property);
      else if (author_important.count(property)) value = &author_important.at(property);
      else if (author_normal.count(property)) value = &author_normal.at(property);
      else if (ua_normal.count(property)) value = &ua_normal.at(property);
      if (!value) continue;
      if (lower(trim(*value)) == "revert") {
        if (author_value && value == author_value && ua_value && lower(trim(*ua_value)) != "revert")
          value = ua_value;
        else value = nullptr;
      }
      if (!value) {
        auto initial = initial_css_value(property);
        if (initial) computed[property] = *initial;
        continue;
      }
      std::string keyword = lower(trim(*value));
      const bool inherited = inherited_css_property(property);
      if (keyword == "inherit" || (keyword == "unset" && inherited)) {
        if (parent_style) {
          if (!inherited) inherit_style_property(style, *parent_style, property);
          continue;
        }
        auto initial = initial_css_value(property);
        if (initial) computed[property] = *initial;
        continue;
      }
      if (keyword == "initial" || (keyword == "unset" && !inherited)) {
        auto initial = initial_css_value(property);
        if (initial) computed[property] = *initial;
        continue;
      }
      computed[property] = *value;
    }
    style.root_font_size = root_font;
    style.viewport_width = viewport;
    style.viewport_height = viewport_height;
    apply(style, computed, parent_width, root_font, viewport, viewport_height);
    if (id == doc.root) root_font = style.font_size;
    if (node.type == Node::Type::Text) style.display = "inline";
    if (node.name == "li") style.list_item = true;
    if (node.name == "a") style.underline = true;
    if (node.name == "input" || node.name == "button" || node.name == "select" ||
        node.name == "textarea") style.display = "inline-block";
    styles[id] = style;
    for (auto child = node.children.rbegin(); child != node.children.rend(); ++child)
      stack.push_back(*child);
  }
  return styles;
}
} // namespace pv
