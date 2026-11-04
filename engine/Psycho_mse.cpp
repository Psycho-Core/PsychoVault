// PsychoVault original source. My source buffers grow a playable range as bytes
// arrive, and my media source hands the newest one to whichever element asked for
// it.
#include "Psycho_mse.hpp"

namespace pv {

bool SourceBuffer::append(const uint8_t *data, size_t size, std::string &error) {
  if (!data || size == 0) {
    error = "there were no bytes to append";
    return false;
  }
  if (data_.size() + size > kMaxMediaBytes) {
    error = "this media source is larger than the media I will hold in memory";
    return false;
  }
  data_.insert(data_.end(), data, data + size);
  // My readers accept a truncated file, so a half-arrived segment still gives me
  // a duration and whatever plays so far.
  std::string parse_error;
  auto parsed = open_media(data_, type, parse_error);
  if (!parsed) {
    if (!parsed_)
      error = parse_error.empty() ? "I could not read the segment I was given" : parse_error;
    return parsed_ != nullptr;
  }
  parsed_ = parsed;
  buffered_.clear();
  double seconds = parsed_->info.duration;
  if (seconds > 0)
    buffered_.emplace_back(0.0, seconds);
  return true;
}

void SourceBuffer::abort(std::string &) {
  // An abort drops the segment being appended. I keep only what already parsed,
  // so there is nothing half built to throw away.
}

bool SourceBuffer::remove(double start, double end, std::string &error) {
  if (start > end) {
    error = "the range I was asked to remove is backwards";
    return false;
  }
  error = "I cannot remove part of a segment I have already decoded";
  return false;
}

void SourceBuffer::clear() {
  data_.clear();
  parsed_.reset();
  buffered_.clear();
}

bool MediaSourceState::add_source_buffer(const std::string &type, std::string &error) {
  if (ended_) {
    error = "this media source has already ended";
    return false;
  }
  if (!media_type_supported(type)) {
    error = "NotSupportedError: I cannot play " + type;
    return false;
  }
  buffers_.push_back(std::make_shared<SourceBuffer>(type));
  notify();
  return true;
}

SourceBuffer *MediaSourceState::buffer(size_t index) {
  return index < buffers_.size() ? buffers_[index].get() : nullptr;
}

bool MediaSourceState::end_of_stream(std::string &) {
  ended_ = true;
  notify();
  return true;
}

double MediaSourceState::duration() const {
  if (duration_override_ >= 0)
    return duration_override_;
  double best = 0;
  for (const auto &buffer : buffers_) {
    auto parsed = buffer->parsed();
    if (parsed)
      best = std::max(best, parsed->info.duration);
  }
  return best;
}

size_t MediaSourceState::total_bytes() const {
  size_t total = 0;
  for (const auto &buffer : buffers_)
    total += buffer->bytes();
  return total;
}

std::shared_ptr<const MediaData> MediaSourceState::newest(std::string &error) const {
  std::shared_ptr<const MediaData> best;
  for (const auto &buffer : buffers_) {
    auto parsed = buffer->parsed();
    if (parsed && (!best || parsed->info.duration >= best->info.duration))
      best = parsed;
  }
  if (!best)
    error = "this media source has no playable data yet";
  return best;
}

void MediaSourceState::notify() {
  if (on_update_)
    on_update_();
}
} // namespace pv
