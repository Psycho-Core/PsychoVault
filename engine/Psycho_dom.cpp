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
  std::string pseudo;
  char relation = ' ';
};
static std::vector<std::vector<SimpleSelector>> parse_selector(std::string_view selector) {
  std::vector<std::vector<SimpleSelector>> groups;
  for (auto &group : split(selector, ',')) {
    std::string s = trim(group);
    if (s.empty() || s.size() > 4096)
      return {};
    std::vector<SimpleSelector> chain;
    size_t p = 0;
    while (p < s.size()) {
      while (p < s.size() && std::isspace(static_cast<unsigned char>(s[p])))
        ++p;
      char relation = ' ';
      if (p < s.size() && (s[p] == '>' || s[p] == '+' || s[p] == '~'))
        relation = s[p++];
      while (p < s.size() && std::isspace(static_cast<unsigned char>(s[p])))
        ++p;
      SimpleSelector part;
      part.relation = chain.empty() ? ' ' : relation;
      if (p < s.size() && (std::isalpha(static_cast<unsigned char>(s[p])) || s[p] == '*')) {
        size_t a = p;
        if (s[p] == '*')
          ++p;
        else
          while (p < s.size() &&
                 (std::isalnum(static_cast<unsigned char>(s[p])) || s[p] == '-' || s[p] == '_'))
            ++p;
        part.tag = lower(s.substr(a, p - a));
      }
      while (p < s.size() && s[p] != ' ' && s[p] != '\t' && s[p] != '\n' && s[p] != '>' &&
             s[p] != '+' && s[p] != '~') {
        char kind = s[p++];
        size_t a = p;
        if (kind == '#' || kind == '.') {
          while (p < s.size() && (std::isalnum(static_cast<unsigned char>(s[p])) || s[p] == '-' ||
                                  s[p] == '_' || static_cast<unsigned char>(s[p]) >= 128))
            ++p;
          auto value = std::string(s.substr(a, p - a));
          if (kind == '#')
            part.id = value;
          else
            part.classes.push_back(value);
        } else if (kind == '[') {
          size_t e = s.find(']', p);
          if (e == std::string::npos)
            return {};
          auto inside = std::string(s.substr(p, e - p));
          p = e + 1;
          std::array<std::string, 3> a;
          size_t op = inside.find_first_of("=^$*~|");
          if (op == std::string::npos)
            a[0] = lower(trim(inside));
          else {
            a[0] = lower(trim(std::string_view(inside).substr(0, op)));
            a[1] = inside.substr(op, inside[op] == '=' ? 1 : 2);
            auto v = trim(std::string_view(inside).substr(op + a[1].size()));
            if (v.size() >= 2 && (v.front() == '"' || v.front() == '\''))
              v = v.substr(1, v.size() - 2);
            a[2] = v;
          }
          part.attrs.push_back(a);
        } else if (kind == ':') {
          if (p < s.size() && s[p] == ':')
            return {};
          while (p < s.size() && (std::isalnum(static_cast<unsigned char>(s[p])) || s[p] == '-'))
            ++p;
          part.pseudo = lower(s.substr(a, p - a));
          if (p < s.size() && s[p] == '(') {
            size_t e = s.find(')', p);
            if (e == std::string::npos)
              return {};
            part.pseudo += s.substr(p, e - p + 1);
            p = e + 1;
          }
        } else
          return {};
      }
      if (part.tag.empty() && part.id.empty() && part.classes.empty() && part.attrs.empty() &&
          part.pseudo.empty())
        return {};
      chain.push_back(std::move(part));
      if (chain.size() > 64)
        return {};
    }
    if (!chain.empty())
      groups.push_back(std::move(chain));
  }
  return groups;
}
static bool match_simple(const Document &d, int n, const SimpleSelector &s) {
  const auto &node = d.nodes[n];
  if (node.type != Node::Type::Element)
    return false;
  if (!s.tag.empty() && s.tag != "*" && node.name != s.tag)
    return false;
  if (!s.id.empty() && node.attribute("id") != s.id)
    return false;
  for (auto &c : s.classes)
    if (!node.has_class(c))
      return false;
  for (auto &a : s.attrs) {
    std::string value;
    bool has = node.get_attribute(a[0], value);
    if (!has)
      return false;
    if (a[1].empty())
      continue;
    if (a[1] == "=" && value != a[2])
      return false;
    if (a[1] == "^=" && !starts(value, a[2]))
      return false;
    if (a[1] == "$=" && !ends(value, a[2]))
      return false;
    if (a[1] == "*=" && value.find(a[2]) == std::string::npos)
      return false;
    if (a[1] == "~=") {
      bool found = false;
      for (auto &v : split(value, ' '))
        if (v == a[2])
          found = true;
      if (!found)
        return false;
    }
  }
  if (!s.pseudo.empty()) {
    if (s.pseudo == "root") {
      if (node.parent < 0 || d.nodes[node.parent].type != Node::Type::Document)
        return false;
    } else if (s.pseudo == "first-child" || s.pseudo == "last-child" ||
               starts(s.pseudo, "nth-child(")) {
      if (node.parent < 0)
        return false;
      std::vector<int> siblings;
      for (int c : d.nodes[node.parent].children)
        if (d.nodes[c].type == Node::Type::Element)
          siblings.push_back(c);
      if (siblings.empty())
        return false;
      size_t index = std::find(siblings.begin(), siblings.end(), n) - siblings.begin();
      if (s.pseudo == "first-child" && index != 0)
        return false;
      if (s.pseudo == "last-child" && index + 1 != siblings.size())
        return false;
      if (starts(s.pseudo, "nth-child(")) {
        auto arg = s.pseudo.substr(10, s.pseudo.size() - 11);
        int target = 0;
        try {
          target = std::stoi(arg);
        } catch (...) {
          return false;
        }
        if (index + 1 != size_t(target))
          return false;
      }
    } else if (s.pseudo == "checked") {
      std::string value;
      if (!node.get_attribute("checked", value) && !node.get_attribute("selected", value))
        return false;
    } else
      return false;
  }
  return true;
}
static bool match_chain(const Document &d, int n, const std::vector<SimpleSelector> &chain,
                        size_t i) {
  if (!match_simple(d, n, chain[i]))
    return false;
  if (i == 0)
    return true;
  const auto &node = d.nodes[n];
  char rel = chain[i].relation;
  if (rel == '>')
    return node.parent >= 0 && match_chain(d, node.parent, chain, i - 1);
  if (rel == '+' || rel == '~') {
    if (node.parent < 0)
      return false;
    const auto &sib = d.nodes[node.parent].children;
    auto it = std::find(sib.begin(), sib.end(), n);
    if (it == sib.begin())
      return false;
    if (rel == '+') {
      --it;
      return d.nodes[*it].type == Node::Type::Element && match_chain(d, *it, chain, i - 1);
    }
    do {
      --it;
      if (d.nodes[*it].type == Node::Type::Element && match_chain(d, *it, chain, i - 1))
        return true;
    } while (it != sib.begin());
    return false;
  }
  for (int p = node.parent; p >= 0; p = d.nodes[p].parent)
    if (match_chain(d, p, chain, i - 1))
      return true;
  return false;
}
bool matches_selector(const Document &d, int n, std::string_view selector) {
  if (n < 0 || n >= int(d.nodes.size()))
    return false;
  auto groups = parse_selector(selector);
  for (auto &chain : groups)
    if (match_chain(d, n, chain, chain.size() - 1))
      return true;
  return false;
}
} // namespace pv
