#include "Psycho_profile.hpp"
#include <chrono>
#include <filesystem>
#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <shlobj.h>
#else
#include <cstdlib>
#endif
namespace pv {
namespace fs = std::filesystem;
static fs::path utf8_path(const std::string &text) {
  auto data = reinterpret_cast<const char8_t *>(text.data());
  return fs::path(data, data + text.size());
}
static std::string utf8_string(const fs::path &path) {
  auto text = path.u8string();
  return std::string(reinterpret_cast<const char *>(text.data()), text.size());
}
#ifdef _WIN32
static std::string narrow(const std::wstring &w) {
  if (w.empty())
    return {};
  int n = WideCharToMultiByte(CP_UTF8, 0, w.data(), int(w.size()), nullptr, 0, nullptr, nullptr);
  std::string s(size_t(n), 0);
  WideCharToMultiByte(CP_UTF8, 0, w.data(), int(w.size()), s.data(), n, nullptr, nullptr);
  return s;
}
#endif
std::string default_profile_directory() {
#ifdef _WIN32
  wchar_t *local = nullptr;
  if (SHGetKnownFolderPath(FOLDERID_LocalAppData, 0, nullptr, &local) != S_OK || !local)
    return ".psychovault";
  std::wstring path = std::wstring(local) + L"\\PsychoVault\\Owned";
  CoTaskMemFree(local);
  return narrow(path);
#else
  const char *home = std::getenv("HOME");
  return std::string(home ? home : ".") + "/.local/share/PsychoVault/Owned";
#endif
}
static double now_seconds() {
  return double(std::chrono::duration_cast<std::chrono::milliseconds>(
                    std::chrono::system_clock::now().time_since_epoch())
                    .count()) /
         1000.0;
}
static bool valid_browser_url(const std::string &value) {
  if (value.empty() || value.size() > 8192)
    return false;
  if (value == "about:blank" || starts(value, "psychovault://"))
    return true;
  auto url = parse_url(value);
  return url && url->network();
}
static std::string bounded(std::string value, size_t limit) {
  if (value.size() > limit)
    value.resize(limit);
  return value;
}
Profile Profile::open(const std::string &directory, bool private_mode) {
  Profile p;
  p.directory = directory;
  p.private_mode = private_mode;
  if (private_mode)
    return p;
  std::error_code ec;
  fs::create_directories(utf8_path(directory), ec);
  if (ec) {
    p.error = "cannot create profile directory";
    return p;
  }
  auto load = [&](const char *name, Json &out) {
    fs::path path = utf8_path(directory) / name;
    if (!fs::exists(path, ec))
      return;
    try {
      auto text = read_text_file(utf8_string(path));
      auto parsed = Json::parse(text);
      if (!parsed)
        throw std::runtime_error("invalid JSON");
      out = std::move(*parsed);
    } catch (const std::exception &) {
      // I keep the damaged file instead of silently replacing the user's data.
      auto stamp = std::to_string(std::chrono::system_clock::now().time_since_epoch().count());
      fs::path backup = path;
      backup += ".corrupt-" + stamp;
      std::error_code ignored;
      fs::rename(path, backup, ignored);
      p.error = std::string("recovered after damaged ") + name + "; backup kept beside profile";
    }
  };
  Json settings;
  load("owned-settings.json", settings);
  load("owned-cookies.json", p.cookies);
  load("owned-storage.json", p.local_storage);
  if (settings.type == Json::Type::Object) {
    p.set_custom_search(settings.str("customSearch"));
    p.set_search_engine(settings.str("searchEngine", p.search_engine));
    p.set_home_page(settings.str("homePage", p.home_page));
    p.restore_tabs = settings.flag("restoreTabs", true);
    p.workspaces = settings.flag("workspaces", true);
    auto read_list = [&](const char *key, std::vector<std::string> &out) {
      const Json *value = settings.find(key);
      if (!value || value->type != Json::Type::Array) return;
      for (const auto &entry : value->array) {
        if (out.size() >= 4096) break;
        if (entry.type == Json::Type::String && !entry.string.empty()) out.push_back(entry.string);
      }
    };
    p.download_directory = settings.str("downloadDirectory");
    p.ask_save_location = settings.flag("askSaveLocation", false);
    p.download_notify = settings.flag("downloadNotify", true);
    p.block_ads = settings.flag("blockAds", true);
    p.block_third_party_cookies = settings.flag("blockThirdPartyCookies", true);
    p.send_dnt = settings.flag("sendDNT", true);
    p.strip_referrer = settings.flag("stripReferrer", false);
    p.clear_on_exit = settings.flag("clearOnExit", false);
    p.web_rtc_policy = settings.str("webRtc", "default");
    read_list("blockedHosts", p.blocked_hosts);
    read_list("blockAllowSites", p.block_allow_sites);
    if (const Json *proxy = settings.find("proxy")) {
      std::string proxy_error;
      auto config = ProxyConfig::from_json(*proxy, proxy_error);
      // A corrupt proxy record must not silently become a direct connection.
      if (config) p.proxy = *config;
      else if (!proxy->str("mode", "direct").empty() && proxy->str("mode", "direct") != "direct")
        p.error = "I ignored a damaged proxy record: " + proxy_error;
    }
    p.theme = settings.str("theme", "dark");
    p.accent = settings.str("accent", "cyan");
    p.high_contrast = settings.flag("highContrast", false);
    p.reduce_motion = settings.flag("reduceMotion", false);
    p.show_tab_close_buttons = settings.flag("showTabCloseButtons", true);
    p.hide_sidebar = settings.flag("hideSidebar", false);
    p.cpu_limit = int(std::clamp(settings.num("cpuLimit", 0), 0.0, 100.0));
    p.sleep_tabs_minutes = int(std::clamp(settings.num("sleepTabsMin", 0), 0.0, 1440.0));
    p.battery_saver = settings.flag("batterySaver", false);
    p.spell_check = settings.flag("spellCheck", true);
    read_list("spellUserWords", p.user_words);
    read_list("extensionDirectories", p.extension_directories);
    read_list("disabledExtensions", p.disabled_extensions);
  }
  Json bookmarks;
  load("owned-bookmarks.json", bookmarks);
  if (bookmarks.type == Json::Type::Array)
    for (auto &b : bookmarks.array) {
      if (p.bookmarks.size() >= 10000)
        break;
      auto url = b.str("url");
      if (b.type == Json::Type::Object && valid_browser_url(url))
        p.bookmarks.push_back({bounded(b.str("title", url), 512), url});
    }
  Json history;
  load("owned-history.json", history);
  if (history.type == Json::Type::Array)
    for (auto &h : history.array) {
      if (p.history.size() >= 5000)
        break;
      auto url = h.str("url");
      double visited = h.num("visited", 0);
      if (h.type == Json::Type::Object && valid_browser_url(url) && std::isfinite(visited) &&
          visited >= 0)
        p.history.push_back({bounded(h.str("title", url), 512), url, visited});
    }
  Json session;
  load("owned-session.json", session);
  if (session.type == Json::Type::Array)
    for (auto &t : session.array) {
      if (p.session_tabs.size() >= 64)
        break;
      if (t.type == Json::Type::String && valid_browser_url(t.string))
        p.session_tabs.push_back(t.string);
    }
  Json state;
  load("owned-session-state.json", state);
  p.session_active = int(state.num("active", 0));
  if (!p.session_tabs.empty())
    p.session_active = std::clamp(p.session_active, 0, int(p.session_tabs.size()) - 1);
  else
    p.session_active = 0;
  return p;
}
bool Profile::save() {
  if (private_mode)
    return true;
  std::error_code ec;
  fs::create_directories(utf8_path(directory), ec);
  if (ec) {
    error = "cannot create profile directory";
    return false;
  }
  Json settings = Json::make_object();
  settings.set("schema", 1);
  settings.set("searchEngine", search_engine);
  settings.set("homePage", home_page);
  settings.set("restoreTabs", restore_tabs);
  settings.set("workspaces", workspaces);
  settings.set("customSearch", custom_search);
  auto write_list = [&](const char *key, const std::vector<std::string> &values) {
    Json list = Json::make_array();
    for (const auto &value : values) list.array.push_back(Json(value));
    settings.set(key, std::move(list));
  };
  settings.set("downloadDirectory", download_directory);
  settings.set("askSaveLocation", ask_save_location);
  settings.set("downloadNotify", download_notify);
  settings.set("blockAds", block_ads);
  settings.set("blockThirdPartyCookies", block_third_party_cookies);
  settings.set("sendDNT", send_dnt);
  settings.set("stripReferrer", strip_referrer);
  settings.set("clearOnExit", clear_on_exit);
  settings.set("webRtc", web_rtc_policy);
  write_list("blockedHosts", blocked_hosts);
  write_list("blockAllowSites", block_allow_sites);
  settings.set("proxy", proxy.to_json());
  settings.set("theme", theme);
  settings.set("accent", accent);
  settings.set("highContrast", high_contrast);
  settings.set("reduceMotion", reduce_motion);
  settings.set("showTabCloseButtons", show_tab_close_buttons);
  settings.set("hideSidebar", hide_sidebar);
  settings.set("cpuLimit", cpu_limit);
  settings.set("sleepTabsMin", sleep_tabs_minutes);
  settings.set("batterySaver", battery_saver);
  settings.set("spellCheck", spell_check);
  write_list("spellUserWords", user_words);
  write_list("extensionDirectories", extension_directories);
  write_list("disabledExtensions", disabled_extensions);
  Json bookmarks_json = Json::make_array();
  for (auto &b : bookmarks) {
    Json j = Json::make_object();
    j.set("title", b.title);
    j.set("url", b.url);
    bookmarks_json.array.push_back(std::move(j));
  }
  Json history_json = Json::make_array();
  for (auto &h : history) {
    Json j = Json::make_object();
    j.set("title", h.title);
    j.set("url", h.url);
    j.set("visited", h.visited);
    history_json.array.push_back(std::move(j));
  }
  bool ok = write_file_atomic(directory + "/owned-settings.json", settings.dump() + "\n", error);
  ok =
      write_file_atomic(directory + "/owned-bookmarks.json", bookmarks_json.dump() + "\n", error) &&
      ok;
  ok =
      write_file_atomic(directory + "/owned-history.json", history_json.dump() + "\n", error) && ok;
  ok = write_file_atomic(directory + "/owned-cookies.json", cookies.dump() + "\n", error) && ok;
  ok = write_file_atomic(directory + "/owned-storage.json", local_storage.dump() + "\n", error) &&
       ok;
  ok = save_session() && ok;
  return ok;
}
bool Profile::save_session() {
  if (private_mode)
    return true;
  std::error_code ec;
  fs::create_directories(utf8_path(directory), ec);
  if (ec) {
    error = "cannot create profile directory";
    return false;
  }
  Json session = Json::make_array();
  for (size_t i = 0; i < session_tabs.size() && i < 64; ++i)
    session.array.push_back(Json(session_tabs[i]));
  Json state = Json::make_object();
  state.set("schema", 1);
  state.set("active",
            session_tabs.empty() ? 0 : std::clamp(session_active, 0, int(session_tabs.size()) - 1));
  bool ok = write_file_atomic(directory + "/owned-session.json", session.dump() + "\n", error);
  ok = write_file_atomic(directory + "/owned-session-state.json", state.dump() + "\n", error) && ok;
  return ok;
}
void Profile::visit(const std::string &title, const std::string &url) {
  if (private_mode || !valid_browser_url(url) || !starts(url, "http"))
    return;
  if (!history.empty() && history.front().url == url) {
    history.front().title = bounded(title.empty() ? url : title, 512);
    history.front().visited = now_seconds();
    return;
  }
  history.insert(history.begin(), {bounded(title.empty() ? url : title, 512), url, now_seconds()});
  if (history.size() > 5000)
    history.resize(5000);
}
void Profile::bookmark(const std::string &title, const std::string &url) {
  if (!valid_browser_url(url))
    return;
  for (auto &b : bookmarks)
    if (b.url == url) {
      b.title = bounded(title.empty() ? url : title, 512);
      return;
    }
  if (bookmarks.size() >= 10000)
    return;
  bookmarks.push_back({bounded(title.empty() ? url : title, 512), url});
}
bool Profile::remove_bookmark(const std::string &url) {
  auto it = std::remove_if(bookmarks.begin(), bookmarks.end(),
                           [&](const Bookmark &b) { return b.url == url; });
  bool removed = it != bookmarks.end();
  bookmarks.erase(it, bookmarks.end());
  return removed;
}
void Profile::clear_history() {
  history.clear();
}
std::string Profile::search_template() const {
  if (search_engine == "custom" && !custom_search.empty())
    return custom_search;
  if (search_engine == "brave")
    return "https://search.brave.com/search?q=%s";
  if (search_engine == "startpage")
    return "https://www.startpage.com/sp/search?query=%s";
  if (search_engine == "wikipedia")
    return "https://en.wikipedia.org/w/index.php?search=%s";
  if (search_engine == "google")
    return "https://www.google.com/search?q=%s";
  if (search_engine == "bing")
    return "https://www.bing.com/search?q=%s";
  return "https://duckduckgo.com/?q=%s";
}
bool Profile::set_search_engine(const std::string &engine) {
  static const std::set<std::string> allowed = {"duckduckgo", "google",    "bing",  "brave",
                                                "startpage",  "wikipedia", "custom"};
  if (!allowed.count(engine))
    return false;
  if (engine == "custom" && custom_search.empty())
    return false;
  search_engine = engine;
  return true;
}
bool Profile::set_home_page(const std::string &url) {
  if (!valid_browser_url(url))
    return false;
  home_page = url;
  return true;
}
bool Profile::set_custom_search(const std::string &template_url) {
  if (template_url.empty()) {
    custom_search.clear();
    return true;
  }
  auto url = parse_url(template_url);
  if (!url || !url->network() || template_url.find("%s") == std::string::npos ||
      template_url.size() > 8192)
    return false;
  custom_search = template_url;
  return true;
}
bool Profile::set_download_directory(const std::string &directory) {
  if (directory.empty()) return false;
  std::error_code ec;
  auto path = std::filesystem::absolute(std::filesystem::path(directory), ec);
  if (ec) return false;
  std::filesystem::create_directories(path, ec);
  if (ec) return false;
  download_directory = path.string();
  return true;
}
void Profile::add_user_word(const std::string &word) {
  auto trimmed = trim(word);
  if (trimmed.empty() || trimmed.size() > 64 || user_words.size() >= 4096) return;
  for (const auto &existing : user_words)
    if (lower(existing) == lower(trimmed)) return;
  user_words.push_back(trimmed);
}
bool Profile::remove_user_word(const std::string &word) {
  auto it = std::remove_if(user_words.begin(), user_words.end(), [&](const std::string &entry) {
    return lower(entry) == lower(trim(word));
  });
  bool removed = it != user_words.end();
  user_words.erase(it, user_words.end());
  return removed;
}
} // namespace pv
