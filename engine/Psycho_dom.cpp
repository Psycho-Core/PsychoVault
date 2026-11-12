#include "Psycho_dom.hpp"
namespace pv {
bool Node::get_attribute(std::string_view key, std::string &value) const {
  for (auto &a : attributes)
    if (a.first == key) {
      value = a.second;
      return true;
    }
  return false;
}
std::string Node::attribute(std::string_view key, std::string fallback) const {
  std::string v;
  return get_attribute(key, v) ? v : std::move(fallback);
}
void Node::set_attribute(std::string key, std::string value) {
  key = lower(key);
  if (key.size() > 256 || value.size() > 65536)
    return;
  for (auto &a : attributes)
    if (a.first == key) {
      a.second = std::move(value);
      return;
    }
  if (attributes.size() < 256)
    attributes.emplace_back(std::move(key), std::move(value));
}
bool Node::has_class(std::string_view name) const {
  for (auto &c : split(attribute("class"), ' '))
    if (c == name)
      return true;
  return false;
}
bool is_void(std::string_view t) {
  static const std::set<std::string> tags = {"area",  "base",   "br",    "col",  "embed",
                                             "hr",    "img",    "input", "link", "meta",
                                             "param", "source", "track", "wbr"};
  return tags.count(std::string(t));
}
Document::Document() {
  nodes.reserve(1024);
  root = create(Node::Type::Document, "#document");
}
int Document::create(Node::Type type, std::string name, std::string text) {
  if (nodes.size() >= size_t(kMaxDomNodes)) {
    if (diagnostics.size() < 100)
      diagnostics.push_back({"html", "DOM node limit reached"});
    return -1;
  }
  Node n;
  n.type = type;
  n.id = int(nodes.size());
  n.name = lower(name);
  n.text = std::move(text);
  nodes.push_back(std::move(n));
  return nodes.back().id;
}
void Document::append(int parent, int child) {
  if (parent < 0 || child < 0 || parent >= int(nodes.size()) || child >= int(nodes.size()) ||
      parent == child)
    return;
  for (int p = parent; p >= 0; p = nodes[p].parent)
    if (p == child)
      return;
  int depth = 0;
  for (int p = parent; p >= 0; p = nodes[p].parent)
    if (++depth > kMaxDomDepth) {
      if (diagnostics.size() < 100)
        diagnostics.push_back({"html", "DOM depth limit reached"});
      return;
    }
  detach(child);
  nodes[child].parent = parent;
  nodes[parent].children.push_back(child);
}
void Document::detach(int child) {
  if (child < 0 || child >= int(nodes.size()))
    return;
  int p = nodes[child].parent;
  if (p >= 0) {
    auto &c = nodes[p].children;
    c.erase(std::remove(c.begin(), c.end(), child), c.end());
  }
  nodes[child].parent = -1;
}
void Document::remove(int child) {
  detach(child);
}
std::string Document::text_content(int id) const {
  if (id < 0 || id >= int(nodes.size()))
    return {};
  std::string out;
  std::vector<int> stack{id};
  while (!stack.empty()) {
    int n = stack.back();
    stack.pop_back();
    const auto &node = nodes[n];
    if (node.type == Node::Type::Text)
      out += node.text;
    for (auto it = node.children.rbegin(); it != node.children.rend(); ++it)
      stack.push_back(*it);
    if (out.size() > kMaxDocument)
      throw std::runtime_error("text size limit");
  }
  return out;
}
void Document::set_text(int id, const std::string &text) {
  if (id < 0 || id >= int(nodes.size()) || text.size() > kMaxDocument)
    return;
  auto children = nodes[id].children;
  for (int c : children)
    remove(c);
  int t = create(Node::Type::Text, "#text", text);
  append(id, t);
}
int Document::element(std::string_view name, int parent) const {
  if (parent == -1)
    parent = root;
  if (parent < 0)
    return -1;
  for (int c : nodes[parent].children)
    if (nodes[c].name == name)
      return c;
  return -1;
}
int Document::by_id(std::string_view id) const {
  for (auto &n : nodes)
    if (n.type == Node::Type::Element && n.attribute("id") == id)
      return n.id;
  return -1;
}
std::vector<int> Document::query(std::string_view selector, int scope) const {
  std::vector<int> out;
  for (auto &n : nodes) {
    if (n.type != Node::Type::Element || (scope >= 0 && n.id == scope))
      continue;
    bool descendant = scope < 0;
    for (int p = n.parent; scope >= 0 && p >= 0; p = nodes[p].parent)
      if (p == scope) {
        descendant = true;
        break;
      }
    if (descendant && matches_selector(*this, n.id, selector))
      out.push_back(n.id);
  }
  return out;
}
int Document::query_one(std::string_view selector, int scope) const {
  auto r = query(selector, scope);
  return r.empty() ? -1 : r.front();
}
std::string Document::title() const {
  int h = head >= 0 ? head : element("head");
  int t = element("title", h);
  return t >= 0 ? trim(text_content(t)) : std::string();
}

struct HtmlParser {
  Document &doc;
  std::string_view s;
  size_t p = 0;
  std::vector<int> stack;
  int context;
  HtmlParser(Document &d, std::string_view html, int c) : doc(d), s(html), context(c) {
    if (c < 0)
      stack.push_back(d.root);
    else
      stack.push_back(c);
  }
  int top() const {
    return stack.back();
  }
  bool raw_mode() const {
    auto &n = doc.nodes[top()];
    return n.name == "script" || n.name == "style" || n.name == "textarea" || n.name == "title" ||
           n.name == "xmp" || n.name == "noembed" || n.name == "noframes";
  }
  void text(std::string value) {
    if (value.empty())
      return;
    if (top() == doc.root && context < 0) {
      if (trim(value).empty())
        return;
      ensure_body();
    }
    int n = doc.create(Node::Type::Text, "#text", std::move(value));
    doc.append(top(), n);
  }
  int ensure_html() {
    int h = doc.element("html");
    if (h < 0) {
      h = doc.create(Node::Type::Element, "html");
      doc.append(doc.root, h);
    }
    return h;
  }
  int ensure_head() {
    if (doc.head < 0) {
      doc.head = doc.create(Node::Type::Element, "head");
      doc.append(ensure_html(), doc.head);
    }
    return doc.head;
  }
  int ensure_body() {
    if (doc.body < 0) {
      ensure_head();
      doc.body = doc.create(Node::Type::Element, "body");
      doc.append(ensure_html(), doc.body);
    }
    return doc.body;
  }
  void open(std::string tag, std::vector<std::pair<std::string, std::string>> attrs,
            bool self_close) {
    if (context < 0) {
      if (tag == "html") {
        if (doc.element("html") < 0) {
          int n = doc.create(Node::Type::Element, tag);
          doc.append(doc.root, n);
          stack.push_back(n);
        } else
          stack.push_back(doc.element("html"));
        return;
      }
      if (tag == "head") {
        int h = ensure_head();
        while (stack.size() > 1)
          stack.pop_back();
        stack.push_back(h);
        return;
      }
      if (tag == "body") {
        int b = ensure_body();
        stack.resize(1);
        stack.push_back(doc.element("html"));
        stack.push_back(b);
        return;
      }
      static const std::set<std::string> head_tags = {"base",  "link",   "meta",    "title",
                                                      "style", "script", "noscript"};
      bool in_head = std::find(stack.begin(), stack.end(), doc.head) != stack.end();
      if (head_tags.count(tag) && (in_head || doc.body < 0)) {
        ensure_head();
        if (!in_head) {
          stack.resize(1);
          stack.push_back(doc.element("html"));
          stack.push_back(doc.head);
        }
      } else {
        ensure_body();
        if (std::find(stack.begin(), stack.end(), doc.body) == stack.end()) {
          stack.resize(1);
          if (doc.element("html") >= 0)
            stack.push_back(doc.element("html"));
          stack.push_back(doc.body);
        }
      }
      if (tag == "p")
        close_named("p", {"address", "article", "aside", "blockquote", "div", "dl", "fieldset",
                          "footer",  "form",    "h1",    "h2",         "h3",  "h4", "h5",
                          "h6",      "header",  "hr",    "main",       "nav", "ol", "p",
                          "pre",     "section", "table", "ul"});
      if (tag == "li")
        close_named("li", {"li"});
      if ((tag == "dt" || tag == "dd"))
        close_named(tag, {"dt", "dd"});
      if (tag.size() == 2 && tag[0] == 'h' && tag[1] >= '1' && tag[1] <= '6')
        close_named(tag, {"h1", "h2", "h3", "h4", "h5", "h6"});
    }
    int n = doc.create(Node::Type::Element, tag);
    if (n < 0)
      return;
    for (auto &a : attrs)
      doc.nodes[n].set_attribute(a.first, a.second);
    doc.append(top(), n);
    if (!is_void(tag) && !self_close)
      stack.push_back(n);
  }
  void close_named(const std::string &, const std::set<std::string> &tags) {
    for (size_t i = stack.size(); i-- > 1;)
      if (tags.count(doc.nodes[stack[i]].name)) {
        stack.resize(i);
        return;
      }
  }
  void close(const std::string &tag) {
    if (tag == "p" && context < 0) {
      bool found = false;
      for (size_t i = stack.size(); i-- > 1;)
        if (doc.nodes[stack[i]].name == "p")
          found = true;
      if (!found)
        open("p", {}, false);
    }
    for (size_t i = stack.size(); i-- > 1;)
      if (doc.nodes[stack[i]].name == tag) {
        stack.resize(i);
        return;
      }
  }
  std::string name() {
    size_t a = p;
    while (p < s.size() && !std::isspace(static_cast<unsigned char>(s[p])) && s[p] != '>' &&
           s[p] != '/')
      ++p;
    return lower(s.substr(a, p - a));
  }
  void attributes(std::vector<std::pair<std::string, std::string>> &attrs) {
    while (p < s.size()) {
      while (p < s.size() && (std::isspace(static_cast<unsigned char>(s[p])) || s[p] == '/'))
        ++p;
      if (p >= s.size() || s[p] == '>')
        break;
      size_t a = p;
      while (p < s.size() && !std::isspace(static_cast<unsigned char>(s[p])) && s[p] != '=' &&
             s[p] != '>' && s[p] != '/')
        ++p;
      std::string key = lower(s.substr(a, p - a));
      if (key.empty()) {
        ++p;
        continue;
      }
      while (p < s.size() && std::isspace(static_cast<unsigned char>(s[p])))
        ++p;
      std::string value;
      if (p < s.size() && s[p] == '=') {
        ++p;
        while (p < s.size() && std::isspace(static_cast<unsigned char>(s[p])))
          ++p;
        if (p < s.size() && (s[p] == '"' || s[p] == '\'')) {
          char q = s[p++];
          a = p;
          while (p < s.size() && s[p] != q)
            ++p;
          value = std::string(s.substr(a, p - a));
          if (p < s.size())
            ++p;
        } else {
          a = p;
          while (p < s.size() && !std::isspace(static_cast<unsigned char>(s[p])) && s[p] != '>')
            ++p;
          value = std::string(s.substr(a, p - a));
        }
      }
      if (attrs.size() < 256 && key.size() <= 256)
        attrs.emplace_back(std::move(key), entity_decode(value));
    }
  }
  void run() {
    while (p < s.size()) {
      if (raw_mode()) {
        std::string tag = doc.nodes[top()].name;
        std::string close = "</" + tag;
        size_t end = p;
        while (end < s.size()) {
          if (s[end] == '<' && lower(s.substr(end, close.size())) == close &&
              (end + close.size() == s.size() ||
               std::isspace(static_cast<unsigned char>(s[end + close.size()])) ||
               s[end + close.size()] == '>'))
            break;
          ++end;
        }
        auto raw = std::string(s.substr(p, end - p));
        if (tag != "script" && tag != "style")
          raw = entity_decode(raw);
        text(std::move(raw));
        p = end;
        if (p >= s.size())
          break;
      }
      if (s[p] != '<') {
        size_t a = p;
        while (p < s.size() && s[p] != '<')
          ++p;
        text(entity_decode(s.substr(a, p - a)));
        continue;
      }
      if (s.substr(p, 4) == "<!--") {
        p += 4;
        size_t e = s.find("-->", p);
        p = e == std::string_view::npos ? s.size() : e + 3;
        continue;
      }
      if (s.substr(p, 2) == "<!") {
        size_t e = s.find('>', p);
        p = e == std::string_view::npos ? s.size() : e + 1;
        continue;
      }
      if (s.substr(p, 2) == "<?") {
        size_t e = s.find('>', p);
        p = e == std::string_view::npos ? s.size() : e + 1;
        continue;
      }
      if (s.substr(p, 2) == "</") {
        p += 2;
        auto tag = name();
        size_t e = s.find('>', p);
        p = e == std::string_view::npos ? s.size() : e + 1;
        close(tag);
        continue;
      }
      ++p;
      auto tag = name();
      if (tag.empty() || !std::isalpha(static_cast<unsigned char>(tag[0]))) {
        text("<");
        continue;
      }
      std::vector<std::pair<std::string, std::string>> attrs;
      attributes(attrs);
      bool self = p > 0 && s[p - 1] == '/';
      while (p < s.size() && s[p] != '>')
        ++p;
      if (p < s.size())
        ++p;
      if (tag == "meta")
        for (auto &a : attrs)
          if (a.first == "charset")
            doc.charset = lower(a.second);
      open(tag, attrs, self);
    }
  }
};
void Document::parse(std::string_view html) {
  if (html.size() > kMaxDocument)
    throw std::runtime_error("HTML size limit");
  nodes.clear();
  diagnostics.clear();
  root = head = body = -1;
  root = create(Node::Type::Document, "#document");
  HtmlParser(*this, html, -1).run();
  int html_node = element("html");
  if (html_node < 0) {
    html_node = create(Node::Type::Element, "html");
    append(root, html_node);
  }
  int h = element("head", html_node);
  if (h < 0) {
    h = create(Node::Type::Element, "head");
    nodes[html_node].children.insert(nodes[html_node].children.begin(), h);
    nodes[h].parent = html_node;
  }
  head = h;
  int b = element("body", html_node);
  if (b < 0) {
    b = create(Node::Type::Element, "body");
    append(html_node, b);
  }
  body = b;
}
void Document::parse_fragment(int parent, std::string_view html) {
  if (parent < 0 || parent >= int(nodes.size()))
    return;
  HtmlParser(*this, html, parent).run();
}

struct SimpleSelector {
  std::string tag, id;
  std::vector<std::string> classes;
  std::vector<std::array<std::string, 3>> attrs;
  std::vector<std::string> pseudos;
  char relation = ' ';
};
static std::vector<std::string> selector_groups(std::string_view text) {
  std::vector<std::string> groups;
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
    else if (c == ',' && parentheses == 0 && brackets == 0) {
      groups.emplace_back(trim(text.substr(start, i - start)));
      start = i + 1;
    }
  }
  groups.emplace_back(trim(text.substr(start)));
  return groups;
}
static std::vector<std::vector<SimpleSelector>> parse_selector(std::string_view selector) {
  std::vector<std::vector<SimpleSelector>> groups;
  for (const std::string &group : selector_groups(selector)) {
    const std::string s = trim(group);
    if (s.empty() || s.size() > 4096) return {};
    std::vector<SimpleSelector> chain;
    size_t p = 0;
    while (p < s.size()) {
      bool had_space = false;
      while (p < s.size() && std::isspace(static_cast<unsigned char>(s[p]))) {
        ++p;
        had_space = true;
      }
      if (p >= s.size()) break;
      char relation = chain.empty() ? ' ' : (had_space ? ' ' : 0);
      if (s[p] == '>' || s[p] == '+' || s[p] == '~') relation = s[p++];
      if (relation == 0) return {};
      while (p < s.size() && std::isspace(static_cast<unsigned char>(s[p]))) ++p;
      SimpleSelector part;
      part.relation = relation;
      if (p < s.size() && (std::isalpha(static_cast<unsigned char>(s[p])) || s[p] == '*')) {
        const size_t begin = p++;
        if (s[begin] != '*')
          while (p < s.size() && (std::isalnum(static_cast<unsigned char>(s[p])) ||
                                  s[p] == '-' || s[p] == '_')) ++p;
        part.tag = lower(std::string_view(s).substr(begin, p - begin));
      }
      while (p < s.size() && !std::isspace(static_cast<unsigned char>(s[p])) &&
             s[p] != '>' && s[p] != '+' && s[p] != '~') {
        const char kind = s[p++];
        if (kind == '#' || kind == '.') {
          const size_t begin = p;
          while (p < s.size() && (std::isalnum(static_cast<unsigned char>(s[p])) || s[p] == '-' ||
                                  s[p] == '_' || static_cast<unsigned char>(s[p]) >= 128)) ++p;
          if (begin == p) return {};
          std::string value = s.substr(begin, p - begin);
          if (kind == '#') part.id = std::move(value);
          else part.classes.push_back(std::move(value));
        } else if (kind == '[') {
          const size_t begin = p;
          char q = 0;
          bool escape = false;
          for (; p < s.size(); ++p) {
            const char c = s[p];
            if (q) {
              if (escape) escape = false;
              else if (c == '\\') escape = true;
              else if (c == q) q = 0;
            } else if (c == '\'' || c == '"') q = c;
            else if (c == ']') break;
          }
          if (p >= s.size()) return {};
          std::string inside = trim(std::string_view(s).substr(begin, p - begin));
          ++p;
          std::array<std::string, 3> attr;
          size_t op = inside.find_first_of("=^$*~|");
          if (op == std::string::npos) attr[0] = lower(trim(inside));
          else {
            attr[0] = lower(trim(std::string_view(inside).substr(0, op)));
            attr[1] = inside.substr(op, inside[op] == '=' ? 1 : 2);
            std::string value = trim(std::string_view(inside).substr(op + attr[1].size()));
            if (value.size() >= 2 && (value.front() == '\'' || value.front() == '"'))
              value = value.substr(1, value.size() - 2);
            attr[2] = std::move(value);
          }
          if (attr[0].empty()) return {};
          part.attrs.push_back(std::move(attr));
        } else if (kind == ':') {
          if (p < s.size() && s[p] == ':') return {};
          const size_t begin = p;
          while (p < s.size() && (std::isalnum(static_cast<unsigned char>(s[p])) || s[p] == '-')) ++p;
          if (begin == p) return {};
          std::string pseudo = lower(std::string_view(s).substr(begin, p - begin));
          if (p < s.size() && s[p] == '(') {
            const size_t open = p++;
            int depth = 1;
            char q = 0;
            bool escape = false;
            for (; p < s.size() && depth; ++p) {
              const char c = s[p];
              if (q) {
                if (escape) escape = false;
                else if (c == '\\') escape = true;
                else if (c == q) q = 0;
              } else if (c == '\'' || c == '"') q = c;
              else if (c == '(') ++depth;
              else if (c == ')') --depth;
            }
            if (depth != 0) return {};
            pseudo += s.substr(open, p - open);
          }
          part.pseudos.push_back(std::move(pseudo));
        } else return {};
      }
      if (part.tag.empty() && part.id.empty() && part.classes.empty() && part.attrs.empty() &&
          part.pseudos.empty()) return {};
      chain.push_back(std::move(part));
      if (chain.size() > 64) return {};
    }
    if (!chain.empty()) groups.push_back(std::move(chain));
  }
  return groups;
}
static bool match_chain(const Document &d, int n, const std::vector<SimpleSelector> &chain,
                        size_t i, const SelectorState *state);
