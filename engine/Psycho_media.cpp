// PsychoVault original source. My own WAV, AVI and MP4 readers and my own player.
// A codec I have not written is refused by name, because pretending to play it
// would be a lie a page could not recover from.
#include "Psycho_media.hpp"
#include <cstring>

namespace pv {
namespace {
uint16_t le16(const uint8_t *p) {
  return uint16_t(uint16_t(p[0]) | (uint16_t(p[1]) << 8));
}
uint32_t le32(const uint8_t *p) {
  return uint32_t(p[0]) | (uint32_t(p[1]) << 8) | (uint32_t(p[2]) << 16) | (uint32_t(p[3]) << 24);
}
uint32_t be32(const uint8_t *p) {
  return (uint32_t(p[0]) << 24) | (uint32_t(p[1]) << 16) | (uint32_t(p[2]) << 8) | uint32_t(p[3]);
}
bool tag_is(const uint8_t *p, const char *tag) {
  return p[0] == uint8_t(tag[0]) && p[1] == uint8_t(tag[1]) && p[2] == uint8_t(tag[2]) &&
         p[3] == uint8_t(tag[3]);
}
bool starts_with(const Bytes &bytes, size_t at, const char *tag) {
  return bytes.size() >= at + 4 && tag_is(&bytes[at], tag);
}
std::string fourcc(const uint8_t *p) {
  std::string out;
  for (int i = 0; i < 4; ++i) {
    char c = char(p[i]);
    out += (c >= 32 && c < 127) ? c : '?';
  }
  return out;
}
// One sample of any width, scaled to the 16-bit range my mixer works in.
int16_t to_s16(const uint8_t *p, int bits, bool is_float) {
  if (is_float && bits == 32) {
    float f = 0;
    std::memcpy(&f, p, sizeof f);
    return int16_t(std::clamp(f, -1.0f, 1.0f) * 32767.0f);
  }
  switch (bits) {
  case 8:
    return int16_t((int(p[0]) - 128) << 8);
  case 16:
    return int16_t(le16(p));
  case 24: {
    int32_t v = int32_t(p[0]) | (int32_t(p[1]) << 8) | (int32_t(p[2]) << 16);
    if (v & 0x800000)
      v -= 0x1000000;
    return int16_t(v >> 8);
  }
  case 32: {
    int32_t v = int32_t(le32(p));
    return int16_t(v >> 16);
  }
  default:
    return 0;
  }
}

// ---- WAV -----------------------------------------------------------------
bool parse_wav(const Bytes &bytes, MediaData &out, std::string &error) {
  if (bytes.size() < 12 || !starts_with(bytes, 0, "RIFF") || !starts_with(bytes, 8, "WAVE")) {
    error = "that file is not a WAV I recognise";
    return false;
  }
  int channels = 0, rate = 0, bits = 0, format = 1;
  size_t data_at = 0, data_size = 0;
  size_t at = 12;
  while (at + 8 <= bytes.size()) {
    auto size = le32(&bytes[at + 4]);
    auto body = at + 8;
    if (tag_is(&bytes[at], "fmt ") && size >= 16 && body + size <= bytes.size()) {
      format = int(le16(&bytes[body]));
      channels = int(le16(&bytes[body + 2]));
      rate = int(le32(&bytes[body + 4]));
      bits = int(le16(&bytes[body + 14]));
      // An extensible header carries the real format in the first two bytes of
      // its sub format field.
      if (format == 0xfffe && size >= 26)
        format = int(le16(&bytes[body + 24]));
    } else if (tag_is(&bytes[at], "data")) {
      data_at = body;
      data_size = std::min<size_t>(size, bytes.size() > body ? bytes.size() - body : 0);
    }
    if (size == 0)
      break;
    at = body + size + (size & 1);
  }
  if (channels <= 0 || channels > 8 || rate < 1000 || rate > 384000 || bits == 0) {
    error = "that WAV has no usable format chunk";
    return false;
  }
  bool is_float = format == 3;
  if (format != 1 && format != 3) {
    out.info.container = "WAV";
    out.info.unsupported = "I only decode uncompressed WAV, and this one is format " +
                           std::to_string(format);
    error = out.info.unsupported;
    return false;
  }
  int bytes_per = bits / 8;
  if (bytes_per <= 0 || bytes_per > 4) {
    error = "that WAV uses a sample width I do not handle";
    return false;
  }
  size_t frame_bytes = size_t(bytes_per) * size_t(channels);
  size_t frames = frame_bytes ? data_size / frame_bytes : 0;
  out.audio.sample_rate = rate;
  out.audio.channels = channels;
  out.audio.samples.reserve(frames * size_t(channels));
  for (size_t i = 0; i < frames; ++i) {
    const uint8_t *frame = &bytes[data_at + i * frame_bytes];
    for (int c = 0; c < channels; ++c)
      out.audio.samples.push_back(to_s16(frame + size_t(c) * size_t(bytes_per), bits, is_float));
  }
  out.info.container = "WAV";
  out.info.audio_codec = (is_float ? "PCM float " : "PCM ") + std::to_string(bits) + "-bit";
  out.info.has_audio = true;
  out.info.sample_rate = rate;
  out.info.channels = channels;
  out.info.duration = out.audio.seconds();
  out.bytes = out.audio.samples.size() * 2;
  return true;
}

// ---- AVI -----------------------------------------------------------------
bool parse_avi(const Bytes &bytes, MediaData &out, std::string &error) {
  if (bytes.size() < 12 || !starts_with(bytes, 0, "RIFF") || !starts_with(bytes, 8, "AVI ")) {
    error = "that file is not an AVI I recognise";
    return false;
  }
  double seconds_per_frame = 0;
  int width = 0, height = 0;
  int audio_rate = 0, audio_channels = 0, audio_bits = 0, audio_tag = 1;
  bool has_video = false, has_audio = false;
  std::string video_codec;
  size_t movi_at = 0, movi_end = 0;
  // An AVI is a nest of lists, so my walker is depth limited and only looks for
  // the header lists and the start of the picture list.
  std::function<void(size_t, size_t, int)> walk = [&](size_t from, size_t to, int depth) {
    if (depth > 6)
      return;
    size_t p = from;
    while (p + 8 <= to && p + 8 <= bytes.size()) {
      auto size = le32(&bytes[p + 4]);
      auto body = p + 8;
      if (body + size > bytes.size() && !tag_is(&bytes[p], "LIST"))
        break;
      if (tag_is(&bytes[p], "LIST") && body + 4 <= bytes.size()) {
        auto end = std::min<size_t>(body + size, bytes.size());
        if (tag_is(&bytes[body], "movi") && movi_at == 0) {
          movi_at = body + 4;
          movi_end = end;
        }
        walk(body + 4, end, depth + 1);
      } else if (tag_is(&bytes[p], "avih") && size >= 40) {
        seconds_per_frame = double(le32(&bytes[body])) / 1e6;
        width = int(le32(&bytes[body + 32]));
        height = int(le32(&bytes[body + 36]));
      } else if (tag_is(&bytes[p], "strh") && size >= 32) {
        if (tag_is(&bytes[body], "vids"))
          has_video = true;
        else if (tag_is(&bytes[body], "auds"))
          has_audio = true;
      } else if (tag_is(&bytes[p], "strf") && size >= 16 && body + 20 <= bytes.size()) {
        if (has_video && video_codec.empty()) {
          video_codec = fourcc(&bytes[body + 16]);
          width = int(le32(&bytes[body + 4]));
          height = int(std::abs(int(le32(&bytes[body + 8]))));
        } else if (has_audio && audio_rate == 0) {
          audio_tag = int(le16(&bytes[body]));
          audio_channels = int(le16(&bytes[body + 2]));
          audio_rate = int(le32(&bytes[body + 4]));
          audio_bits = int(le16(&bytes[body + 14]));
        }
      }
      if (size == 0)
        break;
      p = body + size + (size & 1);
    }
  };
  walk(12, bytes.size(), 0);
  out.info.container = "AVI";
  out.info.width = width;
  out.info.height = height;
  out.info.has_video = has_video;
  out.info.has_audio = has_audio;
  if (seconds_per_frame > 0)
    out.info.frame_rate = 1.0 / seconds_per_frame;
  if (!video_codec.empty())
    out.info.video_codec = video_codec == "MJPG" ? "MJPEG" : video_codec;
  if (has_audio) {
    out.info.sample_rate = audio_rate;
    out.info.channels = audio_channels;
    out.info.audio_codec =
        audio_tag == 1 ? "PCM " + std::to_string(audio_bits) + "-bit"
                       : "codec " + std::to_string(audio_tag);
  }
  if (has_video && video_codec != "MJPG") {
    out.info.unsupported = "this AVI holds " +
                           (video_codec.empty() ? std::string("a codec") : video_codec) +
                           " video, and I only decode MJPEG";
    error = out.info.unsupported;
    return false;
  }
  if (movi_at == 0) {
    error = "this AVI has no picture list I can walk";
    return false;
  }
  if (has_audio && audio_tag != 1) {
    out.info.unsupported = "this AVI has compressed audio I do not decode";
    error = out.info.unsupported;
    return false;
  }
  // The picture list is walked once: each MJPEG chunk is one frame and each audio
  // chunk is PCM I append to the track.
  size_t frame_index = 0, decoded_pixels = 0;
  size_t p = movi_at;
  while (p + 8 <= movi_end && p + 8 <= bytes.size()) {
    auto size = le32(&bytes[p + 4]);
    auto body = p + 8;
    if (body + size > bytes.size())
      break;
    bool is_video = tag_is(&bytes[p], "00dc") || tag_is(&bytes[p], "00db");
    bool is_audio = tag_is(&bytes[p], "01wb");
    if (is_video && has_video && size > 4) {
      try {
        Bytes jpeg(&bytes[body], &bytes[body + size]);
        auto image = decode_jpeg(jpeg);
        decoded_pixels += size_t(image.width) * size_t(image.height);
        if (decoded_pixels > kMaxMediaPixels)
          throw std::runtime_error("this video is bigger than the memory I will spend on it");
        VideoFrame frame;
        frame.at = seconds_per_frame > 0 ? double(frame_index) * seconds_per_frame
                                         : double(out.frames.size()) / 25.0;
        if (width <= 0) {
          width = image.width;
          height = image.height;
        }
        out.bytes += image.pixels.size();
        frame.image = std::move(image);
        out.frames.push_back(std::move(frame));
        if (out.frames.size() > 12000)
          throw std::runtime_error("this video has more frames than I will hold");
      } catch (const std::exception &e) {
        error = std::string("a video frame would not decode: ") + e.what();
        return false;
      }
    } else if (is_audio && has_audio && size > 0) {
      int bytes_per = std::max(1, audio_bits / 8);
      size_t frame_bytes = size_t(bytes_per) * size_t(std::max(1, audio_channels));
      size_t frames = frame_bytes ? size / frame_bytes : 0;
      out.audio.sample_rate = audio_rate;
      out.audio.channels = std::max(1, audio_channels);
      for (size_t i = 0; i < frames; ++i)
        for (int c = 0; c < std::max(1, audio_channels); ++c)
          out.audio.samples.push_back(
              to_s16(&bytes[body + i * frame_bytes + size_t(c) * size_t(bytes_per)], audio_bits,
                     false));
      out.bytes += frames * size_t(audio_channels) * 2;
    }
    if (size == 0)
      break;
    if (is_video)
      ++frame_index;
    p = body + size + (size & 1);
  }
  if (out.frames.empty()) {
    error = "this AVI has no frames I could read";
    return false;
  }
  double video_seconds = out.frames.back().at + (seconds_per_frame > 0 ? seconds_per_frame : 1.0 / 25.0);
  double audio_seconds = out.audio.seconds();
  if (out.audio.sample_rate == 0)
    out.info.audio_codec.clear();
  out.info.duration = std::max(video_seconds, audio_seconds);
  return true;
}

// ---- MP4 -----------------------------------------------------------------
// I walk the box tree, which is what I need to report the duration and the codecs.
// An MP4 box size is big-endian and counts its own header, unlike the RIFF sizes
// my WAV and AVI readers use.
bool parse_mp4(const Bytes &bytes, MediaInfo &info, std::string &error) {
  if (bytes.size() < 12 || !starts_with(bytes, 4, "ftyp")) {
    error = "that file is not an MP4 I recognise";
    return false;
  }
  info.container = "MP4";
  double timescale = 0, duration_units = 0;
  std::string video_codec, audio_codec;
  bool has_video = false, has_audio = false;
  struct Trak {
    bool audio = false;
    std::string codec;
  };
  std::vector<Trak> traks;
  std::function<void(size_t, size_t, int, Trak *)> walk = [&](size_t from, size_t to, int depth,
                                                              Trak *trak) {
    if (depth > 8)
      return;
    size_t p = from;
    while (p + 8 <= to && p + 8 <= bytes.size()) {
      uint64_t size = be32(&bytes[p]);
      size_t header = 8;
      if (size == 1 && p + 16 <= bytes.size()) {
        // A 64-bit size only needs its low half for a bound I already have.
        size = uint64_t(be32(&bytes[p + 8])) << 32 | be32(&bytes[p + 12]);
        header = 16;
      }
      if (size == 0)
        size = bytes.size() - p;
      if (size < header || p + size_t(size) > bytes.size() + size_t(header))
        break;
      auto body = p + header;
      auto end = std::min<size_t>(p + size_t(size), bytes.size());
      if (tag_is(&bytes[p + 4], "moov") || tag_is(&bytes[p + 4], "mdia") ||
          tag_is(&bytes[p + 4], "minf") || tag_is(&bytes[p + 4], "stbl") ||
          tag_is(&bytes[p + 4], "udta") || tag_is(&bytes[p + 4], "edts")) {
        walk(body, end, depth + 1, trak);
      } else if (tag_is(&bytes[p + 4], "trak")) {
        traks.emplace_back();
        walk(body, end, depth + 1, &traks.back());
      } else if (tag_is(&bytes[p + 4], "mvhd") && size >= 20) {
        int version = bytes[body];
        if (version == 1 && size >= 32) {
          timescale = double(be32(&bytes[body + 20]));
          duration_units =
              double(uint64_t(be32(&bytes[body + 24])) << 32 | be32(&bytes[body + 28]));
        } else {
          timescale = double(be32(&bytes[body + 12]));
          duration_units = double(be32(&bytes[body + 16]));
        }
      } else if (tag_is(&bytes[p + 4], "hdlr") && trak && size >= 12) {
        if (tag_is(&bytes[body + 8], "vide"))
          trak->audio = false, has_video = true;
        else if (tag_is(&bytes[body + 8], "soun"))
          trak->audio = true, has_audio = true;
      } else if (tag_is(&bytes[p + 4], "stsd") && trak && size >= 20) {
        // The sample entry sits after the version, flags and entry count. I keep
        // it on the track, because hdlr can come before or after it.
        trak->codec = fourcc(&bytes[body + 12]);
      } else if (tag_is(&bytes[p + 4], "tkhd") && trak && size >= 84) {
        int version = bytes[body];
        size_t width_at = body + (version == 1 ? 88 : 76);
        if (width_at + 8 <= bytes.size()) {
          info.width = int(be32(&bytes[width_at]) >> 16);
          info.height = int(be32(&bytes[width_at + 4]) >> 16);
        }
      }
      p = end;
    }
  };
  walk(0, bytes.size(), 0, nullptr);
  for (const auto &trak : traks) {
    if (trak.codec.empty())
      continue;
    if (trak.audio) {
      if (audio_codec.empty())
        audio_codec = trak.codec;
    } else if (video_codec.empty())
      video_codec = trak.codec;
  }
  if (timescale > 0)
    info.duration = duration_units / timescale;
  info.has_video = has_video;
  info.has_audio = has_audio;
  info.video_codec = video_codec;
  info.audio_codec = audio_codec;
  if (video_codec == "avc1" || video_codec == "avc3")
    info.unsupported = "this is an H.264 video, and I have not written that decoder yet";
  else if (audio_codec == "mp4a")
    info.unsupported = "this is AAC audio, and I have not written that decoder yet";
  else if (video_codec.empty() && audio_codec.empty())
    info.unsupported = "this MP4 has no track I could identify";
  if (!info.unsupported.empty()) {
    error = info.unsupported;
    return false;
  }
  return true;
}
} // namespace

bool probe_media(const Bytes &bytes, MediaInfo &info, std::string &error) {
  info = MediaInfo();
  if (bytes.size() >= 12 && starts_with(bytes, 0, "RIFF")) {
    MediaData data;
    bool ok = false;
    if (starts_with(bytes, 8, "WAVE"))
      ok = parse_wav(bytes, data, error);
    else if (starts_with(bytes, 8, "AVI "))
      ok = parse_avi(bytes, data, error);
    // Even a refused file keeps the header I managed to read, because a duration
    // and a codec name are what a page shows when it explains why it cannot play.
    info = data.info;
    if (!ok && error.empty())
      error = "I do not recognise this media container";
    return ok;
  }
  if (bytes.size() >= 12 && starts_with(bytes, 4, "ftyp"))
    return parse_mp4(bytes, info, error);
  error = "I do not recognise this media container";
  return false;
}

std::shared_ptr<const MediaData> open_media(const Bytes &bytes, const std::string &mime,
                                            std::string &error) {
  if (bytes.empty()) {
    error = "there were no bytes to open";
    return nullptr;
  }
  if (bytes.size() > kMaxMediaBytes) {
    error = "this media is larger than the media I will hold in memory";
    return nullptr;
  }
  auto data = std::make_shared<MediaData>();
  bool ok = false;
  std::string type = lower(mime);
  if (starts_with(bytes, 0, "RIFF") && bytes.size() >= 12) {
    if (starts_with(bytes, 8, "WAVE"))
      ok = parse_wav(bytes, *data, error);
    else if (starts_with(bytes, 8, "AVI "))
      ok = parse_avi(bytes, *data, error);
  } else if (bytes.size() >= 12 && starts_with(bytes, 4, "ftyp")) {
    ok = parse_mp4(bytes, data->info, error);
  } else if (type.find("wav") != std::string::npos) {
    ok = parse_wav(bytes, *data, error);
  } else if (type.find("avi") != std::string::npos) {
    ok = parse_avi(bytes, *data, error);
  } else if (type.find("mp4") != std::string::npos) {
    ok = parse_mp4(bytes, data->info, error);
  } else {
    error = "I do not recognise this media container";
  }
  if (!ok) {
    // A container I understood but cannot decode keeps its reason, because that is
    // the message the page reads back from the element.
    if (!data->info.unsupported.empty())
      error = data->info.unsupported;
    return nullptr;
  }
  return data;
}

bool media_type_supported(const std::string &type) {
  std::string t = lower(type);
  if (t.find("wav") != std::string::npos || t.find("x-wav") != std::string::npos)
    return true;
  if (t.find("avi") != std::string::npos || t.find("msvideo") != std::string::npos)
    return true;
  // An MP4 is a container I can read, but only when a decoder exists for what it
  // holds, and none of the ones a real MP4 carries is written yet.
  return false;
}

bool MediaPlayer::load(const Bytes &bytes, const std::string &mime, std::string &error) {
  auto data = open_media(bytes, mime, error);
  if (!data)
    return false;
  return load_shared(data, error);
}

bool MediaPlayer::load_shared(std::shared_ptr<const MediaData> data, std::string &error) {
  if (!data) {
    error = "there was nothing to play";
    return false;
  }
  data_ = std::move(data);
  duration_ = data_->info.duration;
  current_ = 0;
  ended_ = false;
  paused_ = true;
  ready_ = true;
  ready_state_ = duration_ > 0 ? 4 : 1;
  audio_cursor_ = 0;
  base_time_ = 0;
  base_wall_ = 0;
  error.clear();
  return true;
}

void MediaPlayer::play(double now) {
  if (!ready_ || duration_ <= 0)
    return;
  if (ended_) {
    current_ = 0;
    audio_cursor_ = 0;
    ended_ = false;
  }
  paused_ = false;
  base_time_ = current_;
  base_wall_ = now;
}

void MediaPlayer::pause(double now) {
  if (!paused_)
    advance(now);
  paused_ = true;
}

void MediaPlayer::seek(double seconds, double now) {
  current_ = std::clamp(seconds, 0.0, std::max(0.0, duration_));
  ended_ = false;
  base_time_ = current_;
  base_wall_ = now;
  sync_audio_cursor();
}

void MediaPlayer::sync_audio_cursor() {
  if (!data_ || data_->audio.sample_rate <= 0)
    return;
  audio_cursor_ = current_ * double(data_->audio.sample_rate);
}

void MediaPlayer::advance(double now) {
  if (!playing())
    return;
  current_ = base_time_ + (now - base_wall_) * rate_;
  if (current_ >= duration_) {
    if (loop_) {
      current_ = 0;
      base_time_ = 0;
      base_wall_ = now;
      sync_audio_cursor();
    } else {
      current_ = duration_;
      paused_ = true;
      ended_ = true;
    }
  }
  // If the audio cursor drifted far from the clock I trust the clock, because a
  // drift that big means a seek or a stall.
  if (data_ && data_->audio.sample_rate > 0) {
    double expected = current_ * double(data_->audio.sample_rate);
    if (std::abs(expected - audio_cursor_) > 0.25 * data_->audio.sample_rate)
      audio_cursor_ = expected;
  }
}

const Image *MediaPlayer::frame() const {
  if (!data_ || data_->frames.empty())
    return nullptr;
  // The frame on screen is the last one whose time has passed.
  size_t low = 0, high = data_->frames.size();
  while (low < high) {
    size_t mid = (low + high) / 2;
    if (data_->frames[mid].at <= current_ + 1e-6)
      low = mid + 1;
    else
      high = mid;
  }
  if (low == 0)
    return &data_->frames.front().image;
  return &data_->frames[low - 1].image;
}

size_t MediaPlayer::pull_audio(int16_t *out, size_t frames, int device_rate) {
  if (!data_ || !out || frames == 0 || device_rate <= 0)
    return 0;
  const auto &track = data_->audio;
  if (track.sample_rate <= 0 || track.channels <= 0 || track.samples.empty())
    return 0;
  if (!playing() || muted_ || volume_ <= 0.001)
    return 0;
  double step = double(track.sample_rate) / double(device_rate);
  size_t produced = 0;
  double position = audio_cursor_;
  double total = double(track.samples.size() / size_t(track.channels));
  int channels = std::min(2, track.channels);
  while (produced < frames) {
    if (position >= total)
      break;
    auto sample_at = [&](size_t index, int channel) -> double {
      index = std::min(index, track.samples.size() / size_t(track.channels) - 1);
      size_t at = index * size_t(track.channels) + size_t(std::min(channel, track.channels - 1));
      return at < track.samples.size() ? double(track.samples[at]) : 0.0;
    };
    size_t base = size_t(position);
    double offset = position - double(base);
    for (int c = 0; c < 2; ++c) {
      int source = std::min(c, channels - 1);
      double a = sample_at(base, source);
      double b = sample_at(base + 1, source);
      double value = (a + (b - a) * offset) * volume_;
      // Mono reaches both of my output channels, which is what a browser does.
      out[produced * 2 + size_t(c)] = int16_t(std::clamp(value, -32768.0, 32767.0));
    }
    position += step;
    ++produced;
  }
  audio_cursor_ = position;
  return produced;
}
} // namespace pv
