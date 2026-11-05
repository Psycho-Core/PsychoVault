#pragma once
// My vault is a single sealed file: a passphrase derives a key with PBKDF2,
// and the payload is encrypted with my own ChaCha20-Poly1305. Every primitive
// below is written from the RFC so the vault answers to no third-party crypto.
#include "../engine/Psycho_base.hpp"
#include <map>
#include <string>
#include <vector>

namespace pv {

// PBKDF2 with my SHA-256, one 32-byte block of output (all I need for a key).
Bytes pbkdf2_sha256(const Bytes &password, const Bytes &salt, uint32_t iterations);

// RFC 8439 primitives.
Bytes chacha20(const Bytes &key, const Bytes &nonce, uint32_t counter,
               const Bytes &input);
Bytes poly1305_mac(const Bytes &message, const Bytes &one_time_key);
// Encrypt-then-Poly1305 exactly as RFC 8439 section 2.8 defines it. Decrypt
// returns false on a tag mismatch and never releases plaintext.
Bytes aead_seal(const Bytes &key, const Bytes &nonce, const Bytes &aad,
                const Bytes &plaintext);
bool aead_open(const Bytes &key, const Bytes &nonce, const Bytes &aad,
               const Bytes &sealed, Bytes &plaintext);

// The vault itself: a map of named entries sealed into one blob. The blob on
// disk is PVLT magic, a version, the KDF cost, the salt and nonce, then the
// sealed payload. A wrong passphrase fails at the tag and nothing is read.
class Vault {
public:
  uint32_t iterations = 200000;
  // Create a fresh vault, or open an existing blob. Both throw std::runtime_error
  // on a damaged file or a wrong passphrase.
  static Vault create(const std::string &passphrase);
  static Vault open(const Bytes &blob, const std::string &passphrase);
  Bytes seal() const;
  std::map<std::string, std::string> entries;
  bool dirty = false;
  // I keep the derived key only while the vault is open.
  Bytes key;

private:
  Bytes salt_;
  Bytes nonce_;
};

} // namespace pv
