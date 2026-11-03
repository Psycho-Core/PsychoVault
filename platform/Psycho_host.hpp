// PsychoVault original source. I host my own page inside my own process.
// Nothing here talks to the network or to a window: I take a fetch callback and
// a report callback so the same code runs inside my browser and inside tests.
#pragma once
#include "../engine/Psycho_page.hpp"
#include <functional>
#include <memory>
#include <set>
#include <string>

namespace pv {

// One thing my page host wants the embedder to know about.
struct HostEvent {
  std::string tag;       // frame, image, navigate, dialog, log
  Json frame;            // tag == "frame"
  std::string url;       // tag == "image" supplies an image, "navigate" a target
  std::string text;      // tag == "dialog" or "log"
  std::string subsystem; // tag == "log"
  std::shared_ptr<const Image> image;
};

// I own one Page and drive it: fetch what it asks for, then render.
class Host {
public:
  // I hand every URL to this and get a Resource back. It is called on whichever
  // thread drives the host, so it may block.
  using Fetcher = std::function<Resource(const std::string &url)>;
  using Reporter = std::function<void(HostEvent)>;
  Host(Fetcher fetcher, Reporter reporter);
  ~Host();
  Host(const Host &) = delete;
  Host &operator=(const Host &) = delete;

  // I fetch the document, hand it to my engine, then fetch subresources.
  bool load(const std::string &input, std::string &error);
  void resize(double width, double height);
  void scroll(double y);
  void click(double x, double y);
  void key(const std::string &key, const std::string &text);
  // I run any timer that is due and report a fresh frame. Returns true when I
  // did any work, so a caller can idle instead of spinning.
  bool pump();
  bool loaded() const { return loaded_; }
  double next_timer_delay() const;
  double width() const { return width_; }
  double height() const { return height_; }
  double scroll_position() const { return scroll_; }

private:
  Resource fetch(const std::string &url) const;
  void settle();
  void emit();

  std::unique_ptr<Page> page_;
  Fetcher fetch_;
  Reporter report_;
  std::set<std::string> sent_images_;
  double width_ = 1000, height_ = 700, scroll_ = 0;
  bool loaded_ = false;
};
} // namespace pv
