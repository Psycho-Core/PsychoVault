#pragma once
// P3/S088: the password vault as a service. The sealed file and its cryptography are platform/Psycho_vault;
// this class is the one owner of the open vault: it creates, unlocks, locks and auto-locks it, and lists,
// adds, removes and reveals entries. Every call is safe from any thread (the settings page's bridge calls
// arrive on a session thread, the auto-lock tick on the window's), and a list never carries a password.
#include "../../platform/Psycho_vault.hpp"
#include "../ipc/Psycho_bus.hpp"
#include <functional>
#include <mutex>
#include <string>

namespace pv::rt {

class VaultService {
public:
  struct Seams {
    // Where the sealed blob lives. Reading returns false when there is no file.
    std::function<bool(std::string &blob)> read = {};
    std::function<bool(const std::string &blob, std::string &error)> write = {};
    // Milliseconds on a monotonic clock, for auto-lock.
    std::function<uint64_t()> now_ms = {};
  };
  explicit VaultService(Seams seams) : seams_(std::move(seams)) {}
  ~VaultService() { lock(); }

  // {exists, locked, count}. Never includes an entry.
  Json status() const;
  bool create(const std::string &passphrase, Error &error);
  bool unlock(const std::string &passphrase, Error &error);
  void lock();
  // Locks when it has been unlocked and idle for `minutes` (0 means never).
  bool auto_lock(int minutes);
  // [{site, user, note}] in site order; no password.
  bool list(Json &entries, Error &error);
  bool add(const std::string &site, const std::string &user, const std::string &password,
           const std::string &note, Error &error);
  bool remove(const std::string &site, Error &error);
  bool reveal(const std::string &site, std::string &password, Error &error);
  // 20 characters from an alphabet without look-alikes, one random byte each (64 symbols, no modulo bias).
  static std::string generate();

private:
  bool exists_unlocked() const;
  bool save(Error &error);
  uint64_t now() const { return seams_.now_ms ? seams_.now_ms() : 0; }

  Seams seams_;
  mutable std::mutex mutex_;
  Vault vault_;
  bool open_ = false;
  uint64_t touched_ = 0;
};

} // namespace pv::rt
