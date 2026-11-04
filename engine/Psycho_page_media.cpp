// PsychoVault original source. My page tells its media elements apart, keeps the
// clock moving while they play, and turns a decoded frame into something my paint
// list can draw.
#include "Psycho_page.hpp"
#include <cmath>

namespace pv {
namespace {
double wall_seconds() {
  return double(now_ms()) / 1000.0;
}
std::string format_time(double seconds) {
  if (!(seconds >= 0) || seconds > 359999)
    seconds = 0;
  int total = int(seconds + 0.5);
  int hours = total / 3600, minutes = (total / 60) % 60, secs = total % 60;
  char buffer[32];
  if (hours > 0)
    std::snprintf(buffer, sizeof buffer, "%d:%02d:%02d", hours, minutes, secs);
  else
    std::snprintf(buffer, sizeof buffer, "%d:%02d", minutes, secs);
  return buffer;
}
bool is_media_element(const Node &n) {
  return n.type == Node::Type::Element && (n.name == "video" || n.name == "audio");
}
} // namespace

MediaPlayer *Page::player_for(int node) {
  auto it = media_.find(node);
  return it == media_.end() ? nullptr : &it->second;
}

std::string Page::create_media_object_url(std::shared_ptr<MediaSourceState> source) {
  std::string url = "blob:psychovault/" + std::to_string(next_media_source_++);
  source->set_on_update([this] { /* my tick notices new data by itself */ });
  media_sources_[url] = std::move(source);
  return url;
}

MediaSourceState *Page::media_source(const std::string &url) {
  auto it = media_sources_.find(url);
  return it == media_sources_.end() ? nullptr : it->second.get();
}

void Page::revoke_media_object_url(const std::string &url) {
  media_sources_.erase(url);
}

void Page::load_media_nodes() {
  for (auto &n : doc.nodes)
    if (is_media_element(n))
      update_media(n.id);
}

void Page::update_media(int node) {
  if (node < 0 || node >= int(doc.nodes.size()))
    return;
  const auto &n = doc.nodes[node];
  std::string src = n.attribute("src");
  if (src.empty())
    for (int c : n.children) {
      const auto &child = doc.nodes[c];
      if (child.name == "source" && !child.attribute("src").empty()) {
        src = child.attribute("src");
        break;
      }
    }
  if (src.empty())
    return;
  auto &player = media_[node];
  double now = wall_seconds();
  // A play() I was given before the data arrived starts here, before any listener
  // runs, and then the element tells the page what happened.
  auto announce = [&](MediaPlayer &ready) {
    bool autostart = media_wants_play_.erase(node) != 0;
    if (autostart)
      ready.play(wall_seconds());
    media_revisions_[node] = uint64_t(next_media_revision_++);
    dispatch(node, "loadedmetadata", js_->make_object());
    dispatch(node, "canplay", js_->make_object());
    if (autostart) {
      dispatch(node, "play", js_->make_object());
      dispatch(node, "playing", js_->make_object());
    }
  };
  // A page that handed me a media source gets the newest segment the moment it is
  // appended, which is what makes append-then-play work.
  if (auto source = media_source(src)) {
    std::string error;
    auto newest = source->newest(error);
    if (newest && media_data_[node] != newest.get()) {
      double position = player.ready() ? player.current_time() : 0;
      bool was_playing = !player.paused();
      if (player.load_shared(newest, error)) {
        media_data_[node] = newest.get();
        if (position > 0)
          player.seek(position, now);
        if (was_playing)
          player.play(now);
        announce(player);
      } else if (diagnostics.size() < 200) {
        diagnostics.push_back({"media", error});
      }
    }
    return;
  }
  auto resolved = resolve_url(doc.url, src);
  if (!resolved)
    return;
  auto found = resources_.find(resolved->href());
  if (found == resources_.end()) {
    request(src, ResourceType::Media);
    return;
  }
  if (media_src_[node] == resolved->href())
    return;
  const Resource &resource = found->second;
  if (!resource.ok || resource.bytes.empty()) {
    if (diagnostics.size() < 200)
      diagnostics.push_back(
          {"media", resource.error.empty() ? "this media did not load" : resource.error});
    media_src_[node] = resolved->href();
    return;
  }
  std::string error;
  if (!player.load(resource.bytes, resource.mime, error)) {
    if (diagnostics.size() < 200)
      diagnostics.push_back({"media", error});
    // I keep the reason so a script can read it back from the element.
    media_errors_[node] = error;
    media_src_[node] = resolved->href();
    return;
  }
  media_errors_.erase(node);
  media_src_[node] = resolved->href();
  announce(player);
}

void Page::toggle_media(int node) {
  auto player = player_for(node);
  if (!player) {
    update_media(node);
    player = player_for(node);
  }
  if (!player || !player->ready()) {
    // The click still counts: playback starts when the element has data.
    media_wants_play_.insert(node);
    return;
  }
  double now = wall_seconds();
  if (player->paused()) {
    player->play(now);
    dispatch(node, "play", js_->make_object());
  } else {
    player->pause(now);
    dispatch(node, "pause", js_->make_object());
  }
  media_revisions_[node] = uint64_t(next_media_revision_++);
  dirty_ = true;
}

void Page::seek_media(int node, double fraction) {
  auto player = player_for(node);
  if (!player || !player->ready())
    return;
  double duration = player->duration();
  if (duration <= 0)
    return;
  double now = wall_seconds();
  bool was_playing = !player->paused();
  player->seek(std::clamp(fraction, 0.0, 1.0) * duration, now);
  if (was_playing)
    player->play(now);
  media_revisions_[node] = uint64_t(next_media_revision_++);
  dispatch(node, "seeking", js_->make_object());
  dispatch(node, "timeupdate", js_->make_object());
  dirty_ = true;
}

bool Page::media_playing() const {
  for (const auto &[node, player] : media_)
    if (player.playing())
      return true;
  return false;
}

void Page::media_tick(double now) {
  // Whatever woke me is being handled now, so a wake I raise during this pass is
  // kept for the next one.
  media_wake_ = false;
  if (now < 0)
    now = wall_seconds();
  media_last_tick_ = now;
  open_pending_media_sources();
  // A page can set a src or append a segment at any moment, so I look again.
  load_media_nodes();
  bool timeupdate = now - media_last_timeupdate_ >= 0.25;
  if (timeupdate)
    media_last_timeupdate_ = now;
  for (auto &[node, player] : media_) {
    if (!player.ready())
      continue;
    bool was_playing = player.playing();
    player.advance(now);
    if (was_playing && player.ended()) {
      dispatch(node, "ended", js_->make_object());
      media_revisions_[node] = uint64_t(next_media_revision_++);
    }
    if (timeupdate && (was_playing || !player.paused()))
      dispatch(node, "timeupdate", js_->make_object());
  }
}

size_t Page::pull_audio(int16_t *out, size_t frames, int device_rate) {
  if (!out || frames == 0)
    return 0;
  size_t produced = 0;
  std::vector<int16_t> scratch(frames * 2);
  for (auto &[node, player] : media_) {
    if (!player.playing())
      continue;
    std::fill(scratch.begin(), scratch.end(), int16_t(0));
    size_t got = player.pull_audio(scratch.data(), frames, device_rate);
    if (got == 0)
      continue;
    for (size_t i = 0; i < got * 2; ++i) {
      // I add the samples and clamp, which is what two tracks get.
      int mixed = int(out[i]) + int(scratch[i]);
      out[i] = int16_t(std::clamp(mixed, -32768, 32767));
    }
    produced = std::max(produced, got);
  }
  return produced;
}

Image Page::compose_media_frame(int node, const MediaPlayer &player) const {
  Image out;
  const Image *frame = player.frame();
  auto box = layout_.boxes.find(node);
  if (!frame || !frame->valid())
    return out;
  // I scale the picture into the box my layout gave the element, so a video keeps
  // its own aspect inside whatever size a page asked for.
  int width = box == layout_.boxes.end() ? frame->width : int(std::max(1.0, box->second.w));
  int height = box == layout_.boxes.end() ? frame->height : int(std::max(1.0, box->second.h));
  if (width == frame->width && height == frame->height)
    return *frame;
  out.width = width;
  out.height = height;
  out.pixels.assign(size_t(width) * size_t(height) * 4, 0);
  for (int y = 0; y < height; ++y) {
    int sy = std::clamp(int(double(y) * frame->height / height), 0, frame->height - 1);
    for (int x = 0; x < width; ++x) {
      int sx = std::clamp(int(double(x) * frame->width / width), 0, frame->width - 1);
      const uint8_t *from = &frame->pixels[(size_t(sy) * frame->width + sx) * 4];
      uint8_t *to = &out.pixels[(size_t(y) * width + x) * 4];
      to[0] = from[0];
      to[1] = from[1];
      to[2] = from[2];
      to[3] = from[3];
    }
  }
  return out;
}

void Page::media_command(int node, Json &paint, size_t at) {
  auto box = layout_.boxes.find(node);
  if (box == layout_.boxes.end())
    return;
  const Rect &r = box->second;
  auto player = player_for(node);
  if (!player || r.w <= 0 || r.h <= 0)
    return;
  bool is_video = doc.nodes[node].name == "video";
  auto insert = [&](Json j) { paint.array.insert(paint.array.begin() + std::ptrdiff_t(at++), std::move(j)); };
  // I keep the current frame in the same map my canvases use, so my shell sends
  // the pixels across exactly like an image.
  Image frame = player->ready() ? compose_media_frame(node, *player) : Image();
  if (frame.valid()) {
    media_images_[node] = std::move(frame);
    Json j = Json::make_object();
    j.set("kind", int(PaintCommand::Kind::Canvas));
    j.set("x", r.x);
    j.set("y", r.y);
    j.set("w", r.w);
    j.set("h", r.h);
    j.set("color", 0.0);
    j.set("text", std::string());
    j.set("size", 0.0);
    j.set("thickness", 0.0);
    j.set("bold", false);
    j.set("node", node);
    j.set("url", "media:" + std::to_string(node));
    j.set("gradient", std::string());
    j.set("fixed", false);
    insert(std::move(j));
  } else if (is_video) {
    // A video with no picture yet still owns its box, and I say why.
    Json j = Json::make_object();
    j.set("kind", int(PaintCommand::Kind::Image));
    j.set("x", r.x);
    j.set("y", r.y);
    j.set("w", r.w);
    j.set("h", r.h);
    j.set("color", 0.0);
    std::string why = media_errors_.count(node) ? media_errors_[node] : std::string("video");
    j.set("text", why);
    j.set("size", 12.0);
    j.set("thickness", 0.0);
    j.set("bold", false);
    j.set("node", node);
    j.set("url", std::string());
    j.set("gradient", std::string());
    j.set("fixed", false);
    insert(std::move(j));
  }
  // The control strip is painted from my own paint commands, so it scales and
  // scrolls with the page instead of being a window on top of it. It covers the
  // bottom of the picture the way a browser puts controls over the video, and a
  // box too small for a legible strip gets none.
  double strip = std::min(30.0, r.h * 0.5);
  if (r.h < 24 || r.w < 48)
    return;
  double top = r.y + r.h - strip;
  auto rect = [&](double x, double y, double w, double h, uint32_t color) {
    Json j = Json::make_object();
    j.set("kind", int(PaintCommand::Kind::Rect));
    j.set("x", x);
    j.set("y", y);
    j.set("w", w);
    j.set("h", h);
    j.set("color", double(color));
    j.set("text", std::string());
    j.set("size", 0.0);
    j.set("thickness", 0.0);
    j.set("bold", false);
    j.set("node", node);
    j.set("url", std::string());
    j.set("gradient", std::string());
    j.set("fixed", false);
    insert(std::move(j));
  };
  auto label = [&](double x, double y, const std::string &text, double size, uint32_t color) {
    Json j = Json::make_object();
    j.set("kind", int(PaintCommand::Kind::Text));
    j.set("x", x);
    j.set("y", y);
    j.set("w", 40.0);
    j.set("h", size + 4);
    j.set("color", double(color));
    j.set("text", text);
    j.set("size", size);
    j.set("thickness", 0.0);
    j.set("bold", false);
    j.set("node", node);
    j.set("url", std::string());
    j.set("gradient", std::string());
    j.set("fixed", false);
    insert(std::move(j));
  };
  rect(r.x, top, r.w, strip, 0xcc0d1420u);
  label(r.x + 8, top + std::max(1.0, (strip - 15) / 2), player->paused() ? ">" : "||", 14,
        0xffe6edf3u);
  double duration = std::max(0.001, player->duration());
  double fraction = player->ready() ? std::clamp(player->current_time() / duration, 0.0, 1.0) : 0.0;
  if (r.w >= 100) {
    double bar_x = r.x + 30, bar_w = std::max(10.0, r.w - 30 - 78);
    rect(bar_x, top + strip / 2 - 2, bar_w, 4, 0xff2a3a4au);
    rect(bar_x, top + strip / 2 - 2, bar_w * fraction, 4, 0xff38bdf8u);
    rect(bar_x + bar_w * fraction - 3, top + strip / 2 - 5, 6, 10, 0xffe6edf3u);
  }
  if (r.w >= 170)
    label(r.x + r.w - 70, top + std::max(1.0, (strip - 14) / 2),
          format_time(player->current_time()) + " / " + format_time(duration), 11, 0xff8b98a9u);
}
} // namespace pv
