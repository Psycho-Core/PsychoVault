// PsychoVault original source. My media source extensions: a page appends bytes
// to a source buffer and I build the same media data my container readers
// produce, so append-then-play is a real path rather than a stub.
#pragma once
#include "Psycho_media.hpp"
#include <functional>

namespace pv {

// One source buffer holds what a page appended and the playable range it
// describes. My WAV and AVI readers accept a partial file, so an append that
// arrives in pieces still grows the range instead of failing.
class SourceBuffer {
public:
  explicit SourceBuffer(std::string mime) : type(std::move(mime)) {}
  const std::string &mime() const {
    return type;
  }
  bool append(const uint8_t *data, size_t size, std::string &error);
  void abort(std::string &error);
  // I cannot cut a decoded segment in half, so this refuses and says why.
  bool remove(double start, double end, std::string &error);
  void clear();

  std::shared_ptr<const MediaData> parsed() const {
    return parsed_;
  }
  std::vector<std::pair<double, double>> buffered() const {
    return buffered_;
  }
  size_t bytes() const {
    return data_.size();
  }

private:
  std::string type;
  Bytes data_;
  std::shared_ptr<const MediaData> parsed_;
  std::vector<std::pair<double, double>> buffered_;
};

// A media source is the whole thing a page attaches to an element: append,
// buffered, duration, end of stream, and a callback so a player already attached
// wakes up when more media arrives.
class MediaSourceState {
public:
  bool add_source_buffer(const std::string &type, std::string &error);
  SourceBuffer *buffer(size_t index);
  size_t buffer_count() const {
    return buffers_.size();
  }
  bool end_of_stream(std::string &error);
  bool ended() const {
    return ended_;
  }
  double duration() const;
  void set_duration(double seconds) {
    duration_override_ = seconds;
  }
  // The newest playable data, which is what an attached element plays.
  std::shared_ptr<const MediaData> newest(std::string &error) const;
  void set_on_update(std::function<void()> callback) {
    on_update_ = std::move(callback);
  }
  size_t total_bytes() const;

private:
  std::vector<std::shared_ptr<SourceBuffer>> buffers_;
  bool ended_ = false;
  double duration_override_ = -1;
  std::function<void()> on_update_;
  void notify();
};
} // namespace pv
