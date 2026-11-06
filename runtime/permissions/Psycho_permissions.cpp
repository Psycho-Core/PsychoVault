// PsychoVault original source. See the header for the five laws - this file
// is how I keep them.
#include "Psycho_permissions.hpp"
#include <cctype>

namespace pv::rt {

namespace {

Error refuse(const char *code, const std::string &message) {
  Error error;
  error.code = code;
  error.message = message;
  return error;
}

} // namespace

const std::set<std::string> &PermissionService::known_kinds() {
  // The legacy defaultSettings.denyPerms, verbatim. This is the floor (law 1):
  // the day I add a kind, it enters defaulting to Ask or Deny - never Allow.
  static const std::set<std::string> kinds = {"geolocation", "media",
                                              "clipboard-read", "midi",
                                              "sync-xhr", "wake-lock",
                                              "notifications"};
  return kinds;
}

bool PermissionService::valid_origin(const std::string &origin) {
  // scheme://host[:port] and nothing else - no path, query, fragment,
  // trailing slash, userinfo or uppercase: normalized in, verified here.
  std::string rest;
  if (origin.rfind("https://", 0) == 0) rest = origin.substr(8);
  else if (origin.rfind("http://", 0) == 0) rest = origin.substr(7);
  else return false;
  if (rest.empty()) return false;
  for (char c : rest) {
    if (c == '/' || c == '?' || c == '#' || c == '@' || c == ' ' ||
        (unsigned char)c >= 0x7F || std::isupper((unsigned char)c)) {
      return false;
    }
  }
  // A port, when present, is digits and in range.
  const size_t colon = rest.rfind(':');
  if (colon != std::string::npos) {
    const std::string port = rest.substr(colon + 1);
    if (port.empty() || port.size() > 5) return false;
    unsigned long value = 0;
    for (char c : port) {
      if (!std::isdigit((unsigned char)c)) return false;
      value = value * 10 + (unsigned long)(c - '0');
    }
    if (value == 0 || value > 65535) return false;
  }
  // The host part names somebody.
  return !rest.substr(0, colon).empty();
}

bool PermissionService::check(const std::string &origin,
                              const std::string &kind, Error &error) const {
  if (known_kinds().count(kind) == 0) {
    error = refuse("perm.kind", "unknown permission kind '" + kind + "'");
    return false;
  }
  if (!valid_origin(origin)) {
    error = refuse("perm.origin", "unnormalized origin '" + origin + "'");
    return false;
  }
  return true;
}

bool PermissionService::decide(const std::string &origin,
                               const std::string &kind, Decision &out,
                               Error &error) const {
  error = Error{};
  if (!check(origin, kind, error)) return false;
  auto it = store_.find({origin, kind});
  if (it != store_.end()) {
    out = it->second.decision;
    return true;
  }
  // Law 1: the known kinds are the legacy deny list - all default to Deny.
  out = Decision::Deny;
  return true;
}

bool PermissionService::store(const std::string &origin,
                              const std::string &kind, Decision decision,
                              bool remember, Error &error) {
  if (remember) {
    if (!seams_.save) {
      error = refuse("perm.unavailable",
                     "this build has no profile to remember in");
      return false;
    }
    store_[{origin, kind}] = Entry{decision, true};
    PermissionGrant row;
    row.origin = origin;
    row.kind = kind;
    row.allow = decision == Decision::Allow;
    seams_.save(row);
  } else {
    store_[{origin, kind}] = Entry{decision, false};
  }
  return true;
}

bool PermissionService::grant(const std::string &origin,
                              const std::string &kind, Error &error) {
  error = Error{};
  if (!check(origin, kind, error)) return false;
  return store(origin, kind, Decision::Allow, true, error);
}

bool PermissionService::block(const std::string &origin,
                              const std::string &kind, Error &error) {
  error = Error{};
  if (!check(origin, kind, error)) return false;
  return store(origin, kind, Decision::Deny, true, error);
}

bool PermissionService::reset(const std::string &origin,
                              const std::string &kind, Error &error) {
  error = Error{};
  if (!check(origin, kind, error)) return false;
  auto it = store_.find({origin, kind});
  if (it == store_.end()) return true; // nothing held: a quiet no-op
  const bool was_persistent = it->second.persistent;
  store_.erase(it);
  if (was_persistent && seams_.drop) seams_.drop(origin, kind);
  return true;
}

size_t PermissionService::reset_origin(const std::string &origin,
                                       Error &error) {
  error = Error{};
  if (!valid_origin(origin)) {
    error = refuse("perm.origin", "unnormalized origin '" + origin + "'");
    return 0;
  }
  size_t dropped = 0;
  for (auto it = store_.begin(); it != store_.end();) {
    if (it->first.first == origin) {
      if (it->second.persistent && seams_.drop) {
        seams_.drop(origin, it->first.second);
      }
      it = store_.erase(it);
      ++dropped;
    } else {
      ++it;
    }
  }
  return dropped;
}

uint64_t PermissionService::ask(const std::string &origin,
                                const std::string &kind, Error &error) {
  error = Error{};
  if (!check(origin, kind, error)) return 0;
  // Law 3's coalescing: one visible prompt per (origin, kind), re-asking
  // hands back the live token instead of stacking dialogs.
  for (const auto &[token, key] : pending_) {
    if (key.first == origin && key.second == kind) return token;
  }
  if (pending_.size() >= kPendingLimit) {
    error = refuse("perm.too-many", "16 prompts already wait for answers");
    return 0;
  }
  if (!seams_.show_prompt) {
    error = refuse("perm.unavailable", "this build cannot show prompts");
    return 0;
  }
  const uint64_t token = next_token_++;
  pending_[token] = {origin, kind};
  seams_.show_prompt(origin, kind);
  return token;
}

bool PermissionService::answer(uint64_t token, Decision user, bool remember,
                               Error &error) {
  error = Error{};
  auto it = pending_.find(token);
  if (it == pending_.end()) {
    error = refuse("perm.unknown", "answering a prompt that never pended");
    return false;
  }
  if (user == Decision::Ask) {
    error = refuse("perm.decision", "'ask' is not an answer");
    return false;
  }
  if (remember && !seams_.save) {
    // Law 3: the prompt stays; the user may answer again without remember.
    error = refuse("perm.unavailable",
                   "this build has no profile to remember in");
    return false;
  }
  const std::pair<std::string, std::string> key = it->second;
  pending_.erase(it);
  return store(key.first, key.second, user, remember, error);
}

bool PermissionService::drop_prompt(uint64_t token) {
  return pending_.erase(token) != 0; // a dead tab owes no record
}

bool PermissionService::load_profile(Error &error) {
  error = Error{};
  if (loaded_) {
    error = refuse("perm.state", "the profile is loaded exactly once");
    return false;
  }
  loaded_ = true;
  if (!seams_.load) return true; // the session build has nothing to load
  for (const PermissionGrant &row : seams_.load()) {
    // Law 4: malformed rows drop, not trusted.
    if (known_kinds().count(row.kind) == 0 || !valid_origin(row.origin)) {
      continue;
    }
    store_[{row.origin, row.kind}] =
        Entry{row.allow ? Decision::Allow : Decision::Deny, true};
  }
  return true;
}

} // namespace pv::rt
