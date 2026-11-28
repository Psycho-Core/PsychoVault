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

bool numeric_choices(const SettingSpec &spec) {
  return !spec.choices.empty() &&
         std::all_of(spec.choices.begin(), spec.choices.end(), [](const std::string &c) {
           return !c.empty() && std::all_of(c.begin(), c.end(), [](char ch) { return ch >= '0' && ch <= '9'; });
         });
}

// One entry per non-empty line, trimmed, at most `limit` entries of at most `longest` bytes.
std::vector<std::string> split_lines(const std::string &text, size_t limit, size_t longest) {
  std::vector<std::string> out;
  size_t start = 0;
  while (start <= text.size() && out.size() < limit) {
    size_t end = text.find('\n', start);
    if (end == std::string::npos) end = text.size();
    std::string line = text.substr(start, end - start);
    while (!line.empty() && (line.back() == '\r' || line.back() == ' ' || line.back() == '\t')) line.pop_back();
    size_t first = 0;
    while (first < line.size() && (line[first] == ' ' || line[first] == '\t')) ++first;
    line = line.substr(first);
    if (!line.empty() && line.size() <= longest) out.push_back(line);
    start = end + 1;
  }
  return out;
}

std::string join_lines(const std::vector<std::string> &lines) {
  std::string out;
  for (const std::string &line : lines) {
    if (!out.empty()) out += '\n';
    out += line;
  }
  return out;
}

// The CPU limit select and the profile's percentage: off is no cap, low is a tighter cap.
int cpu_percent(const std::string &mode) { return mode == "low" ? 30 : mode == "balanced" ? 60 : 0; }
std::string cpu_mode(int percent) { return percent <= 0 ? "off" : percent <= 30 ? "low" : "balanced"; }

struct Normalized {
  bool flag = false;
  std::string text;
  int number = 0;
  Json json;
};

bool plain_text(const std::string &text, size_t shortest, size_t longest) {
  return text.size() >= shortest && text.size() <= longest &&
         std::none_of(text.begin(), text.end(), [](unsigned char c) { return c < 0x20 || c == 0x7f; });
}
bool hex_colour(const std::string &text) {
  return text.size() == 7 && text[0] == '#' &&
         std::all_of(text.begin() + 1, text.end(), [](unsigned char c) { return std::isxdigit(c) != 0; });
}
bool web_address(const std::string &text) {
  auto parsed = parse_url(text);
  return text.size() <= 2048 && parsed && parsed->network();
}

// Each structured setting has its own shape; anything else is refused whole.
bool check_structure(const std::string &key, const Json &value, Error &error) {
  auto bad = [&](const std::string &why) {
    error = {"setting.structure", "'" + key + "': " + why};
    return false;
  };
  if (key == "containers") {
    if (value.type != Json::Type::Array || value.array.size() > 12) return bad("a list of at most 12 containers");
    for (const Json &item : value.array)
      if (item.type != Json::Type::Object || !plain_text(item.str("name"), 1, 32) || !hex_colour(item.str("color")))
        return bad("each container needs a name and a #rrggbb colour");
  } else if (key == "services") {
    if (value.type != Json::Type::Array || value.array.size() > 24) return bad("a list of at most 24 services");
    for (const Json &item : value.array)
      if (item.type != Json::Type::Object || !plain_text(item.str("name"), 1, 32) || !web_address(item.str("url")))
        return bad("each service needs a name and an http or https address");
  } else if (key == "speedDial") {
    if (value.type != Json::Type::Array || value.array.size() > 24) return bad("a list of at most 24 tiles");
    for (const Json &item : value.array)
      if (item.type != Json::Type::Object || !plain_text(item.str("title"), 1, 120) || !web_address(item.str("url")))
        return bad("each tile needs a title and an http or https address");
  } else if (key == "sitePermissions") {
    if (value.type != Json::Type::Object || value.object.size() > 500) return bad("rules for at most 500 sites");
    for (const auto &[origin, rule] : value.object) {
      auto parsed = parse_url(origin);
      if (!parsed || !parsed->network() || rule.type != Json::Type::Object || rule.object.size() > 4)
        return bad("each site is an http or https origin with up to four rules");
      for (const auto &[name, setting] : rule.object)
        if ((name != "camera" && name != "microphone" && name != "location" && name != "notifications") ||
            setting.type != Json::Type::String || (setting.string != "allow" && setting.string != "deny"))
          return bad("a rule is camera, microphone, location or notifications set to allow or deny");
    }
  } else {
    return bad("no such structured setting");
  }
  return true;
}

