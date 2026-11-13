#include "Psycho_page.hpp"
#include <chrono>
#include <sstream>
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
static int page_form_owner(const Document &doc, int id);
static bool text_editor_control(const Node &node) {
  if (node.name == "textarea") return true;
  if (node.name != "input") return false;
  const std::string type = lower(node.attribute("type", "text"));
  return type == "text" || type == "search" || type == "url" || type == "tel" ||
         type == "email" || type == "password" || type == "number";
}
static size_t utf16_length(std::string_view text) {
  size_t length = 0;
  for (uint32_t cp : utf8_decode(text)) length += cp > 0xffff ? 2 : 1;
  return length;
}
static size_t utf16_to_byte(std::string_view text, size_t offset) {
  size_t units = 0, bytes = 0;
  for (uint32_t cp : utf8_decode(text)) {
    if (units >= offset) break;
    const size_t width = cp > 0xffff ? 2 : 1;
    if (units + width > offset) break;
    units += width;
    bytes += utf8_encode(cp).size();
  }
  return bytes;
}
static size_t byte_to_utf16(std::string_view text, size_t offset) {
  size_t units = 0, bytes = 0;
  for (uint32_t cp : utf8_decode(text)) {
    const size_t length = utf8_encode(cp).size();
    if (bytes + length > offset) break;
    bytes += length;
    units += cp > 0xffff ? 2 : 1;
  }
  return units;
}
static bool valid_editor_utf8(std::string_view text) {
  std::string normalized;
  for (uint32_t cp : utf8_decode(text)) normalized += utf8_encode(cp);
  return normalized == text;
}
static bool utf8_byte_boundary(std::string_view text, size_t offset) {
  return offset <= text.size() &&
      (offset == text.size() || (static_cast<uint8_t>(text[offset]) & 0xc0) != 0x80);
}
static bool listener_capture_option(const js::Values &arguments) {
  if (arguments.size() < 3) return false;
  const js::Value &options = arguments[2];
  if (options.type == js::Value::Type::Boolean) return options.boolean;
  if (options.type == js::Value::Type::Object && options.object) {
    auto capture = options.object->properties.find("capture");
    return capture != options.object->properties.end() && capture->second.truthy();
  }
  return false;
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
    if (name == "textBaseline")
      return js::Value(c->state.text_baseline);
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
    if (name == "clip")
      return r.make_native([c](js::Values &a) {
        c->clip(!a.empty() && lower(a[0].to_string()) == "evenodd");
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
                   a.size() < 6 || !a[5].truthy());
        return js::Value();
      });
    if (name == "ellipse")
      return r.make_native([c, number](js::Values &a) {
        c->add_ellipse(number(a, 0), number(a, 1), number(a, 2), number(a, 3), number(a, 5),
                       number(a, 6), a.size() < 8 || !a[7].truthy(), number(a, 4));
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
    if (name == "transform")
      return r.make_native([c, number](js::Values &a) {
        c->apply_transform(number(a, 0, 1), number(a, 1), number(a, 2), number(a, 3, 1),
                           number(a, 4), number(a, 5));
        return js::Value();
      });
    if (name == "resetTransform")
      return r.make_native([c](js::Values &) {
        c->set_transform(1, 0, 0, 1, 0, 0);
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
      return r.make_native([this, c](js::Values &) {
        js::Values dashes;
        for (double dash : c->line_dash()) dashes.emplace_back(dash);
        return page.js_->make_array(dashes);
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
      if (name == "addEventListener" || name == "removeEventListener")
        return r.make_native([this, name](js::Values &a) {
          if (a.size() > 1) {
            const std::string type = a[0].to_string();
            const bool capture = listener_capture_option(a);
            if (name == "addEventListener") page.add_listener(page.doc.root, type, a[1], capture);
            else page.remove_listener(page.doc.root, type, a[1], capture);
          }
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
    if (name == "value") {
      if (n.name == "textarea") return js::Value(d.text_content(id));
      if (n.name == "select") {
        int first = -1;
        for (int option : n.children) {
          if (d.nodes[option].name != "option") continue;
          if (first < 0) first = option;
          if (has_attr(d.nodes[option], "selected"))
            return js::Value(d.nodes[option].attribute("value", d.text_content(option)));
        }
        return first >= 0 ? js::Value(d.nodes[first].attribute("value", d.text_content(first)))
                          : js::Value(std::string());
      }
      return js::Value(n.attribute("value"));
    }
    if (name == "selectedIndex" && n.name == "select") {
      int option_index = 0, first = -1;
      for (int option : n.children) {
        if (d.nodes[option].name != "option") continue;
        if (first < 0) first = option_index;
        if (has_attr(d.nodes[option], "selected")) return js::Value(double(option_index));
        ++option_index;
      }
      return js::Value(first < 0 ? -1.0 : double(first));
    }
    if (name == "selected" && n.name == "option") return js::Value(has_attr(n, "selected"));
    if ((name == "selectionStart" || name == "selectionEnd" || name == "selectionDirection") &&
        text_editor_control(n)) {
      const auto &selection = page.editor_selection(id);
      if (name == "selectionStart") return js::Value(double(selection.start));
      if (name == "selectionEnd") return js::Value(double(selection.end));
      return js::Value(selection.direction);
    }
    if (name == "setSelectionRange" && text_editor_control(n))
      return r.make_native([this, id](js::Values &arguments) {
        const size_t length = utf16_length(page.editor_value(id));
        const size_t start = size_t(std::clamp(arguments.size() > 0 ? arguments[0].to_number() : 0.0,
                                               0.0, double(length)));
        const size_t end = size_t(std::clamp(arguments.size() > 1 ? arguments[1].to_number() : 0.0,
                                             0.0, double(length)));
        auto &selection = page.editor_selection(id);
        selection.start = std::min(start, end);
        selection.end = std::max(start, end);
        selection.direction = arguments.size() > 2 ? arguments[2].to_string() : "none";
        if (selection.direction != "forward" && selection.direction != "backward")
          selection.direction = "none";
        page.dirty_ = true;
        return js::Value();
      });
    if (name == "select" && text_editor_control(n))
      return r.make_native([this, id](js::Values &) {
        auto &selection = page.editor_selection(id);
        selection.start = 0;
        selection.end = utf16_length(page.editor_value(id));
        selection.direction = "forward";
        page.dirty_ = true;
        return js::Value();
      });
    if (name == "checked")
      return js::Value(has_attr(n, "checked"));
    if (n.name == "form" && name == "checkValidity")
      return r.make_native([this, id](js::Values &) {
        bool valid = true;
        for (int control : page.doc.query("*", id))
          if ((page.doc.nodes[control].name == "input" || page.doc.nodes[control].name == "select" ||
               page.doc.nodes[control].name == "textarea") && !form_control_valid(page.doc, control)) {
            valid = false;
            page.dispatch(control, "invalid", page.js_->make_object(), false, false);
          }
        return js::Value(valid);
      });
    if ((name == "checkValidity" || name == "reportValidity") && n.type == Node::Type::Element)
      return r.make_native([this, id](js::Values &) {
        const bool valid = form_control_valid(page.doc, id);
        if (!valid) page.dispatch(id, "invalid", page.js_->make_object(), false, false);
        return js::Value(valid);
      });
    if (name == "willValidate")
      return js::Value(n.name == "input" || n.name == "select" || n.name == "textarea");
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
          page.add_listener(id, a[0].to_string(), a[1], listener_capture_option(a));
        return js::Value();
      });
    if (name == "removeEventListener")
      return r.make_native([this, id](js::Values &a) {
        if (a.size() > 1)
          page.remove_listener(id, a[0].to_string(), a[1], listener_capture_option(a));
        return js::Value();
      });
    if (name == "click")
      return r.make_native([this, id](js::Values &) {
        page.activate(id);
        return js::Value();
      });
    if (name == "focus")
      return r.make_native([this, id](js::Values &) {
        page.set_focus(id);
        return js::Value();
      });
    if (name == "blur")
      return r.make_native([this, id](js::Values &) {
        if (page.focused_ == id) page.set_focus(-1);
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
        const double width = value.to_number();
        if (std::isfinite(width) && width > 0) c.state.line_width = width;
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
        const std::string align = lower(value.to_string());
        if (align == "start" || align == "end" || align == "left" || align == "right" ||
            align == "center") c.state.text_align = align;
        return true;
      }
      if (name == "textBaseline") {
        const std::string baseline = lower(value.to_string());
        if (baseline == "top" || baseline == "hanging" || baseline == "middle" ||
            baseline == "alphabetic" || baseline == "ideographic" || baseline == "bottom")
          c.state.text_baseline = baseline;
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
      canvas_context(id);
      auto &ctx = page.canvases_.at(id);
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
      if (text_editor_control(n)) page.set_editor_value(id, value.to_string(), false);
      else if (n.name == "select") {
        bool selected = false;
        for (int option : n.children) if (d.nodes[option].name == "option") {
          const std::string option_value = d.nodes[option].attribute("value", d.text_content(option));
          const bool match = !selected && option_value == value.to_string();
          if (match) selected = true;
          if (match) d.nodes[option].set_attribute("selected", "");
          else std::erase_if(d.nodes[option].attributes,
                             [](auto &attribute) { return attribute.first == "selected"; });
        }
      } else n.set_attribute("value", value.to_string());
      page.dirty_ = true;
      return true;
    }
    if (name == "selectedIndex" && n.name == "select") {
      int option_index = 0;
      for (int option : n.children) if (d.nodes[option].name == "option") {
        if (option_index++ == int(value.to_number())) d.nodes[option].set_attribute("selected", "");
        else std::erase_if(d.nodes[option].attributes,
                           [](auto &attribute) { return attribute.first == "selected"; });
      }
      page.dirty_ = true;
      return true;
    }
    if ((name == "selectionStart" || name == "selectionEnd" || name == "selectionDirection") &&
        text_editor_control(n)) {
      auto &selection = page.editor_selection(id);
      const size_t length = utf16_length(page.editor_value(id));
      if (name == "selectionStart") {
        selection.start = size_t(std::clamp(value.to_number(), 0.0, double(length)));
        if (selection.end < selection.start) selection.end = selection.start;
      } else if (name == "selectionEnd") {
        selection.end = size_t(std::clamp(value.to_number(), double(selection.start), double(length)));
      } else {
        selection.direction = value.to_string();
        if (selection.direction != "forward" && selection.direction != "backward")
          selection.direction = "none";
      }
      page.dirty_ = true;
      return true;
    }
    if (name == "selected" && n.name == "option") {
      if (value.truthy()) {
        n.set_attribute("selected", "");
        if (n.parent >= 0 && d.nodes[n.parent].name == "select" && !has_attr(d.nodes[n.parent], "multiple"))
          for (int option : d.nodes[n.parent].children)
            if (option != id && d.nodes[option].name == "option")
              std::erase_if(d.nodes[option].attributes,
                            [](auto &attribute) { return attribute.first == "selected"; });
      } else std::erase_if(n.attributes,
                           [](auto &attribute) { return attribute.first == "selected"; });
      page.dirty_ = true;
      return true;
    }
    if (name == "checked") {
      if (value.truthy()) {
        if (n.name == "input" && lower(n.attribute("type")) == "radio") {
          const std::string group = n.attribute("name");
          const int owner = page_form_owner(d, id);
          for (int other = 0; !group.empty() && other < int(d.nodes.size()); ++other)
            if (other != id && d.nodes[other].name == "input" &&
                lower(d.nodes[other].attribute("type")) == "radio" &&
                d.nodes[other].attribute("name") == group && page_form_owner(d, other) == owner)
              std::erase_if(d.nodes[other].attributes,
                            [](auto &attribute) { return attribute.first == "checked"; });
        }
        n.set_attribute("checked", "");
      } else std::erase_if(n.attributes, [](auto &attribute) { return attribute.first == "checked"; });
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
    for (const CssDeclaration &declaration : parse_css_declarations(s)) {
      out[declaration.property] = declaration.value +
          (declaration.important ? " !important" : "");
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
void Page::add_listener(int node, const std::string &type, js::Value fn, bool capture) {
  if (listeners_.size() >= 1024 || fn.type_of() != "function") return;
  for (const Listener &listener : listeners_)
    if (listener.node == node && listener.type == type && listener.fn.object == fn.object &&
        listener.capture == capture) return;
  listeners_.push_back({node, type, fn, capture});
  js_->root(fn.object);
}
void Page::remove_listener(int node, const std::string &type, js::Value fn, bool capture) {
  for (auto it = listeners_.begin(); it != listeners_.end(); ++it)
    if (it->node == node && it->type == type && it->fn.object == fn.object &&
        it->capture == capture) {
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
void Page::set_viewport_environment(double width, double height,
                                    const MediaEnvironment &environment) {
  width_ = std::clamp(width, 1.0, 8192.0);
  height_ = std::clamp(height, 1.0, 16384.0);
  media_environment_ = environment;
  media_environment_.device_pixel_ratio =
      std::clamp(std::isfinite(environment.device_pixel_ratio) ? environment.device_pixel_ratio : 1.0,
                 0.25, 8.0);
  media_environment_.scrollbar_width =
      std::clamp(std::isfinite(environment.scrollbar_width) ? environment.scrollbar_width : 8.0,
                 0.0, 64.0);
  auto valid_preference = [](const std::string &value,
                            std::initializer_list<const char *> choices,
                            const char *fallback) {
    for (const char *choice : choices)
      if (value == choice) return value;
    return std::string(fallback);
  };
  media_environment_.prefers_reduced_motion = valid_preference(
      environment.prefers_reduced_motion, {"reduce", "no-preference"}, "no-preference");
  media_environment_.prefers_color_scheme = valid_preference(
      environment.prefers_color_scheme, {"light", "dark"}, "light");
  media_environment_.prefers_contrast = valid_preference(
      environment.prefers_contrast, {"no-preference", "more", "less", "custom"}, "no-preference");
  media_environment_.forced_colors = valid_preference(
      environment.forced_colors, {"none", "active"}, "none");
  dirty_ = true;
  if (js_) {
    js_->global().properties["innerWidth"] = js::Value(width_);
    js_->global().properties["innerHeight"] = js::Value(height_);
    js_->global().properties["devicePixelRatio"] =
        js::Value(media_environment_.device_pixel_ratio);
  }
}
Page::~Page() {
  delete js_->host();
}
void Page::load(const Url &url, const std::string &html, const std::string &mime) {
  hovered_ = active_ = focused_ = -1;
  selections_.clear(); editor_history_.clear(); compositions_.clear(); clipboard_text_.clear();
  control_down_ = shift_down_ = selecting_text_ = editor_dragged_ = false;
  selection_node_ = -1; selection_anchor_ = 0;
  doc.url = url;
  doc.content_type = mime;
  doc.parse(html);
  resources_.clear();
  resource_generation_ = 0;
  layout_document_signature_ = layout_style_signature_ = layout_scroll_signature_ = 0;
  layout_resource_generation_ = 0;
  has_layout_cache_ = false;
  layout_rebuild_count_ = 0;
  cached_layout_width_ = cached_viewport_width_ = cached_layout_height_ = 0;
  cached_layout_scroll_ = -1;
  images_.clear();
  computed_styles_.clear();
  previous_paint_.clear();
  previous_frame_width_ = previous_frame_height_ = previous_frame_scroll_ = 0;
  force_full_damage_ = true;
  animations_.clear();
  transitions_.clear();
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
  js_->global().properties["innerWidth"] = js::Value(width_);
  js_->global().properties["innerHeight"] = js::Value(height_);
  js_->global().properties["devicePixelRatio"] =
      js::Value(media_environment_.device_pixel_ratio);
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
  const bool internal = u && u->scheme == "psychovault" && !u->host.empty();
  if (!u || (!u->network() && !internal))
    return;
  auto href = u->href();
  if (requested_.count(href) || requested_.size() >= kMaxResources)
    return;
  requested_.insert(href);
  requests.push_back({href, type});
}
void Page::resource(const Resource &res) {
  force_full_damage_ = true;
  ++resource_generation_;
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
  std::string source = doc.text_content(id);
  const std::string external_source = n.attribute("src");
  if (!external_source.empty()) {
    auto url = resolve_url(doc.url, external_source);
    if (!url) return;
    auto resource = resources_.find(url->href());
    if (resource == resources_.end() || !resource->second.ok) return;
    source = resource->second.text;
  }
  try {
    js_->run(source);
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
    else if (n.name == "img") {
      // Lazy images are queued from their laid-out viewport box in render().
      // Eager images retain the usual load-time request behavior.
      if (lower(n.attribute("loading")) != "lazy")
        request(n.attribute("src"), ResourceType::Image);
    }
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
namespace {
std::vector<std::string> css_words(const std::string &text) {
  std::vector<std::string> out;
  std::istringstream input(text);
  std::string word;
  while (input >> word) out.push_back(word);
  return out;
}
double css_time_ms(const std::string &text, double fallback = 0) {
  try {
    size_t used = 0;
    double value = std::stod(text, &used);
    const std::string unit = lower(trim(text.substr(used)));
    if (unit == "ms") return std::clamp(value, -60000.0, 60000.0);
    if (unit == "s") return std::clamp(value * 1000.0, -60000.0, 60000.0);
  } catch (...) {}
  return fallback;
}
double easing_progress(const std::string &timing, double progress) {
  progress = std::clamp(progress, 0.0, 1.0);
  if (timing == "ease-in") return progress * progress;
  if (timing == "ease-out") return 1 - (1 - progress) * (1 - progress);
  if (timing == "ease-in-out")
    return progress < 0.5 ? 2 * progress * progress
                          : 1 - std::pow(-2 * progress + 2, 2) / 2;
  if (timing == "ease")
    return progress < 0.5 ? 4 * progress * progress * progress
                          : 1 - std::pow(-2 * progress + 2, 3) / 2;
  if (timing == "step-start") return 1;
  if (timing == "step-end") return progress >= 1 ? 1 : 0;
  if (timing.rfind("steps(", 0) == 0) {
    const size_t close = timing.find(')');
    if (close != std::string::npos) {
      auto args = css_words(std::string(timing.substr(6, close - 6)));
      std::string body = timing.substr(6, close - 6);
      const size_t comma = body.find(',');
      int count = 1;
      try { count = std::max(1, std::stoi(body.substr(0, comma))); } catch (...) {}
      const bool start = comma != std::string::npos && trim(body.substr(comma + 1)) == "start";
      (void)args;
      return std::clamp((std::floor(progress * count) + (start ? 1 : 0)) / count, 0.0, 1.0);
    }
  }
  if (timing.rfind("cubic-bezier(", 0) == 0 && timing.back() == ')') {
    std::vector<double> points;
    std::string body = timing.substr(13, timing.size() - 14);
    size_t begin = 0;
    while (begin <= body.size()) {
      size_t end = body.find(',', begin);
      if (end == std::string::npos) end = body.size();
      try { points.push_back(std::stod(trim(body.substr(begin, end - begin)))); }
      catch (...) { return progress; }
      if (end == body.size()) break;
      begin = end + 1;
    }
    if (points.size() == 4) {
      auto curve = [](double t, double p1, double p2) {
        const double mt = 1 - t;
        return 3 * mt * mt * t * p1 + 3 * mt * t * t * p2 + t * t * t;
      };
      double low = 0, high = 1, t = progress;
      for (int i = 0; i < 20; ++i) {
        const double x = curve(t, points[0], points[2]);
        if (std::abs(x - progress) < 1e-6) break;
        if (x < progress) low = t; else high = t;
        t = (low + high) / 2;
      }
      return curve(t, points[1], points[3]);
    }
  }
  return progress;
}
std::string style_property_value(const Style &style, const std::string &property) {
  if (auto value = style.raw.find(property); value != style.raw.end()) return value->second;
  if (property == "opacity") return std::to_string(style.opacity);
  if (property == "color") {
    const Color &c = style.color;
    return "rgb(" + std::to_string(c.r) + " " + std::to_string(c.g) + " " +
           std::to_string(c.b) + " / " + std::to_string(c.a * 100.0 / 255.0) + "%)";
  }
  if (property == "background-color") {
    const Color &c = style.background;
    return "rgb(" + std::to_string(c.r) + " " + std::to_string(c.g) + " " +
           std::to_string(c.b) + " / " + std::to_string(c.a * 100.0 / 255.0) + "%)";
  }
  if (property == "width" && style.width >= 0) return std::to_string(style.width) + "px";
  if (property == "height" && style.height >= 0) return std::to_string(style.height) + "px";
  return {};
}
std::string interpolate_css_value(const std::string &property, const std::string &from,
                                  const std::string &to, double progress) {
  progress = std::clamp(progress, 0.0, 1.0);
  if (progress <= 0) return from;
  if (progress >= 1) return to;
  if (property == "color" || property == "background-color") {
    const Color a = parse_color(from), b = parse_color(to);
    if (a.transparent && lower(trim(from)) != "transparent") return from;
    if (b.transparent && lower(trim(to)) != "transparent") return from;
    auto channel = [&](int x, int y) { return int(std::lround(x + (y - x) * progress)); };
    const int r = channel(a.r, b.r), g = channel(a.g, b.g), blue = channel(a.b, b.b);
    const int alpha = channel(a.a, b.a);
    return "rgb(" + std::to_string(r) + " " + std::to_string(g) + " " +
           std::to_string(blue) + " / " + std::to_string(alpha * 100.0 / 255.0) + "%)";
  }
  if (property == "opacity" || property == "width" || property == "height") {
    try {
      size_t used_a = 0, used_b = 0;
      const double a = std::stod(from, &used_a), b = std::stod(to, &used_b);
      std::string unit_a = lower(trim(from.substr(used_a)));
      std::string unit_b = lower(trim(to.substr(used_b)));
      if (unit_a.empty()) unit_a = unit_b;
      if (unit_b.empty()) unit_b = unit_a;
      if (unit_a == unit_b && (unit_a.empty() || unit_a == "px"))
        return std::to_string(a + (b - a) * progress) + unit_a;
    } catch (...) {}
  }
  return progress < 1 ? from : to;
}
std::vector<std::string> comma_items(const std::string &value) {
  std::vector<std::string> out;
  size_t begin = 0;
  int depth = 0;
  for (size_t at = 0; at <= value.size(); ++at) {
    if (at < value.size() && value[at] == '(') ++depth;
    else if (at < value.size() && value[at] == ')') --depth;
    if (at == value.size() || (value[at] == ',' && depth == 0)) {
      out.push_back(trim(value.substr(begin, at - begin)));
      begin = at + 1;
    }
  }
  return out;
}
void hash_combine(uint64_t &seed, uint64_t value) {
  seed ^= value + 0x9e3779b97f4a7c15ull + (seed << 6) + (seed >> 2);
}
void hash_text(uint64_t &seed, const std::string &value) {
  hash_combine(seed, uint64_t(std::hash<std::string>{}(value)));
}
uint64_t document_layout_signature(const Document &doc) {
  uint64_t hash = 0;
  for (const Node &node : doc.nodes) {
    hash_combine(hash, uint64_t(node.type));
    hash_combine(hash, uint64_t(uint32_t(node.parent)));
    hash_text(hash, node.name);
    hash_text(hash, node.text);
    for (const auto &[name, value] : node.attributes) {
      hash_text(hash, name);
      hash_text(hash, value);
    }
    for (int child : node.children) hash_combine(hash, uint64_t(uint32_t(child)));
  }
  return hash;
}
uint64_t styles_layout_signature(const std::vector<Style> &styles) {
  uint64_t hash = 0;
  for (const Style &style : styles) {
    hash_text(hash, style.display);
    hash_text(hash, style.font_family);
    hash_text(hash, style.text_align);
    hash_text(hash, style.weight);
    hash_text(hash, style.box_sizing);
    hash_text(hash, style.white_space);
    hash_text(hash, style.text_overflow);
    hash_text(hash, style.position);
    hash_text(hash, style.overflow);
    hash_text(hash, style.scrollbar_gutter);
    hash_text(hash, style.overscroll_behavior);
    hash_text(hash, style.flex_direction);
    hash_text(hash, style.justify_content);
    hash_text(hash, style.align_items);
    hash_text(hash, style.align_self);
    hash_text(hash, style.align_content);
    hash_text(hash, style.flex_wrap);
    hash_text(hash, style.grid_columns);
    hash_text(hash, style.grid_rows);
    hash_text(hash, style.grid_template_areas);
    hash_text(hash, style.grid_column);
    hash_text(hash, style.grid_row);
    hash_text(hash, style.grid_area);
    hash_text(hash, style.grid_auto_flow);
    hash_text(hash, style.justify_items);
    hash_text(hash, style.justify_self);
    hash_text(hash, style.transform);
    hash_text(hash, style.filter);
    hash_text(hash, style.box_shadow);
    hash_text(hash, style.background_image);
    hash_combine(hash, style.color.bgra());
    hash_combine(hash, style.background.bgra());
    hash_combine(hash, style.border_color.bgra());
    for (double value : {style.font_size, style.line_height, style.opacity, style.gap,
                         style.width, style.height, style.min_width, style.max_width,
                         style.min_height, style.max_height, style.root_font_size,
                         style.viewport_width, style.viewport_height, style.top, style.left,
                         style.right, style.bottom, style.flex_grow, style.flex_shrink,
                         style.flex_basis, style.height_percent, style.row_gap})
      hash_combine(hash, uint64_t(std::hash<double>{}(value)));
    for (double value : style.margin) hash_combine(hash, uint64_t(std::hash<double>{}(value)));
    for (double value : style.padding) hash_combine(hash, uint64_t(std::hash<double>{}(value)));
    for (double value : style.border) hash_combine(hash, uint64_t(std::hash<double>{}(value)));
    for (const std::string &layer : style.background_layers) hash_text(hash, layer);
    for (const auto &[name, value] : style.raw) {
      hash_text(hash, name);
      hash_text(hash, value);
    }
    hash_combine(hash, uint64_t(style.bold) | (uint64_t(style.italic) << 1) |
                           (uint64_t(style.underline) << 2) | (uint64_t(style.list_item) << 3));
  }
  return hash;
}
uint64_t local_scroll_signature(const std::unordered_map<int, double> &scrolls) {
  uint64_t hash = 0;
  for (const auto &[node, offset] : scrolls) {
    hash_combine(hash, uint64_t(uint32_t(node)));
    hash_combine(hash, uint64_t(std::hash<double>{}(offset)));
  }
  return hash;
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
  const SelectorState selector_state{hovered_, focused_, active_};
  auto styles = compute_styles(doc, sheets, width_, height_, &selector_state,
                               &media_environment_);
  const double now = double(now_ms());
  // I detect computed-value changes after selector/cascade recomputation, then
  // interpolate only a conservative set of paint/layout scalar properties.
  std::map<std::pair<int, std::string>, RunningTransition> next_transitions;
  auto transition_spec = [&](const Style &style, const std::string &property,
                             double &duration, double &delay, std::string &timing) {
    duration = delay = 0;
    timing = "linear";
    auto shorthand = style.raw.find("transition");
    bool found = false;
    if (shorthand != style.raw.end()) {
      for (const std::string &item : comma_items(shorthand->second)) {
        auto words = css_words(item);
        if (words.empty() || (words[0] != "all" && words[0] != property && words[0] != "none"))
          continue;
        std::vector<double> times;
        for (const std::string &word : words) {
          const double time = css_time_ms(word, std::numeric_limits<double>::quiet_NaN());
          if (std::isfinite(time)) times.push_back(time);
          else if (word == "linear" || word == "ease" || word == "ease-in" ||
                   word == "ease-out" || word == "ease-in-out") timing = word;
        }
        if (!times.empty()) {
          duration = std::max(0.0, times[0]);
          if (times.size() > 1) delay = times[1];
          found = true;
          break;
        }
      }
    }
    if (!found) return false;
    if (auto value = style.raw.find("transition-property"); value != style.raw.end())
      if (value->second != "all" && value->second.find(property) == std::string::npos)
        return false;
    if (auto value = style.raw.find("transition-duration"); value != style.raw.end()) {
      auto items = comma_items(value->second);
      if (!items.empty()) duration = std::max(0.0, css_time_ms(items[0], 0));
    }
    if (auto value = style.raw.find("transition-delay"); value != style.raw.end()) {
      auto items = comma_items(value->second);
      if (!items.empty()) delay = css_time_ms(items[0], 0);
    }
    if (auto value = style.raw.find("transition-timing-function"); value != style.raw.end())
      timing = trim(value->second);
    return duration > 0;
  };
  if (media_environment_.prefers_reduced_motion != "reduce" &&
      computed_styles_.size() == styles.size()) {
    for (int node = 0; node < int(styles.size()); ++node) {
      const Style &before = computed_styles_[size_t(node)];
      const Style &after = styles[size_t(node)];
      for (const char *property_name : {"opacity", "color", "background-color", "width", "height"}) {
        const std::string property = property_name;
        const std::string old_value = style_property_value(before, property);
        const std::string new_value = style_property_value(after, property);
        if (old_value.empty() || new_value.empty() || old_value == new_value) continue;
        double duration = 0, delay = 0;
        std::string timing;
        if (!transition_spec(after, property, duration, delay, timing)) continue;
        const auto key = std::make_pair(node, property);
        RunningTransition current;
        if (auto active = transitions_.find(key); active != transitions_.end() &&
            active->second.to == new_value) {
          current = active->second;
        } else {
          current = {property, timing, old_value, new_value, now, duration, delay};
        }
        next_transitions[key] = current;
      }
    }
  }
  transitions_ = std::move(next_transitions);
  for (auto it = transitions_.begin(); it != transitions_.end();) {
    const auto [node, property] = it->first;
    RunningTransition &transition = it->second;
    const double elapsed = now - transition.start_ms - transition.delay_ms;
    double progress = elapsed <= 0 ? 0 : transition.duration_ms > 0
                                               ? elapsed / transition.duration_ms : 1;
    if (progress >= 1) {
      it = transitions_.erase(it);
      continue;
    }
    progress = easing_progress(transition.timing, progress);
    apply_style_overrides(styles[size_t(node)],
                          {{property, interpolate_css_value(property, transition.from,
                                                            transition.to, progress)}},
                          width_, height_);
    ++it;
  }

  // Keyframes run against the page clock. One named animation per element is
  // intentionally supported; fill, direction, delay and finite/infinite loops
  // cover the common spinner/attention-indicator cases.
  std::map<int, RunningAnimation> active_animations;
  if (media_environment_.prefers_reduced_motion != "reduce") {
    for (int node = 0; node < int(styles.size()); ++node) {
      const Style &style = styles[size_t(node)];
      std::string animation_name;
      if (auto value = style.raw.find("animation-name"); value != style.raw.end())
        animation_name = trim(value->second);
      std::vector<std::string> shorthand;
      if (auto value = style.raw.find("animation"); value != style.raw.end()) {
        shorthand = css_words(value->second);
        if (animation_name.empty())
          for (const std::string &word : shorthand) {
            if (std::isfinite(css_time_ms(word, std::numeric_limits<double>::quiet_NaN())))
              continue;
            if (word != "none" && word != "infinite" && word != "normal" &&
                word != "reverse" && word != "alternate" && word != "alternate-reverse" &&
                word != "forwards" && word != "backwards" && word != "both" &&
                word != "linear" && word != "ease" && word != "ease-in" &&
                word != "ease-out" && word != "ease-in-out") {
              animation_name = word;
              break;
            }
          }
      }
      if (animation_name.empty() || animation_name == "none") continue;
      const CssKeyframes *keyframes = nullptr;
      for (const Stylesheet &sheet : sheets)
        for (const CssKeyframes &candidate : sheet.keyframes)
          if (candidate.name == animation_name) keyframes = &candidate;
      if (!keyframes || keyframes->frames.empty()) continue;
      RunningAnimation animation;
      animation.name = animation_name;
      animation.frames = keyframes->frames;
      for (const std::string &word : shorthand) {
        const double time = css_time_ms(word, std::numeric_limits<double>::quiet_NaN());
        if (std::isfinite(time)) {
          if (animation.duration_ms == 0) animation.duration_ms = std::max(0.0, time);
          else animation.delay_ms = time;
        } else if (word == "linear" || word == "ease" || word == "ease-in" ||
                   word == "ease-out" || word == "ease-in-out") animation.timing = word;
        else if (word == "reverse" || word == "alternate" || word == "alternate-reverse")
          animation.direction = word;
        else if (word == "forwards" || word == "backwards" || word == "both")
          animation.fill = word;
        else if (word == "infinite") animation.iterations = -1;
        else if (word != animation_name && word != "normal") {
          try { animation.iterations = std::max(0.0, std::stod(word)); } catch (...) {}
        }
      }
      if (auto value = style.raw.find("animation-duration"); value != style.raw.end())
        animation.duration_ms = std::max(0.0, css_time_ms(value->second, 0));
      if (auto value = style.raw.find("animation-delay"); value != style.raw.end())
        animation.delay_ms = css_time_ms(value->second, 0);
      if (auto value = style.raw.find("animation-timing-function"); value != style.raw.end())
        animation.timing = trim(value->second);
      if (auto value = style.raw.find("animation-iteration-count"); value != style.raw.end()) {
        if (trim(value->second) == "infinite") animation.iterations = -1;
        else try { animation.iterations = std::max(0.0, std::stod(value->second)); } catch (...) {}
      }
      if (auto value = style.raw.find("animation-direction"); value != style.raw.end())
        animation.direction = trim(value->second);
      if (auto value = style.raw.find("animation-fill-mode"); value != style.raw.end())
        animation.fill = trim(value->second);
      if (auto old = animations_.find(node); old != animations_.end() && old->second.name == animation.name)
        animation.start_ms = old->second.start_ms;
      else animation.start_ms = now;
      active_animations[node] = std::move(animation);
    }
  }
  animations_ = std::move(active_animations);
  for (const auto &[node, animation] : animations_) {
    const double elapsed = now - animation.start_ms - animation.delay_ms;
    const double duration = animation.duration_ms;
    if (duration <= 0 || animation.iterations == 0) continue;
    const double total = animation.iterations < 0 ? std::numeric_limits<double>::infinity()
                                                  : duration * animation.iterations;
    if (elapsed < 0 && animation.fill != "backwards" && animation.fill != "both") continue;
    if (elapsed >= total && animation.fill != "forwards" && animation.fill != "both") continue;
    double progress = elapsed < 0 ? 0 : elapsed >= total ? 1 : std::fmod(elapsed, duration) / duration;
    if (elapsed >= 0 && elapsed < total && animation.direction.find("alternate") == 0 &&
        (int(std::floor(elapsed / duration)) & 1)) progress = 1 - progress;
    if (animation.direction == "reverse" || animation.direction == "alternate-reverse")
      progress = 1 - progress;
    progress = easing_progress(animation.timing, progress);
    std::set<std::string> properties;
    for (const CssKeyframe &frame : animation.frames)
      for (const auto &[property, value] : frame.declarations) {
        (void)value;
        if (property == "opacity" || property == "color" || property == "background-color" ||
            property == "width" || property == "height") properties.insert(property);
      }
    std::map<std::string, std::string> overrides;
    for (const std::string &property : properties) {
      const CssKeyframe *before = nullptr, *after = nullptr;
      for (const CssKeyframe &frame : animation.frames)
        if (frame.declarations.count(property)) {
          if (frame.offset <= progress) before = &frame;
          if (frame.offset >= progress && !after) after = &frame;
        }
      const std::string base = style_property_value(styles[size_t(node)], property);
      const std::string from = before ? before->declarations.at(property) : base;
      const std::string to = after ? after->declarations.at(property) : from;
      const double span = before && after ? after->offset - before->offset : 1.0;
      const double local = before && after && span > 0 ? (progress - before->offset) / span : progress;
      overrides[property] = interpolate_css_value(property, from, to,
                                                    easing_progress(animation.timing, local));
    }
    apply_style_overrides(styles[size_t(node)], overrides, width_, height_);
  }
  computed_styles_ = styles;
  for (const Style &style : styles)
    for (const std::string &layer : style.background_layers) {
      const std::string lowered_layer = lower(layer);
      const size_t open = lowered_layer.find("url(");
      const size_t close = open == std::string::npos ? std::string::npos : layer.find(')', open + 4);
      if (close == std::string::npos) continue;
      std::string url = trim(layer.substr(open + 4, close - open - 4));
      if (url.size() >= 2 && ((url.front() == '\'' && url.back() == '\'') ||
                              (url.front() == '"' && url.back() == '"')))
        url = url.substr(1, url.size() - 2);
      request(std::move(url), ResourceType::Image);
    }
  // Retain the previous layout when only an event arrived and neither the
  // document, computed styles, viewport, scroll offsets nor resources changed.
  // For a real reflow I still use the existing full-layout pass, then remember
  // its inputs so the next state-only interaction can avoid repeating it.
  bool stable_root_gutter = false, forced_root_scrollbar = false;
  bool root_scrollbar_suppressed = false;
  for (size_t i = 0; i < doc.nodes.size() && i < styles.size(); ++i)
    if (doc.nodes[i].name == "html" || doc.nodes[i].name == "body") {
      stable_root_gutter = stable_root_gutter ||
          styles[i].scrollbar_gutter.find("stable") != std::string::npos;
      forced_root_scrollbar = forced_root_scrollbar || styles[i].overflow == "scroll";
      root_scrollbar_suppressed = root_scrollbar_suppressed ||
          styles[i].overflow == "hidden" || styles[i].overflow == "clip";
    }
  const double gutter = media_environment_.scrollbar_width;
  double layout_width = width_;
  if (stable_root_gutter || forced_root_scrollbar ||
      (!root_scrollbar_suppressed && gutter > 0 && layout_.height > height_ + 0.5))
    layout_width = std::max(1.0, width_ - gutter);
  const uint64_t document_signature = document_layout_signature(doc);
  const uint64_t style_signature = styles_layout_signature(styles);
  const uint64_t scroll_signature = local_scroll_signature(local_scroll_);
  const bool needs_layout = !has_layout_cache_ ||
      document_signature != layout_document_signature_ ||
      style_signature != layout_style_signature_ ||
      scroll_signature != layout_scroll_signature_ ||
      resource_generation_ != layout_resource_generation_ ||
      width_ != cached_viewport_width_ || height_ != cached_layout_height_ ||
      scroll_ != cached_layout_scroll_ || layout_width != cached_layout_width_;
  if (needs_layout) {
    layout_width = width_;
    if (stable_root_gutter || forced_root_scrollbar)
      layout_width = std::max(1.0, width_ - gutter);
    Canvas measure{int(layout_width), int(height_)};
    layout_ = layout_document(doc, styles, layout_width, height_, resources_, measure,
                              scroll_, local_scroll_);
    ++layout_rebuild_count_;
    // My shell scrollbar is a classic, non-overlay bar on the document edge.
    // Reserve its width only when document scrolling is active.
    if (!stable_root_gutter && !forced_root_scrollbar && !root_scrollbar_suppressed &&
        gutter > 0 && layout_.height > height_ + 0.5) {
      layout_width = std::max(1.0, width_ - gutter);
      Canvas narrowed_measure{int(layout_width), int(height_)};
      layout_ = layout_document(doc, styles, layout_width, height_, resources_,
                                narrowed_measure, scroll_, local_scroll_);
      ++layout_rebuild_count_;
    }
    layout_document_signature_ = document_signature;
    layout_style_signature_ = style_signature;
    layout_scroll_signature_ = scroll_signature;
    layout_resource_generation_ = resource_generation_;
    cached_layout_width_ = layout_width;
    cached_viewport_width_ = width_;
    cached_layout_height_ = height_;
    cached_layout_scroll_ = scroll_;
    has_layout_cache_ = true;
  }
  content_height_ = layout_.height;
  std::erase_if(local_scroll_, [&](const auto &entry) {
    return !layout_.scroll_containers.count(entry.first);
  });
  for (const auto &[node, container] : layout_.scroll_containers)
    local_scroll_[node] = container.offset;
  dirty_ = false;
}
Json Page::render(double width, double height, double scroll) {
  const double next_width = std::clamp(width, 1.0, 8192.0);
  const double next_height = std::clamp(height, 1.0, 16384.0);
  const double next_scroll = std::max(0.0, scroll);
  if (next_width != width_ || next_height != height_ || next_scroll != scroll_) dirty_ = true;
  width_ = next_width;
  height_ = next_height;
  scroll_ = next_scroll;
  if (js_) {
    js_->global().properties["innerWidth"] = js::Value(width_);
    js_->global().properties["innerHeight"] = js::Value(height_);
    js_->global().properties["devicePixelRatio"] =
        js::Value(media_environment_.device_pixel_ratio);
  }
  if (dirty_) rebuild();
  const double clamped_scroll = std::min(scroll_, std::max(0.0, content_height_ - height_));
  if (clamped_scroll != scroll_) {
    scroll_ = clamped_scroll;
    dirty_ = true;
    rebuild();
  }
  // I begin lazy image fetches once their layout box is near the viewport. The
  // small vertical margin avoids waiting until an image is exactly visible.
  for (const auto &node : doc.nodes) {
    if (node.type != Node::Type::Element || node.name != "img" ||
        lower(node.attribute("loading")) != "lazy")
      continue;
    auto box = layout_.boxes.find(node.id);
    if (box != layout_.boxes.end() && box->second.y + box->second.h >= scroll_ - height_ &&
        box->second.y <= scroll_ + 2 * height_)
      request(node.attribute("src"), ResourceType::Image);
  }
  // I only copy pixels for a canvas that has been drawn on since the last frame.
  for (auto &[id, context] : canvases_) {
    auto seen = canvas_revisions_.find(id);
    if (seen != canvas_revisions_.end() && seen->second == context.revision)
      continue;
    canvas_images_[id] = context.snapshot();
    canvas_revisions_[id] = context.revision;
    force_full_damage_ = true;
  }
  Json frame = Json::make_object();
  frame.set("tag", "frame");
  frame.set("title", title());
  frame.set("url", doc.url.href());
  frame.set("height", content_height_);
  frame.set("scroll", scroll_);
  Json paint = Json::make_array();
  std::vector<PaintCommand> frame_paint;
  const bool editor_focused = focused_ >= 0 && focused_ < int(doc.nodes.size()) &&
                              text_editor_control(doc.nodes[focused_]);
  TextSelection editor_range;
  std::string editor_text;
  if (editor_focused) {
    editor_text = editor_value(focused_);
    editor_range = editor_selection(focused_);
  }
  Canvas text_measure{1, 1};
  for (const PaintCommand &command : layout_.paint) {
    if (editor_focused && command.node == focused_ && command.kind == PaintCommand::Kind::Text) {
      const size_t start_byte = utf16_to_byte(editor_text, editor_range.start);
      const size_t end_byte = utf16_to_byte(editor_text, editor_range.end);
      const std::string before = editor_text.substr(0, start_byte);
      const double before_width = text_measure.text_width(before, command.size, command.bold);
      if (editor_range.end > editor_range.start) {
        const std::string selected = editor_text.substr(start_byte, end_byte - start_byte);
        const double selected_width = text_measure.text_width(selected, command.size, command.bold);
        if (selected_width > 0)
          frame_paint.emplace_back(PaintCommand::Kind::Rect,
              Rect{command.rect.x + before_width, command.rect.y, selected_width, command.rect.h},
              0x803878d0, std::string(), 0, 0, false, focused_);
      }
      frame_paint.push_back(command);
      if (editor_range.start == editor_range.end && (now_ms() / 530) % 2 == 0)
        frame_paint.emplace_back(PaintCommand::Kind::Rect,
            Rect{command.rect.x + before_width, command.rect.y + 1, 1,
                 std::max(1.0, command.rect.h - 2)}, command.color, std::string(), 0, 0, false, focused_);
    } else frame_paint.push_back(command);
  }
  frame.set("focusedEditable", editor_focused);
  for (const auto &c : frame_paint) {
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
    j.set("radius", c.radius);
    j.set("opacity", c.opacity);
    j.set("blur", c.blur);
    j.set("spread", c.spread);
    j.set("offsetX", c.offset_x);
    j.set("offsetY", c.offset_y);
    j.set("rotation", c.rotation);
    j.set("filter", c.filter);
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
  std::vector<DamageSnapshot> current_paint;
  current_paint.reserve(paint.array.size());
  for (const Json &command : paint.array)
    current_paint.push_back({command.dump(),
        {command.num("x"), command.num("y"), command.num("w"), command.num("h")},
        command.flag("fixed")});
  std::vector<DamageSnapshot> damage_regions;
  const bool viewport_changed = previous_frame_width_ != width_ ||
      previous_frame_height_ != height_ || previous_frame_scroll_ != scroll_;
  bool full_damage = force_full_damage_ || viewport_changed ||
      previous_paint_.size() != current_paint.size();
  auto add_damage = [&](Rect rect, bool fixed) {
    const double clip_top = fixed ? 0 : scroll_;
    const double clip_bottom = fixed ? height_ : scroll_ + height_;
    const double left = std::max(0.0, rect.x), right = std::min(width_, rect.x + rect.w);
    const double top = std::max(clip_top, rect.y), bottom = std::min(clip_bottom, rect.y + rect.h);
    if (right <= left || bottom <= top) return;
    Rect clipped{left, top, right - left, bottom - top};
    for (DamageSnapshot &existing : damage_regions) {
      if (existing.fixed != fixed ||
          clipped.x > existing.rect.x + existing.rect.w + 2 ||
          existing.rect.x > clipped.x + clipped.w + 2 ||
          clipped.y > existing.rect.y + existing.rect.h + 2 ||
          existing.rect.y > clipped.y + clipped.h + 2)
        continue;
      const double x = std::min(existing.rect.x, clipped.x);
      const double y = std::min(existing.rect.y, clipped.y);
      const double r = std::max(existing.rect.x + existing.rect.w, clipped.x + clipped.w);
      const double b = std::max(existing.rect.y + existing.rect.h, clipped.y + clipped.h);
      existing.rect = {x, y, r - x, b - y};
      return;
    }
    damage_regions.push_back({{}, clipped, fixed});
  };
  size_t changed_commands = 0;
  if (!full_damage) {
    for (size_t i = 0; i < current_paint.size(); ++i) {
      if (previous_paint_[i].signature == current_paint[i].signature) continue;
      ++changed_commands;
      add_damage(previous_paint_[i].rect, previous_paint_[i].fixed);
      add_damage(current_paint[i].rect, current_paint[i].fixed);
      if (damage_regions.size() > 64) {
        full_damage = true;
        break;
      }
    }
  } else {
    changed_commands = current_paint.size();
  }
  if (full_damage) {
    damage_regions.clear();
    damage_regions.push_back({{}, {0, scroll_, width_, height_}, false});
  }
  constexpr int compositor_tile_size = 128;
  std::set<std::tuple<int, int, bool>> tile_keys;
  bool tile_overflow = false;
  for (const DamageSnapshot &region : damage_regions) {
    if (region.rect.w <= 0 || region.rect.h <= 0) continue;
    const int left = std::max(0, int(std::floor(region.rect.x / compositor_tile_size)));
    const int top = std::max(0, int(std::floor(region.rect.y / compositor_tile_size)));
    const int right = int(std::floor(std::max(region.rect.x, region.rect.x + region.rect.w - 1e-6) /
                                     compositor_tile_size));
    const int bottom = int(std::floor(std::max(region.rect.y, region.rect.y + region.rect.h - 1e-6) /
                                      compositor_tile_size));
    for (int ty = top; ty <= bottom && !tile_overflow; ++ty)
      for (int tx = left; tx <= right; ++tx) {
        tile_keys.emplace(tx, ty, region.fixed);
        if (tile_keys.size() > 256) {
          tile_overflow = true;
          tile_keys.clear();
          break;
        }
      }
  }
  frame.set("paint", std::move(paint));
  Json damage = Json::make_array();
  for (const DamageSnapshot &region : damage_regions) {
    Json item = Json::make_object();
    item.set("x", region.rect.x);
    item.set("y", region.rect.y);
    item.set("w", region.rect.w);
    item.set("h", region.rect.h);
    item.set("fixed", region.fixed);
    damage.array.push_back(std::move(item));
  }
  frame.set("damage", std::move(damage));
  Json tiles = Json::make_array();
  if (!tile_overflow)
    for (const auto &[tx, ty, fixed] : tile_keys) {
      Json item = Json::make_object();
      item.set("x", double(tx * compositor_tile_size));
      item.set("y", double(ty * compositor_tile_size));
      item.set("size", compositor_tile_size);
      item.set("fixed", fixed);
      tiles.array.push_back(std::move(item));
    }
  frame.set("damageTiles", std::move(tiles));
  frame.set("fullDamage", full_damage || tile_overflow);
  Json compositor = Json::make_object();
  compositor.set("tileSize", compositor_tile_size);
  compositor.set("tileCount", double(tile_keys.size()));
  compositor.set("changedCommands", double(changed_commands));
  compositor.set("fullDamage", full_damage || tile_overflow);
  size_t fixed_commands = 0, scrolled_commands = 0, effect_layers = 0;
  for (const Json &command : frame.at("paint").array) {
    if (command.flag("fixed")) ++fixed_commands;
    else ++scrolled_commands;
    if (command.num("kind", -1) == int(PaintCommand::Kind::PushLayer)) ++effect_layers;
  }
  compositor.set("fixedLayerCommands", double(fixed_commands));
  compositor.set("scrolledLayerCommands", double(scrolled_commands));
  compositor.set("effectLayers", double(effect_layers));
  frame.set("compositor", std::move(compositor));
  previous_paint_ = std::move(current_paint);
  previous_frame_width_ = width_;
  previous_frame_height_ = height_;
  previous_frame_scroll_ = scroll_;
  force_full_damage_ = false;
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
    Rect visible = r;
    for (int parent = doc.nodes[id].parent; parent >= 0; parent = doc.nodes[parent].parent) {
      auto clip = layout_.clips.find(parent);
      if (clip == layout_.clips.end()) continue;
      const double left = std::max(visible.x, clip->second.x);
      const double top = std::max(visible.y, clip->second.y);
      const double right = std::min(visible.x + visible.w, clip->second.x + clip->second.w);
      const double bottom = std::min(visible.y + visible.h, clip->second.y + clip->second.h);
      visible = {left, top, std::max(0.0, right - left), std::max(0.0, bottom - top)};
      if (visible.w <= 0 || visible.h <= 0) break;
    }
    const double content_y = layout_.fixed_boxes.count(id) ? y : y + scroll_;
    if (visible.contains(x, content_y) && visible.w * visible.h < area) {
      best = id;
      area = visible.w * visible.h;
    }
  }
  return best;
}
bool Page::dispatch(int target, const std::string &type, js::Value event,
                    bool bubbles, bool cancelable) {
  if (!event.object) event = js_->make_object();
  struct DispatchState {
    bool stopped = false, immediate = false, prevented = false;
    bool cancelable = true, passive = false;
  };
  auto state = std::make_shared<DispatchState>();
  state->cancelable = cancelable;
  js::Object *event_object = event.object;
  event_object->properties["type"] = js::Value(type);
  if (!event_object->properties.count("target"))
    event_object->properties["target"] = target >= 0 ? js_->host_value(target) : js::Value::null();
  event_object->properties["bubbles"] = js::Value(bubbles);
  event_object->properties["cancelable"] = js::Value(cancelable);
  event_object->properties["defaultPrevented"] = js::Value(false);
  event_object->properties["eventPhase"] = js::Value(0);
  event_object->properties["currentTarget"] = js::Value::null();
  event_object->properties["preventDefault"] = js_->make_native([state, event_object](js::Values &) {
    if (state->cancelable && !state->passive) {
      state->prevented = true;
      event_object->properties["defaultPrevented"] = js::Value(true);
    }
    return js::Value();
  });
  event_object->properties["stopPropagation"] = js_->make_native([state](js::Values &) {
    state->stopped = true;
    return js::Value();
  });
  event_object->properties["stopImmediatePropagation"] = js_->make_native([state](js::Values &) {
    state->stopped = true;
    state->immediate = true;
    return js::Value();
  });
  if (target < 0 || target >= int(doc.nodes.size())) return false;
  std::vector<int> path;
  for (int node = target; node >= 0 && node < int(doc.nodes.size()); node = doc.nodes[node].parent)
    path.push_back(node);
  const std::vector<Listener> snapshot = listeners_;
  auto invoke = [&](int node, bool capture, int phase) {
    event_object->properties["currentTarget"] = js_->host_value(node);
    event_object->properties["eventPhase"] = js::Value(phase);
    state->immediate = false;
    for (const Listener &listener : snapshot) {
      if (listener.node != node || listener.type != type || listener.capture != capture) continue;
      const bool still_registered = std::any_of(listeners_.begin(), listeners_.end(),
          [&](const Listener &live) {
            return live.node == listener.node && live.type == listener.type &&
                   live.capture == listener.capture && live.fn.object == listener.fn.object;
          });
      if (!still_registered) continue;
      js_->reset_fuel();
      try {
        js_->call(listener.fn, {event}, js_->host_value(node));
      } catch (js::JsError &error) {
        diagnostics.push_back({"event", error.value.to_string()});
      } catch (std::exception &error) {
        diagnostics.push_back({"event", error.what()});
      }
      if (state->immediate) break;
    }
  };
  // Capture travels from the root to the target's parent.
  for (size_t i = path.size(); i > 1; --i) {
    invoke(path[i - 1], true, 1);
    if (state->stopped) break;
  }
  // A stop at an ancestor prevents reaching the target. At the target,
  // stopPropagation still permits the other listeners on that same node.
  if (!state->stopped) {
    invoke(target, true, 2);
    const bool immediate_at_target = state->immediate;
    if (!immediate_at_target) invoke(target, false, 2);
  }
  if (bubbles && !state->stopped) {
    for (size_t i = 1; i < path.size(); ++i) {
      invoke(path[i], false, 3);
      if (state->stopped) break;
    }
  }
  event_object->properties["currentTarget"] = js::Value::null();
  event_object->properties["eventPhase"] = js::Value(0);
  try {
    js_->drain_microtasks();
  } catch (js::JsError &error) {
    diagnostics.push_back({"microtask", error.value.to_string()});
  } catch (std::exception &error) {
    diagnostics.push_back({"microtask", error.what()});
  }
  dirty_ = true;
  return state->prevented;
}
void Page::set_focus(int target) {
  if (target == focused_) return;
  const int previous = focused_;
  if (previous >= 0 && previous < int(doc.nodes.size())) {
    auto focusout = js_->make_object();
    focusout.object->properties["relatedTarget"] =
        target >= 0 && target < int(doc.nodes.size()) ? js_->host_value(target) : js::Value::null();
    dispatch(previous, "focusout", focusout, true, false);
    auto blur = js_->make_object();
    blur.object->properties["relatedTarget"] =
        target >= 0 && target < int(doc.nodes.size()) ? js_->host_value(target) : js::Value::null();
    dispatch(previous, "blur", blur, false, false);
  }
  focused_ = target >= 0 && target < int(doc.nodes.size()) ? target : -1;
  if (focused_ >= 0) {
    auto focusin = js_->make_object();
    focusin.object->properties["relatedTarget"] =
        previous >= 0 && previous < int(doc.nodes.size()) ? js_->host_value(previous) : js::Value::null();
    dispatch(focused_, "focusin", focusin, true, false);
    auto focus = js_->make_object();
    focus.object->properties["relatedTarget"] =
        previous >= 0 && previous < int(doc.nodes.size()) ? js_->host_value(previous) : js::Value::null();
    dispatch(focused_, "focus", focus, false, false);
  }
  dirty_ = true;
}

static bool page_control_disabled(const Document &doc, int id);
static int focusable_element(const Document &doc, int target);
static int page_form_owner(const Document &doc, int id) {
  if (id < 0 || id >= int(doc.nodes.size())) return -1;
  const std::string explicit_form = doc.nodes[id].attribute("form");
  if (!explicit_form.empty()) {
    int owner = doc.by_id(explicit_form);
    return owner >= 0 && doc.nodes[owner].name == "form" ? owner : -1;
  }
  for (int node = doc.nodes[id].parent; node >= 0; node = doc.nodes[node].parent)
    if (doc.nodes[node].name == "form") return node;
  return -1;
}
static int label_control(const Document &doc, int label) {
  if (label < 0 || label >= int(doc.nodes.size())) return -1;
  auto labelable = [&](int node) {
    if (node < 0 || node >= int(doc.nodes.size())) return false;
    const Node &candidate = doc.nodes[node];
    if (candidate.name == "input" && lower(candidate.attribute("type", "text")) == "hidden") return false;
    return candidate.name == "input" || candidate.name == "button" ||
           candidate.name == "select" || candidate.name == "textarea";
  };
  const std::string for_id = doc.nodes[label].attribute("for");
  if (!for_id.empty()) {
    const int target = doc.by_id(for_id);
    return labelable(target) ? target : -1;
  }
  std::vector<int> pending;
  for (auto child = doc.nodes[label].children.rbegin(); child != doc.nodes[label].children.rend(); ++child)
    pending.push_back(*child);
  while (!pending.empty()) {
    const int node = pending.back(); pending.pop_back();
    if (labelable(node)) return node;
    for (auto child = doc.nodes[node].children.rbegin(); child != doc.nodes[node].children.rend(); ++child)
      pending.push_back(*child);
  }
  return -1;
}
void Page::activate(int target) {
  if (target < 0 || target >= int(doc.nodes.size()))
    return;
  auto event = js_->make_object();
  event.object->properties["type"] = js::Value("click");
  event.object->properties["target"] = js_->host_value(target);
  event.object->properties["detail"] = js::Value(1);
  event.object->properties["button"] = js::Value(0);
  if (dispatch(target, "click", event, true, true)) return;
  for (int node = target; node >= 0 && node < int(doc.nodes.size()); node = doc.nodes[node].parent)
    if (page_control_disabled(doc, node)) return;
  if (doc.nodes[target].name == "label") {
    const int control = label_control(doc, target);
    if (control >= 0 && !page_control_disabled(doc, control)) {
      set_focus(focusable_element(doc, control));
      activate(control);
    }
    return;
  }
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
    if (node.name == "input" && (lower(node.attribute("type")) == "checkbox" ||
                                  lower(node.attribute("type")) == "radio")) {
      const std::string type = lower(node.attribute("type"));
      std::string checked_value;
      const bool was_checked = node.get_attribute("checked", checked_value);
      bool changed = false;
      if (type == "checkbox") {
        if (was_checked)
          std::erase_if(doc.nodes[n].attributes, [](auto &attribute) { return attribute.first == "checked"; });
        else doc.nodes[n].set_attribute("checked", "");
        changed = true;
      } else if (!was_checked) {
        const std::string name = node.attribute("name");
        const int owner = page_form_owner(doc, n);
        for (int other = 0; !name.empty() && other < int(doc.nodes.size()); ++other) {
          if (other == n || doc.nodes[other].name != "input" ||
              lower(doc.nodes[other].attribute("type")) != "radio" ||
              doc.nodes[other].attribute("name") != name || page_form_owner(doc, other) != owner) continue;
          changed |= doc.nodes[other].get_attribute("checked", checked_value);
          std::erase_if(doc.nodes[other].attributes,
                        [](auto &attribute) { return attribute.first == "checked"; });
        }
        doc.nodes[n].set_attribute("checked", "");
        changed = true;
      }
      dirty_ = true;
      if (changed) {
        dispatch(n, "input", js_->make_object(), true, false);
        dispatch(n, "change", js_->make_object(), true, false);
      }
      return;
    }
    const std::string button_type = lower(node.attribute("type", "submit"));
    if ((node.name == "button" && button_type == "reset") ||
        (node.name == "input" && lower(node.attribute("type")) == "reset"))
      return;
    if ((node.name == "button" && button_type == "submit") ||
        (node.name == "input" &&
         (lower(node.attribute("type")) == "submit" || lower(node.attribute("type")) == "image"))) {
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
void Page::pointer_move(double x, double y) {
  if (dirty_) rebuild();
  if (selecting_text_ && selection_node_ >= 0 && selection_node_ < int(doc.nodes.size())) {
    const size_t position = caret_at(selection_node_, x);
    if (position != selection_anchor_) {
      auto &selection = editor_selection(selection_node_);
      selection.start = std::min(selection_anchor_, position);
      selection.end = std::max(selection_anchor_, position);
      selection.direction = position < selection_anchor_ ? "backward" : "forward";
      editor_dragged_ = true;
      dirty_ = true;
    }
  }
  if (scroll_drag_node_ >= 0) {
    auto found = layout_.scroll_containers.find(scroll_drag_node_);
    if (found == layout_.scroll_containers.end()) {
      scroll_drag_node_ = -1;
    } else {
      const auto &scrollbar = found->second;
      const double travel = std::max(1.0, scrollbar.track.h - scrollbar.thumb.h);
      const double offset = std::clamp(scroll_drag_start_offset_ +
          (y - scroll_drag_start_y_) * scrollbar.maximum / travel, 0.0, scrollbar.maximum);
      if (local_scroll_[scroll_drag_node_] != offset) {
        local_scroll_[scroll_drag_node_] = offset;
        dirty_ = true;
      }
      return;
    }
  }
  const int previous = hovered_;
  const int target = hit(x, y);
  pointer_x_ = x; pointer_y_ = y;
  if (previous != target) {
    auto transition = [&](int node, const std::string &type, int related, bool bubbles) {
      if (node < 0) return;
      js::Value event = js_->make_object();
      event.object->properties["relatedTarget"] =
          related >= 0 ? js_->host_value(related) : js::Value::null();
      event.object->properties["clientX"] = js::Value(x);
      event.object->properties["clientY"] = js::Value(y);
      dispatch(node, type, event, bubbles, false);
    };
    transition(previous, "pointerout", target, true);
    transition(previous, "mouseout", target, true);
    transition(previous, "pointerleave", target, false);
    transition(previous, "mouseleave", target, false);
    transition(target, "pointerover", previous, true);
    transition(target, "mouseover", previous, true);
    transition(target, "pointerenter", previous, false);
    transition(target, "mouseenter", previous, false);
    hovered_ = target;
    dirty_ = true;
  }
  auto movement = [&](const std::string &type) {
    if (target < 0) return;
    js::Value event = js_->make_object();
    event.object->properties["clientX"] = js::Value(x);
    event.object->properties["clientY"] = js::Value(y);
    event.object->properties["button"] = js::Value(-1);
    event.object->properties["buttons"] = js::Value(active_ >= 0 ? 1 : 0);
    dispatch(target, type, event, true, false);
  };
  movement("pointermove");
  movement("mousemove");
}
bool Page::wheel(double x, double y, double delta) {
  if (dirty_) rebuild();
  if (!std::isfinite(delta) || delta == 0) return false;
  const int target = hit(x, y);
  double remaining = delta;
  bool changed = false;
  for (int node = target; node >= 0 && node < int(doc.nodes.size()); node = doc.nodes[node].parent) {
    auto found = layout_.scroll_containers.find(node);
    if (found == layout_.scroll_containers.end() ||
        !found->second.viewport.contains(x, y + scroll_)) continue;
    const auto &container = found->second;
    const double next = std::clamp(container.offset + remaining, 0.0, container.maximum);
    const double consumed = next - container.offset;
    if (std::abs(consumed) > 0.001) {
      local_scroll_[node] = next;
      remaining -= consumed;
      changed = true;
      dirty_ = true;
    }
    if (std::abs(remaining) < 0.001 ||
        container.overscroll == "contain" || container.overscroll == "none") return true;
  }
  return changed;
}
bool Page::scroll_key(const std::string &key) {
  if (dirty_) rebuild();
  const bool supported = key == "Home" || key == "End" || key == "PageUp" ||
      key == "PageDown" || key == "ArrowUp" || key == "ArrowDown";
  if (!supported) return false;
  const int target = focused_ >= 0 ? focused_ : doc.body;
  js::Value event = js_->make_object();
  event.object->properties["key"] = js::Value(key);
  event.object->properties["code"] = js::Value(key);
  event.object->properties["target"] = js_->host_value(target);
  if (dispatch(target, "keydown", event)) return true;
  return scroll_key_default(key, target);
}
void Page::pointer_down(double x, double y) {
  if (dirty_) rebuild();
  suppress_click_ = false;
  scrollbar_pointer_ = false;
  scroll_drag_node_ = -1;
  pointer_click_pending_ = false;
  pointer_click_allowed_ = false;
  pointer_down_target_ = -1;
  selecting_text_ = false;
  editor_dragged_ = false;
  selection_node_ = -1;
  const double document_y = y + scroll_;
  for (const auto &[node, scrollbar] : layout_.scroll_containers) {
    if (!scrollbar.track.contains(x, document_y)) continue;
    bool visible = true;
    for (int parent = doc.nodes[node].parent; parent >= 0; parent = doc.nodes[parent].parent) {
      auto clip = layout_.clips.find(parent);
      if (clip != layout_.clips.end() && !clip->second.contains(x, document_y)) {
        visible = false; break;
      }
    }
    if (!visible) continue;
    scrollbar_pointer_ = true;
    active_ = -1;
    hovered_ = -1;
    if (scrollbar.thumb.contains(x, document_y)) {
      scroll_drag_node_ = node;
      scroll_drag_start_y_ = y;
      scroll_drag_start_offset_ = scrollbar.offset;
    } else {
      const double travel = std::max(1.0, scrollbar.track.h - scrollbar.thumb.h);
      const double fraction = std::clamp((document_y - scrollbar.track.y - scrollbar.thumb.h / 2) /
                                         travel, 0.0, 1.0);
      local_scroll_[node] = fraction * scrollbar.maximum;
      dirty_ = true;
    }
    dirty_ = true;
    return;
  }
  pointer_click_pending_ = false;
  pointer_click_allowed_ = false;
  pointer_x_ = x; pointer_y_ = y;
  const int target = hit(x, y);
  pointer_down_target_ = target;
  bool is_disabled = false;
  for (int node = target; node >= 0 && node < int(doc.nodes.size()); node = doc.nodes[node].parent)
    if (page_control_disabled(doc, node)) { is_disabled = true; break; }
  active_ = is_disabled ? -1 : target;
  hovered_ = target;
  dirty_ = true;
  if (target < 0) return;
  auto make_pointer_event = [&]() {
    js::Value event = js_->make_object();
    event.object->properties["clientX"] = js::Value(x);
    event.object->properties["clientY"] = js::Value(y);
    event.object->properties["button"] = js::Value(0);
    event.object->properties["buttons"] = js::Value(1);
    event.object->properties["pointerId"] = js::Value(1);
    event.object->properties["pointerType"] = js::Value("mouse");
    event.object->properties["isPrimary"] = js::Value(true);
    return event;
  };
  const bool pointer_prevented = dispatch(target, "pointerdown", make_pointer_event());
  bool mouse_prevented = false;
  if (!pointer_prevented)
    mouse_prevented = dispatch(target, "mousedown", make_pointer_event());
  if (!pointer_prevented && !mouse_prevented && !is_disabled) {
    const int focus_target = focusable_element(doc, target);
    set_focus(focus_target);
    if (focus_target >= 0 && text_editor_control(doc.nodes[focus_target])) {
      selection_node_ = focus_target;
      selection_anchor_ = caret_at(focus_target, x);
      auto &selection = editor_selection(focus_target);
      selection.start = selection.end = selection_anchor_;
      selection.direction = "none";
      selecting_text_ = true;
      dirty_ = true;
    }
  }
}
void Page::pointer_up(double x, double y) {
  if (dirty_) rebuild();
  if (scrollbar_pointer_) {
    suppress_click_ = true;
    scrollbar_pointer_ = false;
    scroll_drag_node_ = -1;
    pointer_down_target_ = -1;
    pointer_click_pending_ = false;
    return;
  }
  pointer_x_ = x; pointer_y_ = y;
  const int target = hit(x, y);
  auto make_pointer_event = [&]() {
    js::Value event = js_->make_object();
    event.object->properties["clientX"] = js::Value(x);
    event.object->properties["clientY"] = js::Value(y);
    event.object->properties["button"] = js::Value(0);
    event.object->properties["buttons"] = js::Value(0);
    event.object->properties["pointerId"] = js::Value(1);
    event.object->properties["pointerType"] = js::Value("mouse");
    event.object->properties["isPrimary"] = js::Value(true);
    return event;
  };
  if (target >= 0) {
    dispatch(target, "pointerup", make_pointer_event());
    dispatch(target, "mouseup", make_pointer_event());
  }
  pointer_click_pending_ = pointer_down_target_ >= 0;
  pointer_click_allowed_ = pointer_down_target_ == target && target >= 0;
  selecting_text_ = false;
  pointer_down_target_ = -1;
  active_ = -1;
  if (hovered_ != target) {
    hovered_ = target;
    dirty_ = true;
  }
}
static bool page_control_disabled(const Document &doc, int id) {
  if (id < 0 || id >= int(doc.nodes.size())) return false;
  const Node &node = doc.nodes[id];
  const bool control = node.name == "button" || node.name == "input" || node.name == "select" ||
                       node.name == "textarea" || node.name == "option" || node.name == "optgroup" ||
                       node.name == "fieldset";
  if (!control) return false;
  std::string ignored;
  if (node.get_attribute("disabled", ignored)) return true;
  if (node.name == "option" && node.parent >= 0 &&
      doc.nodes[node.parent].name == "optgroup" &&
      doc.nodes[node.parent].get_attribute("disabled", ignored)) return true;
  for (int parent = node.parent; parent >= 0; parent = doc.nodes[parent].parent)
    if (doc.nodes[parent].name == "fieldset" &&
        doc.nodes[parent].get_attribute("disabled", ignored)) return true;
  return false;
}
static int focusable_element(const Document &doc, int target) {
  for (int node = target; node >= 0 && node < int(doc.nodes.size()); node = doc.nodes[node].parent) {
    const Node &candidate = doc.nodes[node];
    if (candidate.type != Node::Type::Element || page_control_disabled(doc, node)) continue;
    std::string ignored;
    if (candidate.name == "a" && candidate.get_attribute("href", ignored)) return node;
    if (candidate.name == "button" || candidate.name == "input" || candidate.name == "select" ||
        candidate.name == "textarea" || candidate.get_attribute("tabindex", ignored)) return node;
  }
  return -1;
}
void Page::click(double x, double y) {
  if (suppress_click_) { suppress_click_ = false; return; }
  if (pointer_click_pending_) {
    const bool allowed = pointer_click_allowed_;
    pointer_click_pending_ = pointer_click_allowed_ = false;
    if (!allowed || editor_dragged_) {
      editor_dragged_ = false;
      selecting_text_ = false;
      return;
    }
  }
  note_user_activation();
  if (dirty_) rebuild();
  int target = hit(x, y);
  hovered_ = target;
  active_ = -1;
  const int focus_target = focusable_element(doc, target);
  set_focus(focus_target);
  if (focus_target >= 0 && text_editor_control(doc.nodes[focus_target])) {
    const size_t caret = caret_at(focus_target, x);
    auto &selection = editor_selection(focus_target);
    selection.start = selection.end = caret;
    selection.direction = "none";
    selection_node_ = -1;
  }
  dirty_ = true;
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
    set_focus(n);
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
  if (target >= 0) activate(target);
}
std::string Page::editor_value(int node) const {
  if (node < 0 || node >= int(doc.nodes.size())) return {};
  const Node &element = doc.nodes[node];
  return element.name == "textarea" ? doc.text_content(node) : element.attribute("value");
}
Page::TextSelection &Page::editor_selection(int node) {
  auto [at, inserted] = selections_.try_emplace(node);
  const size_t length = utf16_length(editor_value(node));
  if (inserted) at->second.start = at->second.end = length;
  at->second.start = std::min(at->second.start, length);
  at->second.end = std::clamp(at->second.end, at->second.start, length);
  return at->second;
}
void Page::set_editor_value(int node, std::string value, bool record_undo) {
  if (node < 0 || node >= int(doc.nodes.size()) || !text_editor_control(doc.nodes[node])) return;
  std::string old = editor_value(node);
  if (old == value) return;
  if (record_undo) {
    auto &history = editor_history_[node];
    history.undo.push_back(old);
    if (history.undo.size() > 100) history.undo.erase(history.undo.begin());
    history.redo.clear();
  }
  if (doc.nodes[node].name == "textarea") doc.set_text(node, value);
  else doc.nodes[node].set_attribute("value", value);
  auto &selection = editor_selection(node);
  selection.start = selection.end = utf16_length(value);
  selection.direction = "none";
  dirty_ = true;
}
void Page::replace_editor_selection(int node, const std::string &inserted) {
  if (node < 0 || node >= int(doc.nodes.size()) || !text_editor_control(doc.nodes[node])) return;
  const std::string value = editor_value(node);
  auto &selection = editor_selection(node);
  const size_t start = utf16_to_byte(value, selection.start);
  const size_t end = utf16_to_byte(value, selection.end);
  std::string updated = value.substr(0, start) + inserted + value.substr(end);
  if (updated == value) return;
  auto &history = editor_history_[node];
  history.undo.push_back(value);
  if (history.undo.size() > 100) history.undo.erase(history.undo.begin());
  history.redo.clear();
  if (doc.nodes[node].name == "textarea") doc.set_text(node, updated);
  else doc.nodes[node].set_attribute("value", updated);
  const size_t caret = selection.start + utf16_length(inserted);
  selection.start = selection.end = caret;
  selection.direction = "none";
  dirty_ = true;
  dispatch(node, "input", js_->make_object(), true, false);
}
bool Page::scroll_key_default(const std::string &key, int target) {
  if (target < 0 || target >= int(doc.nodes.size())) target = doc.body;
  const bool home = key == "Home", end = key == "End";
  const bool page_up = key == "PageUp", page_down = key == "PageDown";
  const bool up = key == "ArrowUp", down = key == "ArrowDown";
  if (!home && !end && !page_up && !page_down && !up && !down) return false;
  double remaining = 0;
  bool found_container = false;
  for (int node = target; node >= 0 && node < int(doc.nodes.size()); node = doc.nodes[node].parent) {
    auto found = layout_.scroll_containers.find(node);
    if (found == layout_.scroll_containers.end()) continue;
    found_container = true;
    const auto &container = found->second;
    if (home || end) {
      const double next = home ? 0 : container.maximum;
      if (std::abs(next - container.offset) > 0.001) {
        local_scroll_[node] = next; dirty_ = true;
      }
      return true;
    }
    if (remaining == 0)
      remaining = (page_up ? -std::max(40.0, container.viewport.h * 0.8)
                   : page_down ? std::max(40.0, container.viewport.h * 0.8)
                   : up ? -40.0 : 40.0);
    const double next = std::clamp(container.offset + remaining, 0.0, container.maximum);
    remaining -= next - container.offset;
    if (std::abs(next - container.offset) > 0.001) {
      local_scroll_[node] = next; dirty_ = true;
    }
    if (std::abs(remaining) < 0.001 || container.overscroll == "contain" ||
        container.overscroll == "none") return true;
  }
  if (!found_container || std::abs(remaining) > 0.001) {
    const double step = key == "ArrowUp" ? -40 : key == "ArrowDown" ? 40
                       : key == "PageUp" ? -std::max(40.0, height_ * 0.8)
                       : key == "PageDown" ? std::max(40.0, height_ * 0.8)
                       : key == "Home" ? -scroll_ : key == "End" ? content_height_ - scroll_ : 0;
    const double next = std::clamp(scroll_ + step, 0.0, std::max(0.0, content_height_ - height_));
    if (std::abs(next - scroll_) > 0.001) {
      scroll_ = next;
      dirty_ = true;
      return true;
    }
  }
  return false;
}
size_t Page::caret_at(int node, double x) const {
  const std::string value = editor_value(node);
  double text_x = layout_.boxes.count(node) ? layout_.boxes.at(node).x + 8 : 0;
  double font_size = 16;
  bool bold = false;
  for (const auto &command : layout_.paint)
    if (command.node == node && command.kind == PaintCommand::Kind::Text) {
      text_x = command.rect.x;
      font_size = command.size;
      bold = command.bold;
      break;
    }
  Canvas measure{1, 1};
  size_t best = 0;
  double previous = text_x;
  for (uint32_t cp : utf8_decode(value)) {
    const size_t units = cp > 0xffff ? 2 : 1;
    const size_t next = best + units;
    const std::string prefix = value.substr(0, utf16_to_byte(value, next));
    const double position = text_x + measure.text_width(prefix, font_size, bold);
    if (x < (previous + position) * 0.5) return best;
    best = next;
    previous = position;
  }
  return best;
}
bool Page::editor_key(int node, const std::string &key, const std::string &text) {
  if (node < 0 || node >= int(doc.nodes.size()) || !text_editor_control(doc.nodes[node])) return false;
  auto &selection = editor_selection(node);
  const std::string value = editor_value(node);
  const size_t length = utf16_length(value);
  const bool has_selection = selection.start != selection.end;
  if (control_down_ && (key == "a" || key == "A")) {
    selection.start = 0; selection.end = length; selection.direction = "forward"; dirty_ = true; return true;
  }
  if (control_down_ && (key == "z" || key == "Z" || key == "y" || key == "Y")) {
    const bool redo = key == "y" || key == "Y" || (shift_down_ && (key == "z" || key == "Z"));
    auto &history = editor_history_[node];
    auto &from = redo ? history.redo : history.undo;
    auto &to = redo ? history.undo : history.redo;
    if (!from.empty()) {
      to.push_back(value);
      std::string restored = std::move(from.back()); from.pop_back();
      if (doc.nodes[node].name == "textarea") doc.set_text(node, restored);
      else doc.nodes[node].set_attribute("value", restored);
      selection.start = selection.end = utf16_length(restored);
      dirty_ = true;
      dispatch(node, "input", js_->make_object(), true, false);
    }
    return true;
  }
  if (control_down_ && (key == "c" || key == "C")) { edit_clipboard(false); return true; }
  if (control_down_ && (key == "x" || key == "X")) { edit_clipboard(true); return true; }
  if (control_down_ && (key == "v" || key == "V")) { replace_editor_selection(node, clipboard_text_); return true; }
  if (key == "Backspace" || key == "Delete") {
    if (!has_selection) {
      if (key == "Backspace" && selection.start > 0) {
        const size_t byte = utf16_to_byte(value, selection.start);
        size_t previous = byte;
        while (previous > 0 && (uint8_t(value[previous - 1]) & 0xc0) == 0x80) --previous;
        if (previous > 0) --previous;
        selection.start = byte_to_utf16(value, previous);
      } else if (key == "Delete" && selection.end < length) {
        const size_t byte = utf16_to_byte(value, selection.end);
        size_t next = byte + 1;
        while (next < value.size() && (uint8_t(value[next]) & 0xc0) == 0x80) ++next;
        selection.end = byte_to_utf16(value, next);
      } else return true;
    }
    replace_editor_selection(node, "");
    return true;
  }
  if (key == "ArrowLeft" || key == "ArrowRight" || key == "ArrowUp" ||
      key == "ArrowDown" || key == "Home" || key == "End") {
    size_t next = selection.direction == "backward" ? selection.start : selection.end;
    if ((key == "ArrowUp" || key == "ArrowDown") && doc.nodes[node].name == "textarea") {
      const size_t byte = utf16_to_byte(value, selection.end);
      const size_t line_start = byte == 0 ? 0 : value.rfind('\n', byte - 1) == std::string::npos
          ? 0 : value.rfind('\n', byte - 1) + 1;
      const size_t line_end_at = value.find('\n', byte);
      const size_t line_end = line_end_at == std::string::npos ? value.size() : line_end_at;
      const size_t column = byte - line_start;
      if (key == "ArrowUp" && line_start > 0) {
        const size_t prior_end = line_start - 1;
        const size_t prior_start_at = prior_end == 0 ? std::string::npos : value.rfind('\n', prior_end - 1);
        const size_t prior_start = prior_start_at == std::string::npos ? 0 : prior_start_at + 1;
        next = byte_to_utf16(value, prior_start + std::min(column, prior_end - prior_start));
      } else if (key == "ArrowDown" && line_end < value.size()) {
        const size_t next_start = line_end + 1;
        const size_t next_end_at = value.find('\n', next_start);
        const size_t next_end = next_end_at == std::string::npos ? value.size() : next_end_at;
        next = byte_to_utf16(value, next_start + std::min(column, next_end - next_start));
      }
    } else if (key == "ArrowLeft") {
      if (has_selection && !shift_down_) next = selection.start;
      else if (next > 0) {
        size_t byte = utf16_to_byte(value, next);
        if (byte > 0) {
          --byte; while (byte > 0 && (uint8_t(value[byte]) & 0xc0) == 0x80) --byte;
          next = byte_to_utf16(value, byte);
        }
      }
    } else if (key == "ArrowRight") {
      if (has_selection && !shift_down_) next = selection.end;
      else if (next < length) {
        size_t byte = utf16_to_byte(value, next), end = byte + 1;
        while (end < value.size() && (uint8_t(value[end]) & 0xc0) == 0x80) ++end;
        next = byte_to_utf16(value, end);
      }
    } else if (key == "Home" || key == "End") {
      if (doc.nodes[node].name == "textarea") {
        const size_t byte = utf16_to_byte(value, selection.end);
        const size_t line_start = byte == 0 ? 0 : value.rfind('\n', byte - 1) == std::string::npos
            ? 0 : value.rfind('\n', byte - 1) + 1;
        const size_t line_end_at = value.find('\n', byte);
        const size_t line_end = line_end_at == std::string::npos ? value.size() : line_end_at;
        next = byte_to_utf16(value, key == "Home" ? line_start : line_end);
      } else next = key == "Home" ? 0 : length;
    }
    if (shift_down_) {
      if (selection_node_ != node) {
        selection_anchor_ = has_selection ? selection.start : selection.end;
        selection_node_ = node;
      }
      selection.start = std::min(selection_anchor_, next);
      selection.end = std::max(selection_anchor_, next);
      selection.direction = next < selection_anchor_ ? "backward" : "forward";
    } else {
      selection.start = selection.end = next;
      selection.direction = "none";
      selection_node_ = -1;
    }
    dirty_ = true;
    return true;
  }
  if (key == "Enter" && doc.nodes[node].name != "textarea") return false;
  if (key == "Enter" && doc.nodes[node].name == "textarea") {
    replace_editor_selection(node, "\n");
    return true;
  }
  if (!text.empty() && !control_down_) {
    replace_editor_selection(node, text);
    return true;
  }
  return false;
}
std::string Page::edit_clipboard(bool cut) {
  const int node = focused_;
  if (node < 0 || node >= int(doc.nodes.size()) || !text_editor_control(doc.nodes[node])) return {};
  auto &selection = editor_selection(node);
  if (selection.start == selection.end) return {};
  const std::string value = editor_value(node);
  clipboard_text_ = value.substr(utf16_to_byte(value, selection.start),
                                 utf16_to_byte(value, selection.end) - utf16_to_byte(value, selection.start));
  auto event = js_->make_object();
  event.object->properties["text"] = js::Value(clipboard_text_);
  if (dispatch(node, cut ? "cut" : "copy", event, true, true)) {
    clipboard_text_.clear();
    return {};
  }
  if (cut) replace_editor_selection(node, "");
  return clipboard_text_;
}
void Page::paste_text(const std::string &text) {
  if (text.size() > 65536 || !valid_editor_utf8(text) || focused_ < 0 ||
      focused_ >= int(doc.nodes.size()) || !text_editor_control(doc.nodes[focused_])) return;
  auto event = js_->make_object();
  event.object->properties["text"] = js::Value(text);
  if (dispatch(focused_, "paste", event, true, true)) return;
  replace_editor_selection(focused_, text);
}
void Page::composition_start() {
  const int node = focused_;
  if (node < 0 || node >= int(doc.nodes.size()) || !text_editor_control(doc.nodes[node])) return;
  auto &state = compositions_[node];
  if (state.active) return;
  const auto &selection = editor_selection(node);
  state = {editor_value(node), {}, selection.start, selection.end, true};
  dispatch(node, "compositionstart", js_->make_object(), true, false);
}
void Page::composition_update(const std::string &preedit, size_t caret) {
  const int node = focused_;
  auto found = compositions_.find(node);
  if (found == compositions_.end() || !found->second.active || preedit.size() > 4096 ||
      !valid_editor_utf8(preedit) || !utf8_byte_boundary(preedit, caret)) return;
  auto &state = found->second;
  state.preedit = preedit;
  const std::string before = state.original.substr(0, utf16_to_byte(state.original, state.start));
  const std::string after = state.original.substr(utf16_to_byte(state.original, state.end));
  const size_t composition_caret = byte_to_utf16(preedit, caret);
  std::string value = before + preedit + after;
  if (doc.nodes[node].name == "textarea") doc.set_text(node, value);
  else doc.nodes[node].set_attribute("value", value);
  auto &selection = editor_selection(node);
  selection.start = selection.end = state.start + composition_caret;
  dirty_ = true;
  auto event = js_->make_object();
  event.object->properties["data"] = js::Value(preedit);
  dispatch(node, "compositionupdate", event, true, false);
  dispatch(node, "input", js_->make_object(), true, false);
}
void Page::composition_commit(const std::string &text) {
  const int node = focused_;
  auto found = compositions_.find(node);
  if (found == compositions_.end() || !found->second.active || !valid_editor_utf8(text) || text.size() > 4096) return;
  auto state = std::move(found->second);
  compositions_.erase(found);
  if (doc.nodes[node].name == "textarea") doc.set_text(node, state.original);
  else doc.nodes[node].set_attribute("value", state.original);
  auto &selection = editor_selection(node);
  selection.start = state.start; selection.end = state.end;
  replace_editor_selection(node, text);
  auto event = js_->make_object();
  event.object->properties["data"] = js::Value(text);
  dispatch(node, "compositionend", event, true, false);
}
void Page::composition_cancel() {
  const int node = focused_;
  auto found = compositions_.find(node);
  if (found == compositions_.end() || !found->second.active) return;
  auto state = std::move(found->second);
  compositions_.erase(found);
  if (doc.nodes[node].name == "textarea") doc.set_text(node, state.original);
  else doc.nodes[node].set_attribute("value", state.original);
  auto &selection = editor_selection(node);
  selection.start = state.start; selection.end = state.end;
  dirty_ = true;
  auto event = js_->make_object();
  event.object->properties["data"] = js::Value();
  dispatch(node, "compositionend", event, true, false);
}

void Page::key(const std::string &key, const std::string &text) {
  const int event_target = focused_ >= 0 && focused_ < int(doc.nodes.size()) ? focused_ : doc.body;
  if (!key.empty() || !text.empty()) {
    std::string event_key = key.empty() ? text : key;
    if (event_key == "Space") event_key = " ";
    if (!key.empty()) {
      js::Value event = js_->make_object();
      event.object->properties["key"] = js::Value(event_key);
      event.object->properties["code"] = js::Value(event_key);
      event.object->properties["target"] = js_->host_value(event_target);
      if (dispatch(event_target, "keydown", event)) return;
    }
    if (!text.empty()) {
      js::Value press = js_->make_object();
      press.object->properties["key"] = js::Value(event_key);
      press.object->properties["data"] = js::Value(text);
      press.object->properties["target"] = js_->host_value(event_target);
      if (dispatch(event_target, "keypress", press)) return;
    }
  }
  if (key == "Control") { control_down_ = true; return; }
  if (key == "Shift") { shift_down_ = true; selection_node_ = -1; return; }
  if (key == "Alt") return;
  if (key == "Escape" && compositions_.count(focused_) && compositions_[focused_].active) {
    composition_cancel();
    return;
  }
  if (focused_ >= 0 && focused_ < int(doc.nodes.size()) &&
      editor_key(focused_, key, text)) return;
  const bool scroll_navigation = key == "ArrowUp" || key == "ArrowDown" || key == "PageUp" ||
                                key == "PageDown" || key == "Home" || key == "End";
  if (scroll_navigation && (event_target < 0 || event_target >= int(doc.nodes.size()) ||
                            (!text_editor_control(doc.nodes[event_target]) &&
                             doc.nodes[event_target].name != "select"))) {
    scroll_key_default(key, event_target);
    return;
  }
  if (focused_ >= 0 && focused_ < int(doc.nodes.size()) && doc.nodes[focused_].name == "select" &&
      (key == "ArrowDown" || key == "ArrowUp" || key == "Home" || key == "End")) {
    Node &select = doc.nodes[focused_];
    std::vector<int> options;
    for (int option : select.children)
      if (doc.nodes[option].name == "option" && !page_control_disabled(doc, option)) options.push_back(option);
    if (!options.empty()) {
      int selected = -1;
      for (size_t i = 0; i < options.size(); ++i)
        if (has_attr(doc.nodes[options[i]], "selected")) { selected = int(i); break; }
      const int current = selected < 0 ? 0 : selected;
      const int next = key == "Home" ? 0 : key == "End" ? int(options.size()) - 1
                    : std::clamp(current + (key == "ArrowDown" ? 1 : -1), 0, int(options.size()) - 1);
      if (selected != next) {
        for (int option : select.children)
          if (doc.nodes[option].name == "option")
            std::erase_if(doc.nodes[option].attributes,
                          [](auto &attribute) { return attribute.first == "selected"; });
        doc.nodes[options[next]].set_attribute("selected", "");
        dirty_ = true;
        dispatch(focused_, "input", js_->make_object(), true, false);
        dispatch(focused_, "change", js_->make_object(), true, false);
      }
    }
    return;
  }
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
  if (focused_ < 0 || focused_ >= int(doc.nodes.size())) {
    if (key != "Tab") return;
  }
  if (key == "Tab") {
    std::vector<int> tabbable;
    for (int node = 0; node < int(doc.nodes.size()); ++node)
      if (focusable_element(doc, node) == node) {
        std::string raw = doc.nodes[node].attribute("tabindex");
        int order = 0;
        try { if (!raw.empty()) order = std::stoi(raw); } catch (...) { order = 0; }
        if (order >= 0) tabbable.push_back(node);
      }
    if (!tabbable.empty()) {
      auto current = std::find(tabbable.begin(), tabbable.end(), focused_);
      const size_t next = current == tabbable.end() || ++current == tabbable.end()
          ? 0 : size_t(current - tabbable.begin());
      set_focus(tabbable[next]);
    }
    return;
  }
  if (focused_ < 0 || focused_ >= int(doc.nodes.size())) return;
  auto &n = doc.nodes[focused_];
  if (key == "Enter") {
    int form = n.parent;
    while (form >= 0 && doc.nodes[form].name != "form")
      form = doc.nodes[form].parent;
    if (form >= 0)
      submit_form(form);
  }
}
void Page::key_up(const std::string &key) {
  const int target = focused_ >= 0 && focused_ < int(doc.nodes.size()) ? focused_ : doc.body;
  js::Value event = js_->make_object();
  event.object->properties["key"] = js::Value(key);
  event.object->properties["code"] = js::Value(key);
  event.object->properties["target"] = js_->host_value(target);
  dispatch(target, "keyup", event, true, false);
  if (key == "Control") control_down_ = false;
  if (key == "Shift") { shift_down_ = false; selection_node_ = -1; }
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
  if (form < 0 || form >= int(doc.nodes.size()) || doc.nodes[form].name != "form") return;
  const std::vector<int> controls = doc.query("*", form);
  for (int control : controls) {
    const std::string &name = doc.nodes[control].name;
    if ((name == "input" || name == "select" || name == "textarea") &&
        !form_control_valid(doc, control)) {
      dispatch(control, "invalid", js_->make_object(), false, false);
      return;
    }
  }
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
bool Page::animation_pending() const {
  const double now = double(now_ms());
  for (const auto &[node, animation] : animations_) {
    (void)node;
    if (animation.iterations < 0 || animation.duration_ms <= 0 ||
        now < animation.start_ms + animation.delay_ms +
                  animation.duration_ms * animation.iterations)
      return true;
  }
  for (const auto &[key, transition] : transitions_) {
    (void)key;
    if (now < transition.start_ms + transition.delay_ms + transition.duration_ms)
      return true;
  }
  return false;
}
double Page::next_timer_delay() const {
  double best = js_->timers.empty() ? 86400000.0 : 86400000.0;
  const double now = double(now_ms());
  for (const auto &timer : js_->timers)
    best = std::min(best, std::max(0.0, timer.due - now));
  if (animation_pending()) best = std::min(best, 16.0);
  return best >= 86400000.0 ? -1 : best;
}
void Page::tick() {
  media_tick();
  if (animation_pending()) {
    animation_clock_ms_ = double(now_ms());
    dirty_ = true;
  }
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
