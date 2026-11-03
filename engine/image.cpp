#include "image.hpp"
namespace pv {
namespace {
uint32_t be32(const uint8_t *p) {
  return uint32_t(p[0]) << 24 | uint32_t(p[1]) << 16 | uint32_t(p[2]) << 8 | p[3];
}
struct Bits {
  const Bytes &data;
  size_t pos = 2; // skip zlib header
  uint32_t buffer = 0;
  int count = 0;
  explicit Bits(const Bytes &b) : data(b) {}
  uint32_t read(int n) {
    while (count < n) {
      if (pos >= data.size())
        throw std::runtime_error("PNG compressed stream truncated");
      buffer |= uint32_t(data[pos++]) << count;
      count += 8;
    }
    uint32_t v = buffer & ((1u << n) - 1);
    buffer >>= n;
    count -= n;
    return v;
  }
  void align() {
    buffer = 0;
    count = 0;
  }
};
struct Huffman {
  std::array<uint16_t, 16> counts{};
  std::vector<uint16_t> symbols;
  Huffman() = default;
  explicit Huffman(const uint8_t *lengths, size_t n) {
    for (size_t i = 0; i < n; ++i) {
      if (lengths[i] > 15)
        throw std::runtime_error("PNG invalid Huffman length");
      ++counts[lengths[i]];
    }
    counts[0] = 0;
    int left = 1;
    for (int len = 1; len <= 15; ++len) {
      left <<= 1;
      left -= counts[len];
      if (left < 0)
        throw std::runtime_error("PNG over-subscribed Huffman code");
    }
    std::array<uint16_t, 16> offsets{};
    for (int len = 1; len < 15; ++len)
      offsets[len + 1] = uint16_t(offsets[len] + counts[len]);
    symbols.resize(n);
    for (size_t i = 0; i < n; ++i)
      if (lengths[i])
        symbols[offsets[lengths[i]]++] = uint16_t(i);
    // A single one-bit code is legal in a distance tree; incomplete larger trees
    // are rejected by the decoder when no matching code exists.
  }
  uint16_t decode(Bits &bits) const {
    int code = 0, first = 0, index = 0;
    for (int len = 1; len <= 15; ++len) {
      code |= int(bits.read(1));
      int count = counts[len];
      if (code - first < count)
        return symbols[size_t(index + code - first)];
      index += count;
      first = (first + count) << 1;
      code <<= 1;
    }
    throw std::runtime_error("PNG invalid Huffman symbol");
  }
};
Bytes inflate(const Bytes &z, size_t expected) {
  if (z.size() < 6 || (z[0] & 15) != 8 || (z[0] >> 4) > 7 || (z[1] & 32) ||
      ((uint32_t(z[0]) << 8 | z[1]) % 31))
    throw std::runtime_error("PNG invalid zlib header");
  Bits bits(z);
  Bytes out;
  out.reserve(expected);
  bool final = false;
  while (!final) {
    final = bits.read(1) != 0;
    int type = int(bits.read(2));
    auto append = [&](uint8_t b) {
      if (out.size() >= expected)
        throw std::runtime_error("PNG expanded data exceeds image bound");
      out.push_back(b);
    };
    if (type == 0) {
      bits.align();
      if (bits.pos + 4 > z.size())
        throw std::runtime_error("PNG stored block truncated");
      uint16_t len = uint16_t(z[bits.pos] | z[bits.pos + 1] << 8);
      uint16_t complement = uint16_t(z[bits.pos + 2] | z[bits.pos + 3] << 8);
      bits.pos += 4;
      if (uint16_t(~len) != complement || bits.pos + len > z.size())
        throw std::runtime_error("PNG invalid stored block");
      for (uint16_t i = 0; i < len; ++i)
        append(z[bits.pos++]);
    } else if (type == 1 || type == 2) {
      Huffman lit, dist;
      if (type == 1) {
        uint8_t ll[288], dl[32];
        for (int i = 0; i < 288; ++i)
          ll[i] = uint8_t(i < 144 ? 8 : i < 256 ? 9 : i < 280 ? 7 : 8);
        for (int i = 0; i < 32; ++i)
          dl[i] = 5;
        lit = Huffman(ll, 288);
        dist = Huffman(dl, 32);
      } else {
        static constexpr uint8_t order[19] = {16, 17, 18, 0, 8,  7, 9,  6, 10, 5,
                                              11, 4,  12, 3, 13, 2, 14, 1, 15};
        int hlit = int(bits.read(5)) + 257, hdist = int(bits.read(5)) + 1,
            hclen = int(bits.read(4)) + 4;
        if (hlit > 286 || hdist > 32)
          throw std::runtime_error("PNG invalid Huffman counts");
        uint8_t cl[19]{};
        for (int i = 0; i < hclen; ++i)
          cl[order[i]] = uint8_t(bits.read(3));
        Huffman code_tree(cl, 19);
        std::vector<uint8_t> lengths(size_t(hlit + hdist));
        size_t i = 0;
        while (i < lengths.size()) {
          uint16_t sym = code_tree.decode(bits);
          if (sym < 16)
            lengths[i++] = uint8_t(sym);
          else {
            int repeat = 0;
            uint8_t value = 0;
            if (sym == 16) {
              if (!i)
                throw std::runtime_error("PNG repeat without previous length");
              value = lengths[i - 1];
              repeat = 3 + int(bits.read(2));
            } else if (sym == 17)
              repeat = 3 + int(bits.read(3));
            else if (sym == 18)
              repeat = 11 + int(bits.read(7));
            else
              throw std::runtime_error("PNG invalid code-length symbol");
            if (i + size_t(repeat) > lengths.size())
              throw std::runtime_error("PNG code-length overflow");
            while (repeat--)
              lengths[i++] = value;
          }
        }
        if (!lengths[256])
          throw std::runtime_error("PNG missing end-of-block code");
        lit = Huffman(lengths.data(), size_t(hlit));
        dist = Huffman(lengths.data() + hlit, size_t(hdist));
      }
      while (true) {
        uint16_t sym = lit.decode(bits);
        if (sym < 256)
          append(uint8_t(sym));
        else if (sym == 256)
          break;
        else {
          static constexpr uint16_t len_base[29] = {3,  4,  5,  6,   7,   8,   9,   10,  11, 13,
                                                    15, 17, 19, 23,  27,  31,  35,  43,  51, 59,
                                                    67, 83, 99, 115, 131, 163, 195, 227, 258};
          static constexpr uint8_t len_extra[29] = {0, 0, 0, 0, 0, 0, 0, 0, 1, 1, 1, 1, 2, 2, 2,
                                                    2, 3, 3, 3, 3, 4, 4, 4, 4, 5, 5, 5, 5, 0};
          static constexpr uint16_t dist_base[30] = {
              1,    2,    3,    4,    5,    7,    9,    13,    17,    25,
              33,   49,   65,   97,   129,  193,  257,  385,   513,   769,
              1025, 1537, 2049, 3073, 4097, 6145, 8193, 12289, 16385, 24577};
          static constexpr uint8_t dist_extra[30] = {0, 0, 0,  0,  1,  1,  2,  2,  3,  3,
                                                     4, 4, 5,  5,  6,  6,  7,  7,  8,  8,
                                                     9, 9, 10, 10, 11, 11, 12, 12, 13, 13};
          if (sym < 257 || sym > 285)
            throw std::runtime_error("PNG invalid length symbol");
          int li = sym - 257;
          size_t length = len_base[li] + bits.read(len_extra[li]);
          uint16_t ds = dist.decode(bits);
          if (ds > 29)
            throw std::runtime_error("PNG invalid distance symbol");
          size_t distance = dist_base[ds] + bits.read(dist_extra[ds]);
          if (distance > out.size() || out.size() + length > expected)
            throw std::runtime_error("PNG invalid back-reference");
          for (size_t j = 0; j < length; ++j)
            out.push_back(out[out.size() - distance]);
        }
      }
    } else
      throw std::runtime_error("PNG reserved compression block");
  }
  if (out.size() != expected)
    throw std::runtime_error("PNG expanded size mismatch");
  bits.align();
  if (bits.pos + 4 != z.size())
    throw std::runtime_error("PNG trailing compressed bytes");
  if (be32(z.data() + bits.pos) != adler32(out.data(), out.size()))
    throw std::runtime_error("PNG checksum mismatch");
  return out;
}
int paeth(int a, int b, int c) {
  int p = a + b - c, pa = std::abs(p - a), pb = std::abs(p - b), pc = std::abs(p - c);
  return pa <= pb && pa <= pc ? a : pb <= pc ? b : c;
}
uint8_t sample(const uint8_t *row, size_t index, int depth) {
  if (depth == 8)
    return row[index];
  if (depth == 16)
    return row[index * 2];
  int per = 8 / depth;
  int shift = 8 - depth * (int(index % size_t(per)) + 1);
  return uint8_t((row[index / size_t(per)] >> shift) & ((1 << depth) - 1));
}
uint8_t scale_sample(uint8_t v, int depth, int max) {
  if (depth == 16 || depth == 8)
    return v;
  return uint8_t((int(v) * 255 + max / 2) / max);
}
} // namespace
uint32_t crc32(const uint8_t *data, size_t size, uint32_t initial) {
  uint32_t c = initial ^ 0xffffffffu;
  for (size_t i = 0; i < size; ++i) {
    c ^= data[i];
    for (int k = 0; k < 8; ++k)
      c = (c >> 1) ^ (0xedb88320u & (0u - (c & 1)));
  }
  return c ^ 0xffffffffu;
}
uint32_t adler32(const uint8_t *data, size_t size) {
  uint32_t a = 1, b = 0;
  for (size_t i = 0; i < size; ++i) {
    a = (a + data[i]) % 65521;
    b = (b + a) % 65521;
  }
  return b << 16 | a;
}
Image decode_png(const Bytes &encoded) {
  static constexpr uint8_t signature[8] = {137, 80, 78, 71, 13, 10, 26, 10};
  if (encoded.size() < 8 || !std::equal(signature, signature + 8, encoded.begin()))
    throw std::runtime_error("not a PNG");
  Image image;
  int depth = 0, color = 0;
  Bytes palette, transparency, compressed;
  bool ihdr = false, ended = false;
  size_t pos = 8;
  auto critical = [](const uint8_t *t) { return (t[0] & 32) == 0; };
  while (pos + 12 <= encoded.size()) {
    uint32_t len = be32(encoded.data() + pos);
    if (len > kMaxResourceBytes || pos + 12 + size_t(len) > encoded.size())
      throw std::runtime_error("PNG chunk length invalid");
    const uint8_t *type = encoded.data() + pos + 4, *data = type + 4;
    if (crc32(type, size_t(len) + 4) != be32(data + len))
      throw std::runtime_error("PNG chunk checksum mismatch");
    std::string name(reinterpret_cast<const char *>(type), 4);
    if (ended && name != "IEND")
      throw std::runtime_error("PNG data after end");
    if (name == "IHDR") {
      if (ihdr || len != 13)
        throw std::runtime_error("PNG invalid header");
      uint32_t w = be32(data), h = be32(data + 4);
      if (!w || !h || w > 16384 || h > 16384 || size_t(w) * h > kMaxImagePixels)
        throw std::runtime_error("PNG dimensions exceed limit");
      image.width = int(w);
      image.height = int(h);
      depth = data[8];
      color = data[9];
      if (data[10] || data[11] || data[12])
        throw std::runtime_error("unsupported PNG encoding or interlace");
      bool valid =
          (color == 0 && (depth == 1 || depth == 2 || depth == 4 || depth == 8 || depth == 16)) ||
          (color == 3 && (depth == 1 || depth == 2 || depth == 4 || depth == 8)) ||
          ((color == 2 || color == 4 || color == 6) && (depth == 8 || depth == 16));
      if (!valid)
        throw std::runtime_error("unsupported PNG color type");
      ihdr = true;
    } else if (name == "PLTE") {
      if (!ihdr || color == 0 || color == 4 || !palette.empty() || len % 3 || len > 768)
        throw std::runtime_error("PNG invalid palette");
      palette.assign(data, data + len);
    } else if (name == "tRNS") {
      if (!ihdr || !transparency.empty() || color == 4 || color == 6)
        throw std::runtime_error("PNG invalid transparency");
      transparency.assign(data, data + len);
    } else if (name == "IDAT") {
      if (!ihdr || (color == 3 && palette.empty()))
        throw std::runtime_error("PNG data before header/palette");
      if (compressed.size() + len > kMaxResourceBytes)
        throw std::runtime_error("PNG compressed size limit");
      compressed.insert(compressed.end(), data, data + len);
    } else if (name == "IEND") {
      if (len)
        throw std::runtime_error("PNG invalid end chunk");
      ended = true;
    } else if (critical(type))
      throw std::runtime_error("unsupported critical PNG chunk");
    pos += 12 + size_t(len);
  }
  if (!ihdr || !ended || pos != encoded.size() || compressed.empty())
    throw std::runtime_error("PNG incomplete");
  int channels = color == 0 ? 1 : color == 2 ? 3 : color == 3 ? 1 : color == 4 ? 2 : 4;
  size_t row_bits = size_t(image.width) * size_t(channels) * size_t(depth);
  size_t stride = (row_bits + 7) / 8;
  if (stride > (kMaxImageRawBytes - size_t(image.height)) / size_t(image.height))
    throw std::runtime_error("PNG scanline size limit");
  Bytes raw = inflate(compressed, (stride + 1) * size_t(image.height));
  Bytes previous(stride);
  image.pixels.resize(size_t(image.width) * size_t(image.height) * 4);
  int max_sample = (1 << depth) - 1;
  for (int y = 0; y < image.height; ++y) {
    uint8_t *row = raw.data() + size_t(y) * (stride + 1);
    uint8_t filter = row[0];
    uint8_t *p = row + 1;
    if (filter > 4)
      throw std::runtime_error("PNG invalid scanline filter");
    int step = std::max(1, channels * depth / 8);
    for (size_t i = 0; i < stride; ++i) {
      int a = i >= size_t(step) ? p[i - size_t(step)] : 0, b = previous[i];
      int c = i >= size_t(step) ? previous[i - size_t(step)] : 0;
      int add = filter == 0   ? 0
                : filter == 1 ? a
                : filter == 2 ? b
                : filter == 3 ? (a + b) / 2
                              : paeth(a, b, c);
      p[i] = uint8_t(p[i] + add);
    }
    for (int x = 0; x < image.width; ++x) {
      uint8_t r = 0, g = 0, b = 0, a = 255;
      if (color == 0 || color == 4) {
        r = g = b = scale_sample(sample(p, size_t(x * channels), depth), depth, max_sample);
        if (color == 4)
          a = scale_sample(sample(p, size_t(x * channels + 1), depth), depth, max_sample);
        else if (transparency.size() >= 2) {
          uint16_t key =
              depth == 16 ? uint16_t(p[x * 2] << 8 | p[x * 2 + 1]) : sample(p, size_t(x), depth);
          uint16_t target = uint16_t(transparency[0] << 8 | transparency[1]);
          if (key == target)
            a = 0;
        }
      } else if (color == 3) {
        int index = sample(p, size_t(x), depth);
        if (size_t(index * 3 + 2) >= palette.size())
          throw std::runtime_error("PNG palette index out of range");
        r = palette[size_t(index * 3)];
        g = palette[size_t(index * 3 + 1)];
        b = palette[size_t(index * 3 + 2)];
        if (size_t(index) < transparency.size())
          a = transparency[size_t(index)];
      } else {
        int step16 = depth == 16 ? 2 : 1;
        size_t off = size_t(x * channels * step16);
        r = p[off];
        g = p[off + size_t(step16)];
        b = p[off + size_t(2 * step16)];
        if (color == 6)
          a = p[off + size_t(3 * step16)];
        else if (transparency.size() == 6) {
          bool match = true;
          for (int ch = 0; ch < 3; ++ch) {
            uint16_t v = depth == 16
                             ? uint16_t(p[off + size_t(ch * 2)] << 8 | p[off + size_t(ch * 2 + 1)])
                             : p[off + size_t(ch)];
            uint16_t key =
                uint16_t(transparency[size_t(ch * 2)] << 8 | transparency[size_t(ch * 2 + 1)]);
            if (v != key)
              match = false;
          }
          if (match)
            a = 0;
        }
      }
      size_t out = (size_t(y) * size_t(image.width) + size_t(x)) * 4;
      image.pixels[out] = b;
      image.pixels[out + 1] = g;
      image.pixels[out + 2] = r;
      image.pixels[out + 3] = a;
    }
    previous.assign(p, p + stride);
  }
  return image;
}
} // namespace pv
