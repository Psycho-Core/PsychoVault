// PsychoVault original source. I drive my own engine inside my own process.
#include "Psycho_host.hpp"
#include <algorithm>
#include <exception>

namespace pv {
namespace {
// A page may pull subresources through CSS imports and scripts, so I allow a
// few rounds. The engine already deduplicates URLs and caps the total.
constexpr int kSubresourceRounds = 8;
} // namespace

Host::Host(Fetcher fetcher, Reporter reporter)
    : page_(std::make_unique<Page>()), fetch_(std::move(fetcher)), report_(std::move(reporter)) {
  // I give my engine the same fetcher I use myself, so a script that calls
  // fetch() reaches the network through my own stack.
  page_->fetcher = [this](const std::string &url) { return fetch(url); };
}
Host::~Host() = default;

void Host::use_cookies(CookieJar *jar) {
  page_->use_cookies(jar);
}
void Host::use_local_storage(Storage *storage) {
  page_->use_local_storage(storage);
}
void Host::set_poster(Poster poster) {
  poster_ = std::move(poster);
}

Resource Host::fetch(const std::string &url) const {
  Resource failure;
  failure.url = url;
  failure.status = 0;
  if (!fetch_) {
    failure.ok = false;
    failure.error = "I have no fetcher, so I cannot load this page";
    return failure;
  }
  try {
    Resource resource = fetch_(url);
    if (resource.url.empty())
      resource.url = url;
    return resource;
  } catch (const std::exception &e) {
    // I never let a fetch failure escape: one broken resource must not kill the
    // page or the thread that drives it.
    Resource resource;
    resource.url = url;
    resource.status = 0;
    resource.ok = false;
    resource.error = e.what();
    return resource;
  }
}

bool Host::load(const std::string &input, std::string &error) {
  auto url = parse_url(input);
  if (!url) {
    error = "Invalid URL";
    return false;
  }
  Resource main = fetch(url->href());
  if (!main.ok) {
    error = main.error.empty() ? "Navigation failed" : main.error;
    return false;
  }
  loaded_ = false;
  sent_images_.clear();
  scroll_ = 0;
  // I honour a redirect: the document's base URL is where the bytes came from,
  // not where I asked for them.
  auto base = main.url.empty() ? std::nullopt : parse_url(main.url);
  page_->load(base ? *base : *url, main.text, main.mime);
  loaded_ = true;
  settle();
  return true;
}

void Host::resize(double width, double height) {
  width_ = std::clamp(width, 1.0, 8192.0);
  height_ = std::clamp(height, 1.0, 16384.0);
  if (loaded_)
    settle();
}

void Host::scroll(double y) {
  scroll_ = std::max(0.0, y);
  if (loaded_)
    settle();
}

void Host::click(double x, double y) {
  if (!loaded_)
    return;
  page_->click(x, y);
  settle();
}

void Host::key(const std::string &key, const std::string &text) {
  if (!loaded_)
    return;
  page_->key(key, text);
  settle();
}

double Host::next_timer_delay() const {
  return loaded_ ? page_->next_timer_delay() : -1.0;
}

bool Host::pump() {
  if (!loaded_)
    return false;
  double delay = page_->next_timer_delay();
  bool timer_due = delay >= 0 && delay <= 0;
  // Playing media and a freshly appended segment are work of their own: the clock
  // has to move even when no timer is due and nothing is being fetched.
  bool playing = page_->media_pending();
  if (!timer_due && !playing && page_->requests.empty())
    return false;
  if (timer_due || playing)
    page_->tick();
  settle();
  return true;
}

bool Host::media_playing() const {
  return loaded_ && page_->media_playing();
}

size_t Host::pull_audio(int16_t *out, size_t frames, int device_rate) {
  return loaded_ ? page_->pull_audio(out, frames, device_rate) : 0;
}

void Host::settle() {
  // I fetch everything my page asked for, then render once. Each round may add
  // new requests, so I keep going until it is quiet.
  for (int round = 0; round < kSubresourceRounds; ++round) {
    if (!page_->posts.empty()) {
      send_post();
      continue;
    }
    if (page_->requests.empty())
      break;
    std::vector<std::string> requests = std::move(page_->requests);
    page_->requests.clear();
    for (const auto &url : requests)
      page_->resource(fetch(url));
  }
  page_->requests.clear();
  emit();
}

void Host::send_post() {
  std::vector<Page::FormPost> pending = std::move(page_->posts);
  page_->posts.clear();
  for (const auto &post : pending) {
    if (!poster_) {
      page_->diagnostics.push_back({"forms", "I have no way to send a form post here"});
      continue;
    }
    Resource response;
    try {
      response = poster_(post.url, post.body, post.content_type);
    } catch (const std::exception &error) {
      response.url = post.url;
      response.ok = false;
      response.status = 0;
      response.error = error.what();
    }
    // A server that answered with an error status still answered, so I show
    // what it sent rather than pretending the request never arrived.
    if (!response.ok && response.status < 100) {
      page_->diagnostics.push_back(
          {"forms", response.error.empty() ? "the form post failed" : response.error});
      continue;
    }
    auto base = response.url.empty() ? parse_url(post.url) : parse_url(response.url);
    if (base)
      page_->load(*base, response.text, response.mime);
    reported_url_.clear();
    loaded_ = true;
  }
}

void Host::emit() {
  Json frame = page_->render(width_, height_, scroll_);
  for (const auto &[url, image] : page_->images()) {
    if (sent_images_.count(url) || !image.valid())
      continue;
    sent_images_.insert(url);
    if (!report_)
      continue;
    HostEvent event;
    event.tag = "image";
    event.url = url;
    event.image = std::make_shared<const Image>(image);
    report_(std::move(event));
  }
  // A canvas keeps its own bitmap, so I send the new pixels whenever it has
  // been drawn on since the last frame.
  for (const auto &[node, image] : page_->canvas_images()) {
    if (!image.valid())
      continue;
    auto seen = sent_canvas_.find(node);
    auto revision = page_->canvas_revisions().find(node);
    if (seen != sent_canvas_.end() && revision != page_->canvas_revisions().end() &&
        seen->second == revision->second)
      continue;
    if (revision != page_->canvas_revisions().end())
      sent_canvas_[node] = revision->second;
    if (!report_)
      continue;
    HostEvent event;
    event.tag = "image";
    event.url = "canvas:" + std::to_string(node);
    event.image = std::make_shared<const Image>(image);
    report_(std::move(event));
  }
  // A video or an audio element sends its picture under the same key its paint
  // command uses, so my shell needs no special case to draw it.
  for (const auto &[node, image] : page_->media_images()) {
    if (!image.valid())
      continue;
    auto revision = page_->media_revisions().find(node);
    auto seen = sent_media_.find(node);
    auto player = page_->player_for(node);
    bool moving = player && player->playing();
    if (!moving && seen != sent_media_.end() && revision != page_->media_revisions().end() &&
        seen->second == revision->second)
      continue;
    if (revision != page_->media_revisions().end())
      sent_media_[node] = revision->second;
    if (!report_)
      continue;
    HostEvent event;
    event.tag = "image";
    event.url = "media:" + std::to_string(node);
    event.image = std::make_shared<const Image>(image);
    report_(std::move(event));
  }
  if (report_) {
    HostEvent event;
    event.tag = "frame";
    event.frame = std::move(frame);
    report_(std::move(event));
  }
  // A page can change its own address without loading anything, through
  // history.pushState or because a form post came back. I tell my shell so the
  // address bar stays true.
  std::string current = page_->doc.url.href();
  if (report_ && current != reported_url_) {
    reported_url_ = current;
    HostEvent event;
    event.tag = "url";
    event.url = current;
    report_(std::move(event));
  }
  for (const auto &target : page_->navigations) {
    if (!report_)
      break;
    HostEvent event;
    event.tag = "navigate";
    event.url = target;
    report_(std::move(event));
  }
  page_->navigations.clear();
  for (const auto &dialog : page_->dialogs) {
    if (!report_)
      break;
    HostEvent event;
    event.tag = "dialog";
    event.text = dialog;
    report_(std::move(event));
  }
  page_->dialogs.clear();
  for (const auto &diagnostic : page_->diagnostics) {
    if (!report_)
      break;
    HostEvent event;
    event.tag = "log";
    event.subsystem = diagnostic.subsystem;
    event.text = diagnostic.message;
    report_(std::move(event));
  }
  page_->diagnostics.clear();
}
} // namespace pv
