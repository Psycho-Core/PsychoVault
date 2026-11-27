// PsychoVault original source.
#include "Psycho_settings.hpp"
#include <algorithm>
#include <cmath>

namespace pv::rt {

const std::vector<SettingSpec> &SettingsService::catalog() {
  static const std::vector<SettingSpec> table = {
#include "Psycho_settings_catalog.inc"
  };
  return table;
}

const SettingSpec *SettingsService::find(const std::string &key) {
  for (const SettingSpec &spec : catalog())
    if (key == spec.key) return &spec;
  return nullptr;
}

const char *SettingsService::unwired_owner() { return "S141-S145"; }

namespace {
std::string choice_text(const Json &value) {
  if (value.type == Json::Type::String) return value.string;
  if (value.type == Json::Type::Number && std::isfinite(value.number) &&
      value.number == std::floor(value.number))
    return std::to_string(static_cast<long long>(value.number));
  return {};
}
} // namespace

Json SettingsService::snapshot() const {
  Json values = Json::make_object(), unwired = Json::make_object();
  for (const SettingSpec &spec : catalog()) {
    const std::string key = spec.key;
    if (!spec.wired) {
      unwired.set(key, Json(std::string(unwired_owner())));
      continue;
    }
    if (key == "search") values.set(key, Json(profile_.search_engine));
    else if (key == "homepage") values.set(key, Json(profile_.home_page));
    else if (key == "restore") values.set(key, Json(profile_.restore_tabs));
    else if (key == "workspacesEnabled") values.set(key, Json(profile_.workspaces));
    else if (key == "blockAds") values.set(key, Json(profile_.block_ads));
    else if (key == "blockThirdPartyCookies") values.set(key, Json(profile_.block_third_party_cookies));
    else if (key == "sendDNT") values.set(key, Json(profile_.send_dnt));
    else if (key == "stripReferrer") values.set(key, Json(profile_.strip_referrer));
    else if (key == "blockAutoplay") values.set(key, Json(profile_.block_autoplay));
    else if (key == "webRtc") values.set(key, Json(profile_.web_rtc_policy));
    else if (key == "downloadDir") values.set(key, Json(profile_.download_directory));
    else if (key == "askSaveLocation") values.set(key, Json(profile_.ask_save_location));
    else if (key == "downloadNotify") values.set(key, Json(profile_.download_notify));
    else if (key == "batterySaver") values.set(key, Json(profile_.battery_saver));
    else if (key == "sleepTabsMin") values.set(key, Json(profile_.sleep_tabs_minutes));
    else if (key == "theme") values.set(key, Json(profile_.theme));
    else if (key == "accent") values.set(key, Json(profile_.accent));
    else if (key == "hideSidebar") values.set(key, Json(profile_.hide_sidebar));
    else if (key == "highContrast") values.set(key, Json(profile_.high_contrast));
    else if (key == "reduceMotion") values.set(key, Json(profile_.reduce_motion));
    else if (key == "showTabCloseButtons") values.set(key, Json(profile_.show_tab_close_buttons));
    else if (key == "panicEnabled") values.set(key, Json(profile_.panic_enabled));
    else if (key == "panicUrl") values.set(key, Json(profile_.panic_url));
    else if (key == "spellCheck") values.set(key, Json(profile_.spell_check));
    else if (key == "spellBuiltin") values.set(key, Json(profile_.spell_builtin));
    else if (key == "vaultLockMin") values.set(key, Json(profile_.vault_lock_minutes));
  }
  Json out = Json::make_object();
  out.set("values", std::move(values));
  out.set("unwired", std::move(unwired));
  return out;
}

namespace {
struct Normalized {
  bool flag = false;
  std::string text;
  int number = 0;
};
bool normalize(const std::string &key, const Json &value, Normalized &out, Error &error);
} // namespace

bool SettingsService::validate(const std::string &key, const Json &value, Error &error) {
  Normalized ignored;
  return normalize(key, value, ignored, error);
}

namespace {
bool normalize(const std::string &key, const Json &value, Normalized &out, Error &error) {
  error = Error{};
  const SettingSpec *spec = SettingsService::find(key);
  if (!spec) {
    error = {"setting.unknown", "no setting is named '" + key + "'"};
    return false;
  }
  if (!spec->wired) {
    error = {"setting.unwired", "'" + key + "' is not connected to the profile yet (owner " +
                                    std::string(SettingsService::unwired_owner()) + ")"};
    return false;
  }
  bool flag = false;
  std::string text;
  int number = 0;
  switch (spec->kind) {
  case SettingKind::Bool:
    if (value.type != Json::Type::Bool) {
      error = {"setting.type", "'" + key + "' takes true or false"};
      return false;
    }
    flag = value.boolean;
    break;
  case SettingKind::Choice:
    text = choice_text(value);
    if (std::find(spec->choices.begin(), spec->choices.end(), text) == spec->choices.end()) {
      error = {"setting.choice", "'" + text.substr(0, 64) + "' is not a choice for '" + key + "'"};
      return false;
    }
    break;
  case SettingKind::Number:
    if (value.type != Json::Type::Number || !std::isfinite(value.number) ||
        value.number != std::floor(value.number) || value.number < 0 || value.number > 1440) {
      error = {"setting.range", "'" + key + "' takes a whole number from 0 to 1440"};
      return false;
    }
    number = static_cast<int>(value.number);
    break;
  case SettingKind::Text:
    if (value.type != Json::Type::String || value.string.size() > 2048 ||
        std::any_of(value.string.begin(), value.string.end(),
                    [](unsigned char c) { return c < 0x20 || c == 0x7f; })) {
      error = {"setting.text", "'" + key + "' takes plain text of at most 2048 characters"};
      return false;
    }
    text = value.string;
    break;
  }
  out.flag = flag;
  out.text = text;
  out.number = number;
  return true;
}
} // namespace

bool SettingsService::set(const std::string &key, const Json &value, Error &error) {
  Normalized normalized;
  if (!normalize(key, value, normalized, error)) return false;
  const bool flag = normalized.flag;
  const std::string &text = normalized.text;
  const int number = normalized.number;
  auto refuse = [&](const char *why) {
    error = {"setting.invalid", "'" + key + "': " + why};
    return false;
  };
  if (key == "search") { if (!profile_.set_search_engine(text)) return refuse("unsupported engine"); }
  else if (key == "homepage") { if (!profile_.set_home_page(text)) return refuse("not an address I can open"); }
  else if (key == "restore") profile_.restore_tabs = flag;
  else if (key == "workspacesEnabled") profile_.workspaces = flag;
  else if (key == "blockAds") profile_.block_ads = flag;
  else if (key == "blockThirdPartyCookies") profile_.block_third_party_cookies = flag;
  else if (key == "sendDNT") profile_.send_dnt = flag;
  else if (key == "stripReferrer") profile_.strip_referrer = flag;
  else if (key == "blockAutoplay") profile_.block_autoplay = flag;
  else if (key == "webRtc") profile_.web_rtc_policy = text;
  else if (key == "downloadDir") { if (!profile_.set_download_directory(text)) return refuse("that folder cannot be used"); }
  else if (key == "askSaveLocation") profile_.ask_save_location = flag;
  else if (key == "downloadNotify") profile_.download_notify = flag;
  else if (key == "batterySaver") profile_.battery_saver = flag;
  else if (key == "sleepTabsMin") profile_.sleep_tabs_minutes = number;
  else if (key == "theme") profile_.theme = text;
  else if (key == "accent") profile_.accent = text;
  else if (key == "hideSidebar") profile_.hide_sidebar = flag;
  else if (key == "highContrast") profile_.high_contrast = flag;
  else if (key == "reduceMotion") profile_.reduce_motion = flag;
  else if (key == "showTabCloseButtons") profile_.show_tab_close_buttons = flag;
  else if (key == "panicEnabled") profile_.panic_enabled = flag;
  else if (key == "panicUrl") {
    auto parsed = parse_url(text);
    if (!parsed || !parsed->network()) return refuse("needs an http or https address");
    profile_.panic_url = text;
  }
  else if (key == "spellCheck") profile_.spell_check = flag;
  else if (key == "spellBuiltin") profile_.spell_builtin = flag;
  else if (key == "vaultLockMin") profile_.vault_lock_minutes = std::atoi(text.c_str());
  else return refuse("no profile field is mapped");
  return true;
}

} // namespace pv::rt
