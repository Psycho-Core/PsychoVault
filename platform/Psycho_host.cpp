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
  if (!timer_due && page_->requests.empty())
    return false;
  if (timer_due)
    page_->tick();
  settle();
  return true;
}

void Host::settle() {
  // I fetch everything my page asked for, then render once. Each round may add
  // new requests, so I keep going until it is quiet.
  for (int round = 0; round < kSubresourceRounds; ++round) {
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
  if (report_) {
    HostEvent event;
    event.tag = "frame";
    event.frame = std::move(frame);
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
