#include "Psycho_page.hpp"
#include <chrono>
namespace pv {
static constexpr int kDocument = -1, kLocation = -3, kStyleBase = 1000000, kClassBase = 2000000;
static bool has_attr(const Node &n, const std::string &key) {
  std::string v;
  return n.get_attribute(key, v);
}
class PageHost : public js::Host {
public:
  Page &page;
  explicit PageHost(Page &p) : page(p) {}
  js::Value node(int id) {
    return page.js_->host_value(id, id == kDocument ? "Document" : "HTMLElement");
  }
  js::Value list(const std::vector<int> &ids) {
    js::Values values;
    for (int id : ids)
      values.push_back(node(id));
    return page.js_->make_array(values);
  }
  void log(const std::string &text) override {
    if (page.diagnostics.size() < 200)
      page.diagnostics.push_back({"console", text.substr(0, 4096)});
  }
  js::Value get(int id, const std::string &name) override {
    auto &r = *page.js_;
    auto &d = page.doc;
    if (id == kLocation) {
      if (name == "href")
        return js::Value(d.url.href());
      if (name == "origin")
        return js::Value(d.url.origin());
      if (name == "protocol")
        return js::Value(d.url.scheme + ":");
      if (name == "host")
        return js::Value(d.url.authority());
      if (name == "hostname")
        return js::Value(d.url.host);
      if (name == "pathname")
        return js::Value(d.url.path);
      if (name == "search")
        return js::Value(d.url.query.empty() ? "" : "?" + d.url.query);
      if (name == "hash")
        return js::Value(d.url.fragment.empty() ? "" : "#" + d.url.fragment);
      if (name == "assign" || name == "replace")
        return r.make_native([this](js::Values &a) {
          if (!a.empty())
            page.request_navigation(a[0].to_string());
          return js::Value();
        });
      return js::Value();
    }
    if (id == kDocument) {
      if (name == "documentElement")
        return node(d.element("html"));
      if (name == "body")
        return node(d.body);
      if (name == "head")
        return node(d.head);
      if (name == "title")
        return js::Value(d.title());
      if (name == "URL" || name == "documentURI")
        return js::Value(d.url.href());
      if (name == "location")
        return r.host_value(kLocation, "Location");
      if (name == "getElementById")
        return r.make_native([this](js::Values &a) {
          int id = a.empty() ? -1 : page.doc.by_id(a[0].to_string());
          return id < 0 ? js::Value::null() : node(id);
        });
      if (name == "querySelector")
        return r.make_native([this](js::Values &a) {
          int id = a.empty() ? -1 : page.doc.query_one(a[0].to_string());
          return id < 0 ? js::Value::null() : node(id);
        });
      if (name == "querySelectorAll")
        return r.make_native([this](js::Values &a) {
          return list(a.empty() ? std::vector<int>{} : page.doc.query(a[0].to_string()));
        });
      if (name == "getElementsByTagName")
        return r.make_native([this](js::Values &a) {
          std::vector<int> ids;
          auto tag = lower(a.empty() ? "" : a[0].to_string());
          for (auto &n : page.doc.nodes)
            if (n.type == Node::Type::Element && (tag == "*" || n.name == tag))
              ids.push_back(n.id);
          return list(ids);
        });
      if (name == "createElement")
        return r.make_native([this](js::Values &a) {
          int id = page.doc.create(Node::Type::Element, a.empty() ? "div" : a[0].to_string());
          page.dirty_ = true;
          return node(id);
        });
      if (name == "createTextNode")
        return r.make_native([this](js::Values &a) {
          int id = page.doc.create(Node::Type::Text, "#text", a.empty() ? "" : a[0].to_string());
          page.dirty_ = true;
          return node(id);
        });
      if (name == "addEventListener")
        return r.make_native([this](js::Values &a) {
          if (a.size() > 1)
            page.add_listener(page.doc.root, a[0].to_string(), a[1]);
          return js::Value();
        });
      if (name == "write")
        return r.make_native([this](js::Values &a) {
          std::string html;
          for (auto &v : a)
            html += v.to_string();
          page.doc.parse_fragment(page.doc.body, html);
          page.dirty_ = true;
          return js::Value();
        });
      return js::Value();
    }
    if (id >= kStyleBase && id < kClassBase) {
      int n = id - kStyleBase;
      if (n < 0 || n >= int(d.nodes.size()))
        return js::Value();
      auto style = parse_inline(d.nodes[n].attribute("style"));
      auto it = style.find(name);
      if (it != style.end())
        return js::Value(it->second);
      if (name == "setProperty")
        return r.make_native([this, n](js::Values &a) {
          if (a.size() > 1) {
            auto s = parse_inline(page.doc.nodes[n].attribute("style"));
            s[a[0].to_string()] = a[1].to_string();
            page.doc.nodes[n].set_attribute("style", serialize_inline(s));
            page.dirty_ = true;
          }
          return js::Value();
        });
      if (name == "removeProperty")
        return r.make_native([this, n](js::Values &a) {
          if (!a.empty()) {
            auto s = parse_inline(page.doc.nodes[n].attribute("style"));
            s.erase(a[0].to_string());
            page.doc.nodes[n].set_attribute("style", serialize_inline(s));
            page.dirty_ = true;
          }
          return js::Value();
        });
      return js::Value("");
    }
    if (id >= kClassBase) {
      int n = id - kClassBase;
      if (n < 0 || n >= int(d.nodes.size()))
        return js::Value();
      if (name == "add")
        return r.make_native([this, n](js::Values &a) {
          auto classes = split(page.doc.nodes[n].attribute("class"), ' ');
          for (auto &v : a) {
            auto c = v.to_string();
            if (std::find(classes.begin(), classes.end(), c) == classes.end())
              classes.push_back(c);
          }
          std::string out;
          for (auto &c : classes) {
            if (!out.empty())
              out += ' ';
            out += c;
          }
          page.doc.nodes[n].set_attribute("class", out);
          page.dirty_ = true;
          return js::Value();
        });
      if (name == "remove")
        return r.make_native([this, n](js::Values &a) {
          auto classes = split(page.doc.nodes[n].attribute("class"), ' ');
          for (auto &v : a)
            std::erase(classes, v.to_string());
          std::string out;
          for (auto &c : classes) {
            if (!out.empty())
              out += ' ';
            out += c;
          }
          page.doc.nodes[n].set_attribute("class", out);
          page.dirty_ = true;
          return js::Value();
        });
      if (name == "contains")
        return r.make_native([this, n](js::Values &a) {
          return js::Value(!a.empty() && page.doc.nodes[n].has_class(a[0].to_string()));
        });
      if (name == "toggle")
        return r.make_native([this, n](js::Values &a) {
          if (a.empty())
            return js::Value(false);
          auto c = a[0].to_string();
          bool has = page.doc.nodes[n].has_class(c);
          auto classes = split(page.doc.nodes[n].attribute("class"), ' ');
          if (has)
            std::erase(classes, c);
          else
            classes.push_back(c);
          std::string out;
          for (auto &x : classes) {
            if (!out.empty())
              out += ' ';
            out += x;
          }
          page.doc.nodes[n].set_attribute("class", out);
          page.dirty_ = true;
          return js::Value(!has);
        });
      return js::Value();
    }
    if (id < 0 || id >= int(d.nodes.size()))
      return js::Value();
    auto &n = d.nodes[id];
    if (name == "nodeType")
      return js::Value(n.type == Node::Type::Element ? 1.0
                       : n.type == Node::Type::Text  ? 3.0
                                                     : 9.0);
    if (name == "tagName" || name == "nodeName")
      return js::Value(n.type == Node::Type::Element ? upper_tag(n.name) : n.name);
    if (name == "id")
      return js::Value(n.attribute("id"));
    if (name == "className")
      return js::Value(n.attribute("class"));
    if (name == "textContent" || name == "innerText")
      return js::Value(d.text_content(id));
    if (name == "innerHTML")
      return js::Value(serialize_children(d, id));
    if (name == "value")
      return js::Value(n.name == "textarea" ? d.text_content(id) : n.attribute("value"));
    if (name == "checked")
      return js::Value(has_attr(n, "checked"));
    if (name == "href" || name == "src")
      return js::Value(n.attribute(name));
    if (name == "style")
      return r.host_value(kStyleBase + id, "CSSStyleDeclaration");
    if (name == "classList")
      return r.host_value(kClassBase + id, "DOMTokenList");
    if (name == "parentElement" || name == "parentNode")
      return n.parent >= 0 ? node(n.parent) : js::Value::null();
    if (name == "children" || name == "childNodes")
      return list(n.children);
    if (name == "firstElementChild")
      return n.children.empty() ? js::Value::null() : node(n.children.front());
    if (name == "attributes")
      return js::Value();
    if (name == "getAttribute")
      return r.make_native([this, id](js::Values &a) {
        std::string v;
        if (a.empty() || !page.doc.nodes[id].get_attribute(a[0].to_string(), v))
          return js::Value::null();
        return js::Value(v);
      });
    if (name == "setAttribute")
      return r.make_native([this, id](js::Values &a) {
        if (a.size() > 1) {
          page.doc.nodes[id].set_attribute(a[0].to_string(), a[1].to_string());
          page.dirty_ = true;
        }
        return js::Value();
      });
    if (name == "removeAttribute")
      return r.make_native([this, id](js::Values &a) {
        if (!a.empty()) {
          auto key = lower(a[0].to_string());
          auto &attrs = page.doc.nodes[id].attributes;
          std::erase_if(attrs, [&](auto &p) { return p.first == key; });
          page.dirty_ = true;
        }
        return js::Value();
      });
    if (name == "hasAttribute")
      return r.make_native([this, id](js::Values &a) {
        std::string v;
        return js::Value(!a.empty() && page.doc.nodes[id].get_attribute(a[0].to_string(), v));
      });
    if (name == "appendChild" || name == "append")
      return r.make_native([this, id](js::Values &a) {
        for (auto &v : a)
          if (v.type == js::Value::Type::Object && v.object &&
              v.object->kind == js::Object::Kind::Host)
            page.doc.append(id, v.object->host);
        page.dirty_ = true;
        return a.empty() ? js::Value() : a[0];
      });
    if (name == "remove")
      return r.make_native([this, id](js::Values &) {
        page.doc.remove(id);
        page.dirty_ = true;
        return js::Value();
      });
    if (name == "querySelector")
      return r.make_native([this, id](js::Values &a) {
        int n = a.empty() ? -1 : page.doc.query_one(a[0].to_string(), id);
        return n < 0 ? js::Value::null() : node(n);
      });
    if (name == "querySelectorAll")
      return r.make_native([this, id](js::Values &a) {
        return list(a.empty() ? std::vector<int>{} : page.doc.query(a[0].to_string(), id));
      });
    if (name == "addEventListener")
      return r.make_native([this, id](js::Values &a) {
        if (a.size() > 1)
          page.add_listener(id, a[0].to_string(), a[1]);
        return js::Value();
      });
    if (name == "removeEventListener")
      return r.make_native([this, id](js::Values &a) {
        if (a.size() > 1)
          page.remove_listener(id, a[0].to_string(), a[1]);
        return js::Value();
      });
    if (name == "click")
      return r.make_native([this, id](js::Values &) {
        page.activate(id);
        return js::Value();
      });
    if (name == "focus")
      return r.make_native([this, id](js::Values &) {
        page.focused_ = id;
        return js::Value();
      });
    if (name == "preventDefault" || name == "stopPropagation")
      return r.make_native([](js::Values &) { return js::Value(); });
    return js::Value();
  }
  bool set(int id, const std::string &name, const js::Value &value) override {
    auto &d = page.doc;
    if (id == kLocation && name == "href") {
      page.request_navigation(value.to_string());
      return true;
    }
    if (id == kDocument && name == "title") {
      int t = d.element("title", d.head);
      if (t < 0) {
        t = d.create(Node::Type::Element, "title");
        d.append(d.head, t);
      }
      d.set_text(t, value.to_string());
      page.dirty_ = true;
      return true;
    }
    if (id >= kStyleBase && id < kClassBase) {
      int n = id - kStyleBase;
      if (n >= 0 && n < int(d.nodes.size())) {
        auto s = parse_inline(d.nodes[n].attribute("style"));
        s[name] = value.to_string();
        d.nodes[n].set_attribute("style", serialize_inline(s));
        page.dirty_ = true;
      }
      return true;
    }
    if (id < 0 || id >= int(d.nodes.size()))
      return false;
    auto &n = d.nodes[id];
    if (name == "textContent" || name == "innerText") {
      d.set_text(id, value.to_string());
      page.dirty_ = true;
      return true;
    }
    if (name == "innerHTML") {
      for (int c : n.children)
        d.remove(c);
      d.parse_fragment(id, value.to_string());
      page.dirty_ = true;
      return true;
    }
    if (name == "value") {
      if (n.name == "textarea")
        d.set_text(id, value.to_string());
      else
        n.set_attribute("value", value.to_string());
      page.dirty_ = true;
      return true;
    }
    if (name == "checked") {
      if (value.truthy())
        n.set_attribute("checked", "");
      else
        std::erase_if(n.attributes, [](auto &a) { return a.first == "checked"; });
      page.dirty_ = true;
      return true;
    }
    if (name == "id") {
      n.set_attribute("id", value.to_string());
      return true;
    }
    if (name == "className") {
      n.set_attribute("class", value.to_string());
      page.dirty_ = true;
      return true;
    }
    if (name == "href" || name == "src") {
      n.set_attribute(name, value.to_string());
      page.dirty_ = true;
      return true;
    }
    return false;
  }
  js::Value call(int, const std::string &, js::Values &) override {
    return js::Value();
  }
  static bool has_attr(const Node &n, const std::string &key) {
    std::string v;
    return n.get_attribute(key, v);
  }
  static std::string upper_tag(std::string s) {
    for (char &c : s)
      if (c >= 'a' && c <= 'z')
        c -= 32;
    return s;
  }
  static std::map<std::string, std::string> parse_inline(const std::string &s) {
    std::map<std::string, std::string> out;
    for (auto &p : split(s, ';')) {
      auto c = p.find(':');
      if (c != std::string::npos)
        out[lower(trim(std::string_view(p).substr(0, c)))] =
            trim(std::string_view(p).substr(c + 1));
    }
    return out;
  }
  static std::string serialize_inline(const std::map<std::string, std::string> &s) {
    std::string out;
    for (auto &p : s) {
      if (!out.empty())
        out += ';';
      out += p.first + ":" + p.second;
    }
    return out;
  }
  static std::string serialize_children(const Document &d, int id) {
    std::string out;
    for (int c : d.nodes[id].children)
      out += serialize_node(d, c);
    return out;
  }
  static std::string serialize_node(const Document &d, int id) {
    const auto &n = d.nodes[id];
    if (n.type == Node::Type::Text)
      return html_escape(n.text);
    if (n.type != Node::Type::Element)
      return "";
    std::string out = "<" + n.name;
    for (auto &a : n.attributes)
      out += " " + a.first + "=\"" + html_escape(a.second) + "\"";
    if (is_void(n.name))
      return out + ">";
    out += ">";
    for (int c : n.children)
      out += serialize_node(d, c);
    return out + "</" + n.name + ">";
  }
};
void Page::request_navigation(const std::string &input) {
  if (navigations.size() < 8) {
    auto u = resolve_url(doc.url, input);
    if (u && u->network())
      navigations.push_back(u->href());
    else if (u && u->scheme == "psychovault")
      navigations.push_back(u->href());
  }
}
void Page::add_listener(int node, const std::string &type, js::Value fn) {
  if (listeners_.size() >= 1024 || fn.type_of() != "function")
    return;
  listeners_.push_back({node, type, fn});
  js_->root(fn.object);
}
void Page::remove_listener(int node, const std::string &type, js::Value fn) {
  for (auto it = listeners_.begin(); it != listeners_.end(); ++it)
    if (it->node == node && it->type == type && it->fn.object == fn.object) {
      js_->unroot(it->fn.object);
      listeners_.erase(it);
      return;
    }
}
Page::Page() {
  js_ = std::make_unique<js::Runtime>(new PageHost(*this));
}
Page::~Page() {
  delete js_->host();
}
void Page::load(const Url &url, const std::string &html, const std::string &mime) {
  doc.url = url;
  doc.content_type = mime;
  doc.parse(html);
  resources_.clear();
  images_.clear();
  requested_.clear();
  executed_.clear();
  listeners_.clear();
  requests.clear();
  navigations.clear();
  dialogs.clear();
  diagnostics = doc.diagnostics;
  delete js_->host();
  js_ = std::make_unique<js::Runtime>(new PageHost(*this));
  js_->global().properties["location"] = js_->host_value(kLocation, "Location");
  js_->global().properties["alert"] = js_->make_native([this](js::Values &a) {
    if (dialogs.size() < 8)
      dialogs.push_back(a.empty() ? "" : a[0].to_string());
    return js::Value();
  });
  int base = doc.query_one("base[href]");
  if (base >= 0) {
    auto u = resolve_url(url, doc.nodes[base].attribute("href"));
    if (u)
      doc.url = *u;
  }
  dirty_ = true;
  run_scripts();
}
void Page::request(std::string input) {
  auto u = resolve_url(doc.url, input);
  if (!u || !u->network())
    return;
  auto href = u->href();
  if (requested_.count(href) || requested_.size() >= kMaxResources)
    return;
  requested_.insert(href);
  requests.push_back(href);
}
void Page::resource(const Resource &res) {
  Resource stored = res;
  static constexpr uint8_t png_signature[8] = {137, 80, 78, 71, 13, 10, 26, 10};
  bool png = lower(stored.mime).find("image/png") != std::string::npos ||
             (stored.bytes.size() >= 8 &&
              std::equal(png_signature, png_signature + 8, stored.bytes.begin()));
  if (stored.ok && png) {
    try {
      Image image = decode_png(stored.bytes);
      stored.image_width = image.width;
      stored.image_height = image.height;
      images_[stored.url] = std::move(image);
    } catch (const std::exception &e) {
      stored.ok = false;
      stored.error = std::string("PNG decode failed: ") + e.what();
      diagnostics.push_back({"image", stored.error});
    }
  }
  resources_[stored.url] = std::move(stored);
  dirty_ = true;
  run_scripts();
}
void Page::execute_script(int id) {
  if (executed_.count(id))
    return;
  executed_.insert(id);
  const auto &n = doc.nodes[id];
  auto type = lower(n.attribute("type"));
  if (!type.empty() && type != "text/javascript" && type != "application/javascript" &&
      type != "module")
    return;
  if (type == "module") {
    diagnostics.push_back({"js", "ES modules are not implemented; script was not executed"});
    return;
  }
  try {
    js_->run(doc.text_content(id));
  } catch (js::JsError &e) {
    diagnostics.push_back({"js", e.value.to_string().substr(0, 2048)});
  } catch (std::exception &e) {
    diagnostics.push_back({"js", std::string(e.what()).substr(0, 2048)});
  }
  dirty_ = true;
}
void Page::run_scripts() {
  for (auto &n : doc.nodes) {
    if (n.type != Node::Type::Element)
      continue;
    if (n.name == "script") {
      auto src = n.attribute("src");
      if (src.empty())
        execute_script(n.id);
      else {
        auto u = resolve_url(doc.url, src);
        if (!u)
          continue;
        auto it = resources_.find(u->href());
        if (it == resources_.end()) {
          request(src);
          if (!has_attr(n, "async") && !has_attr(n, "defer"))
            return;
        } else if (it->second.ok)
          execute_script(n.id);
        else
          executed_.insert(n.id);
      }
    } else if (n.name == "link" && lower(n.attribute("rel")) == "stylesheet")
      request(n.attribute("href"));
    else if (n.name == "img")
      request(n.attribute("src"));
  }
  // Deferred scripts run after the blocking scripts have had their resources
  // delivered.
  for (auto &n : doc.nodes)
    if (n.name == "script" && has_attr(n, "defer") && !n.attribute("src").empty()) {
      auto u = resolve_url(doc.url, n.attribute("src"));
      if (u && resources_.count(u->href()))
        execute_script(n.id);
    }
}
void Page::rebuild() {
  std::vector<Stylesheet> sheets;
  int order = 0;
  for (auto &n : doc.nodes)
    if (n.name == "style") {
      Stylesheet s;
      s.parse(doc.text_content(n.id), order);
      sheets.push_back(std::move(s));
    }
  for (auto &n : doc.nodes)
    if (n.name == "link" && lower(n.attribute("rel")) == "stylesheet") {
      auto u = resolve_url(doc.url, n.attribute("href"));
      if (u) {
        auto it = resources_.find(u->href());
        if (it != resources_.end() && it->second.ok) {
          Stylesheet s;
          s.parse(it->second.text, order);
          sheets.push_back(std::move(s));
        }
      }
    }
  auto styles = compute_styles(doc, sheets, width_);
  Canvas measure{int(width_), int(height_)};
  layout_ = layout_document(doc, styles, width_, height_, resources_, measure);
  content_height_ = layout_.height;
  dirty_ = false;
}
Json Page::render(double width, double height, double scroll) {
  width_ = std::clamp(width, 1.0, 8192.0);
  height_ = std::clamp(height, 1.0, 16384.0);
  scroll_ = std::max(0.0, scroll);
  if (dirty_)
    rebuild();
  scroll_ = std::min(scroll_, std::max(0.0, content_height_ - height_));
  Json frame = Json::make_object();
  frame.set("tag", "frame");
  frame.set("title", title());
  frame.set("url", doc.url.href());
  frame.set("height", content_height_);
  frame.set("scroll", scroll_);
  Json paint = Json::make_array();
  for (auto &c : layout_.paint) {
    if (paint.array.size() >= 50000)
      break;
    Json j = Json::make_object();
    j.set("kind", int(c.kind));
    j.set("x", c.rect.x);
    j.set("y", c.rect.y);
    j.set("w", c.rect.w);
    j.set("h", c.rect.h);
    j.set("color", double(c.color));
    j.set("text", c.text);
    j.set("size", c.size);
    j.set("thickness", c.thickness);
    j.set("bold", c.bold);
    j.set("node", c.node);
    j.set("url", c.url);
    paint.array.push_back(std::move(j));
  }
  frame.set("paint", std::move(paint));
  return frame;
}
std::string Page::title() const {
  auto t = doc.title();
  return t.empty() ? doc.url.href() : t;
}
int Page::hit(double x, double y) const {
  int best = -1;
  double area = std::numeric_limits<double>::max();
  for (auto &[id, r] : layout_.boxes) {
    if (r.contains(x, y + scroll_) && r.w * r.h < area) {
      best = id;
      area = r.w * r.h;
    }
  }
  return best;
}
void Page::dispatch(int target, const std::string &type, js::Value event) {
  std::vector<int> path;
  for (int n = target; n >= 0; n = doc.nodes[n].parent)
    path.push_back(n);
  bool stop = false;
  for (int n : path) {
    for (auto &l : listeners_) {
      if (l.node != n || l.type != type)
        continue;
      js_->reset_fuel();
      try {
        js::Value result = js_->call(l.fn, {event}, js_->host_value(n));
        if (result.type == js::Value::Type::Boolean && !result.boolean)
          stop = true;
      } catch (js::JsError &e) {
        diagnostics.push_back({"event", e.value.to_string()});
      } catch (std::exception &e) {
        diagnostics.push_back({"event", e.what()});
      }
    }
    if (stop)
      break;
  }
  dirty_ = true;
}
void Page::activate(int target) {
  if (target < 0 || target >= int(doc.nodes.size()))
    return;
  auto event = js_->make_object();
  event.object->properties["type"] = js::Value("click");
  event.object->properties["target"] = js_->host_value(target);
  bool prevented = false;
  event.object->properties["preventDefault"] = js_->make_native([&prevented](js::Values &) {
    prevented = true;
    return js::Value();
  });
  event.object->properties["stopPropagation"] =
      js_->make_native([](js::Values &) { return js::Value(); });
  dispatch(target, "click", event);
  if (prevented)
    return;
  int n = target;
  while (n >= 0) {
    const auto &node = doc.nodes[n];
    if (node.name == "a") {
      request_navigation(node.attribute("href"));
      return;
    }
    if (node.name == "input" && lower(node.attribute("type")) == "checkbox") {
      std::string v;
      if (node.get_attribute("checked", v))
        std::erase_if(doc.nodes[n].attributes, [](auto &a) { return a.first == "checked"; });
      else
        doc.nodes[n].set_attribute("checked", "");
      dirty_ = true;
      return;
    }
    if (node.name == "button" ||
        (node.name == "input" &&
         (lower(node.attribute("type")) == "submit" || lower(node.attribute("type")) == "button" ||
          lower(node.attribute("type")) == "image"))) {
      int form = node.parent;
      while (form >= 0 && doc.nodes[form].name != "form")
        form = doc.nodes[form].parent;
      if (form >= 0)
        submit_form(form);
      return;
    }
    n = node.parent;
  }
}
void Page::click(double x, double y) {
  int target = hit(x, y);
  if (target >= 0) {
    focused_ = target;
    activate(target);
  }
}
void Page::key(const std::string &key, const std::string &text) {
  if (focused_ < 0 || focused_ >= int(doc.nodes.size()))
    return;
  auto &n = doc.nodes[focused_];
  if (n.name == "input" || n.name == "textarea") {
    auto type = lower(n.attribute("type"));
    if (type != "checkbox" && type != "radio" && type != "button" && type != "submit") {
      std::string value = n.name == "textarea" ? doc.text_content(focused_) : n.attribute("value");
      if (key == "Backspace" && !value.empty()) {
        auto cps = utf8_decode(value);
        cps.pop_back();
        value.clear();
        for (auto c : cps)
          value += utf8_encode(c);
      } else if (key == "Enter" && n.name == "textarea")
        value += '\n';
      else if (!text.empty())
        value += text;
      if (n.name == "textarea")
        doc.set_text(focused_, value);
      else
        n.set_attribute("value", value);
      auto event = js_->make_object();
      event.object->properties["type"] = js::Value("input");
      event.object->properties["target"] = js_->host_value(focused_);
      dispatch(focused_, "input", event);
      dirty_ = true;
    }
  }
  if (key == "Enter") {
    int form = n.parent;
    while (form >= 0 && doc.nodes[form].name != "form")
      form = doc.nodes[form].parent;
    if (form >= 0)
      submit_form(form);
  }
}
void Page::submit_form(int form) {
  const auto &f = doc.nodes[form];
  std::string method = lower(f.attribute("method", "get"));
  if (method != "get") {
    diagnostics.push_back({"forms", "Only GET form submission is implemented"});
    return;
  }
  std::string action = f.attribute("action");
  auto base = resolve_url(doc.url, action.empty() ? doc.url.href() : action);
  if (!base || !base->network())
    return;
  std::string query;
  for (int n : doc.query("*", form)) {
    const auto &input = doc.nodes[n];
    auto name = input.attribute("name");
    if (name.empty())
      continue;
    if ((input.name == "input" || input.name == "textarea" || input.name == "select")) {
      std::string value = input.name == "textarea" ? doc.text_content(n) : input.attribute("value");
      if (input.name == "select") {
        for (int o : input.children)
          if (doc.nodes[o].name == "option" && has_attr(doc.nodes[o], "selected"))
            value = doc.text_content(o);
      }
      if (!query.empty())
        query += '&';
      query += percent_encode(name, true) + "=" + percent_encode(value, true);
    }
  }
  base->query = query;
  navigations.push_back(base->href());
}
double Page::next_timer_delay() const {
  if (js_->timers.empty())
    return -1;
  double now = double(now_ms()), best = 86400000;
  for (auto &t : js_->timers)
    best = std::min(best, std::max(0.0, t.due - now));
  return best;
}
void Page::tick() {
  double now = double(now_ms());
  std::vector<js::Runtime::Timer> due;
  for (auto it = js_->timers.begin(); it != js_->timers.end();) {
    if (it->due <= now) {
      due.push_back(*it);
      it = js_->timers.erase(it);
    } else
      ++it;
  }
  for (auto &t : due) {
    js_->reset_fuel();
    try {
      js_->call(t.callback, t.args);
    } catch (js::JsError &e) {
      diagnostics.push_back({"timer", e.value.to_string()});
    } catch (std::exception &e) {
      diagnostics.push_back({"timer", e.what()});
    }
    // My promise callbacks run as soon as the timer job finishes.
    try {
      js_->drain_microtasks();
    } catch (js::JsError &e) {
      diagnostics.push_back({"microtask", e.value.to_string()});
    } catch (std::exception &e) {
      diagnostics.push_back({"microtask", e.what()});
    }
    dirty_ = true;
  }
}
} // namespace pv
