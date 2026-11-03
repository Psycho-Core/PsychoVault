// PsychoVault original source. I read SVG myself and draw it with my own raster:
// shapes, a path parser and the transforms an icon or a logo needs. I do not
// support filters, masks, text or animation, and I say so in my status notes.
#include "Psycho_image.hpp"
#include "Psycho_raster.hpp"
#include <cmath>
#include <map>
#include <string>
#include <vector>

namespace pv {
namespace {
struct Transform {
  double a = 1, b = 0, c = 0, d = 1, e = 0, f = 0;
  Point apply(double x, double y) const {
    return {a * x + c * y + e, b * x + d * y + f};
  }
  Transform operator*(const Transform &other) const {
    // Mine is applied first, then other, which is how an SVG group chain reads.
    Transform out;
    out.a = a * other.a + b * other.c;
    out.b = a * other.b + b * other.d;
    out.c = c * other.a + d * other.c;
    out.d = c * other.b + d * other.d;
    out.e = e * other.a + f * other.c + other.e;
    out.f = e * other.b + f * other.d + other.f;
    return out;
  }
};

struct Node {
  std::string name;
  std::map<std::string, std::string> attributes;
  std::vector<Node> children;
};

std::string trim_copy(const std::string &text) {
  size_t begin = 0, end = text.size();
  while (begin < end && std::isspace(static_cast<unsigned char>(text[begin])))
    ++begin;
  while (end > begin && std::isspace(static_cast<unsigned char>(text[end - 1])))
    --end;
  return text.substr(begin, end - begin);
}

double number_of(const std::string &text, double fallback = 0) {
  try {
    size_t used = 0;
    double value = std::stod(text, &used);
    return used ? value : fallback;
  } catch (...) {
    return fallback;
  }
}

// A small XML reader, enough for the shape documents I support.
bool parse_xml(const std::string &source, Node &root) {
  std::vector<Node *> stack{&root};
  size_t at = 0;
  int depth = 0;
  while (at < source.size()) {
    auto open = source.find('<', at);
    if (open == std::string::npos)
      break;
    if (source.compare(open, 4, "<!--") == 0) {
      auto close = source.find("-->", open);
      if (close == std::string::npos)
        return false;
      at = close + 3;
      continue;
    }
    if (source.compare(open, 9, "<![CDATA[") == 0) {
      auto close = source.find("]]>", open);
      if (close == std::string::npos)
        return false;
      at = close + 3;
      continue;
    }
    if (source.compare(open, 2, "<?") == 0 || source.compare(open, 2, "<!") == 0) {
      auto close = source.find('>', open);
      if (close == std::string::npos)
        return false;
      at = close + 1;
      continue;
    }
    auto close = open + 1;
    bool quoted = false;
    char quote_char = 0;
    for (; close < source.size(); ++close) {
      char ch = source[close];
      if (quoted) {
        if (ch == quote_char)
          quoted = false;
      } else if (ch == '"' || ch == '\'') {
        quoted = true;
        quote_char = ch;
      } else if (ch == '>')
        break;
    }
    if (close >= source.size())
      return false;
    std::string tag = source.substr(open + 1, close - open - 1);
    at = close + 1;
    bool closing = !tag.empty() && tag[0] == '/';
    bool self_closing = !tag.empty() && tag.back() == '/';
    if (closing) {
      if (stack.size() > 1)
        stack.pop_back();
      continue;
    }
    if (self_closing)
      tag.pop_back();
    size_t name_end = 0;
    while (name_end < tag.size() && !std::isspace(static_cast<unsigned char>(tag[name_end])) &&
           tag[name_end] != '/')
      ++name_end;
    Node node;
    node.name = tag.substr(0, name_end);
    size_t cursor = name_end;
    while (cursor < tag.size()) {
      while (cursor < tag.size() && std::isspace(static_cast<unsigned char>(tag[cursor])))
        ++cursor;
      size_t key_start = cursor;
      while (cursor < tag.size() && tag[cursor] != '=' &&
             !std::isspace(static_cast<unsigned char>(tag[cursor])))
        ++cursor;
      std::string key = tag.substr(key_start, cursor - key_start);
      while (cursor < tag.size() && std::isspace(static_cast<unsigned char>(tag[cursor])))
        ++cursor;
      if (cursor < tag.size() && tag[cursor] == '=') {
        ++cursor;
        while (cursor < tag.size() && std::isspace(static_cast<unsigned char>(tag[cursor])))
          ++cursor;
        if (cursor < tag.size() && (tag[cursor] == '"' || tag[cursor] == '\'')) {
          char quote = tag[cursor++];
          size_t value_start = cursor;
          while (cursor < tag.size() && tag[cursor] != quote)
            ++cursor;
          if (!key.empty())
            node.attributes[key] = tag.substr(value_start, cursor - value_start);
          ++cursor;
        }
      } else if (key.empty())
        ++cursor;
    }
    if (++depth > 512)
      return false;
    stack.back()->children.push_back(std::move(node));
    if (!self_closing)
      stack.push_back(&stack.back()->children.back());
    else
      --depth;
  }
  return true;
}

uint32_t parse_color(const std::string &value, uint32_t fallback, bool *transparent) {
  std::string text = trim_copy(value);
  for (char &ch : text)
    ch = char(std::tolower(static_cast<unsigned char>(ch)));
  if (transparent)
    *transparent = text.empty() || text == "none";
  if (text.empty() || text == "none")
    return fallback;
  auto hex_value = [](char ch) -> int {
    if (ch >= '0' && ch <= '9')
      return ch - '0';
    if (ch >= 'a' && ch <= 'f')
      return ch - 'a' + 10;
    return 0;
  };
  if (text[0] == '#') {
    std::string digits = text.substr(1);
    if (digits.size() == 3)
      digits = {digits[0], digits[0], digits[1], digits[1], digits[2], digits[2]};
    if (digits.size() >= 6) {
      int r = hex_value(digits[0]) * 16 + hex_value(digits[1]);
      int g = hex_value(digits[2]) * 16 + hex_value(digits[3]);
      int b = hex_value(digits[4]) * 16 + hex_value(digits[5]);
      int a = 255;
      if (digits.size() >= 8)
        a = hex_value(digits[6]) * 16 + hex_value(digits[7]);
      return (uint32_t(a) << 24) | (uint32_t(r) << 16) | (uint32_t(g) << 8) | uint32_t(b);
    }
    return fallback;
  }
  if (text.rfind("rgb(", 0) == 0) {
    auto body = text.substr(4);
    std::vector<double> parts;
    std::string current;
    for (char ch : body) {
      if (ch == ',' || ch == ')' || ch == ' ') {
        if (!current.empty())
          parts.push_back(number_of(current));
        current.clear();
      } else if (std::isdigit(static_cast<unsigned char>(ch)) || ch == '.')
        current += ch;
    }
    if (!current.empty())
      parts.push_back(number_of(current));
    if (parts.size() >= 3)
      return 0xff000000u | (uint32_t(parts[0]) << 16) | (uint32_t(parts[1]) << 8) | uint32_t(parts[2]);
    return fallback;
  }
  static const std::map<std::string, uint32_t> names = {
      {"black", 0xff000000u}, {"white", 0xffffffffu}, {"red", 0xffff0000u},
      {"green", 0xff008000u}, {"blue", 0xff0000ffu}, {"gray", 0xff808080u},
      {"grey", 0xff808080u}, {"silver", 0xffc0c0c0u}, {"navy", 0xff000080u},
      {"orange", 0xffffa500u}, {"yellow", 0xffffff00u}, {"purple", 0xff800080u}};
  auto found = names.find(text);
  return found == names.end() ? fallback : found->second;
}

std::vector<double> numbers_in(const std::string &text) {
  std::vector<double> out;
  const char *p = text.c_str();
  while (*p) {
    if (std::isdigit(static_cast<unsigned char>(*p)) || *p == '-' || *p == '+' || *p == '.') {
      char *end = nullptr;
      double value = std::strtod(p, &end);
      if (end == p)
        ++p;
      else {
        out.push_back(value);
        p = end;
      }
    } else
      ++p;
  }
  return out;
}

Transform parse_transform(const std::string &text) {
  Transform result;
  size_t at = 0;
  while (at < text.size()) {
    auto open = text.find('(', at);
    if (open == std::string::npos)
      break;
    auto close = text.find(')', open);
    if (close == std::string::npos)
      break;
    std::string name = trim_copy(text.substr(at, open - at));
    auto args = numbers_in(text.substr(open + 1, close - open - 1));
    Transform step;
    if (name == "translate" && !args.empty()) {
      step.e = args[0];
      step.f = args.size() > 1 ? args[1] : 0;
    } else if (name == "scale" && !args.empty()) {
      step.a = args[0];
      step.d = args.size() > 1 ? args[1] : args[0];
    } else if (name == "rotate" && !args.empty()) {
      double angle = args[0] * 3.14159265358979323846 / 180.0;
      double cx = args.size() > 2 ? args[1] : 0, cy = args.size() > 2 ? args[2] : 0;
      Transform to_origin, rotation, back;
      to_origin.e = -cx;
      to_origin.f = -cy;
      rotation.a = std::cos(angle);
      rotation.b = std::sin(angle);
      rotation.c = -std::sin(angle);
      rotation.d = std::cos(angle);
      back.e = cx;
      back.f = cy;
      step = to_origin * rotation * back;
    } else if (name == "matrix" && args.size() >= 6) {
      step.a = args[0];
      step.b = args[1];
      step.c = args[2];
      step.d = args[3];
      step.e = args[4];
      step.f = args[5];
    }
    result = step * result;
    at = close + 1;
  }
  return result;
}

// I flatten arcs into short segments, which is enough for icons.
void add_arc(std::vector<Point> &points, Point from, double rx, double ry, double rotation,
             bool large, bool sweep, Point to) {
  rx = std::fabs(rx);
  ry = std::fabs(ry);
  if (rx < 0.0001 || ry < 0.0001) {
    points.push_back(to);
    return;
  }
  double angle = rotation * 3.14159265358979323846 / 180.0;
  double cos_a = std::cos(angle), sin_a = std::sin(angle);
  double dx = (from.x - to.x) / 2, dy = (from.y - to.y) / 2;
  double x1 = cos_a * dx + sin_a * dy, y1 = -sin_a * dx + cos_a * dy;
  double lambda = (x1 * x1) / (rx * rx) + (y1 * y1) / (ry * ry);
  if (lambda > 1) {
    rx *= std::sqrt(lambda);
    ry *= std::sqrt(lambda);
  }
  double sign = large != sweep ? 1.0 : -1.0;
  double numerator = rx * rx * ry * ry - rx * rx * y1 * y1 - ry * ry * x1 * x1;
  double denominator = rx * rx * y1 * y1 + ry * ry * x1 * x1;
  double factor = denominator > 0 ? std::sqrt(std::max(0.0, numerator / denominator)) : 0;
  double cxp = sign * factor * rx * y1 / ry;
  double cyp = sign * factor * -ry * x1 / rx;
  double cx = cos_a * cxp - sin_a * cyp + (from.x + to.x) / 2;
  double cy = sin_a * cxp + cos_a * cyp + (from.y + to.y) / 2;
  auto angle_of = [](double ux, double uy, double vx, double vy) {
    double dot = ux * vx + uy * vy;
    double length = std::sqrt((ux * ux + uy * uy) * (vx * vx + vy * vy));
    double value = length > 0 ? std::acos(std::clamp(dot / length, -1.0, 1.0)) : 0;
    return (ux * vy - uy * vx) < 0 ? -value : value;
  };
  double start = angle_of(1, 0, (x1 - cxp) / rx, (y1 - cyp) / ry);
  double sweep_angle = angle_of((x1 - cxp) / rx, (y1 - cyp) / ry, (-x1 - cxp) / rx, (-y1 - cyp) / ry);
  if (!sweep && sweep_angle > 0)
    sweep_angle -= 2 * 3.14159265358979323846;
  else if (sweep && sweep_angle < 0)
    sweep_angle += 2 * 3.14159265358979323846;
  int steps = std::clamp(int(std::fabs(sweep_angle) * 8) + 2, 2, 128);
  for (int i = 1; i <= steps; ++i) {
    double t = start + sweep_angle * double(i) / steps;
    double px = rx * std::cos(t), py = ry * std::sin(t);
    points.push_back({cos_a * px - sin_a * py + cx, sin_a * px + cos_a * py + cy});
  }
}

struct Style {
  uint32_t fill = 0xff000000u, stroke = 0;
  double stroke_width = 1;
  bool has_fill = true, has_stroke = false;
};

struct Painter {
  Canvas &canvas;
  Transform transform;
  Style style;
  void shape(const std::vector<Point> &points, bool closed) {
    if (points.size() < 2)
      return;
    std::vector<Point> mapped;
    mapped.reserve(points.size());
    for (const auto &p : points)
      mapped.push_back(transform.apply(p.x, p.y));
    if (closed && style.has_fill && (style.fill >> 24))
      canvas.polygon(mapped, style.fill);
    if (style.has_stroke && (style.stroke >> 24))
      canvas.polyline(mapped, style.stroke, std::max(1.0, style.stroke_width), closed);
  }
};

void draw_node(const Node &node, Painter painter, int depth) {
  if (depth > 32)
    return;
  if (node.name == "defs" || node.name == "title" || node.name == "desc" ||
      node.name == "metadata" || node.name == "style")
    return;
  Style style = painter.style;
  auto attribute = [&](const char *key) -> std::string {
    auto it = node.attributes.find(key);
    return it == node.attributes.end() ? std::string() : it->second;
  };
  bool transparent = false;
  if (!attribute("fill").empty()) {
    style.fill = parse_color(attribute("fill"), style.fill, &transparent);
    style.has_fill = !transparent;
  }
  if (!attribute("stroke").empty()) {
    style.stroke = parse_color(attribute("stroke"), style.stroke, &transparent);
    style.has_stroke = !transparent && (style.stroke >> 24);
  }
  if (!attribute("stroke-width").empty())
    style.stroke_width = std::max(0.0, number_of(attribute("stroke-width"), 1));
  if (auto opacity = attribute("opacity"); !opacity.empty()) {
    double alpha = std::clamp(number_of(opacity, 1.0), 0.0, 1.0);
    auto scale = [&](uint32_t color) {
      uint32_t a = uint32_t(double(color >> 24) * alpha);
      return (a << 24) | (color & 0x00ffffffu);
    };
    style.fill = scale(style.fill);
    style.stroke = scale(style.stroke);
  }
  Painter child = painter;
  child.style = style;
  if (auto transform = attribute("transform"); !transform.empty())
    child.transform = parse_transform(transform) * painter.transform;

  auto coordinate = [&](const char *key, double fallback) {
    auto text = attribute(key);
    return text.empty() ? fallback : number_of(text, fallback);
  };
  if (node.name == "rect") {
    double x = coordinate("x", 0), y = coordinate("y", 0);
    double w = coordinate("width", 0), h = coordinate("height", 0);
    if (w > 0 && h > 0)
      child.shape({{x, y}, {x + w, y}, {x + w, y + h}, {x, y + h}}, true);
  } else if (node.name == "circle" || node.name == "ellipse") {
    double cx = coordinate("cx", 0), cy = coordinate("cy", 0);
    double rx = node.name == "circle" ? coordinate("r", 0) : coordinate("rx", 0);
    double ry = node.name == "circle" ? rx : coordinate("ry", 0);
    if (rx > 0 && ry > 0) {
      std::vector<Point> points;
      int steps = std::clamp(int(std::max(rx, ry) * 3) + 16, 16, 180);
      for (int i = 0; i < steps; ++i) {
        double t = 2 * 3.14159265358979323846 * i / steps;
        points.push_back({cx + rx * std::cos(t), cy + ry * std::sin(t)});
      }
      child.shape(points, true);
    }
  } else if (node.name == "line") {
    child.shape({{coordinate("x1", 0), coordinate("y1", 0)}, {coordinate("x2", 0), coordinate("y2", 0)}},
                false);
  } else if (node.name == "polyline" || node.name == "polygon") {
    auto values = numbers_in(attribute("points"));
    std::vector<Point> points;
    for (size_t i = 0; i + 1 < values.size(); i += 2)
      points.push_back({values[i], values[i + 1]});
    child.shape(points, node.name == "polygon");
  } else if (node.name == "path") {
    // The path grammar I support: M L H V C S Q T A Z, upper and lower case.
    const std::string data = attribute("d");
    std::vector<std::vector<Point>> subpaths;
    std::vector<Point> current;
    Point cursor, start;
    bool closed = false, has_cursor = false;
    char previous_command = 0;
    Point last_control;
    size_t at = 0;
    auto next_number = [&](double &out) {
      while (at < data.size() && (std::isspace(static_cast<unsigned char>(data[at])) || data[at] == ','))
        ++at;
      if (at >= data.size())
        return false;
      char *end = nullptr;
      double value = std::strtod(data.c_str() + at, &end);
      if (end == data.c_str() + at)
        return false;
      at = size_t(end - data.c_str());
      out = value;
      return true;
    };
    auto flush_subpath = [&]() {
      if (current.size() > 1)
        subpaths.push_back(current);
      current.clear();
    };
    while (at < data.size()) {
      while (at < data.size() && (std::isspace(static_cast<unsigned char>(data[at])) || data[at] == ','))
        ++at;
      if (at >= data.size())
        break;
      char command = data[at];
      if (!((command >= 'A' && command <= 'Z') || (command >= 'a' && command <= 'z')))
        break;
      bool relative = command >= 'a' && command <= 'z';
      char upper = char(std::toupper(static_cast<unsigned char>(command)));
      ++at;
      auto point = [&](double x, double y) {
        return relative ? Point{cursor.x + x, cursor.y + y} : Point{x, y};
      };
      if (upper == 'Z') {
        if (!current.empty()) {
          current.push_back(start);
          flush_subpath();
        }
        cursor = start;
        closed = true;
        previous_command = upper;
        continue;
      }
      // Every command takes at least one set of arguments and repeats if the
      // numbers keep coming, which is how an SVG path compresses runs of lines.
      bool first = true;
      while (true) {
        double a = 0, b = 0, c = 0, d = 0, e = 0, f = 0, g = 0;
        if (upper == 'M' || upper == 'L' || upper == 'T') {
          if (!next_number(a) || !next_number(b))
            break;
        } else if (upper == 'H' || upper == 'V') {
          if (!next_number(a))
            break;
        } else if (upper == 'C') {
          if (!next_number(a) || !next_number(b) || !next_number(c) || !next_number(d) ||
              !next_number(e) || !next_number(f))
            break;
        } else if (upper == 'S' || upper == 'Q') {
          if (!next_number(a) || !next_number(b) || !next_number(c) || !next_number(d))
            break;
        } else if (upper == 'A') {
          if (!next_number(a) || !next_number(b) || !next_number(c) || !next_number(d) ||
              !next_number(e) || !next_number(f) || !next_number(g))
            break;
        } else {
          break;
        }
        if (first && upper == 'M') {
          flush_subpath();
          cursor = point(a, b);
          start = cursor;
          has_cursor = true;
          current.push_back(cursor);
          first = false;
          // A following number pair is an implicit line to.
          double nx = 0, ny = 0;
          size_t save = at;
          if (next_number(nx) && next_number(ny)) {
            cursor = point(nx, ny);
            current.push_back(cursor);
            continue;
          }
          at = save;
          break;
        }
        first = false;
        if (upper == 'L' || upper == 'T') {
          Point target = point(a, b);
          if (upper == 'T' && (previous_command == 'Q' || previous_command == 'T')) {
            Point control{2 * cursor.x - last_control.x, 2 * cursor.y - last_control.y};
            for (int step = 1; step <= 8; ++step) {
              double t = double(step) / 8;
              double mt = 1 - t;
              Point p{mt * mt * cursor.x + 2 * mt * t * control.x + t * t * target.x,
                      mt * mt * cursor.y + 2 * mt * t * control.y + t * t * target.y};
              current.push_back(p);
            }
            last_control = control;
          } else {
            current.push_back(target);
          }
          cursor = target;
        } else if (upper == 'H') {
          cursor = {relative ? cursor.x + a : a, cursor.y};
          current.push_back(cursor);
        } else if (upper == 'V') {
          cursor = {cursor.x, relative ? cursor.y + a : a};
          current.push_back(cursor);
        } else if (upper == 'C') {
          Point c1 = point(a, b), c2 = point(c, d), target = point(e, f);
          for (int step = 1; step <= 12; ++step) {
            double t = double(step) / 12, mt = 1 - t;
            Point p{mt * mt * mt * cursor.x + 3 * mt * mt * t * c1.x + 3 * mt * t * t * c2.x +
                        t * t * t * target.x,
                    mt * mt * mt * cursor.y + 3 * mt * mt * t * c1.y + 3 * mt * t * t * c2.y +
                        t * t * t * target.y};
            current.push_back(p);
          }
          last_control = c2;
          cursor = target;
        } else if (upper == 'S') {
          Point c1 = (previous_command == 'C' || previous_command == 'S')
                         ? Point{2 * cursor.x - last_control.x, 2 * cursor.y - last_control.y}
                         : cursor;
          Point c2 = point(a, b), target = point(c, d);
          for (int step = 1; step <= 12; ++step) {
            double t = double(step) / 12, mt = 1 - t;
            Point p{mt * mt * mt * cursor.x + 3 * mt * mt * t * c1.x + 3 * mt * t * t * c2.x +
                        t * t * t * target.x,
                    mt * mt * mt * cursor.y + 3 * mt * mt * t * c1.y + 3 * mt * t * t * c2.y +
                        t * t * t * target.y};
            current.push_back(p);
          }
          last_control = c2;
          cursor = target;
        } else if (upper == 'Q') {
          Point control = point(a, b), target = point(c, d);
          for (int step = 1; step <= 10; ++step) {
            double t = double(step) / 10, mt = 1 - t;
            Point p{mt * mt * cursor.x + 2 * mt * t * control.x + t * t * target.x,
                    mt * mt * cursor.y + 2 * mt * t * control.y + t * t * target.y};
            current.push_back(p);
          }
          last_control = control;
          cursor = target;
        } else if (upper == 'A') {
          Point target = point(e, f);
          add_arc(current, cursor, a, b, c, d != 0, e != 0, target);
          cursor = target;
        }
        previous_command = upper;
        if (upper == 'Z')
          break;
      }
      (void)has_cursor;
      (void)closed;
    }
    flush_subpath();
    for (const auto &subpath : subpaths)
      child.shape(subpath, true);
  }
  for (const auto &grandchild : node.children)
    draw_node(grandchild, child, depth + 1);
}

const Node *find_svg(const Node &node, int depth) {
  if (depth > 32)
    return nullptr;
  if (node.name == "svg")
    return &node;
  for (const auto &child : node.children)
    if (const Node *found = find_svg(child, depth + 1))
      return found;
  return nullptr;
}
} // namespace

// I render into the size the page asked for and scale the viewBox to fit.
Image render_svg(const std::string &source, int width, int height) {
  Node root;
  if (!parse_xml(source, root))
    throw std::runtime_error("SVG: the document is not well formed");
  const Node *svg = find_svg(root, 0);
  if (!svg)
    throw std::runtime_error("SVG: I found no svg element");
  auto dimension = [&](const char *key) -> double {
    auto it = svg->attributes.find(key);
    if (it == svg->attributes.end())
      return 0;
    return number_of(it->second, 0);
  };
  double view_x = 0, view_y = 0, view_w = dimension("width"), view_h = dimension("height");
  if (auto box = svg->attributes.find("viewBox"); box != svg->attributes.end()) {
    auto values = numbers_in(box->second);
    if (values.size() >= 4) {
      view_x = values[0];
      view_y = values[1];
      view_w = values[2];
      view_h = values[3];
    }
  }
  if (view_w <= 0 || view_h <= 0)
    throw std::runtime_error("SVG: the document has no size");
  if (width <= 0)
    width = int(std::lround(view_w));
  if (height <= 0)
    height = int(std::lround(view_h));
  width = std::clamp(width, 1, 4096);
  height = std::clamp(height, 1, 4096);
  Image image;
  image.width = width;
  image.height = height;
  image.pixels.assign(size_t(width) * height * 4, 0);
  Canvas canvas(width, height, 0x00000000);
  Painter painter{canvas, {}, {}};
  painter.transform.a = double(width) / view_w;
  painter.transform.d = double(height) / view_h;
  painter.transform.e = -view_x * painter.transform.a;
  painter.transform.f = -view_y * painter.transform.d;
  draw_node(*svg, painter, 0);
  image.pixels = canvas.pixels;
  return image;
}
} // namespace pv
