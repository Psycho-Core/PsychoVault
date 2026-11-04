// PsychoVault original source. My own media containers, my own player and my own
// mixer. Nothing here borrows a decoder: WAV and MJPEG frames are decoded by me,
// and any codec I have not written is named instead of imitated.
#pragma once
#include "Psycho_image.hpp"
namespace pv {

// I hold a whole file's media in memory with a hard bound, because a browser tab
// that plays a video should not be able to use the machine up.
constexpr size_t kMaxMediaBytes = 64u * 1024u * 1024u;
constexpr size_t kMaxMediaPixels = 48u * 1024u * 1024u;

struct MediaInfo {
  std::string container;   // WAV, AVI or MP4
  std::string audio_codec; // "PCM 16-bit", "AAC", ...
  std::string video_codec; // "MJPEG", "H.264", ...
  double duration = 0;     // seconds, 0 when the file does not say
  double frame_rate = 0;
  int width = 0, height = 0;
  int sample_rate = 0, channels = 0;
  bool has_audio = false, has_video = false;
  // Set when I understood the container but have no decoder for what it holds.
  // The text names the codec, and it is the error a page reads back.
  std::string unsupported;
};

struct AudioTrack {
  int sample_rate = 0;
  int channels = 0;
  std::vector<int16_t> samples; // interleaved
  double seconds() const {
    return sample_rate > 0 && channels > 0
               ? double(samples.size() / size_t(channels)) / double(sample_rate)
               : 0.0;
  }
};

struct VideoFrame {
  double at = 0;
  Image image;
};

class MediaData {
public:
  MediaInfo info;
  AudioTrack audio;
  std::vector<VideoFrame> frames;
  size_t bytes = 0;
};

// I open a file by what is in it, not by what a server said it was, and the MIME
// type is only a hint when the first bytes are inconclusive.
std::shared_ptr<const MediaData> open_media(const Bytes &bytes, const std::string &mime,
                                            std::string &error);
// I read the container header alone, which is what a media element needs before
// it decides whether it can play something at all.
bool probe_media(const Bytes &bytes, MediaInfo &info, std::string &error);
// True for the types my own decoders handle, false for everything else.
bool media_type_supported(const std::string &type);

// One element's playback state. The clock is wall time, so a slow frame does not
// slow the audio down and a seek moves both together.
class MediaPlayer {
public:
  bool load(const Bytes &bytes, const std::string &mime, std::string &error);
  bool load_shared(std::shared_ptr<const MediaData> data, std::string &error);

  const MediaInfo &info() const {
    return data_ ? data_->info : empty_info_;
  }
  bool ready() const {
    return ready_;
  }
  double duration() const {
    return duration_;
  }
  double current_time() const {
    return current_;
  }
  bool paused() const {
    return paused_;
  }
  bool ended() const {
    return ended_;
  }
  int ready_state() const {
    return ready_state_;
  }
  void set_volume(double v) {
    volume_ = std::clamp(v, 0.0, 1.0);
  }
  double volume() const {
    return volume_;
  }
  void set_muted(bool muted) {
    muted_ = muted;
  }
  bool muted() const {
    return muted_;
  }
  void set_loop(bool loop) {
    loop_ = loop;
  }
  bool loop() const {
    return loop_;
  }
  void set_rate(double rate) {
    rate_ = std::clamp(rate, 0.25, 4.0);
  }
  double rate() const {
    return rate_;
  }

  void play(double now);
  void pause(double now);
  void seek(double seconds, double now);
  void advance(double now);
  bool playing() const {
    return !paused_ && !ended_ && ready_ && duration_ > 0 && info().unsupported.empty();
  }
  const Image *frame() const;
  // I write interleaved stereo 16-bit frames at the rate the device asked for and
  // report how many I wrote. Linear interpolation does the resampling.
  size_t pull_audio(int16_t *out, size_t frames, int device_rate);
  void sync_audio_cursor();

private:
  std::shared_ptr<const MediaData> data_;
  MediaInfo empty_info_;
  double duration_ = 0, current_ = 0, rate_ = 1.0, volume_ = 1.0;
  double base_time_ = 0, base_wall_ = 0, audio_cursor_ = 0;
  bool paused_ = true, ended_ = false, muted_ = false, loop_ = false, ready_ = false;
  int ready_state_ = 0;
};
} // namespace pv
