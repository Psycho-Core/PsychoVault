#pragma once
#include "Psycho_canvas.hpp"
#include "Psycho_cookies.hpp"
#include "Psycho_js.hpp"
#include "Psycho_layout.hpp"
#include "Psycho_storage.hpp"
#include "Psycho_image.hpp"
namespace pv {
class Page {
public:
  Document doc;
  std::vector<Diagnostic> diagnostics;
  std::vector<std::string> requests, navigations, dialogs;
  // My host sets this so scripts can fetch with my own network stack.
  std::function<Resource(const std::string &)> fetcher;
  // I keep the cookies and local storage for the page I have loaded. My host
  // hands me a shared jar so every tab sees the same cookies; without a host I
  // keep my own, which is what my tests use.
  CookieJar &cookies();
  void use_cookies(CookieJar *jar);
  Storage &local_storage();
  void use_local_storage(Storage *storage);
  Storage &session_storage() {
    return session_;
  }
  // A form my page submitted. My host turns this into a real request.
  struct FormPost {
    std::string url, method, body, content_type;
  };
  std::vector<FormPost> posts;
  // history.pushState and replaceState, in the order the page set them.
  struct HistoryEntry {
    std::string url;
    js::Value state;
  };
  const std::vector<HistoryEntry> &history() const {
    return history_;
  }
  size_t history_index() const {
    return history_index_;
  }
  void set_history(std::vector<HistoryEntry> entries, size_t index);
  Page();
  ~Page();
  void load(const Url &url, const std::string &html, const std::string &mime = "text/html");
  void resource(const Resource &res);
  Json render(double width, double height, double scroll);
  void click(double x, double y);
  void key(const std::string &key, const std::string &text);
  double next_timer_delay() const;
  void tick();
  // I run whatever timers are due now and report whether I ran any.
  bool run_due_timers();
  std::string title() const;
  const std::map<std::string, Image> &images() const {
    return images_;
  }
  // A canvas paints its own bitmap, so my shell asks for the latest ones.
  const std::map<int, Image> &canvas_images() const {
    return canvas_images_;
  }
  const std::map<int, uint64_t> &canvas_revisions() const {
    return canvas_revisions_;
  }
  double content_height() const {
    return content_height_;
  }

private:
  struct Listener {
    int node;
    std::string type;
    js::Value fn;
  };
  std::unique_ptr<js::Runtime> js_;
  std::vector<Listener> listeners_;
  std::map<std::string, Resource> resources_;
  std::map<std::string, Image> images_;
  std::map<int, Canvas2D> canvases_;
  std::map<int, Gradient> gradients_;
  std::map<int, Image> canvas_images_;
  std::map<int, uint64_t> canvas_revisions_;
  int next_gradient_ = 0;
  std::set<int> executed_;
  std::set<std::string> requested_;
  std::set<std::string> webp_reported_;
  CookieJar own_cookies_;
  CookieJar *jar_ = &own_cookies_;
  Storage own_local_;
  Storage *local_ = &own_local_;
  Storage session_;
  std::vector<HistoryEntry> history_;
  size_t history_index_ = 0;
  LayoutResult layout_;
  double width_ = 1000, height_ = 700, scroll_ = 0, content_height_ = 0;
  int focused_ = -1;
  bool dirty_ = true;
  void run_scripts();
  void install_fetch();
  bool pump_for_await();
  void execute_script(int node);
  void request(std::string url);
  void rebuild();
  void dispatch(int target, const std::string &type, js::Value event);
  int hit(double x, double y) const;
  void submit_form(int form);
  void request_navigation(const std::string &input);
  void add_listener(int node, const std::string &type, js::Value fn);
  void remove_listener(int node, const std::string &type, js::Value fn);
  void activate(int target);
  friend class PageHost;
};
} // namespace pv
