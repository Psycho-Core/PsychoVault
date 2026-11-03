#include "profile.hpp"
#include <chrono>
#ifdef _WIN32
#define NOMINMAX
#include <shlobj.h>
#include <windows.h>
#else
#include <cstdlib>
#include <sys/stat.h>
#endif
namespace pv {
#ifdef _WIN32
static std::wstring wide(const std::string &s) {
  int n = MultiByteToWideChar(CP_UTF8, 0, s.c_str(), int(s.size()), nullptr, 0);
  std::wstring w(n, 0);
  MultiByteToWideChar(CP_UTF8, 0, s.c_str(), int(s.size()), w.data(), n);
  return w;
}
static std::string narrow(const std::wstring &w) {
  int n = WideCharToMultiByte(CP_UTF8, 0, w.c_str(), int(w.size()), nullptr, 0, nullptr, nullptr);
  std::string s(n, 0);
  WideCharToMultiByte(CP_UTF8, 0, w.c_str(), int(w.size()), s.data(), n, nullptr, nullptr);
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
static bool make_directory(const std::string &path) {
#ifdef _WIN32
  std::wstring w = wide(path);
  size_t pos = 0;
  while ((pos = w.find_first_of(L"\\/", pos + 1)) != std::wstring::npos) {
    std::wstring part = w.substr(0, pos);
    CreateDirectoryW(part.c_str(), nullptr);
  }
  return CreateDirectoryW(w.c_str(), nullptr) || GetLastError() == ERROR_ALREADY_EXISTS;
#else
  return mkdir(path.c_str(), 0700) == 0 || errno == EEXIST;
#endif
}
static double now_seconds() {
  return double(std::chrono::duration_cast<std::chrono::milliseconds>(
                    std::chrono::system_clock::now().time_since_epoch())
                    .count()) /
         1000.0;
}
Profile Profile::open(const std::string &directory, bool private_mode) {
  Profile p;
  p.directory = directory;
  p.private_mode = private_mode;
  if (private_mode)
    return p;
  if (!make_directory(directory)) {
    p.error = "cannot create profile directory";
    return p;
  }
  auto load = [&](const char *name) -> Json {
    try {
      auto text = read_text_file(directory + "/" + name);
      auto j = Json::parse(text);
      return j ? *j : Json();
    } catch (...) {
      return Json();
    }
  };
  auto settings = load("owned-settings.json");
  p.search_engine = settings.str("searchEngine", p.search_engine);
  p.home_page = settings.str("homePage", p.home_page);
  p.restore_tabs = settings.flag("restoreTabs", true);
  p.workspaces = settings.flag("workspaces", true);
  p.custom_search = settings.str("customSearch");
  auto bookmarks = load("owned-bookmarks.json");
  if (bookmarks.type == Json::Type::Array)
    for (auto &b : bookmarks.array)
      p.bookmarks.push_back({b.str("title"), b.str("url")});
  auto history = load("owned-history.json");
  if (history.type == Json::Type::Array)
    for (auto &h : history.array)
      p.history.push_back({h.str("title"), h.str("url"), h.num("visited")});
  auto session = load("owned-session.json");
  if (session.type == Json::Type::Array)
    for (auto &t : session.array)
      if (t.type == Json::Type::String)
        p.session_tabs.push_back(t.string);
  return p;
}
bool Profile::save() {
  if (private_mode)
    return true;
  if (!make_directory(directory))
    return false;
  Json settings = Json::make_object();
  settings.set("schema", 1);
  settings.set("searchEngine", search_engine);
  settings.set("homePage", home_page);
  settings.set("restoreTabs", restore_tabs);
  settings.set("workspaces", workspaces);
  settings.set("customSearch", custom_search);
  Json bookmarks_json = Json::make_array();
  for (auto &b : this->bookmarks) {
    Json j = Json::make_object();
    j.set("title", b.title);
    j.set("url", b.url);
    bookmarks_json.array.push_back(j);
  }
  Json history_json = Json::make_array();
  for (auto &h : this->history) {
    Json j = Json::make_object();
    j.set("title", h.title);
    j.set("url", h.url);
    j.set("visited", h.visited);
    history_json.array.push_back(j);
  }
  Json session = Json::make_array();
  for (auto &t : session_tabs)
    session.array.push_back(Json(t));
  return write_file_atomic(directory + "/owned-settings.json", settings.dump() + "\n", error) &&
         write_file_atomic(directory + "/owned-bookmarks.json", bookmarks_json.dump() + "\n",
                           error) &&
         write_file_atomic(directory + "/owned-history.json", history_json.dump() + "\n", error) &&
         write_file_atomic(directory + "/owned-session.json", session.dump() + "\n", error);
}
void Profile::visit(const std::string &title, const std::string &url) {
  if (private_mode || !starts(url, "http"))
    return;
  history.insert(history.begin(), {title, url, now_seconds()});
  if (history.size() > 5000)
    history.resize(5000);
}
void Profile::bookmark(const std::string &title, const std::string &url) {
  for (auto &b : bookmarks)
    if (b.url == url)
      return;
  bookmarks.push_back({title, url});
  if (bookmarks.size() > 10000)
    bookmarks.pop_back();
}
std::string Profile::search_template() const {
  if (search_engine == "brave")
    return "https://search.brave.com/search?q=%s";
  if (search_engine == "startpage")
    return "https://www.startpage.com/sp/search?query=%s";
  if (search_engine == "wikipedia")
    return "https://en.wikipedia.org/w/index.php?search=%s";
  return "https://duckduckgo.com/?q=%s";
}
} // namespace pv
