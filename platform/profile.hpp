#pragma once
#include "../engine/base.hpp"
namespace pv {
struct Bookmark {
  std::string title, url;
};
struct HistoryEntry {
  std::string title, url;
  double visited = 0;
};
struct Profile {
  std::string directory;
  bool private_mode = false, restore_tabs = true, workspaces = true;
  std::string search_engine = "duckduckgo", home_page = "psychovault://newtab",
              custom_search;
  std::vector<Bookmark> bookmarks;
  std::vector<HistoryEntry> history;
  std::vector<std::string> session_tabs;
  std::string error;
  static Profile open(const std::string &directory, bool private_mode);
  bool save();
  void visit(const std::string &title, const std::string &url);
  void bookmark(const std::string &title, const std::string &url);
  std::string search_template() const;
};
std::string default_profile_directory();
} // namespace pv
