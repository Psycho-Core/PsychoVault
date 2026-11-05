#include "Psycho_vault.hpp"
#include <cstring>
#include <stdexcept>

namespace pv {

namespace {

Bytes to_bytes(std::string_view text) {
  return Bytes(text.begin(), text.end());
}

uint32_t load32(const uint8_t *p) {
  return uint32_t(p[0]) | uint32_t(p[1]) << 8 | uint32_t(p[2]) << 16 |
         uint32_t(p[3]) << 24;
}
void store32(uint8_t *p, uint32_t v) {
  p[0] = uint8_t(v);
  p[1] = uint8_t(v >> 8);
  p[2] = uint8_t(v >> 16);
  p[3] = uint8_t(v >> 24);
}

// HMAC-SHA-256 over my own SHA-256. RFC 2104.
Bytes hmac_sha256(const Bytes &key, const Bytes &message) {
  Bytes k = key;
  if (k.size() > 64)
    k = sha256(k);
  k.resize(64, 0);
  Bytes inner(64 + message.size()), outer(64 + 32);
  for (size_t i = 0; i < 64; ++i) {
    inner[i] = uint8_t(k[i] ^ 0x36);
    outer[i] = uint8_t(k[i] ^ 0x5c);
  }
  std::memcpy(inner.data() + 64, message.data(), message.size());
  Bytes inner_hash = sha256(inner);
  std::memcpy(outer.data() + 64, inner_hash.data(), 32);
  return sha256(outer);
}

uint32_t rotate_left(uint32_t v, int bits) {
  return v << bits | v >> (32 - bits);
}

// The quarter round on state words a, b, c, d.
#define PV_QR(st, a, b, c, d)                 \
  st[a] += st[b];                             \
  st[d] = rotate_left(st[d] ^ st[a], 16);     \
  st[c] += st[d];                             \
  st[b] = rotate_left(st[b] ^ st[c], 12);     \
  st[a] += st[b];                             \
  st[d] = rotate_left(st[d] ^ st[a], 8);      \
  st[c] += st[d];                             \
  st[b] = rotate_left(st[b] ^ st[c], 7)

} // namespace

Bytes pbkdf2_sha256(const Bytes &password, const Bytes &salt, uint32_t iterations) {
  // One block is 32 bytes, which is exactly the key size I need.
  Bytes block = salt;
  block.push_back(0);
  block.push_back(0);
  block.push_back(0);
  block.push_back(1);
  Bytes u = hmac_sha256(password, block);
  Bytes result = u;
  for (uint32_t i = 1; i < iterations; ++i) {
    u = hmac_sha256(password, u);
    for (size_t j = 0; j < 32; ++j)
      result[j] ^= u[j];
  }
  return result;
}

Bytes chacha20(const Bytes &key, const Bytes &nonce, uint32_t counter,
               const Bytes &input) {
  if (key.size() != 32 || nonce.size() != 12)
    throw std::runtime_error("chacha20 needs a 32-byte key and a 12-byte nonce");
  Bytes out(input.size(), 0);
  uint32_t block_counter = counter;
  for (size_t offset = 0; offset < input.size(); offset += 64) {
    uint32_t st[16] = {0x61707865, 0x3320646e, 0x79622d32, 0x6b206574};
    for (size_t i = 0; i < 8; ++i)
      st[4 + i] = load32(key.data() + i * 4);
    st[12] = block_counter++;
    for (size_t i = 0; i < 3; ++i)
      st[13 + i] = load32(nonce.data() + i * 4);
    uint32_t work[16];
    std::memcpy(work, st, sizeof work);
    for (int round = 0; round < 10; ++round) {
      PV_QR(work, 0, 4, 8, 12);
      PV_QR(work, 1, 5, 9, 13);
      PV_QR(work, 2, 6, 10, 14);
      PV_QR(work, 3, 7, 11, 15);
      PV_QR(work, 0, 5, 10, 15);
      PV_QR(work, 1, 6, 11, 12);
      PV_QR(work, 2, 7, 8, 13);
      PV_QR(work, 3, 4, 9, 14);
    }
    for (size_t i = 0; i < 16; ++i)
      work[i] += st[i];
    uint8_t stream[64];
    for (size_t i = 0; i < 16; ++i)
      store32(stream + i * 4, work[i]);
    size_t n = std::min<size_t>(64, input.size() - offset);
    for (size_t i = 0; i < n; ++i)
      out[offset + i] = uint8_t(input[offset + i] ^ stream[i]);
  }
  return out;
}

Bytes poly1305_mac(const Bytes &message, const Bytes &one_time_key) {
  if (one_time_key.size() != 32)
    throw std::runtime_error("poly1305 needs a 32-byte one-time key");
  // Accumulator and key limbs, 26 bits each, exactly as the RFC describes.
  uint64_t r0 = load32(one_time_key.data()) & 0x3ffffff;
  uint64_t r1 = (load32(one_time_key.data() + 3) >> 2) & 0x3ffff03;
  uint64_t r2 = (load32(one_time_key.data() + 6) >> 4) & 0x3ffc0ff;
  uint64_t r3 = (load32(one_time_key.data() + 9) >> 6) & 0x3f03fff;
  uint64_t r4 = (load32(one_time_key.data() + 12) >> 8) & 0x00fffff;
  uint32_t s1 = uint32_t(r1 * 5), s2 = uint32_t(r2 * 5), s3 = uint32_t(r3 * 5),
           s4 = uint32_t(r4 * 5);
  uint64_t h0 = 0, h1 = 0, h2 = 0, h3 = 0, h4 = 0;
  for (size_t offset = 0; offset < message.size(); offset += 16) {
    size_t n = std::min<size_t>(16, message.size() - offset);
    uint8_t block[17] = {0};
    std::memcpy(block, message.data() + offset, n);
    block[n] = 1;
    uint64_t t0 = load32(block);
    uint64_t t1 = load32(block + 4);
    uint64_t t2 = load32(block + 8);
    uint64_t t3 = load32(block + 12);
    uint64_t t4 = block[16];
    h0 += t0 & 0x3ffffff;
    h1 += ((t0 >> 26) | (t1 << 6)) & 0x3ffffff;
    h2 += ((t1 >> 20) | (t2 << 12)) & 0x3ffffff;
    h3 += ((t2 >> 14) | (t3 << 18)) & 0x3ffffff;
    h4 += (t3 >> 8) | (t4 << 24);
    uint64_t d0 = h0 * r0 + h1 * s4 + h2 * s3 + h3 * s2 + h4 * s1;
    uint64_t d1 = h0 * r1 + h1 * r0 + h2 * s4 + h3 * s3 + h4 * s2;
    uint64_t d2 = h0 * r2 + h1 * r1 + h2 * r0 + h3 * s4 + h4 * s3;
    uint64_t d3 = h0 * r3 + h1 * r2 + h2 * r1 + h3 * r0 + h4 * s4;
    uint64_t d4 = h0 * r4 + h1 * r3 + h2 * r2 + h3 * r1 + h4 * r0;
    uint64_t carry = d0 >> 26;
    h0 = d0 & 0x3ffffff;
    d1 += carry;
    carry = d1 >> 26;
    h1 = d1 & 0x3ffffff;
    d2 += carry;
    carry = d2 >> 26;
    h2 = d2 & 0x3ffffff;
    d3 += carry;
    carry = d3 >> 26;
    h3 = d3 & 0x3ffffff;
    d4 += carry;
    carry = d4 >> 26;
    h4 = d4 & 0x3ffffff;
    h0 += carry * 5;
    carry = h0 >> 26;
    h0 &= 0x3ffffff;
    h1 += carry;
  }
  uint64_t carry = h1 >> 26;
  h1 &= 0x3ffffff;
  h2 += carry;
  carry = h2 >> 26;
  h2 &= 0x3ffffff;
  h3 += carry;
  carry = h3 >> 26;
  h3 &= 0x3ffffff;
  h4 += carry;
  carry = h4 >> 26;
  h4 &= 0x3ffffff;
  h0 += carry * 5;
  carry = h0 >> 26;
  h0 &= 0x3ffffff;
  h1 += carry;
  // Compute h + 5 - 2^130 limb by limb; a negative top limb means h stays.
  uint64_t g0 = h0 + 5;
  carry = g0 >> 26;
  g0 &= 0x3ffffff;
  uint64_t g1 = h1 + carry;
  carry = g1 >> 26;
  g1 &= 0x3ffffff;
  uint64_t g2 = h2 + carry;
  carry = g2 >> 26;
  g2 &= 0x3ffffff;
  uint64_t g3 = h3 + carry;
  carry = g3 >> 26;
  g3 &= 0x3ffffff;
  int64_t g4 = int64_t(h4) + int64_t(carry) - (int64_t(1) << 26);
  // select is all ones when g4 went negative, in which case h wins.
  uint64_t select = uint64_t(g4 >> 63);
  h0 = (h0 & select) | (g0 & ~select);
  h1 = (h1 & select) | (g1 & ~select);
  h2 = (h2 & select) | (g2 & ~select);
  h3 = (h3 & select) | (g3 & ~select);
  h4 = (h4 & select) | (uint64_t(g4) & ~select);
  // Fold the limbs back into 128 bits and add the s half of the key.
  // Each lane is truncated to 32 bits before the key half goes in, like the
  // reference implementations: the upper bits live in the next lane already.
  uint64_t f0 = uint64_t(uint32_t(h0 | h1 << 26)) + load32(one_time_key.data() + 16);
  uint64_t f1 = uint64_t(uint32_t(h1 >> 6 | h2 << 20)) +
                load32(one_time_key.data() + 20) + (f0 >> 32);
  uint64_t f2 = uint64_t(uint32_t(h2 >> 12 | h3 << 14)) +
                load32(one_time_key.data() + 24) + (f1 >> 32);
  uint64_t f3 = uint64_t(uint32_t(h3 >> 18 | h4 << 8)) +
                load32(one_time_key.data() + 28) + (f2 >> 32);
  Bytes tag(16);
  store32(tag.data(), uint32_t(f0));
  store32(tag.data() + 4, uint32_t(f1));
  store32(tag.data() + 8, uint32_t(f2));
  store32(tag.data() + 12, uint32_t(f3));
  return tag;
}

namespace {
Bytes aead_mac_data(const Bytes &aad, const Bytes &cipher) {
  Bytes data = aad;
  if (data.size() % 16)
    data.resize(data.size() + (16 - data.size() % 16), 0);
  data.insert(data.end(), cipher.begin(), cipher.end());
  if (data.size() % 16)
    data.resize(data.size() + (16 - data.size() % 16), 0);
  uint8_t lens[16] = {0};
  store32(lens, uint32_t(aad.size()));
  store32(lens + 8, uint32_t(cipher.size()));
  data.insert(data.end(), lens, lens + 16);
  return data;
}
bool tag_equal(const Bytes &a, const Bytes &b) {
  if (a.size() != b.size())
    return false;
  uint8_t diff = 0;
  for (size_t i = 0; i < a.size(); ++i)
    diff |= uint8_t(a[i] ^ b[i]);
  return diff == 0;
}
} // namespace

Bytes aead_seal(const Bytes &key, const Bytes &nonce, const Bytes &aad,
                const Bytes &plaintext) {
  // The one-time key is block counter zero of my stream, per the RFC.
  Bytes otk = chacha20(key, nonce, 0, Bytes(32, 0));
  Bytes cipher = chacha20(key, nonce, 1, plaintext);
  Bytes tag = poly1305_mac(aead_mac_data(aad, cipher), otk);
  Bytes sealed = cipher;
  sealed.insert(sealed.end(), tag.begin(), tag.end());
  return sealed;
}

bool aead_open(const Bytes &key, const Bytes &nonce, const Bytes &aad,
               const Bytes &sealed, Bytes &plaintext) {
  if (sealed.size() < 16)
    return false;
  Bytes cipher(sealed.begin(), sealed.end() - 16);
  Bytes tag(sealed.end() - 16, sealed.end());
  Bytes otk = chacha20(key, nonce, 0, Bytes(32, 0));
  if (!tag_equal(poly1305_mac(aead_mac_data(aad, cipher), otk), tag))
    return false;
  plaintext = chacha20(key, nonce, 1, cipher);
  return true;
}

// --- The vault blob ----------------------------------------------------------
//
// "PVLT" | u32 version | u32 iterations | 16-byte salt | 12-byte nonce |
// sealed JSON map of entries. The header is the AEAD's associated data, so a
// tampered cost or salt fails open instead of being used.

namespace {
constexpr char kMagic[4] = {'P', 'V', 'L', 'T'};
constexpr uint32_t kVersion = 1;

Bytes header_of(uint32_t iterations, const Bytes &salt, const Bytes &nonce) {
  Bytes header(kMagic, kMagic + 4);
  uint8_t tmp[4];
  store32(tmp, kVersion);
  header.insert(header.end(), tmp, tmp + 4);
  store32(tmp, iterations);
  header.insert(header.end(), tmp, tmp + 4);
  header.insert(header.end(), salt.begin(), salt.end());
  header.insert(header.end(), nonce.begin(), nonce.end());
  return header;
}

Bytes payload_of(const std::map<std::string, std::string> &entries) {
  // One JSON object; values are UTF-8 strings. Entry names are bounded on the
  // way in rather than trusted on the way out.
  Json root = Json::make_object();
  for (auto &entry : entries)
    root.set(entry.first.substr(0, 256), entry.second);
  return to_bytes(root.dump());
}

std::map<std::string, std::string> entries_of(const Bytes &payload) {
  std::map<std::string, std::string> entries;
  auto parsed = Json::parse(std::string(payload.begin(), payload.end()));
  if (!parsed || parsed->type != Json::Type::Object)
    throw std::runtime_error("the vault payload is not my JSON map");
  for (auto &pair : parsed->object)
    if (pair.second.type == Json::Type::String)
      entries[pair.first] = pair.second.string;
  return entries;
}
} // namespace

Vault Vault::create(const std::string &passphrase) {
  Vault vault;
  vault.salt_ = random_bytes(16);
  vault.nonce_ = random_bytes(12);
  vault.key = pbkdf2_sha256(to_bytes(passphrase), vault.salt_, vault.iterations);
  vault.dirty = true;
  return vault;
}

Vault Vault::open(const Bytes &blob, const std::string &passphrase) {
  if (blob.size() < 4 + 4 + 4 + 16 + 12 + 16)
    throw std::runtime_error("not my vault file");
  if (std::memcmp(blob.data(), kMagic, 4) != 0)
    throw std::runtime_error("not my vault file");
  uint32_t version = load32(blob.data() + 4);
  uint32_t iterations = load32(blob.data() + 8);
  if (version != kVersion)
    throw std::runtime_error("a vault version I do not understand");
  if (iterations < 1000 || iterations > 4000000)
    throw std::runtime_error("a vault with a cost I will not run");
  Vault vault;
  vault.iterations = iterations;
  vault.salt_.assign(blob.begin() + 12, blob.begin() + 28);
  vault.nonce_.assign(blob.begin() + 28, blob.begin() + 40);
  Bytes sealed(blob.begin() + 40, blob.end());
  Bytes header(blob.begin(), blob.begin() + 40);
  vault.key = pbkdf2_sha256(to_bytes(passphrase), vault.salt_, iterations);
  Bytes plaintext;
  if (!aead_open(vault.key, vault.nonce_, header, sealed, plaintext))
    throw std::runtime_error("wrong passphrase or a damaged vault");
  vault.entries = entries_of(plaintext);
  return vault;
}

Bytes Vault::seal() const {
  if (key.empty())
    throw std::runtime_error("the vault is locked");
  Bytes header = header_of(iterations, salt_, nonce_);
  Bytes sealed = aead_seal(key, nonce_, header, payload_of(entries));
  header.insert(header.end(), sealed.begin(), sealed.end());
  return header;
}

} // namespace pv
