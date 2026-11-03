// PsychoVault original source. My own baseline JPEG decoder: Huffman tables,
// dequantisation, an 8x8 inverse cosine transform and the colour conversion.
// I read the file myself and never call a system image API.
#include "Psycho_image.hpp"
#include <array>
#include <cmath>
#include <cstring>

namespace pv {
namespace {
constexpr double kPi = 3.14159265358979323846;

struct Huffman {
  // count[n] is how many codes of length n, values[] lists the symbols in code
  // order. Decoding walks one bit at a time, which is fast enough for a still
  // image and needs no big lookup table.
  std::array<int, 17> count{};
  std::vector<uint8_t> values;
  int min_code[17]{}, max_code[17]{}, value_index[17]{};
  bool ready = false;

  void build() {
    int code = 0, index = 0;
    for (int length = 1; length <= 16; ++length) {
      value_index[length] = index;
      min_code[length] = code;
      code += count[length];
      index += count[length];
      max_code[length] = count[length] ? code - 1 : -1;
      if (code > (1 << length))
        throw std::runtime_error("JPEG: over-subscribed Huffman table");
      code <<= 1;
    }
    ready = index == int(values.size());
  }
};

struct Component {
  int id = 0, h = 1, v = 1, quant = 0, dc = 0, ac = 0;
  int width = 0, height = 0;   // in samples
  std::vector<uint8_t> plane;  // one byte per sample
  int stride = 0;
  int predictor = 0;
};

struct Reader {
  const Bytes &data;
  size_t at = 0;
  explicit Reader(const Bytes &d) : data(d) {}
  int byte() {
    if (at >= data.size())
      throw std::runtime_error("JPEG: file ends early");
    return data[at++];
  }
  int word() { return (byte() << 8) | byte(); }
  void skip(size_t n) {
    if (at + n > data.size())
      throw std::runtime_error("JPEG: marker runs past the end of the file");
    at += n;
  }
};

struct Bits {
  Reader &reader;
  uint32_t buffer = 0;
  int count = 0;
  bool hit_marker = false;
  explicit Bits(Reader &r) : reader(r) {}
  int bit() {
    if (count == 0) {
      int value = reader.byte();
      if (value == 0xff) {
        int next = reader.byte();
        // 0xff00 is a stuffed data byte; anything else is a marker and there are
        // no more bits in this scan.
        while (next == 0xff)
          next = reader.byte();
        if (next != 0) {
          hit_marker = true;
          reader.at -= 2;
          return 0;
        }
      }
      buffer = uint32_t(value);
      count = 8;
    }
    --count;
    return int((buffer >> count) & 1);
  }
  int bits(int n) {
    int value = 0;
    for (int i = 0; i < n; ++i)
      value = (value << 1) | bit();
    return value;
  }
  int extend(int n) {
    if (n == 0)
      return 0;
    int value = bits(n);
    return value < (1 << (n - 1)) ? value - (1 << n) + 1 : value;
  }
  void reset() {
    count = 0;
    hit_marker = false;
  }
};

int decode_symbol(Bits &bits, const Huffman &table) {
  int code = 0;
  for (int length = 1; length <= 16; ++length) {
    code = (code << 1) | bits.bit();
    if (bits.hit_marker)
      throw std::runtime_error("JPEG: scan ended inside a code");
    if (table.max_code[length] >= 0 && code >= table.min_code[length] &&
        code <= table.max_code[length]) {
      size_t index = size_t(table.value_index[length] + (code - table.min_code[length]));
      if (index >= table.values.size())
        throw std::runtime_error("JPEG: Huffman code out of range");
      return table.values[index];
    }
  }
  throw std::runtime_error("JPEG: no Huffman code matched");
}

// I run the separable inverse cosine transform in floating point, which keeps
// rounding differences small enough that a photo looks right.
struct Idct {
  std::array<double, 64> cosine{};
  Idct() {
    for (int x = 0; x < 8; ++x)
      for (int u = 0; u < 8; ++u)
        cosine[size_t(x * 8 + u)] = std::cos((2 * x + 1) * u * kPi / 16.0) * (u == 0 ? 0.35355339 : 0.5);
  }
  void run(const std::array<int, 64> &in, uint8_t *out, int stride) {
    std::array<double, 64> temp{};
    for (int y = 0; y < 8; ++y)
      for (int x = 0; x < 8; ++x) {
        double sum = 0;
        for (int u = 0; u < 8; ++u)
          sum += cosine[size_t(x * 8 + u)] * in[size_t(y * 8 + u)];
        temp[size_t(y * 8 + x)] = sum;
      }
    for (int x = 0; x < 8; ++x)
      for (int y = 0; y < 8; ++y) {
        double sum = 0;
        for (int v = 0; v < 8; ++v)
          sum += cosine[size_t(y * 8 + v)] * temp[size_t(v * 8 + x)];
        int value = int(std::lround(sum + 128.0));
        out[size_t(y) * stride + x] = uint8_t(std::clamp(value, 0, 255));
      }
  }
};
} // namespace

Image decode_jpeg(const Bytes &encoded) {
  if (encoded.size() < 4 || encoded[0] != 0xff || encoded[1] != 0xd8)
    throw std::runtime_error("JPEG: missing start of image marker");
  Reader reader(encoded);
  reader.skip(2);
  std::array<std::array<int, 64>, 4> quant{};
  std::array<Huffman, 4> dc{}, ac{};
  std::vector<Component> components;
  int width = 0, height = 0, max_h = 1, max_v = 1, restart_interval = 0;
  bool saw_frame = false, saw_scan = false;

  while (true) {
    int marker = reader.byte();
    if (marker != 0xff)
      continue;
    int kind = reader.byte();
    while (kind == 0xff)
      kind = reader.byte();
    if (kind == 0xd9)
      break;
    if (kind == 0x01 || (kind >= 0xd0 && kind <= 0xd7))
      continue;
    int length = reader.word();
    if (length < 2)
      throw std::runtime_error("JPEG: bad segment length");
    size_t payload = size_t(length) - 2;
    size_t segment_end = reader.at + payload;
    if (segment_end > encoded.size())
      throw std::runtime_error("JPEG: segment runs past the end of the file");
    if (kind == 0xdb) {
      while (reader.at < segment_end) {
        int info = reader.byte();
        int precision = info >> 4, index = info & 15;
        if (index > 3)
          throw std::runtime_error("JPEG: too many quantisation tables");
        for (int i = 0; i < 64; ++i)
          quant[size_t(index)][size_t(i)] = precision ? reader.word() : reader.byte();
      }
    } else if (kind == 0xc4) {
      while (reader.at < segment_end) {
        int info = reader.byte();
        int cls = info >> 4, index = info & 15;
        if (index > 3)
          throw std::runtime_error("JPEG: too many Huffman tables");
        Huffman &table = cls ? ac[size_t(index)] : dc[size_t(index)];
        int total = 0;
        for (int i = 1; i <= 16; ++i) {
          table.count[size_t(i)] = reader.byte();
          total += table.count[size_t(i)];
        }
        table.values.resize(size_t(total));
        for (int i = 0; i < total; ++i)
          table.values[size_t(i)] = uint8_t(reader.byte());
        table.build();
      }
    } else if (kind == 0xc0 || kind == 0xc1) {
      int precision = reader.byte();
      if (precision != 8)
        throw std::runtime_error("JPEG: only eight bit samples are supported");
      height = reader.word();
      width = reader.word();
      int count = reader.byte();
      if (width <= 0 || height <= 0 || count < 1 || count > 4)
        throw std::runtime_error("JPEG: bad frame size");
      components.assign(size_t(count), Component{});
      max_h = max_v = 1;
      for (auto &component : components) {
        component.id = reader.byte();
        int sampling = reader.byte();
        component.h = sampling >> 4;
        component.v = sampling & 15;
        if (component.h < 1 || component.h > 4 || component.v < 1 || component.v > 4)
          throw std::runtime_error("JPEG: bad sampling factor");
        component.quant = reader.byte();
        max_h = std::max(max_h, component.h);
        max_v = std::max(max_v, component.v);
      }
      if (size_t(width) * size_t(height) > kMaxImagePixels)
        throw std::runtime_error("JPEG: image is larger than I will decode");
      for (auto &component : components) {
        component.width = (width * component.h + max_h - 1) / max_h;
        component.height = (height * component.v + max_v - 1) / max_v;
        component.stride = component.width;
        component.plane.assign(size_t(component.width) * component.height, 0);
      }
      saw_frame = true;
    } else if (kind == 0xc2 || (kind >= 0xc3 && kind <= 0xcf && kind != 0xc4 && kind != 0xcc)) {
      throw std::runtime_error("JPEG: I only decode baseline and extended sequential files");
    } else if (kind == 0xdd) {
      restart_interval = reader.word();
    } else if (kind == 0xda) {
      if (!saw_frame)
        throw std::runtime_error("JPEG: scan before frame header");
      int count = reader.byte();
      if (count != int(components.size()))
        throw std::runtime_error("JPEG: scan does not match the frame");
      std::vector<Component *> order;
      for (int i = 0; i < count; ++i) {
        int id = reader.byte();
        int tables = reader.byte();
        Component *found = nullptr;
        for (auto &component : components)
          if (component.id == id)
            found = &component;
        if (!found)
          throw std::runtime_error("JPEG: scan names an unknown component");
        found->dc = tables >> 4;
        found->ac = tables & 15;
        order.push_back(found);
      }
      // I skip the spectral and successive approximation bytes; baseline uses
      // the fixed values, and I refuse progressive files before this point.
      reader.skip(3);
      if (reader.at != segment_end)
        reader.at = segment_end;
      Bits bits(reader);
      Idct idct;
      int mcus_x = (width + 8 * max_h - 1) / (8 * max_h);
      int mcus_y = (height + 8 * max_v - 1) / (8 * max_v);
      int restart_left = restart_interval;
      int mcu = 0;
      for (int my = 0; my < mcus_y; ++my)
        for (int mx = 0; mx < mcus_x; ++mx) {
          if (restart_interval && restart_left == 0) {
            // A restart marker resets the bit stream and every predictor.
            bits.reset();
            if (reader.at < encoded.size() && encoded[reader.at] == 0xff) {
              ++reader.at;
              int marker_kind = reader.byte();
              while (marker_kind == 0xff)
                marker_kind = reader.byte();
              if (marker_kind < 0xd0 || marker_kind > 0xd7)
                throw std::runtime_error("JPEG: expected a restart marker");
            }
            for (auto &component : components)
              component.predictor = 0;
            restart_left = restart_interval;
          }
          for (Component *component : order) {
            for (int by = 0; by < component->v; ++by)
              for (int bx = 0; bx < component->h; ++bx) {
                std::array<int, 64> block{};
                int value = decode_symbol(bits, dc[size_t(component->dc)]);
                int diff = bits.extend(value);
                component->predictor += diff;
                // Coefficients live in the block at the top left, then the AC
                // values arrive as run and size pairs.
                std::array<int, 64> placement{};
                placement[0] = component->predictor * quant[size_t(component->quant)][0];
                for (int k = 1; k < 64;) {
                  int symbol = decode_symbol(bits, ac[size_t(component->ac)]);
                  int run = symbol >> 4, size = symbol & 15;
                  if (size == 0) {
                    if (run == 15) {
                      k += 16;
                      continue;
                    }
                    break;
                  }
                  k += run;
                  if (k > 63)
                    break;
                  int coefficient = bits.extend(size);
                  static const int zigzag[64] = {
                      0,  1,  8,  16, 9,  2,  3,  10, 17, 24, 32, 25, 18, 11, 4,  5,
                      12, 19, 26, 33, 40, 48, 41, 34, 27, 20, 13, 6,  7,  14, 21, 28,
                      35, 42, 49, 56, 57, 50, 43, 36, 29, 22, 15, 23, 30, 37, 44, 51,
                      58, 59, 52, 45, 38, 31, 39, 46, 53, 60, 61, 54, 47, 55, 62, 63};
                  placement[size_t(zigzag[k])] =
                      coefficient * quant[size_t(component->quant)][size_t(k)];
                  ++k;
                }
                (void)block;
                int pixel_x = (mx * component->h + bx) * 8;
                int pixel_y = (my * component->v + by) * 8;
                if (pixel_x >= component->width || pixel_y >= component->height)
                  continue;
                // I decode into a temporary so a block at the edge cannot write
                // past the plane.
                std::array<uint8_t, 64> single{};
                idct.run(placement, single.data(), 8);
                for (int y = 0; y < 8; ++y)
                  for (int x = 0; x < 8; ++x) {
                    int px = pixel_x + x, py = pixel_y + y;
                    if (px >= component->width || py >= component->height)
                      continue;
                    component->plane[size_t(py) * component->stride + px] =
                        single[size_t(y * 8 + x)];
                  }
              }
          }
          if (restart_interval)
            --restart_left;
          ++mcu;
        }
      (void)mcu;
      saw_scan = true;
    } else {
      // Everything else is metadata I do not need.
    }
    if (reader.at < segment_end)
      reader.at = segment_end;
  }
  if (!saw_frame || !saw_scan)
    throw std::runtime_error("JPEG: no image data");
  if (size_t(width) * size_t(height) * 4 > kMaxImageRawBytes)
    throw std::runtime_error("JPEG: image is larger than I will decode");

  Image image;
  image.width = width;
  image.height = height;
  image.pixels.assign(size_t(width) * size_t(height) * 4, 255);
  // A subsampled colour plane is stretched back to full size with a bilinear
  // filter, which is what keeps a photo from looking like coloured tiles.
  std::vector<std::vector<uint8_t>> full(std::size(components) ? components.size() : 0);
  for (size_t index = 0; index < components.size(); ++index) {
    const Component &component = components[index];
    double fx = double(component.h) / double(max_h), fy = double(component.v) / double(max_v);
    if (fx >= 0.999 && fy >= 0.999)
      continue;
    auto &plane = full[index];
    plane.assign(size_t(width) * height, 0);
    for (int y = 0; y < height; ++y) {
      double sy = std::clamp((y + 0.5) * fy - 0.5, 0.0, double(component.height - 1));
      int y0 = int(sy), y1 = std::min(y0 + 1, component.height - 1);
      double ty = sy - y0;
      for (int x = 0; x < width; ++x) {
        double sx = std::clamp((x + 0.5) * fx - 0.5, 0.0, double(component.width - 1));
        int x0 = int(sx), x1 = std::min(x0 + 1, component.width - 1);
        double tx = sx - x0;
        auto at = [&](int px, int py) {
          return double(component.plane[size_t(py) * component.stride + px]);
        };
        double top = at(x0, y0) * (1 - tx) + at(x1, y0) * tx;
        double bottom = at(x0, y1) * (1 - tx) + at(x1, y1) * tx;
        plane[size_t(y) * width + x] = uint8_t(std::lround(top * (1 - ty) + bottom * ty));
      }
    }
  }
  auto sample = [&](size_t index, int x, int y) -> int {
    const Component &component = components[index];
    if (!full[index].empty())
      return full[index][size_t(y) * width + x];
    if (x >= component.width)
      x = component.width - 1;
    if (y >= component.height)
      y = component.height - 1;
    return component.plane[size_t(y) * component.stride + x];
  };
  for (int y = 0; y < height; ++y) {
    for (int x = 0; x < width; ++x) {
      size_t out = (size_t(y) * width + x) * 4;
      if (components.size() == 1) {
        int g = sample(0, x, y);
        image.pixels[out] = uint8_t(g);
        image.pixels[out + 1] = uint8_t(g);
        image.pixels[out + 2] = uint8_t(g);
        image.pixels[out + 3] = 255;
        continue;
      }
      const Component &cy = components[0], &cb = components[1], &cr = components[2];
      (void)cy;
      (void)cb;
      (void)cr;
      int lum = sample(0, x, y);
      int blue = sample(1, x, y) - 128;
      int red = sample(2, x, y) - 128;
      int r = int(std::lround(lum + 1.402 * red));
      int g = int(std::lround(lum - 0.344136 * blue - 0.714136 * red));
      int b = int(std::lround(lum + 1.772 * blue));
      image.pixels[out] = uint8_t(std::clamp(b, 0, 255));
      image.pixels[out + 1] = uint8_t(std::clamp(g, 0, 255));
      image.pixels[out + 2] = uint8_t(std::clamp(r, 0, 255));
      image.pixels[out + 3] = 255;
    }
  }
  return image;
}
} // namespace pv
