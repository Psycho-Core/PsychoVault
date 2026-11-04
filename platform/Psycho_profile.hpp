#pragma once
#include "../engine/Psycho_base.hpp"
#include "Psycho_proxy.hpp"
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
  // Downloads
  std::string download_directory;
  bool ask_save_location = false, download_notify = true;
  // Privacy and security
  bool block_ads = true, block_third_party_cookies = true, send_dnt = true;
  bool strip_referrer = false, clear_on_exit = false;
  // My HTTPS policy: upgrade everything, and the hosts that told me to keep using
  // HTTPS, persisted so a restart cannot be talked back down to plain HTTP.
  bool https_only = false;
  std::string hsts;
  // A filter list file on disk, if the user points me at one. I never download a
  // list on my own.
  std::string filter_list_path;
  std::string web_rtc_policy = "default";
  std::vector<std::string> blocked_hosts, block_allow_sites;
  // A VPN extension owns this; I persist it so a restart does not silently
  // drop the user back to a direct connection.
  ProxyConfig proxy;
  // Appearance and accessibility
  std::string theme = "dark", accent = "cyan";
  bool high_contrast = false, reduce_motion = false, show_tab_close_buttons = true;
  bool hide_sidebar = false;
  // Performance
  int cpu_limit = 0, sleep_tabs_minutes = 0, memory_limit_mb = 0;
  bool battery_saver = false;
  // Spelling
  bool spell_check = true;
  std::vector<std::string> user_words;
  // Extensions the user installed, so I can reload them on the next launch.
  std::vector<std::string> extension_directories;
  // Site state I keep with the profile so a restart is not a sign-out.
  Json cookies, local_storage;
  std::vector<std::string> disabled_extensions;
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
  bool set_download_directory(const std::string &directory);
  void add_user_word(const std::string &word);
  bool remove_user_word(const std::string &word);
};
std::string default_profile_directory();
} // namespace pv
