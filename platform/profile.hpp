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
class Profile {
public:
  std::string directory;
  bool private_mode = false, restore_tabs = true, workspaces = true;
  std::string search_engine = "duckduckgo", home_page = "psychovault://newtab", custom_search;
  std::vector<Bookmark> bookmarks;
  std::vector<HistoryEntry> history;
  std::vector<std::string> session_tabs;
  int session_active = 0;
  std::string error;
  static Profile open(const std::string &directory, bool private_mode);
  bool save();
  bool save_session();
  void visit(const std::string &title, const std::string &url);
  void bookmark(const std::string &title, const std::string &url);
  bool remove_bookmark(const std::string &url);
  void clear_history();
  std::string search_template() const;
  bool set_search_engine(const std::string &engine);
  bool set_home_page(const std::string &url);
  bool set_custom_search(const std::string &template_url);
};
std::string default_profile_directory();
} // namespace pv
