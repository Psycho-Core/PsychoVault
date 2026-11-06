// PsychoVault original source. The private session has no persistence path by
// construction: its stores and permission service are owned only by this
// object. See the laws in Psycho_private.hpp.
#include "Psycho_private.hpp"

namespace pv::rt {

namespace {

Error refuse(const char *code, const std::string &message) {
  Error error;
  error.code = code;
  error.message = message;
  return error;
}

} // namespace

PrivateSession::PrivateSession() : PrivateSession(Seams{}) {}

PrivateSession::PrivateSession(Seams seams)
    : permissions_(PermissionService::Seams{
          {}, {}, {}, std::move(seams.show_permission_prompt)}) {}

bool PrivateSession::valid_store(PrivateStore store) {
  return store == PrivateStore::Cookies || store == PrivateStore::LocalStorage;
}

bool PrivateSession::check_key(PrivateStore store, const std::string &origin,
                               const std::string &key, Error &error) {
  if (!valid_store(store)) {
    error = refuse("private.store", "unknown private store");
    return false;
  }
  if (!PermissionService::valid_origin(origin)) {
    error = refuse("private.origin", "private state needs a normalized web origin");
    return false;
  }
  if (key.empty()) {
    error = refuse("private.key", "private store keys cannot be empty");
    return false;
  }
  return true;
}

bool PrivateSession::set(PrivateStore store, const std::string &origin,
                         const std::string &key, const std::string &value,
                         Error &error) {
  error = Error{};
  if (!check_key(store, origin, key, error)) return false;
  Store &target = store == PrivateStore::Cookies ? cookies_ : local_storage_;
  target[{origin, key}] = value;
  return true;
}

bool PrivateSession::get(PrivateStore store, const std::string &origin,
                         const std::string &key, std::string &value,
                         bool &found, Error &error) const {
  error = Error{};
  found = false;
  if (!check_key(store, origin, key, error)) return false;
  const Store &target = store == PrivateStore::Cookies ? cookies_ : local_storage_;
  const auto it = target.find({origin, key});
  if (it != target.end()) {
    value = it->second;
    found = true;
  } else {
    value.clear();
  }
  return true;
}

bool PrivateSession::erase(PrivateStore store, const std::string &origin,
                           const std::string &key, Error &error) {
  error = Error{};
  if (!check_key(store, origin, key, error)) return false;
  Store &target = store == PrivateStore::Cookies ? cookies_ : local_storage_;
  target.erase({origin, key});
  return true;
}

void PrivateSession::clear_stores() {
  cookies_.clear();
  local_storage_.clear();
}

size_t PrivateSession::size(PrivateStore store) const {
  if (store == PrivateStore::Cookies) return cookies_.size();
  if (store == PrivateStore::LocalStorage) return local_storage_.size();
  return 0;
}

} // namespace pv::rt