bool normalize(const std::string &key, const Json &value, Normalized &out, Error &error) {
  error = Error{};
  const SettingSpec *spec = SettingsService::find(key);
  if (!spec) {
    error = {"setting.unknown", "no setting is named '" + key + "'"};
    return false;
  }
  switch (spec->kind) {
  case SettingKind::Bool:
    if (value.type != Json::Type::Bool) {
      error = {"setting.type", "'" + key + "' takes true or false"};
      return false;
    }
    out.flag = value.boolean;
    break;
  case SettingKind::Choice:
    out.text = choice_text(value);
    if (std::find(spec->choices.begin(), spec->choices.end(), out.text) == spec->choices.end()) {
      error = {"setting.choice", "'" + out.text.substr(0, 64) + "' is not a choice for '" + key + "'"};
      return false;
    }
    break;
  case SettingKind::Number:
    if (value.type != Json::Type::Number || !std::isfinite(value.number) ||
        value.number != std::floor(value.number) || value.number < 0 || value.number > 1440) {
      error = {"setting.range", "'" + key + "' takes a whole number from 0 to 1440"};
      return false;
    }
    out.number = static_cast<int>(value.number);
    break;
  case SettingKind::Text:
    if (value.type != Json::Type::String || value.string.size() > 2048 ||
        std::any_of(value.string.begin(), value.string.end(),
                    [](unsigned char c) { return c < 0x20 || c == 0x7f; })) {
      error = {"setting.text", "'" + key + "' takes plain text of at most 2048 characters"};
      return false;
    }
    out.text = value.string;
    break;
  case SettingKind::Json:
    if (!check_structure(key, value, error)) return false;
    out.json = value;
    break;
  case SettingKind::Lines:
    if (value.type != Json::Type::String || value.string.size() > 65536 ||
        std::any_of(value.string.begin(), value.string.end(),
                    [](unsigned char c) { return (c < 0x20 && c != '\n' && c != '\r' && c != '\t') || c == 0x7f; })) {
      error = {"setting.lines", "'" + key + "' takes plain text lines of at most 64 KiB"};
      return false;
    }
    out.text = value.string;
    break;
  }
  return true;
}

} // namespace

bool SettingsService::validate(const std::string &key, const Json &value, Error &error) {
  Normalized ignored;
  return normalize(key, value, ignored, error);
}

namespace {
// The rail's services before the owner changes the list.
Json default_services() {
  static const char *const names[][2] = {{"Twitch", "https://www.twitch.tv/"}, {"YouTube Music", "https://music.youtube.com/"},
                                         {"YouTube", "https://www.youtube.com/"}, {"X", "https://x.com/"},
                                         {"Reddit", "https://www.reddit.com/"}, {"GitHub", "https://github.com/"},
                                         {"WhatsApp", "https://web.whatsapp.com/"}, {"Telegram", "https://web.telegram.org/"},
                                         {"Spotify", "https://open.spotify.com/"}};
  Json list = Json::make_array();
  for (const auto &entry : names) {
    Json item = Json::make_object();
    item.set("name", entry[0]);
    item.set("url", entry[1]);
    list.array.push_back(std::move(item));
  }
  return list;
}
} // namespace

Json SettingsService::snapshot() const {
  Json values = Json::make_object();
  for (const SettingSpec &spec : catalog()) {
    const std::string key = spec.key;
    if (key == "search") values.set(key, Json(profile_.search_engine));
    else if (key == "homepage") values.set(key, Json(profile_.home_page));
    else if (key == "restore") values.set(key, Json(profile_.restore_tabs));
    else if (key == "workspacesEnabled") values.set(key, Json(profile_.workspaces));
    else if (key == "remember") values.set(key, Json(profile_.remember_history));
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
    else if (key == "cpuLimit") values.set(key, Json(cpu_mode(profile_.cpu_limit)));
    else if (key == "theme") {
      // A profile from before themes existed says "dark"; that is the midnight theme.
      const auto &choices = spec.choices;
      values.set(key, Json(std::find(choices.begin(), choices.end(), profile_.theme) != choices.end() ? profile_.theme : std::string("midnight")));
    }
    else if (key == "accent") values.set(key, Json(profile_.accent));
    else if (key == "hideSidebar") values.set(key, Json(profile_.hide_sidebar));
    else if (key == "highContrast") values.set(key, Json(profile_.high_contrast));
    else if (key == "reduceMotion") values.set(key, Json(profile_.reduce_motion));
    else if (key == "showTabCloseButtons") values.set(key, Json(profile_.show_tab_close_buttons));
    else if (key == "panicEnabled") values.set(key, Json(profile_.panic_enabled));
    else if (key == "panicUrl") values.set(key, Json(profile_.panic_url));
    else if (key == "spellCheck") values.set(key, Json(profile_.spell_check));
    else if (key == "spellBuiltin") values.set(key, Json(profile_.spell_builtin));
    else if (key == "spellLangs") values.set(key, Json(profile_.spell_langs));
    else if (key == "spellDictUrl") values.set(key, Json(profile_.spell_dict_source));
    else if (key == "spellUserWords") values.set(key, Json(join_lines(profile_.user_words)));
    else if (key == "blockAllowSites") values.set(key, Json(join_lines(profile_.block_allow_sites)));
    else if (key == "blockCustomDomains") values.set(key, Json(join_lines(profile_.blocked_hosts)));
    else if (key == "vaultLockMin") values.set(key, Json(profile_.vault_lock_minutes));
    else if (key == "speedDial") {
      Json tiles = Json::make_array();
      for (const Bookmark &tile : profile_.speed_dial) {
        Json item = Json::make_object();
        item.set("title", tile.title);
        item.set("url", tile.url);
        tiles.array.push_back(std::move(item));
      }
      values.set(key, std::move(tiles));
    }
    else if (const Json *stored = profile_.extra_settings.find(key)) values.set(key, *stored);
    else if (key == "services") values.set(key, default_services());
    else if (key == "containers") values.set(key, Json::make_array());
    else if (key == "sitePermissions") values.set(key, Json::make_object());
  }
  Json out = Json::make_object();
  out.set("values", std::move(values));
  out.set("unwired", Json::make_object());
  return out;
}

