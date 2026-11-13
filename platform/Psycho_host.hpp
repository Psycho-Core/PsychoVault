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
  // Every request carries what kind of thing it is, because my filter rules and my
  // mixed-content rule both need to know.
  using Fetcher = std::function<Resource(const std::string &url, ResourceType type)>;
  // I send a form body through my own network stack, never through a shortcut.
  using Poster = std::function<Resource(const std::string &url, const std::string &body,
                                        const std::string &content_type)>;
  using Reporter = std::function<void(HostEvent)>;
  Host(Fetcher fetcher, Reporter reporter);
  ~Host();
  Host(const Host &) = delete;
  Host &operator=(const Host &) = delete;

  // I fetch the document, hand it to my engine, then fetch subresources.
  bool load(const std::string &input, std::string &error);
  void resize(double width, double height);
  void set_media_environment(const MediaEnvironment &environment);
  void scroll(double y);
  void wheel(double x, double y, double delta);
  void scroll_key(const std::string &key);
  void pointer_move(double x, double y);
  void pointer_down(double x, double y);
  void pointer_up(double x, double y);
  void click(double x, double y);
  void key(const std::string &key, const std::string &text);
  void key_up(const std::string &key);
  std::string edit_clipboard(bool cut);
  void paste_text(const std::string &text);
  void composition_start();
  void composition_update(const std::string &text, size_t caret);
  void composition_commit(const std::string &text);
  void composition_cancel();
  // I run any timer that is due and report a fresh frame. Returns true when I
  // did any work, so a caller can idle instead of spinning.
  bool pump();
  bool loaded() const { return loaded_; }
  // I hand the same jar and the same local storage to every tab.
  void use_cookies(CookieJar *jar);
  void use_local_storage(Storage *storage);
  void set_poster(Poster poster);
  void set_page_bootstrap(std::function<void(js::Runtime &)> bootstrap);
  CookieJar &cookies() { return page_->cookies(); }
  Storage &local_storage() { return page_->local_storage(); }
  void set_autoplay_blocked(bool blocked) { page_->block_autoplay = blocked; }
  Storage &session_storage() { return page_->session_storage(); }
  double next_timer_delay() const;
  // Media. My shell asks whether anything is playing so it can keep its own clock
  // running, and pulls stereo samples at whatever rate its device wants.
  bool media_playing() const;
  size_t pull_audio(int16_t *out, size_t frames, int device_rate);
  double width() const { return width_; }
  double height() const { return height_; }
  double scroll_position() const { return scroll_; }

private:
  Resource fetch(const std::string &url, ResourceType type) const;
  void settle();
  void emit();
  void send_post();

  std::unique_ptr<Page> page_;
  Fetcher fetch_;
  Poster poster_;
  Reporter report_;
  std::string reported_url_;
  std::set<std::string> sent_images_;
  // I remember which canvas revision I last handed over, so an idle canvas is
  // not copied again.
  std::map<int, uint64_t> sent_canvas_;
  // A playing video sends a new frame every tick, so I remember the revision I
  // last handed over rather than comparing pixels.
  std::map<int, uint64_t> sent_media_;
  double width_ = 1000, height_ = 700, scroll_ = 0;
  MediaEnvironment media_environment_;
  bool loaded_ = false;
};
} // namespace pv
