#include "Psycho_page.hpp"
#include <chrono>
#include <thread>
namespace pv {
static constexpr int kDocument = -1, kLocation = -3, kStorageLocal = -5, kStorageSession = -6,
                     kHistory = -7, kStyleBase = 1000000, kClassBase = 2000000,
                     kCanvasBase = 3000000, kGradientBase = 4000000,
                     kMseBase = 5000000, kBufferBase = 6000000;
// Cookies expire against wall-clock seconds, which is what a date header uses.
static double cookie_now() {
  return double(now_ms()) / 1000.0;
}
static bool has_attr(const Node &n, const std::string &key) {
  std::string v;
  return n.get_attribute(key, v);
}
// A buffered range is a small object with length, start(i) and end(i), which is
// the shape a page actually uses.
static js::Value time_ranges(js::Runtime &r, std::vector<std::pair<double, double>> spans) {
  js::Value out = r.make_object();
  out.object->host_type = "TimeRanges";
  out.object->properties["length"] = js::Value(double(spans.size()));
  out.object->properties["start"] = r.make_native([spans](js::Values &a) {
    size_t i = a.empty() ? 0 : size_t(std::max(0.0, a[0].to_number()));
    return js::Value(i < spans.size() ? spans[i].first : 0.0);
  });
  out.object->properties["end"] = r.make_native([spans](js::Values &a) {
    size_t i = a.empty() ? 0 : size_t(std::max(0.0, a[0].to_number()));
    return js::Value(i < spans.size() ? spans[i].second : 0.0);
  });
  return out;
}
class PageHost : public js::Host {
public:
  Page &page;
  explicit PageHost(Page &p) : page(p) {}
  js::Value node(int id) {
    return page.js_->host_value(id, id == kDocument ? "Document" : "HTMLElement");
  }
  // A media source and a source buffer carry their own listeners, and a function
  // I hold has to be a root or my collector frees it.
  js::Value listen(int id, const std::string &which) {
    return page.js_->make_native([this, id, which](js::Values &a) {
      if (a.empty())
        return js::Value();
      auto &registered = page.host_listeners_[id];
      if (which == "addEventListener") {
        if (a.size() > 1) {
          registered.push_back({id, a[0].to_string(), a[1]});
          if (a[1].object)
            page.js_->root(a[1].object);
        }
      } else {
        std::erase_if(registered, [&](auto &l) { return l.type == a[0].to_string(); });
      }
      return js::Value();
    });
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
  Storage &storage_for(int id) {
    return id == kStorageLocal ? page.local_storage() : page.session_storage();
  }
  js::Value storage_get(int id, const std::string &name) {
    auto &r = *page.js_;
    Storage &store = storage_for(id);
    Storage *store_ptr = &store;
    std::string origin = page.doc.url.origin();
    if (name == "length")
      return js::Value(double(store_ptr->length(origin)));
    if (name == "key")
      return r.make_native([store_ptr, origin](js::Values &a) {
        if (a.empty())
          return js::Value::null();
        auto key = store_ptr->key(origin, size_t(std::max(0.0, a[0].to_number())));
        return key.empty() ? js::Value::null() : js::Value(key);
      });
    if (name == "getItem")
      return r.make_native([store_ptr, origin](js::Values &a) {
        bool found = false;
        auto value = store_ptr->get(origin, a.empty() ? std::string() : a[0].to_string(), &found);
        return found ? js::Value(value) : js::Value::null();
      });
    if (name == "setItem")
      return r.make_native([store_ptr, origin](js::Values &a) {
        if (a.size() < 2)
          return js::Value();
        if (!store_ptr->set(origin, a[0].to_string(), a[1].to_string()))
          throw js::JsError{js::Value("QuotaExceededError: web storage is full for this site")};
        return js::Value();
      });
    if (name == "removeItem")
      return r.make_native([store_ptr, origin](js::Values &a) {
        if (!a.empty())
          store_ptr->remove(origin, a[0].to_string());
        return js::Value();
      });
    if (name == "clear")
      return r.make_native([store_ptr, origin](js::Values &) {
        store_ptr->clear(origin);
        return js::Value();
      });
    bool found = false;
    auto value = store_ptr->get(origin, name, &found);
    return found ? js::Value(value) : js::Value();
  }
  js::Value history_get(const std::string &name) {
    auto &r = *page.js_;
    if (name == "length")
      return js::Value(double(page.history_.size()));
    if (name == "state") {
      if (page.history_.empty() || page.history_index_ >= page.history_.size())
        return js::Value::null();
      return page.history_[page.history_index_].state;
    }
    if (name == "pushState" || name == "replaceState") {
      bool replace = name == "replaceState";
      return r.make_native([this, replace](js::Values &a) {
        step_history(a, replace);
        return js::Value();
      });
    }
    if (name == "back" || name == "forward" || name == "go") {
      long fixed = name == "back" ? -1 : (name == "forward" ? 1 : 0);
      bool relative = name == "go";
      return r.make_native([this, fixed, relative](js::Values &a) {
        long delta = relative ? long(a.empty() ? 0.0 : a[0].to_number()) : fixed;
        go_to(delta);
        return js::Value();
      });
    }
    return js::Value();
  }
  void step_history(js::Values &a, bool replace) {
    auto &d = page.doc;
    std::string target = a.size() > 2 ? a[2].to_string() : d.url.href();
    auto resolved = resolve_url(d.url, target);
    // A page may only rewrite its own history entry, never another origin's.
    if (!resolved || resolved->origin() != d.url.origin()) {
      if (page.diagnostics.size() < 200)
        page.diagnostics.push_back({"history", "I refused a history change to another origin"});
      return;
    }
    Page::HistoryEntry entry{resolved->href(), a.empty() ? js::Value::null() : a[0]};
    if (replace && !page.history_.empty()) {
      page.history_[page.history_index_] = entry;
    } else {
      page.history_.resize(page.history_index_ + 1);
      page.history_.push_back(entry);
      page.history_index_ = page.history_.size() - 1;
    }
    d.url = *resolved;
    page.dirty_ = true;
  }
  void go_to(long delta) {
    long target = long(page.history_index_) + delta;
    if (target < 0 || target >= long(page.history_.size()) || target == long(page.history_index_))
      return;
    page.history_index_ = size_t(target);
    if (auto url = parse_url(page.history_[page.history_index_].url))
      page.doc.url = *url;
    page.dirty_ = true;
    js::Value event = page.js_->make_object();
    event.object->properties["type"] = js::Value("popstate");
    event.object->properties["state"] = page.history_[page.history_index_].state;
    page.dispatch(page.doc.root, "popstate", event);
  }
  // ---- Canvas 2D ---------------------------------------------------------
  js::Value canvas_context(int node) {
    if (!page.canvases_.count(node)) {
      const auto &n = page.doc.nodes[node];
      auto size = [&](const char *name, int fallback) {
        auto text = trim(n.attribute(name));
        try {
          if (!text.empty()) {
            size_t used = 0;
            double value = std::stod(text, &used);
            if (used == text.size() && value > 0 && value <= 8192)
              return int(value);
          }
        } catch (...) {
        }
        return fallback;
      };
      page.canvases_.emplace(node, Canvas2D(size("width", 300), size("height", 150)));
    }
    return page.js_->host_value(kCanvasBase + node, "CanvasRenderingContext2D");
  }
  // I resolve whatever a script passes to drawImage: another canvas, or an
  // image element whose bytes I already decoded.
  bool image_argument(const js::Value &value, Image &out) {
    if (value.type != js::Value::Type::Object || !value.object ||
        value.object->kind != js::Object::Kind::Host)
      return false;
    int node = value.object->host;
    if (node < 0 || node >= int(page.doc.nodes.size()))
      return false;
    const auto &n = page.doc.nodes[node];
    if (n.name == "canvas") {
      auto it = page.canvases_.find(node);
      if (it == page.canvases_.end()) {
        out.width = 0;
        out.height = 0;
        return false;
      }
      out = it->second.snapshot();
      return out.valid();
    }
    if (n.name == "img") {
      auto resolved = resolve_url(page.doc.url, n.attribute("src"));
      std::string key = resolved ? resolved->href() : n.attribute("src");
      auto found = page.images_.find(key);
      if (found == page.images_.end() || !found->second.valid())
        return false;
      out = found->second;
      return true;
    }
    return false;
  }
  js::Value canvas_member(Canvas2D *c, const std::string &name, int canvas_node) {
    auto &r = *page.js_;
    auto number = [](js::Values &a, size_t at, double fallback = 0) {
      return a.size() > at ? a[at].to_number() : fallback;
    };
    if (name == "fillStyle") {
      if (c->state.has_gradient && c->state.fill_gradient_id >= 0)
        return r.host_value(kGradientBase + c->state.fill_gradient_id, "CanvasGradient");
      return js::Value(c->state.fill_style);
    }
    if (name == "strokeStyle")
      return js::Value(c->state.stroke_style);
    if (name == "lineWidth")
      return js::Value(c->state.line_width);
    if (name == "globalAlpha")
      return js::Value(c->state.global_alpha);
    if (name == "font")
      return js::Value(c->state.font);
    if (name == "textAlign")
      return js::Value(c->state.text_align);
    if (name == "canvas")
      return node(canvas_node);
    if (name == "beginPath")
      return r.make_native([c](js::Values &) {
        c->begin_path();
        return js::Value();
      });
    if (name == "closePath")
      return r.make_native([c](js::Values &) {
        c->close_path();
        return js::Value();
      });
    if (name == "moveTo")
      return r.make_native([c, number](js::Values &a) {
        c->move_to(number(a, 0), number(a, 1));
        return js::Value();
      });
    if (name == "lineTo")
      return r.make_native([c, number](js::Values &a) {
        c->line_to(number(a, 0), number(a, 1));
        return js::Value();
      });
    if (name == "rect")
      return r.make_native([c, number](js::Values &a) {
        c->add_rect(number(a, 0), number(a, 1), number(a, 2), number(a, 3));
        return js::Value();
      });
    if (name == "arc")
      return r.make_native([c, number](js::Values &a) {
        c->add_arc(number(a, 0), number(a, 1), number(a, 2), number(a, 3), number(a, 4),
                   a.size() < 6 || a[5].truthy());
        return js::Value();
      });
    if (name == "ellipse")
      return r.make_native([c, number](js::Values &a) {
        c->add_ellipse(number(a, 0), number(a, 1), number(a, 2), number(a, 3), number(a, 4),
                       number(a, 5), a.size() < 8 || a[7].truthy());
        return js::Value();
      });
    if (name == "quadraticCurveTo")
      return r.make_native([c, number](js::Values &a) {
        c->add_quadratic(number(a, 0), number(a, 1), number(a, 2), number(a, 3));
        return js::Value();
      });
    if (name == "bezierCurveTo")
      return r.make_native([c, number](js::Values &a) {
        c->add_bezier(number(a, 0), number(a, 1), number(a, 2), number(a, 3), number(a, 4),
                      number(a, 5));
        return js::Value();
      });
    if (name == "fill")
      return r.make_native([c](js::Values &) {
        c->fill();
        return js::Value();
      });
    if (name == "stroke")
      return r.make_native([c](js::Values &) {
        c->stroke();
        return js::Value();
      });
    if (name == "fillRect")
      return r.make_native([c, number](js::Values &a) {
        c->fill_rect(number(a, 0), number(a, 1), number(a, 2), number(a, 3));
        return js::Value();
      });
    if (name == "strokeRect")
      return r.make_native([c, number](js::Values &a) {
        c->stroke_rect(number(a, 0), number(a, 1), number(a, 2), number(a, 3));
        return js::Value();
      });
    if (name == "clearRect")
      return r.make_native([c, number](js::Values &a) {
        c->clear_rect(number(a, 0), number(a, 1), number(a, 2), number(a, 3));
        return js::Value();
      });
    if (name == "fillText")
      return r.make_native([c, number](js::Values &a) {
        c->fill_text(number(a, 1), number(a, 2), a.empty() ? std::string() : a[0].to_string());
        return js::Value();
      });
    if (name == "strokeText")
      return r.make_native([c, number](js::Values &a) {
        c->stroke_text(number(a, 1), number(a, 2), a.empty() ? std::string() : a[0].to_string());
        return js::Value();
      });
    if (name == "measureText")
      return r.make_native([this, c](js::Values &a) {
        auto text = a.empty() ? std::string() : a[0].to_string();
        auto out = page.js_->make_object();
        out.object->properties["width"] = js::Value(c->text_width(text));
        return out;
      });
    if (name == "save")
      return r.make_native([c](js::Values &) {
        c->save();
        return js::Value();
      });
    if (name == "restore")
      return r.make_native([c](js::Values &) {
        c->restore();
        return js::Value();
      });
    if (name == "translate")
      return r.make_native([c, number](js::Values &a) {
        c->translate(number(a, 0), number(a, 1));
        return js::Value();
      });
    if (name == "scale")
      return r.make_native([c, number](js::Values &a) {
        c->scale(number(a, 0), number(a, 1, 1));
        return js::Value();
      });
    if (name == "rotate")
      return r.make_native([c, number](js::Values &a) {
        c->rotate(number(a, 0));
        return js::Value();
      });
    if (name == "setTransform")
      return r.make_native([c, number](js::Values &a) {
        c->set_transform(number(a, 0, 1), number(a, 1), number(a, 2), number(a, 3, 1), number(a, 4),
                         number(a, 5));
        return js::Value();
      });
    if (name == "drawImage")
      return r.make_native([this, c, number](js::Values &a) {
        if (a.empty())
          return js::Value();
        Image source;
        if (!image_argument(a[0], source))
          return js::Value();
        // The three argument form draws at natural size, the five argument form
        // scales it, and the nine argument form takes a piece of the source.
        if (a.size() >= 9) {
          c->draw_image_rect(source, number(a, 1), number(a, 2), number(a, 3), number(a, 4),
                             number(a, 5), number(a, 6), number(a, 7), number(a, 8));
          return js::Value();
        }
        double x = number(a, 1), y = number(a, 2);
        double w = a.size() > 4 ? number(a, 3) : double(source.width);
        double h = a.size() > 4 ? number(a, 4) : double(source.height);
        c->draw_image(source, x, y, w, h);
        return js::Value();
      });
    if (name == "roundRect")
      return r.make_native([c, number](js::Values &a) {
        c->add_round_rect(number(a, 0), number(a, 1), number(a, 2), number(a, 3), number(a, 4));
        return js::Value();
      });
    if (name == "setLineDash")
      return r.make_native([this, c](js::Values &a) {
        std::vector<double> dashes;
        js::Values values;
        if (!a.empty() && page.js_->iterate(a[0], values))
          for (auto &value : values)
            dashes.push_back(value.to_number());
        c->set_line_dash(dashes);
        return js::Value();
      });
    if (name == "getLineDash")
      return r.make_native([this](js::Values &) {
        return page.js_->make_array({});
      });
    if (name == "toDataURL")
      return r.make_native([c](js::Values &) { return js::Value(c->data_url()); });
    if (name == "getImageData")
      return r.make_native([this, c, number](js::Values &a) {
        int x = int(number(a, 0)), y = int(number(a, 1));
        int w = int(number(a, 2)), h = int(number(a, 3));
        auto pixels = c->get_pixels(x, y, w, h);
        js::Values data;
        data.reserve(pixels.size());
        for (auto byte : pixels)
          data.push_back(js::Value(double(byte)));
        auto out = page.js_->make_object();
        out.object->properties["width"] = js::Value(double(std::max(1, w)));
        out.object->properties["height"] = js::Value(double(std::max(1, h)));
        out.object->properties["data"] = page.js_->make_array(data);
        return out;
      });
    if (name == "putImageData")
      return r.make_native([this, c, number](js::Values &a) {
        if (a.empty() || a[0].type != js::Value::Type::Object || !a[0].object)
          return js::Value();
        auto &image = a[0].object->properties;
        int w = int(image["width"].to_number()), h = int(image["height"].to_number());
        js::Values values;
        if (!page.js_->iterate(image["data"], values))
          return js::Value();
        std::vector<uint8_t> pixels;
        pixels.reserve(values.size());
        for (auto &v : values)
          pixels.push_back(uint8_t(std::clamp(v.to_number(), 0.0, 255.0)));
        c->put_pixels(int(number(a, 1)), int(number(a, 2)), w, h, pixels);
        return js::Value();
      });
    if (name == "createLinearGradient" || name == "createRadialGradient")
      return r.make_native([this, number, radial = name == "createRadialGradient"](js::Values &a) {
        Gradient gradient;
        gradient.radial = radial;
        if (radial) {
          gradient.x0 = number(a, 0);
          gradient.y0 = number(a, 1);
          gradient.r0 = number(a, 2);
          gradient.x1 = number(a, 3);
          gradient.y1 = number(a, 4);
          gradient.r1 = number(a, 5);
        } else {
          gradient.x0 = number(a, 0);
          gradient.y0 = number(a, 1);
          gradient.x1 = number(a, 2);
          gradient.y1 = number(a, 3);
        }
        int index = page.next_gradient_++;
        page.gradients_[index] = std::move(gradient);
        return page.js_->host_value(kGradientBase + index, "CanvasGradient");
      });
    return js::Value();
  }
  js::Value get(int id, const std::string &name) override {
    auto &r = *page.js_;
    // The canvas context and a gradient each answer for themselves.
    if (id >= kGradientBase && id < kGradientBase + 1000000) {
      int index = id - kGradientBase;
      if (name == "addColorStop")
        return r.make_native([this, index](js::Values &a) {
          auto it = page.gradients_.find(index);
          if (a.size() < 2 || it == page.gradients_.end())
            return js::Value();
          Color color = parse_color(a[1].to_string());
          it->second.stops.push_back({std::clamp(a[0].to_number(), 0.0, 1.0), color.bgra()});
          return js::Value();
        });
      return js::Value();
    }
    if (id >= kCanvasBase && id < kCanvasBase + 1000000) {
      auto it = page.canvases_.find(id - kCanvasBase);
      return it == page.canvases_.end() ? js::Value()
                                        : canvas_member(&it->second, name, id - kCanvasBase);
    }
    auto &d = page.doc;
    if (id == kStorageLocal || id == kStorageSession)
      return storage_get(id, name);
    if (id == kHistory)
      return history_get(name);
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
      if (name == "cookie")
        return js::Value(page.cookies().document_string(d.url, cookie_now()));
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
    if (id >= kClassBase && id < kCanvasBase) {
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
    if (id >= kMseBase && id < kMseBase + 1000000) {
      auto it = page.mse_objects_.find(id - kMseBase);
      if (it == page.mse_objects_.end())
        return js::Value();
      auto state = it->second;
      if (name == "addSourceBuffer")
        return r.make_native([this, id, state](js::Values &a) {
          std::string type = a.empty() ? std::string() : a[0].to_string();
          std::string error;
          if (!state->add_source_buffer(type, error))
            throw js::JsError{js::Value(error)};
          int handle = page.next_mse_++;
          page.mse_buffers_[handle] = {id - kMseBase, state->buffer_count() - 1};
          return page.js_->host_value(kBufferBase + handle, "SourceBuffer");
        });
      if (name == "endOfStream")
        return r.make_native([state](js::Values &) {
          std::string error;
          state->end_of_stream(error);
          return js::Value();
        });
      if (name == "duration")
        return js::Value(state->duration());
      if (name == "readyState")
        return js::Value(state->ended() ? "ended" : "open");
      if (name == "addEventListener" || name == "removeEventListener")
        return listen(id, name);
      return js::Value();
    }
    if (id >= kBufferBase && id < kBufferBase + 1000000) {
      auto found = page.mse_buffers_.find(id - kBufferBase);
      if (found == page.mse_buffers_.end())
        return js::Value();
      auto owner = page.mse_objects_.find(found->second.first);
      if (owner == page.mse_objects_.end())
        return js::Value();
      SourceBuffer *buffer = owner->second->buffer(found->second.second);
      if (!buffer)
        return js::Value();
      if (name == "appendBuffer")
        return r.make_native([this, id, buffer](js::Values &a) {
          if (a.empty() || a[0].type != js::Value::Type::Object || !a[0].object ||
              !a[0].object->bytes)
            throw js::JsError{
                js::Value("TypeError: appendBuffer needs an ArrayBuffer or a Uint8Array")};
          auto source = a[0].object;
          size_t offset = source->host_type == "ArrayBuffer" ? 0 : source->byte_offset;
          size_t length = source->host_type == "ArrayBuffer" ? source->bytes->size() : source->length;
          if (offset > source->bytes->size())
            offset = source->bytes->size();
          length = std::min(length, source->bytes->size() - offset);
          std::string error;
          if (!buffer->append(source->bytes->data() + offset, length, error))
            throw js::JsError{js::Value(error)};
          // The element attached to this source can play the new bytes now, and it
          // needs one more pass to pick them up.
          page.load_media_nodes();
          page.media_wake_ = true;
          page.dispatch_host(id, "updateend");
          page.dirty_ = true;
          return js::Value();
        });
      if (name == "abort")
        return r.make_native([buffer](js::Values &) {
          std::string error;
          buffer->abort(error);
          return js::Value();
        });
      if (name == "remove")
        return r.make_native([buffer](js::Values &a) {
          std::string error;
          double start = a.size() > 0 ? a[0].to_number() : 0;
          double end = a.size() > 1 ? a[1].to_number() : 0;
          if (!buffer->remove(start, end, error))
            throw js::JsError{js::Value(error)};
          return js::Value();
        });
      if (name == "buffered")
        return time_ranges(r, buffer->buffered());
      if (name == "mode")
        return js::Value("segments");
      if (name == "updating")
        return js::Value(false);
      if (name == "timestampOffset")
        return js::Value(0.0);
      if (name == "addEventListener" || name == "removeEventListener")
        return listen(id, name);
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
    if (n.type == Node::Type::Element && (n.name == "video" || n.name == "audio")) {
      MediaPlayer *player = page.player_for(id);
      if (name == "src")
        return js::Value(n.attribute("src"));
      if (name == "currentSrc")
        return js::Value(page.media_src_.count(id) ? page.media_src_[id] : std::string());
      if (name == "currentTime")
        return js::Value(player ? player->current_time() : 0.0);
      if (name == "duration")
        return js::Value(player && player->ready() ? player->duration() : 0.0);
      if (name == "paused")
        return js::Value(player ? player->paused() : true);
      if (name == "ended")
        return js::Value(player ? player->ended() : false);
      if (name == "volume")
        return js::Value(player ? player->volume() : 1.0);
      if (name == "muted")
        return js::Value(player ? player->muted() : false);
      if (name == "loop")
        return js::Value(player ? player->loop() : false);
      if (name == "playbackRate")
        return js::Value(player ? player->rate() : 1.0);
      if (name == "readyState")
        return js::Value(double(player ? player->ready_state() : 0));
      if (name == "videoWidth" || name == "videoHeight") {
        const Image *frame = player ? player->frame() : nullptr;
        return js::Value(double(frame ? (name == "videoWidth" ? frame->width : frame->height) : 0));
      }
      if (name == "error") {
        auto known = page.media_errors_.find(id);
        if (known == page.media_errors_.end())
          return js::Value::null();
        js::Value error = r.make_object();
        error.object->host_type = "MediaError";
        error.object->properties["code"] = js::Value(4.0);
        error.object->properties["message"] = js::Value(known->second);
        return error;
      }
      if (name == "buffered")
        return time_ranges(r, player && player->ready()
                                  ? std::vector<std::pair<double, double>>{{0.0, player->duration()}}
                                  : std::vector<std::pair<double, double>>{});
      if (name == "play")
        return r.make_native([this, id](js::Values &) {
          auto &rt = *page.js_;
          js::Value promise = rt.make_promise();
          if (page.block_autoplay && !page.user_active_recently()) {
            // Scripted playback without a gesture is refused with a rejected
            // promise, which is exactly what a page is told to expect.
            page.dispatch(id, "pause", rt.make_object());
            rt.settle_promise(promise.object, 0,
                              js::Value());
            return promise;
          }
          MediaPlayer *player = page.player_for(id);
          if (!player) {
            page.update_media(id);
            player = page.player_for(id);
          }
          if (!player || !player->ready()) {
            // I resolve now and start the moment the bytes arrive, which is the
            // order a page that calls play() early expects.
            page.media_wants_play_.insert(id);
            rt.settle_promise(promise.object, 1, js::Value());
            return promise;
          }
          player->play(double(now_ms()) / 1000.0);
          page.media_revisions_[id] = uint64_t(page.next_media_revision_++);
          page.dispatch(id, "play", rt.make_object());
          page.dispatch(id, "playing", rt.make_object());
          rt.settle_promise(promise.object, 1, js::Value());
          return promise;
        });
      if (name == "pause")
        return r.make_native([this, id](js::Values &) {
          MediaPlayer *player = page.player_for(id);
          if (player && player->ready() && !player->paused()) {
            player->pause(double(now_ms()) / 1000.0);
            page.media_revisions_[id] = uint64_t(page.next_media_revision_++);
            page.dispatch(id, "pause", page.js_->make_object());
          }
          return js::Value();
        });
      if (name == "load")
        return r.make_native([this, id](js::Values &) {
          page.media_.erase(id);
          page.media_src_.erase(id);
          page.media_errors_.erase(id);
          page.update_media(id);
          page.dirty_ = true;
          return js::Value();
        });
      if (name == "canPlayType")
        return r.make_native([](js::Values &a) {
          std::string type = a.empty() ? std::string() : a[0].to_string();
          return js::Value(media_type_supported(type) ? "probably" : "");
        });
    }
    if (name == "href" || name == "src")
      return js::Value(n.attribute(name));
    if (n.name == "canvas" && (name == "width" || name == "height")) {
      auto it = page.canvases_.find(id);
      int bitmap = name == "width" ? (it == page.canvases_.end() ? 300 : it->second.width())
                                   : (it == page.canvases_.end() ? 150 : it->second.height());
      return js::Value(double(bitmap));
    }
    if (n.name == "canvas" && name == "getContext")
      return r.make_native([this, id](js::Values &a) {
        if (a.empty() || lower(a[0].to_string()) != "2d")
          return js::Value::null();
        return canvas_context(id);
      });
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
    if (id >= kMseBase && id < kMseBase + 1000000) {
      auto it = page.mse_objects_.find(id - kMseBase);
      if (it == page.mse_objects_.end())
        return false;
      if (name == "duration") {
        it->second->set_duration(value.to_number());
        return true;
      }
      return false;
    }
    if (id >= kCanvasBase && id < kCanvasBase + 1000000) {
      auto it = page.canvases_.find(id - kCanvasBase);
      if (it == page.canvases_.end())
        return false;
      Canvas2D &c = it->second;
      if (name == "fillStyle" || name == "strokeStyle") {
        bool gradient = value.type == js::Value::Type::Object && value.object &&
                        value.object->kind == js::Object::Kind::Host &&
                        value.object->host_type == "CanvasGradient";
        if (gradient) {
          auto found = page.gradients_.find(value.object->host - kGradientBase);
          if (found != page.gradients_.end() && name == "fillStyle") {
            c.state.fill_gradient = found->second;
            c.state.fill_gradient_id = value.object->host - kGradientBase;
            c.state.has_gradient = true;
            return true;
          }
          return false;
        }
        Color color = parse_color(value.to_string());
        if (name == "fillStyle") {
          c.state.fill = color.bgra();
          c.state.fill_style = value.to_string();
          c.state.has_gradient = false;
          c.state.fill_gradient_id = -1;
        } else {
          c.state.stroke = color.bgra();
          c.state.stroke_style = value.to_string();
        }
        return true;
      }
      if (name == "lineWidth") {
        c.state.line_width = std::max(0.0, value.to_number());
        return true;
      }
      if (name == "globalAlpha") {
        c.state.global_alpha = std::clamp(value.to_number(), 0.0, 1.0);
        return true;
      }
      if (name == "font") {
        c.state.font = value.to_string();
        return true;
      }
      if (name == "textAlign") {
        c.state.text_align = lower(value.to_string());
        return true;
      }
      return false;
    }
    if (id == kStorageLocal || id == kStorageSession) {
      if (!storage_for(id).set(d.url.origin(), name, value.to_string()))
        throw js::JsError{js::Value("QuotaExceededError: web storage is full for this site")};
      return true;
    }
    if (id == kDocument && name == "cookie") {
      page.cookies().set_from_document(d.url, value.to_string(), cookie_now());
      return true;
    }
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
    if (n.name == "canvas" && (name == "width" || name == "height")) {
      auto &ctx = page.canvases_[id];
      int w = name == "width" ? int(value.to_number()) : ctx.width();
      int h = name == "height" ? int(value.to_number()) : ctx.height();
      ctx.resize(std::clamp(w, 1, 8192), std::clamp(h, 1, 8192));
      page.dirty_ = true;
      return true;
    }
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
    if (n.type == Node::Type::Element && (n.name == "video" || n.name == "audio")) {
      MediaPlayer *player = page.player_for(id);
      double now = double(now_ms()) / 1000.0;
      if (name == "currentTime") {
        if (!player || !player->ready())
          return false;
        bool was_playing = !player->paused();
        player->seek(std::max(0.0, value.to_number()), now);
        if (was_playing)
          player->play(now);
        page.media_revisions_[id] = uint64_t(page.next_media_revision_++);
        page.dispatch(id, "timeupdate", page.js_->make_object());
        page.dirty_ = true;
        return true;
      }
      if (name == "volume") {
        if (player)
          player->set_volume(value.to_number());
        return true;
      }
      if (name == "muted") {
        if (player)
          player->set_muted(value.truthy());
        return true;
      }
      if (name == "loop") {
        if (player)
          player->set_loop(value.truthy());
        return true;
      }
      if (name == "playbackRate") {
        if (player)
          player->set_rate(value.to_number());
        return true;
      }
      if (name == "src") {
        n.set_attribute("src", value.to_string());
        // A new source means a new player, which is what a load does.
        page.media_.erase(id);
        page.media_src_.erase(id);
        page.media_errors_.erase(id);
        page.media_wake_ = true;
        page.update_media(id);
        page.dirty_ = true;
        return true;
      }
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
CookieJar &Page::cookies() {
  return *jar_;
}
void Page::use_cookies(CookieJar *jar) {
  jar_ = jar ? jar : &own_cookies_;
}
Storage &Page::local_storage() {
  return *local_;
}
void Page::use_local_storage(Storage *storage) {
  local_ = storage ? storage : &own_local_;
}
void Page::set_history(std::vector<HistoryEntry> entries, size_t index) {
  history_ = std::move(entries);
  history_index_ = history_.empty() ? 0 : std::min(index, history_.size() - 1);
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
  js_->global().properties["localStorage"] = js_->host_value(kStorageLocal, "Storage");
  js_->global().properties["sessionStorage"] = js_->host_value(kStorageSession, "Storage");
  js_->global().properties["history"] = js_->host_value(kHistory, "History");
  // A fresh document starts with one history entry, the page itself.
  history_.clear();
  history_.push_back(HistoryEntry{url.href(), js::Value::null()});
  history_index_ = 0;
  posts.clear();
  media_.clear();
  media_images_.clear();
  media_revisions_.clear();
  media_src_.clear();
  media_errors_.clear();
  media_data_.clear();
  media_sources_.clear();
  mse_objects_.clear();
  mse_buffers_.clear();
  mse_opened_.clear();
  media_wants_play_.clear();
  host_listeners_.clear();
  js_->set_pump([this] { return pump_for_await(); });
  install_fetch();
  install_media();
  js_->global().properties["alert"] = js_->make_native([this](js::Values &a) {
    if (dialogs.size() < 8)
      dialogs.push_back(a.empty() ? "" : a[0].to_string());
    return js::Value();
  });
  if (script_bootstrap_) {
    try {
      script_bootstrap_(*js_);
    } catch (const std::exception &error) {
      if (diagnostics.size() < 200)
        diagnostics.push_back({"app-bootstrap", error.what()});
    }
  }
  int base = doc.query_one("base[href]");
  if (base >= 0) {
    auto u = resolve_url(url, doc.nodes[base].attribute("href"));
    if (u)
      doc.url = *u;
  }
  dirty_ = true;
  run_scripts();
  load_media_nodes();
}
// I turn my own JSON tree into script values so json() needs no second parser.
static js::Value from_json(js::Runtime &r, const Json &json) {
  switch (json.type) {
  case Json::Type::Bool:
    return js::Value(json.boolean);
  case Json::Type::Number:
    return js::Value(json.number);
  case Json::Type::String:
    return js::Value(json.string);
  case Json::Type::Array: {
    js::Values items;
    for (auto &item : json.array)
      items.push_back(from_json(r, item));
    return r.make_array(items);
  }
  case Json::Type::Object: {
    js::Value object = r.make_object();
    for (auto &entry : json.object)
      object.object->properties[entry.first] = from_json(r, entry.second);
    return object;
  }
  case Json::Type::Null:
  default:
    return js::Value::null();
  }
}
// A body is already in hand, so the promise it hands back is already settled.
static js::Value text_promise(js::Runtime &rt, const std::string &text) {
  js::Value promise = rt.make_promise();
  rt.settle_promise(promise.object, 1, js::Value(text));
  return promise;
}
void Page::install_fetch() {
  if (!js_)
    return;
  js_->global().properties["fetch"] = js_->make_native([this](js::Values &a) {
    auto &r = *js_;
    js::Value promise = r.make_promise();
    if (a.empty()) {
      r.settle_promise(promise.object, 2, js::Value("TypeError: fetch needs a URL"));
      return promise;
    }
    auto target = resolve_url(doc.url, a[0].to_string());
    if (!target || !target->network() || !fetcher) {
      r.settle_promise(promise.object, 2, js::Value("TypeError: this URL cannot be fetched"));
      return promise;
    }
    std::string href = target->href();
    Resource res;
    try {
      res = fetcher(href);
    } catch (const std::exception &e) {
      res = Resource();
      res.url = href;
      res.ok = false;
      res.error = e.what();
    }
    // A response I received resolves even when its status is an error status;
    // only a request that never produced one rejects.
    bool received = res.status >= 100;
    if (!res.ok && !received) {
      r.settle_promise(promise.object, 2,
                       js::Value("TypeError: " + (res.error.empty() ? "network error" : res.error)));
      return promise;
    }
    std::string body = res.text;
    if (body.empty() && !res.bytes.empty())
      body.assign(res.bytes.begin(), res.bytes.end());
    js::Value response = r.make_object();
    response.object->host_type = "Response";
    response.object->properties["url"] = js::Value(href);
    response.object->properties["ok"] = js::Value(res.status >= 200 && res.status < 300);
    response.object->properties["status"] = js::Value(double(res.status));
    response.object->properties["statusText"] = js::Value(res.status == 200 ? "OK" : "");
    response.object->properties["redirected"] = js::Value(false);
    response.object->properties["type"] = js::Value("basic");
    js::Value headers = r.make_object();
    headers.object->host_type = "Headers";
    headers.object->properties["get"] = r.make_native([res](js::Values &args) {
      std::string name = lower(args.empty() ? "" : args[0].to_string());
      if (name == "content-type")
        return js::Value(res.mime);
      if (name == "content-length")
        return js::Value(std::to_string(res.text.empty() ? res.bytes.size() : res.text.size()));
      return js::Value::null();
    });
    response.object->properties["headers"] = headers;
    response.object->properties["text"] =
        r.make_native([this, body](js::Values &) { return text_promise(*js_, body); });
    // A page that wants the bytes themselves gets a real ArrayBuffer rather than a
    // string, which is what a media append needs.
    Bytes body_bytes = res.bytes.empty() ? Bytes(body.begin(), body.end()) : res.bytes;
    response.object->properties["arrayBuffer"] =
        r.make_native([this, body_bytes](js::Values &) {
          js::Value promise = js_->make_promise();
          js::Value buffer = js_->make_object();
          buffer.object->host_type = "ArrayBuffer";
          buffer.object->bytes = std::make_shared<std::vector<uint8_t>>(body_bytes);
          buffer.object->properties["byteLength"] = js::Value(double(body_bytes.size()));
          js_->settle_promise(promise.object, 1, buffer);
          return promise;
        });
    response.object->properties["json"] = r.make_native([this, body](js::Values &) {
      auto parsed = Json::parse(body);
      if (!parsed)
        return text_promise(*js_, std::string());
      js::Value p = js_->make_promise();
      js_->settle_promise(p.object, 1, from_json(*js_, *parsed));
      return p;
    });
    r.settle_promise(promise.object, 1, response);
    return promise;
  });
}
void Page::dispatch_host(int id, const std::string &type) {
  auto found = host_listeners_.find(id);
  if (found == host_listeners_.end())
    return;
  js::Value event = js_->make_object();
  event.object->properties["type"] = js::Value(type);
  event.object->properties["target"] = js_->host_value(id, "EventTarget");
  for (auto &listener : found->second) {
    if (listener.type != type)
      continue;
    js_->reset_fuel();
    try {
      js_->call(listener.fn, {event}, js_->host_value(id, "EventTarget"));
    } catch (js::JsError &e) {
      diagnostics.push_back({"event", e.value.to_string()});
    } catch (std::exception &e) {
      diagnostics.push_back({"event", e.what()});
    }
  }
  // Work a listener queued, which is how a fetch inside sourceopen reaches the
  // source buffer in the same task, runs before I go back to the page.
  try {
    js_->drain_microtasks();
  } catch (js::JsError &e) {
    diagnostics.push_back({"microtask", e.value.to_string()});
  } catch (std::exception &e) {
    diagnostics.push_back({"microtask", e.what()});
  }
  dirty_ = true;
}
// A media source opens once the page has had a chance to listen for it, which is
// the same order a real task queue gives a page.
void Page::open_pending_media_sources() {
  for (auto &[handle, state] : mse_objects_) {
    (void)state;
    if (!mse_opened_.insert(handle).second)
      continue;
    dispatch_host(kMseBase + handle, "sourceopen");
  }
}
void Page::install_media() {
  if (!js_)
    return;
  auto &r = *js_;
  js_->global().properties["MediaSource"] = r.make_native([this](js::Values &) {
    int handle = next_mse_++;
    mse_objects_[handle] = std::make_shared<MediaSourceState>();
    return js_->host_value(kMseBase + handle, "MediaSource");
  });
  // Audio() is the one-line way a page makes an element, so it is here too.
  js_->global().properties["Audio"] = r.make_native([this](js::Values &a) {
    int id = doc.create(Node::Type::Element, "audio");
    if (!a.empty())
      doc.nodes[id].set_attribute("src", a[0].to_string());
    dirty_ = true;
    return js_->host_value(id, "HTMLElement");
  });
  js::Value url = r.make_object();
  url.object->host_type = "URL";
  url.object->properties["createObjectURL"] = r.make_native([this](js::Values &a) {
    if (a.empty() || a[0].type != js::Value::Type::Object || !a[0].object ||
        a[0].object->host < kMseBase || a[0].object->host >= kMseBase + 1000000)
      throw js::JsError{js::Value("TypeError: createObjectURL needs a MediaSource here")};
    auto found = mse_objects_.find(a[0].object->host - kMseBase);
    if (found == mse_objects_.end())
      throw js::JsError{js::Value("TypeError: that MediaSource is gone")};
    return js::Value(create_media_object_url(found->second));
  });
  url.object->properties["revokeObjectURL"] = r.make_native([this](js::Values &a) {
    if (!a.empty())
      revoke_media_object_url(a[0].to_string());
    return js::Value();
  });
  js_->global().properties["URL"] = url;
}
void Page::request(std::string input, ResourceType type) {
  auto u = resolve_url(doc.url, input);
  if (!u || !u->network())
    return;
  auto href = u->href();
  if (requested_.count(href) || requested_.size() >= kMaxResources)
    return;
  requested_.insert(href);
  requests.push_back({href, type});
}
void Page::resource(const Resource &res) {
  Resource stored = res;
  static constexpr uint8_t png_signature[8] = {137, 80, 78, 71, 13, 10, 26, 10};
  static constexpr uint8_t jpeg_signature[3] = {0xff, 0xd8, 0xff};
  auto mime = lower(stored.mime);
  auto starts = [&](const uint8_t *signature, size_t length) {
    return stored.bytes.size() >= length &&
           std::equal(signature, signature + length, stored.bytes.begin());
  };
  auto looks_like_svg = [&]() {
    if (mime.find("image/svg") != std::string::npos)
      return true;
    std::string head(stored.bytes.begin(),
                     stored.bytes.begin() + std::ptrdiff_t(std::min<size_t>(stored.bytes.size(), 512)));
    auto at = head.find("<svg");
    return at != std::string::npos && head.find("<?xml") != std::string::npos;
  };
  // WebP is a format I have not written a decoder for yet. I say so once per
  // file instead of leaving a silent hole where a picture should be.
  auto looks_like_webp = [&]() {
    if (mime.find("image/webp") != std::string::npos)
      return true;
    return stored.bytes.size() > 12 && std::equal(stored.bytes.begin(), stored.bytes.begin() + 4,
                                                  "RIFF") &&
           std::equal(stored.bytes.begin() + 8, stored.bytes.begin() + 12, "WEBP");
  };
  if (looks_like_webp() && !webp_reported_.count(stored.url)) {
    webp_reported_.insert(stored.url);
    diagnostics.push_back(
        {"image", "I cannot decode WebP yet, so this picture is empty: " + stored.url});
  }
  bool png = mime.find("image/png") != std::string::npos || starts(png_signature, 8);
  bool jpeg = mime.find("image/jpeg") != std::string::npos ||
              mime.find("image/JPEG") != std::string::npos || starts(jpeg_signature, 3);
  bool svg = looks_like_svg();
  if (stored.ok && (png || jpeg || svg)) {
    try {
      Image image;
      std::string kind = png ? "PNG" : jpeg ? "JPEG" : "SVG";
      if (png)
        image = decode_png(stored.bytes);
      else if (jpeg)
        image = decode_jpeg(stored.bytes);
      else {
        std::string text(stored.bytes.begin(), stored.bytes.end());
        image = render_svg(text, int(stored.image_width), int(stored.image_height));
      }
      stored.image_width = image.width;
      stored.image_height = image.height;
      images_[stored.url] = std::move(image);
    } catch (const std::exception &e) {
      stored.ok = false;
      stored.error = std::string(png ? "PNG" : jpeg ? "JPEG" : "SVG") + " decode failed: " + e.what();
      diagnostics.push_back({"image", stored.error});
    }
  }
  resources_[stored.url] = std::move(stored);
  dirty_ = true;
  run_scripts();
  load_media_nodes();
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
          request(src, ResourceType::Script);
          if (!has_attr(n, "async") && !has_attr(n, "defer"))
            return;
        } else if (it->second.ok)
          execute_script(n.id);
        else
          executed_.insert(n.id);
      }
    } else if (n.name == "link" && lower(n.attribute("rel")) == "stylesheet")
      request(n.attribute("href"), ResourceType::Stylesheet);
    else if (n.name == "img")
      request(n.attribute("src"), ResourceType::Image);
    else if (n.name == "video" || n.name == "audio") {
      if (!n.attribute("src").empty())
        request(n.attribute("src"), ResourceType::Media);
      for (int c : n.children)
        if (doc.nodes[c].name == "source" && !doc.nodes[c].attribute("src").empty())
          request(doc.nodes[c].attribute("src"), ResourceType::Media);
    }
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
  // I only copy pixels for a canvas that has been drawn on since the last frame.
  for (auto &[id, context] : canvases_) {
    auto seen = canvas_revisions_.find(id);
    if (seen != canvas_revisions_.end() && seen->second == context.revision)
      continue;
    canvas_images_[id] = context.snapshot();
    canvas_revisions_[id] = context.revision;
  }
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
    j.set("gradient", c.gradient);
    j.set("fixed", c.fixed);
    paint.array.push_back(std::move(j));
  }
  // A video or an audio element draws its current frame and its controls over the
  // box layout reserved for it, so a later box still paints on top of an earlier
  // one.
  for (const auto &[node, player] : media_) {
    (void)player;
    size_t at = paint.array.size();
    for (size_t i = paint.array.size(); i > 0; --i) {
      const Json &command = paint.array[i - 1];
      if (command.num("node", -1) == node &&
          command.num("kind", -1) == int(PaintCommand::Kind::Rect)) {
        at = i;
        break;
      }
    }
    media_command(node, paint, at);
  }
  frame.set("paint", std::move(paint));
  // The real laid-out box for each element. The paint list is not a layout
  // report: a box with no background paints nothing, so anything that needs a
  // box must read it from here.
  Json boxes = Json::make_array();
  for (auto &[id, r] : layout_.boxes) {
    if (boxes.array.size() >= 20000)
      break;
    Json j = Json::make_object();
    j.set("node", id);
    j.set("x", r.x);
    j.set("y", r.y);
    j.set("w", r.w);
    j.set("h", r.h);
    boxes.array.push_back(std::move(j));
  }
  frame.set("boxes", std::move(boxes));
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
  // Microtasks belonging to this event run before I return to the page, which is
  // the order a browser gives a task and its promise callbacks.
  try {
    js_->drain_microtasks();
  } catch (js::JsError &e) {
    diagnostics.push_back({"microtask", e.value.to_string()});
  } catch (std::exception &e) {
    diagnostics.push_back({"microtask", e.what()});
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
  for (int n = target; n >= 0 && n < int(doc.nodes.size()); n = doc.nodes[n].parent) {
    const auto &node = doc.nodes[n];
    if (node.type == Node::Type::Element && (node.name == "video" || node.name == "audio")) {
      toggle_media(n);
      return;
    }
  }
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
  note_user_activation();
  int target = hit(x, y);
  // A media element owns everything inside its box: the strip along the bottom is
  // the scrub bar, and the rest of the box plays or pauses it.
  for (int n = target; n >= 0 && n < int(doc.nodes.size()); n = doc.nodes[n].parent) {
    const auto &node = doc.nodes[n];
    if (node.type != Node::Type::Element || (node.name != "video" && node.name != "audio"))
      continue;
    auto box = layout_.boxes.find(n);
    if (box == layout_.boxes.end())
      break;
    const Rect &r = box->second;
    focused_ = n;
    double strip = std::min(30.0, r.h * 0.5);
    bool controls = r.h >= 24 && r.w >= 48 && y - r.y >= r.h - strip;
    if (controls && r.w >= 100) {
      double bar_x = 30, bar_w = std::max(10.0, r.w - 30 - 78);
      double rel_x = x - r.x;
      if (rel_x >= bar_x && rel_x <= bar_x + bar_w) {
        seek_media(n, (rel_x - bar_x) / bar_w);
        return;
      }
    }
    toggle_media(n);
    return;
  }
  if (target >= 0) {
    focused_ = target;
    activate(target);
  }
}
void Page::key(const std::string &key, const std::string &text) {
  if (key == "Space") {
    int node = focused_;
    bool usable = node >= 0 && node < int(doc.nodes.size()) && doc.nodes[node].type == Node::Type::Element &&
                  (doc.nodes[node].name == "video" || doc.nodes[node].name == "audio");
    if (!usable) {
      node = -1;
      for (auto &n : doc.nodes)
        if (n.type == Node::Type::Element && (n.name == "video" || n.name == "audio") &&
            media_.count(n.id)) {
          node = n.id;
          break;
        }
    }
    if (node >= 0 && media_.count(node)) {
      toggle_media(node);
      return;
    }
  }
  if (focused_ < 0 || focused_ >= int(doc.nodes.size()))
    return;
  if (!key.empty() || !text.empty()) {
    std::string event_key = key.empty() ? text : key;
    if (event_key == "Space") event_key = " ";
    js::Value event = js_->make_object();
    event.object->properties["type"] = js::Value("keydown");
    event.object->properties["key"] = js::Value(event_key);
    event.object->properties["target"] = js_->host_value(focused_);
    dispatch(focused_, "keydown", event);
    if (focused_ < 0 || focused_ >= int(doc.nodes.size()))
      return;
  }
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
// A form body is encoded the way a browser does it, where a space becomes a
// plus and everything outside the unreserved set is escaped.
static std::string form_encode(const std::string &text) {
  static const char *hex = "0123456789ABCDEF";
  std::string out;
  for (unsigned char c : text) {
    if (std::isalnum(c) || c == '-' || c == '_' || c == '.' || c == '*')
      out += char(c);
    else if (c == ' ')
      out += '+';
    else {
      out += '%';
      out += hex[c >> 4];
      out += hex[c & 15];
    }
  }
  return out;
}
void Page::submit_form(int form) {
  const auto &f = doc.nodes[form];
  std::string method = lower(f.attribute("method", "get"));
  std::string action = f.attribute("action");
  auto base = resolve_url(doc.url, action.empty() ? doc.url.href() : action);
  if (!base || !base->network())
    return;
  std::vector<std::pair<std::string, std::string>> fields;
  for (int n : doc.query("*", form)) {
    const auto &input = doc.nodes[n];
    if (input.type != Node::Type::Element)
      continue;
    auto name = input.attribute("name");
    // A control that cannot be typed into is not submitted either.
    if (name.empty() || has_attr(input, "disabled"))
      continue;
    if (input.name == "input") {
      auto type = lower(input.attribute("type", "text"));
      // A submit button does not send its own value, and an unchecked box sends
      // nothing at all.
      if (type == "submit" || type == "button" || type == "image" || type == "reset")
        continue;
      if ((type == "checkbox" || type == "radio") && !has_attr(input, "checked"))
        continue;
      // A ticked box with no value of its own submits the word "on".
      auto value = input.attribute("value");
      if (value.empty() && (type == "checkbox" || type == "radio"))
        value = "on";
      fields.emplace_back(name, value);
    } else if (input.name == "textarea") {
      fields.emplace_back(name, doc.text_content(n));
    } else if (input.name == "select") {
      // A multiple select sends every chosen option, a single one sends the
      // first chosen option and falls back to the first option at all.
      bool multiple = has_attr(input, "multiple");
      std::vector<std::string> chosen;
      for (int o : input.children) {
        const auto &option = doc.nodes[o];
        if (option.name != "option" || !has_attr(option, "selected"))
          continue;
        chosen.push_back(has_attr(option, "value") ? option.attribute("value")
                                                   : doc.text_content(o));
        if (!multiple)
          break;
      }
      if (chosen.empty())
        for (int o : input.children)
          if (doc.nodes[o].name == "option") {
            chosen.push_back(has_attr(doc.nodes[o], "value") ? doc.nodes[o].attribute("value")
                                                             : doc.text_content(o));
            break;
          }
      for (auto &value : chosen)
        fields.emplace_back(name, value);
    }
  }
  if (method == "post") {
    FormPost post;
    post.url = base->href();
    post.method = "POST";
    std::string enctype = lower(f.attribute("enctype", "application/x-www-form-urlencoded"));
    if (enctype == "multipart/form-data") {
      std::string boundary = "----PsychoVaultForm" + std::to_string(now_ms());
      for (const auto &[name, value] : fields) {
        post.body += "--" + boundary + "\r\n";
        post.body += "Content-Disposition: form-data; name=\"" + name + "\"\r\n\r\n";
        post.body += value + "\r\n";
      }
      post.body += "--" + boundary + "--\r\n";
      post.content_type = "multipart/form-data; boundary=" + boundary;
    } else if (enctype == "text/plain") {
      for (const auto &[name, value] : fields)
        post.body += name + "=" + value + "\r\n";
      post.content_type = "text/plain";
    } else {
      for (const auto &[name, value] : fields) {
        if (!post.body.empty())
          post.body += '&';
        post.body += form_encode(name) + "=" + form_encode(value);
      }
      post.content_type = "application/x-www-form-urlencoded";
    }
    // My host sends this itself and renders the answer, so this is not a plain
    // navigation and must not be replayed as a GET.
    posts.push_back(post);
    return;
  }
  std::string query;
  for (const auto &[name, value] : fields) {
    if (!query.empty())
      query += '&';
    query += form_encode(name) + "=" + form_encode(value);
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
  media_tick();
  run_due_timers();
}
bool Page::run_due_timers() {
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
  return !due.empty();
}
// While a script waits on a promise I still let its timers run, so
// `await new Promise(r => setTimeout(r, 50))` behaves the way it should.
bool Page::pump_for_await() {
  if (run_due_timers())
    return true;
  if (js_->timers.empty())
    return false;
  std::this_thread::sleep_for(std::chrono::milliseconds(1));
  run_due_timers();
  return true;
}
} // namespace pv
