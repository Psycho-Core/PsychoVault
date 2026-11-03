#pragma once
#include "js.hpp"
#include "layout.hpp"
#include "image.hpp"
namespace pv {
class Page {
public:
  Document doc;
  std::vector<Diagnostic> diagnostics;
  std::vector<std::string> requests, navigations, dialogs;
  Page();
  ~Page();
  void load(const Url &url, const std::string &html, const std::string &mime = "text/html");
  void resource(const Resource &res);
  Json render(double width, double height, double scroll);
  void click(double x, double y);
  void key(const std::string &key, const std::string &text);
  double next_timer_delay() const;
  void tick();
  std::string title() const;
  const std::map<std::string, Image> &images() const {
    return images_;
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
  std::set<int> executed_;
  std::set<std::string> requested_;
  LayoutResult layout_;
  double width_ = 1000, height_ = 700, scroll_ = 0, content_height_ = 0;
  int focused_ = -1;
  bool dirty_ = true;
  void run_scripts();
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
