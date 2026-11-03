#pragma once
#include "Psycho_base.hpp"
namespace pv {
constexpr size_t kMaxImagePixels = 16u * 1024u * 1024u;
constexpr size_t kMaxImageRawBytes = 64u * 1024u * 1024u + 16384u;
struct Image {
  int width = 0, height = 0;
  Bytes pixels; // straight-alpha BGRA, owned decoder output
  bool valid() const {
    return width > 0 && height > 0 && pixels.size() == size_t(width) * size_t(height) * 4;
  }
};
// Supports the non-interlaced PNG color types. Errors are values at the caller;
// this function throws on malformed input or an exceeded resource bound.
Image decode_png(const Bytes &encoded);
uint32_t crc32(const uint8_t *data, size_t size, uint32_t initial = 0);
uint32_t adler32(const uint8_t *data, size_t size);
} // namespace pv