bool SettingsService::set(const std::string &key, const Json &value, Error &error) {
  Normalized normalized;
  if (!normalize(key, value, normalized, error)) return false;
  const SettingSpec *spec = find(key);
  const bool flag = normalized.flag;
  const std::string &text = normalized.text;
  const int number = normalized.number;
  auto refuse = [&](const std::string &why) {
    error = {"setting.invalid", "'" + key + "': " + why};
    return false;
  };
  if (key == "customSearchEngines") {
    // Every non-empty line must be "shortcut | name | https://...%s", or nothing is saved.
    size_t start = 0, number = 0;
    while (start <= text.size()) {
      size_t end = text.find('\n', start);
      if (end == std::string::npos) end = text.size();
      ++number;
      std::string line = text.substr(start, end - start);
      while (!line.empty() && (line.back() == '\r' || line.back() == ' ')) line.pop_back();
      std::string shortcut, name, url;
      if (!line.empty() && !Profile::parse_search_shortcut(line, shortcut, name, url))
        return refuse("line " + std::to_string(number) + " is not 'shortcut | name | https://address-with-%s'");
      start = end + 1;
    }
  }
  if (key == "search") { if (!profile_.set_search_engine(text)) return refuse("unsupported engine"); }
  else if (key == "homepage") { if (!profile_.set_home_page(text)) return refuse("not an address I can open"); }
  else if (key == "restore") profile_.restore_tabs = flag;
  else if (key == "workspacesEnabled") profile_.workspaces = flag;
  else if (key == "remember") profile_.remember_history = flag;
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
  else if (key == "cpuLimit") profile_.cpu_limit = cpu_percent(text);
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
  else if (key == "spellLangs") profile_.spell_langs = text;
  else if (key == "spellDictUrl") profile_.spell_dict_source = text;
  else if (key == "spellUserWords") profile_.user_words = split_lines(text, 4096, 64);
  else if (key == "blockAllowSites") profile_.block_allow_sites = split_lines(text, 4096, 253);
  else if (key == "blockCustomDomains") profile_.blocked_hosts = split_lines(text, 4096, 253);
  else if (key == "vaultLockMin") profile_.vault_lock_minutes = std::atoi(text.c_str());
  else if (key == "speedDial") {
    profile_.speed_dial.clear();
    for (const Json &item : normalized.json.array) profile_.speed_dial.push_back({item.str("title"), item.str("url")});
  }
  else {
    // No dedicated field: the profile keeps the validated value in its extra-settings bag.
    Json stored;
    switch (spec->kind) {
    case SettingKind::Bool: stored = Json(flag); break;
    case SettingKind::Number: stored = Json(number); break;
    case SettingKind::Choice: stored = numeric_choices(*spec) ? Json(std::atoi(text.c_str())) : Json(text); break;
    case SettingKind::Text:
    case SettingKind::Lines: stored = Json(text); break;
    case SettingKind::Json: stored = normalized.json; break;
    }
    profile_.extra_settings.set(key, std::move(stored));
  }
  if (key == "clearOnExit") profile_.clear_on_exit = text != "nothing";
  return true;
}

} // namespace pv::rt
