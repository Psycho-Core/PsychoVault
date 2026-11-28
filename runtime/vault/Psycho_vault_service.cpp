// PsychoVault original source.
#include "Psycho_vault_service.hpp"
#include <algorithm>

namespace pv::rt {

namespace {
constexpr const char *kSeparator = " | ";

void scrub(std::string &text) { std::fill(text.begin(), text.end(), '\0'); }

bool plain(const std::string &field, size_t longest) {
  return field.size() <= longest && field.find(kSeparator) == std::string::npos &&
         std::none_of(field.begin(), field.end(), [](unsigned char c) { return c < 0x20 || c == 0x7f; });
}

// A row is "site | user | password | note" (the format the first vault used, so an existing file opens).
std::vector<std::string> split_row(const std::string &row) {
  std::vector<std::string> parts;
  size_t start = 0;
  while (parts.size() < 3) {
    const size_t at = row.find(kSeparator, start);
    if (at == std::string::npos) break;
    parts.push_back(row.substr(start, at - start));
    start = at + 3;
  }
  parts.push_back(row.substr(start));
  while (parts.size() < 4) parts.push_back({});
  return parts;
}
} // namespace

Json VaultService::status() const {
  std::lock_guard<std::mutex> lock(mutex_);
  Json out = Json::make_object();
  std::string blob;
  const bool exists = open_ || (seams_.read && seams_.read(blob));
  scrub(blob);
  out.set("exists", exists);
  out.set("locked", !open_);
  out.set("count", double(open_ ? vault_.entries.size() : 0));
  return out;
}

bool VaultService::save(Error &error) {
  const Bytes sealed = vault_.seal();
  std::string blob(reinterpret_cast<const char *>(sealed.data()), sealed.size());
  std::string failure;
  if (!seams_.write || !seams_.write(blob, failure)) {
    error = {"vault.write", "the vault could not be saved" + (failure.empty() ? std::string() : ": " + failure)};
    return false;
  }
  vault_.dirty = false;
  return true;
}

bool VaultService::create(const std::string &passphrase, Error &error) {
  std::lock_guard<std::mutex> lock(mutex_);
  error = Error{};
  if (passphrase.size() < 8) {
    error = {"vault.passphrase", "a vault passphrase needs at least 8 characters"};
    return false;
  }
  std::string existing;
  if (seams_.read && seams_.read(existing)) {
    scrub(existing);
    error = {"vault.exists", "a vault already exists: unlock it instead"};
    return false;
  }
  try {
    vault_ = Vault::create(passphrase);
  } catch (const std::exception &failure) {
    error = {"vault.create", failure.what()};
    return false;
  }
  open_ = true;
  touched_ = now();
  if (!save(error)) {
    open_ = false;
    return false;
  }
  return true;
}

bool VaultService::unlock(const std::string &passphrase, Error &error) {
  std::lock_guard<std::mutex> lock(mutex_);
  error = Error{};
  std::string raw;
  if (!seams_.read || !seams_.read(raw)) {
    error = {"vault.missing", "there is no vault yet"};
    return false;
  }
  try {
    vault_ = Vault::open(Bytes(raw.begin(), raw.end()), passphrase);
  } catch (const std::exception &) {
    scrub(raw);
    error = {"vault.passphrase", "that passphrase does not match the vault"};
    return false;
  }
  scrub(raw);
  open_ = true;
  touched_ = now();
  return true;
}

void VaultService::lock() {
  std::lock_guard<std::mutex> lock(mutex_);
  if (!open_) return;
  std::fill(vault_.key.begin(), vault_.key.end(), uint8_t(0));
  for (auto &entry : vault_.entries) scrub(entry.second);
  vault_.entries.clear();
  open_ = false;
}

bool VaultService::auto_lock(int minutes) {
  bool due = false;
  {
    std::lock_guard<std::mutex> lock(mutex_);
    due = open_ && minutes > 0 && now() - touched_ > uint64_t(minutes) * 60000;
  }
  if (due) lock();
  return due;
}

bool VaultService::list(Json &entries, Error &error) {
  std::lock_guard<std::mutex> lock(mutex_);
  error = Error{};
  if (!open_) {
    error = {"vault.locked", "unlock the vault first"};
    return false;
  }
  touched_ = now();
  entries = Json::make_array();
  for (const auto &[site, row] : vault_.entries) {
    const auto parts = split_row(row);
    Json item = Json::make_object();
    item.set("site", site);
    item.set("user", parts[1]);
    item.set("note", parts[3]);
    entries.array.push_back(std::move(item));
  }
  return true;
}

bool VaultService::add(const std::string &site, const std::string &user, const std::string &password,
                       const std::string &note, Error &error) {
  std::lock_guard<std::mutex> lock(mutex_);
  error = Error{};
  if (!open_) {
    error = {"vault.locked", "unlock the vault first"};
    return false;
  }
  if (site.empty() || password.empty() || !plain(site, 253) || !plain(user, 256) || !plain(password, 1024) ||
      !plain(note, 512)) {
    error = {"vault.entry", "an entry needs a site and a password, without line breaks or \" | \""};
    return false;
  }
  if (vault_.entries.count(site)) {
    error = {"vault.duplicate", "the vault already keeps a login for " + site};
    return false;
  }
  vault_.entries[site] = site + kSeparator + user + kSeparator + password + kSeparator + note;
  vault_.dirty = true;
  touched_ = now();
  if (!save(error)) {
    scrub(vault_.entries[site]);
    vault_.entries.erase(site);
    return false;
  }
  return true;
}

bool VaultService::remove(const std::string &site, Error &error) {
  std::lock_guard<std::mutex> lock(mutex_);
  error = Error{};
  if (!open_) {
    error = {"vault.locked", "unlock the vault first"};
    return false;
  }
  auto found = vault_.entries.find(site);
  if (found == vault_.entries.end()) {
    error = {"vault.missing-entry", "no login is kept for " + site};
    return false;
  }
  scrub(found->second);
  vault_.entries.erase(found);
  vault_.dirty = true;
  touched_ = now();
  return save(error);
}

bool VaultService::reveal(const std::string &site, std::string &password, Error &error) {
  std::lock_guard<std::mutex> lock(mutex_);
  error = Error{};
  if (!open_) {
    error = {"vault.locked", "unlock the vault first"};
    return false;
  }
  const auto found = vault_.entries.find(site);
  if (found == vault_.entries.end()) {
    error = {"vault.missing-entry", "no login is kept for " + site};
    return false;
  }
  password = split_row(found->second)[2];
  touched_ = now();
  return true;
}

std::string VaultService::generate() {
  static const char alphabet[] = "abcdefghijkmnpqrstuvwxyzABCDEFGHJKLMNPQRSTUVWXYZ23456789!@#$%^&*";
  const Bytes random = random_bytes(20);
  std::string out;
  for (uint8_t byte : random) out.push_back(alphabet[byte & 63]);
  return out;
}

} // namespace pv::rt