static bool is_self_or_ancestor(const Document &d, int candidate, int node) {
  for (int current = node; current >= 0; current = d.nodes[current].parent)
    if (current == candidate) return true;
  return false;
}
static bool nth_child_matches(size_t one_based_index, std::string expression) {
  expression = lower(trim(expression));
  expression.erase(std::remove_if(expression.begin(), expression.end(),
                                  [](unsigned char c) { return std::isspace(c); }),
                   expression.end());
  if (expression == "odd") return one_based_index % 2 == 1;
  if (expression == "even") return one_based_index % 2 == 0;
  const size_t n = expression.find('n');
  try {
    if (n == std::string::npos) return one_based_index == size_t(std::stoul(expression));
    const std::string a_text = expression.substr(0, n);
    const std::string b_text = expression.substr(n + 1);
    const int a = a_text.empty() || a_text == "+" ? 1 : a_text == "-" ? -1 : std::stoi(a_text);
    const int b = b_text.empty() ? 0 : std::stoi(b_text);
    const int delta = int(one_based_index) - b;
    return a == 0 ? delta == 0 : delta % a == 0 && delta / a >= 0;
  } catch (...) { return false; }
}
static bool is_form_control(const Node &node) {
  return node.name == "button" || node.name == "input" || node.name == "select" ||
         node.name == "textarea" || node.name == "option" || node.name == "optgroup" ||
         node.name == "fieldset";
}
static bool disabled(const Document &d, int n) {
  const Node &node = d.nodes[n];
  if (!is_form_control(node)) return false;
  std::string ignored;
  if (node.get_attribute("disabled", ignored)) return true;
  if (node.name == "option" && node.parent >= 0 &&
      d.nodes[node.parent].name == "optgroup" &&
      d.nodes[node.parent].get_attribute("disabled", ignored)) return true;
  for (int p = node.parent; p >= 0; p = d.nodes[p].parent)
    if (d.nodes[p].name == "fieldset" && d.nodes[p].get_attribute("disabled", ignored)) return true;
  return false;
}
static int form_owner(const Document &doc, int node) {
  if (node < 0 || node >= int(doc.nodes.size())) return -1;
  const std::string explicit_form = doc.nodes[node].attribute("form");
  if (!explicit_form.empty()) {
    const int owner = doc.by_id(explicit_form);
    return owner >= 0 && doc.nodes[owner].name == "form" ? owner : -1;
  }
  for (int parent = doc.nodes[node].parent; parent >= 0; parent = doc.nodes[parent].parent)
    if (doc.nodes[parent].name == "form") return parent;
  return -1;
}
bool form_control_valid(const Document &doc, int id) {
  if (id < 0 || id >= int(doc.nodes.size())) return true;
  const Node &node = doc.nodes[id];
  if (node.type != Node::Type::Element || !is_form_control(node) || disabled(doc, id)) return true;
  std::string ignored;
  if (node.name == "button" || node.name == "fieldset" || node.name == "optgroup" ||
      node.name == "option") return true;
  if (node.name == "input") {
    const std::string input_type = lower(node.attribute("type", "text"));
    if (input_type == "hidden" || input_type == "button" || input_type == "submit" ||
        input_type == "reset" || input_type == "image") return true;
  }
  const bool required = node.get_attribute("required", ignored);
  if (node.name == "input" && lower(node.attribute("type", "text")) == "radio" && required) {
    const std::string name = node.attribute("name");
    const int owner = form_owner(doc, id);
    for (int other = 0; !name.empty() && other < int(doc.nodes.size()); ++other)
      if (other != id && doc.nodes[other].name == "input" &&
          lower(doc.nodes[other].attribute("type", "text")) == "radio" &&
          doc.nodes[other].attribute("name") == name && form_owner(doc, other) == owner &&
          !disabled(doc, other) && doc.nodes[other].get_attribute("checked", ignored)) return true;
  }
  std::string value;
  if (node.name == "textarea") value = doc.text_content(id);
  else if (node.name == "select") {
    for (int option : node.children)
      if (doc.nodes[option].name == "option" && doc.nodes[option].get_attribute("selected", ignored)) {
        value = doc.nodes[option].attribute("value", doc.text_content(option));
        break;
      }
    if (value.empty() && !node.children.empty())
      for (int option : node.children)
        if (doc.nodes[option].name == "option") {
          value = doc.nodes[option].attribute("value", doc.text_content(option));
          break;
        }
  } else value = node.attribute("value");
  const std::string type = node.name == "input" ? lower(node.attribute("type", "text")) : "text";
  if (required) {
    if (type == "checkbox") {
      if (!node.get_attribute("checked", ignored)) return false;
    } else if (type == "radio") {
      if (!node.get_attribute("checked", ignored)) return false;
    } else if (value.empty() && type != "range") return false;
  }
  if (value.empty()) return true;
  if (type == "email") {
    const size_t at = value.find('@');
    if (at == std::string::npos || at == 0 || at + 1 >= value.size() ||
        value.find('@', at + 1) != std::string::npos || value.find('.', at + 1) == std::string::npos)
      return false;
  }
  if (type == "number" || type == "range") {
    double number = 0;
    try {
      size_t used = 0;
      number = std::stod(value, &used);
      if (used != value.size() || !std::isfinite(number)) return false;
    } catch (...) { return false; }
    auto bound = [&](const char *name, bool minimum) {
      const std::string raw = node.attribute(name);
      if (raw.empty()) return true;
      try {
        size_t used = 0;
        const double limit = std::stod(raw, &used);
        return used == raw.size() && (minimum ? number >= limit : number <= limit);
      } catch (...) { return true; }
    };
    if (!bound("min", true) || !bound("max", false)) return false;
    const std::string raw_step = node.attribute("step");
    if (!raw_step.empty() && lower(raw_step) != "any") {
      try {
        const double step = std::stod(raw_step);
        const double base = node.attribute("min").empty() ? 0.0 : std::stod(node.attribute("min"));
        if (step > 0 && std::abs((number - base) / step - std::round((number - base) / step)) > 1e-7)
          return false;
      } catch (...) { }
    }
  }
  return true;
}
static bool match_simple(const Document &d, int n, const SimpleSelector &s,
                         const SelectorState *state) {
  if (n < 0 || n >= int(d.nodes.size())) return false;
  const Node &node = d.nodes[n];
  if (node.type != Node::Type::Element) return false;
  if (!s.tag.empty() && s.tag != "*" && node.name != s.tag) return false;
  if (!s.id.empty() && node.attribute("id") != s.id) return false;
  for (const std::string &c : s.classes) if (!node.has_class(c)) return false;
  for (const auto &a : s.attrs) {
    std::string value;
    if (!node.get_attribute(a[0], value)) return false;
    if (a[1].empty()) continue;
    if (a[1] == "=" && value != a[2]) return false;
    if (a[1] == "^=" && !starts(value, a[2])) return false;
    if (a[1] == "$=" && !ends(value, a[2])) return false;
    if (a[1] == "*=" && value.find(a[2]) == std::string::npos) return false;
    if (a[1] == "~=") {
      bool found = false;
      for (const auto &word : split(value, ' ')) if (word == a[2]) found = true;
      if (!found) return false;
    }
    if (a[1] == "|=" && value != a[2] && !starts(value, a[2] + "-")) return false;
  }
  for (const std::string &pseudo : s.pseudos) {
    if (pseudo == "root") {
      if (node.parent < 0 || d.nodes[node.parent].type != Node::Type::Document) return false;
    } else if (pseudo == "first-child" || pseudo == "last-child" ||
               pseudo == "only-child" || starts(pseudo, "nth-child(")) {
      if (node.parent < 0) return false;
      std::vector<int> siblings;
      for (int child : d.nodes[node.parent].children)
        if (d.nodes[child].type == Node::Type::Element) siblings.push_back(child);
      auto it = std::find(siblings.begin(), siblings.end(), n);
      if (it == siblings.end()) return false;
      const size_t index = size_t(it - siblings.begin()) + 1;
      if (pseudo == "first-child" && index != 1) return false;
      if (pseudo == "last-child" && index != siblings.size()) return false;
      if (pseudo == "only-child" && siblings.size() != 1) return false;
      if (starts(pseudo, "nth-child(") &&
          !nth_child_matches(index, pseudo.substr(10, pseudo.size() - 11))) return false;
    } else if (pseudo == "checked") {
      std::string ignored;
      const std::string type = lower(node.attribute("type"));
      const bool checked_input = node.name == "input" && (type == "checkbox" || type == "radio") &&
                                 node.get_attribute("checked", ignored);
      const bool selected_option = node.name == "option" && node.get_attribute("selected", ignored);
      if (!checked_input && !selected_option) return false;
    } else if (pseudo == "disabled") {
      if (!disabled(d, n)) return false;
    } else if (pseudo == "enabled") {
      if (!is_form_control(node) || disabled(d, n)) return false;
    } else if (pseudo == "required" || pseudo == "optional") {
      if (node.name != "input" && node.name != "select" && node.name != "textarea") return false;
      std::string required_value;
      const bool required = node.get_attribute("required", required_value);
      if ((pseudo == "required") != required) return false;
    } else if (pseudo == "valid" || pseudo == "invalid") {
      if ((node.name != "input" && node.name != "select" && node.name != "textarea") ||
          form_control_valid(d, n) == (pseudo == "invalid")) return false;
    } else if (pseudo == "hover") {
      if (!state || state->hovered < 0 || !is_self_or_ancestor(d, n, state->hovered)) return false;
    } else if (pseudo == "active") {
      if (!state || state->active < 0 || !is_self_or_ancestor(d, n, state->active)) return false;
    } else if (pseudo == "focus") {
      if (!state || state->focused != n) return false;
    } else if (pseudo == "focus-within" || pseudo == "focus-visible") {
      if (!state || state->focused < 0 ||
          (pseudo == "focus-visible" ? state->focused != n : !is_self_or_ancestor(d, n, state->focused)))
        return false;
    } else if (starts(pseudo, "not(") && pseudo.back() == ')') {
      static thread_local unsigned negation_depth = 0;
      if (negation_depth >= 64) return false;
      ++negation_depth;
      const std::string_view argument(pseudo.data() + 4, pseudo.size() - 5);
      bool excluded = false;
      for (const auto &group : parse_selector(argument))
        if (!group.empty() && match_chain(d, n, group, group.size() - 1, state)) excluded = true;
      --negation_depth;
      if (excluded) return false;
    } else return false;
  }
  return true;
}
static bool match_chain(const Document &d, int n, const std::vector<SimpleSelector> &chain,
                        size_t i, const SelectorState *state) {
  if (i >= chain.size() || !match_simple(d, n, chain[i], state)) return false;
  if (i == 0) return true;
  const Node &node = d.nodes[n];
  const char rel = chain[i].relation;
  if (rel == '>') return node.parent >= 0 && match_chain(d, node.parent, chain, i - 1, state);
  if (rel == '+' || rel == '~') {
    if (node.parent < 0) return false;
    const auto &siblings = d.nodes[node.parent].children;
    auto it = std::find(siblings.begin(), siblings.end(), n);
    if (it == siblings.begin() || it == siblings.end()) return false;
    do {
      --it;
      if (d.nodes[*it].type == Node::Type::Element && match_chain(d, *it, chain, i - 1, state))
        return true;
      if (rel == '+') return false;
    } while (it != siblings.begin());
    return false;
  }
  for (int parent = node.parent; parent >= 0; parent = d.nodes[parent].parent)
    if (match_chain(d, parent, chain, i - 1, state)) return true;
  return false;
}
bool matches_selector(const Document &d, int n, std::string_view selector,
                      const SelectorState *state) {
  if (n < 0 || n >= int(d.nodes.size())) return false;
  for (const auto &group : parse_selector(selector))
    if (!group.empty() && match_chain(d, n, group, group.size() - 1, state)) return true;
  return false;
}
} // namespace pv
