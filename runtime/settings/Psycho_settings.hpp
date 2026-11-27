#pragma once
// P3/S088: the settings service. It is the one place a settings page's change becomes a profile
// change: each control is a catalog row (kind, allowed choices, whether a profile field backs it),
// a value is validated against its row before anything is written, and a control with no backing
// field is refused with the step that will own it rather than accepted and forgotten.
#include "../../platform/Psycho_profile.hpp"
#include "../ipc/Psycho_bus.hpp"
#include <string>
#include <vector>

namespace pv::rt {

enum class SettingKind { Bool, Choice, Text, Number };
struct SettingSpec {
  const char *key;
  SettingKind kind;
  std::vector<std::string> choices;
  bool wired;
};

class SettingsService {
public:
  explicit SettingsService(Profile &profile) : profile_(profile) {}
  static const std::vector<SettingSpec> &catalog();
  static const SettingSpec *find(const std::string &key);
  // Where an unwired control's persistence is owned (P6, S141-S145; effects arrive in P7).
  static const char *unwired_owner();
  // {"values": {key: value, ...}} for every wired key and {"unwired": {key: owner}} for the rest.
  Json snapshot() const;
  // Pure check of a value against the key's catalog row; touches no profile. Safe from any thread.
  static bool validate(const std::string &key, const Json &value, Error &error);
  // Validates, then applies to the profile in memory. The caller saves the profile.
  bool set(const std::string &key, const Json &value, Error &error);

private:
  Profile &profile_;
};

} // namespace pv::rt
