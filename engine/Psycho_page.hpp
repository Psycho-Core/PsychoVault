#pragma once
#include "Psycho_canvas.hpp"
#include "Psycho_cookies.hpp"
#include "Psycho_js.hpp"
#include "Psycho_layout.hpp"
#include "Psycho_storage.hpp"
#include "Psycho_image.hpp"
#include "Psycho_media.hpp"
#include "Psycho_mse.hpp"
#include "Psycho_security.hpp"
namespace pv {
class Page {
public:
  Document doc;
  std::vector<Diagnostic> diagnostics;
  // One thing my page wants from the network, with the kind of thing it is. A filter
  // rule can name a type, so the type is recorded where the request is made rather
  // than guessed from a file extension later.
  struct PageRequest {
    std::string url;
    ResourceType type = ResourceType::Other;
  };
  std::vector<PageRequest> requests;
  std::vector<std::string> navigations, dialogs;
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
  // My autoplay gate: when on, a scripted play() needs a real click within the
  // last couple of seconds, which is how every mainstream gesture gate works.
  bool block_autoplay = false;
  void note_user_activation() { last_activation_ms_ = double(now_ms()); }
  bool user_active_recently() const {
    return double(now_ms()) - last_activation_ms_ < 2500.0;
  }
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
  // Media. My page owns the players, so the clock, the frame and the audio all
  // come from one place.
  const std::map<int, Image> &media_images() const {
    return media_images_;
  }
  const std::map<int, uint64_t> &media_revisions() const {
    return media_revisions_;
  }
  bool media_playing() const;
  // Playing media and a freshly appended segment are work of their own, so my
  // host keeps ticking while either is true.
  bool media_pending() const {
    return media_playing() || media_wake_ || mse_opened_.size() < mse_objects_.size();
  }
  // I move every player to the current moment and rebuild the frame it shows.
  void media_tick(double now = -1);
  size_t pull_audio(int16_t *out, size_t frames, int device_rate);
  MediaPlayer *player_for(int node);
  // A page can attach a media source it built itself.
  std::string create_media_object_url(std::shared_ptr<MediaSourceState> source);
  MediaSourceState *media_source(const std::string &url);
  void revoke_media_object_url(const std::string &url);

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
  std::map<int, MediaPlayer> media_;
  std::map<int, Image> media_images_;
  std::map<int, uint64_t> media_revisions_;
  std::map<std::string, std::shared_ptr<MediaSourceState>> media_sources_;
  std::map<int, std::string> media_src_;
  std::map<int, std::string> media_errors_;
  std::map<int, const MediaData *> media_data_;
  std::map<int, std::shared_ptr<MediaSourceState>> mse_objects_;
  // A source buffer handle points at the media source that owns it and its index.
  std::map<int, std::pair<int, size_t>> mse_buffers_;
  std::set<int> mse_opened_;
  // A page can ask to play before the bytes arrive, so I remember the request and
  // start the moment the element has something to play.
  std::set<int> media_wants_play_;
  double last_activation_ms_ = -1e15;
  // Events that belong to an object rather than an element, keyed by handle.
  std::map<int, std::vector<Listener>> host_listeners_;
  bool media_wake_ = false;
  int next_media_source_ = 1, next_mse_ = 1;
  int next_media_revision_ = 1;
  double media_last_tick_ = 0, media_last_timeupdate_ = 0;
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
  // The media globals: MediaSource, Audio and URL.createObjectURL, plus the events
  // an object owns rather than an element.
  void install_media();
  void dispatch_host(int id, const std::string &type);
  void open_pending_media_sources();
  // I load a media element from whatever src it points at, including a blob URL a
  // page made with a media source.
  void update_media(int node);
  void load_media_nodes();
  Image compose_media_frame(int node, const MediaPlayer &player) const;
  void media_command(int node, Json &paint, size_t at);
  void toggle_media(int node);
  void seek_media(int node, double fraction);
  bool pump_for_await();
  void execute_script(int node);
  void request(std::string url, ResourceType type = ResourceType::Other);
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
