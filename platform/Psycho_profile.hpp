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
  // The new-tab speed dial. Empty in a fresh profile means the factory row,
  // not "dial disabled" - defaults are filled in on load.
  std::vector<Bookmark> speed_dial;
  std::vector<std::string> session_tabs;
  // Pin and mute ride along with the session as one '1'/'0' flag per tab.
  std::string session_pinned, session_muted;
  int session_active = 0;
  // Workspaces persist as one small JSON document; the shell owns the shape.
  Json load_workspaces() const;
  bool save_workspaces(const Json &state);
  // The panic button, the dismissed-update tag and my diagnostics files.
  bool panic_enabled = false;
  std::string panic_url, dismissed_update;
  // running.marker witnesses a clean exit; surviving a crash means it is still
  // there on the next launch.
  bool crashed_last_run() const;
  void mark_running();
  void clear_running_marker();
  void append_error_log(const std::string &line);
  std::vector<std::string> read_error_log(size_t max_lines = 20) const;
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
  bool history_dirty = false; // visited pages pending flush to disk
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
