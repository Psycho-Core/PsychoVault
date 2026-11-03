// PsychoVault original Windows shell. Website code runs in pv-renderer, not
// here.
#ifndef _WIN32
#error "The native shell targets Windows. Use pv-engine for headless engine tests."
#endif
#define NOMINMAX
#include "../engine/Psycho_page.hpp"
#include "../platform/Psycho_network.hpp"
#include "../platform/Psycho_process.hpp"
#include "../platform/Psycho_profile.hpp"
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <deque>
#include <dwmapi.h>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <shellapi.h>
#include <thread>
#include <windows.h>
#include <windowsx.h>

namespace pv {
static constexpr UINT WM_FRAME = WM_APP + 1, WM_NAVIGATE = WM_APP + 2, WM_DIALOG = WM_APP + 3,
                      WM_DONE = WM_APP + 4, WM_IMAGE = WM_APP + 5;
static constexpr uint32_t BG = 0xff05080f, RAIL = 0xff080d16, CHROME = 0xff070b12,
                          PANEL = 0xff0d1420, BORDER = 0xff1c2736, TEXT = 0xffe6edf3,
                          MUTED = 0xff8b98a9, ACCENT = 0xff38bdf8;
struct Update {
  int tab;
  uint64_t generation;
  Json frame;
  std::string error, url, title;
};
struct Session;
struct Tab {
  int id;
  std::string url, title = "New Tab";
  std::vector<std::string> history;
  size_t index = 0;
  bool loading = false, started = false, recorded = false;
  double scroll = 0;
  uint64_t generation = 0;
  Json frame;
  std::string error;
  std::shared_ptr<Session> session;
  std::map<std::string, Image> images;
  size_t image_bytes = 0;
};
struct Hit {
  Rect rect;
  std::string action;
  int index = 0;
  std::string value;
};
struct Session {
  Process process;
  std::thread worker;
  HWND hwnd = nullptr;
  int tab = 0;
  uint64_t generation = 0;
  std::string url;
  double width = 1000, height = 700;
  std::mutex mutex;
  std::condition_variable changed;
  std::deque<Json> outgoing;
  ~Session() {
    process.request_stop();
    changed.notify_all();
    // The worker captures a shared_ptr to this Session, so its Process handles
    // stay alive until run() returns. When the last reference is released on
    // the worker itself, detaching avoids a self-join; other owners may join.
    if (worker.joinable()) {
      if (worker.get_id() == std::this_thread::get_id())
        worker.detach();
      else
        worker.join();
    }
  }
  void send(Json j) {
    std::lock_guard lock(mutex);
    if (process.stopping())
      return;
    auto tag = j.str("tag");
    if (tag == "resize" || tag == "scroll") {
      for (auto &queued : outgoing)
        if (queued.str("tag") == tag) {
          queued = std::move(j);
          return;
        }
    }
    if (outgoing.size() >= 1024)
      return;
    outgoing.push_back(std::move(j));
    changed.notify_all();
  }
  std::optional<Json> take() {
    std::lock_guard lock(mutex);
    if (outgoing.empty())
      return std::nullopt;
    Json j = std::move(outgoing.front());
    outgoing.pop_front();
    return j;
  }
  void run();
};
static void post_update(HWND hwnd, UINT message, Update *update) {
  if (!hwnd || !PostMessageW(hwnd, message, WPARAM(update->tab), LPARAM(update)))
    delete update;
}
void Session::run() {
  auto fail = [&](const std::string &error) {
    post_update(hwnd, WM_FRAME, new Update{tab, generation, Json(), error, {}, {}});
    post_update(hwnd, WM_DONE, new Update{tab, generation, Json(), {}, {}, {}});
  };
  std::string error;
  if (!process.start(executable_dir() + "\\pv-renderer.exe", "", true, error)) {
    fail("Renderer sandbox failed: " + error);
    return;
  }
  auto target = parse_url(url);
  if (!target) {
    fail("Invalid URL");
    return;
  }
  Resource main = fetch_url(*target);
  if (process.stopping())
    return;
  if (!main.ok) {
    fail(main.error.empty() ? "Navigation failed" : main.error);
    return;
  }
  Json load = Json::make_object();
  load.set("tag", "load");
  load.set("url", main.url);
  load.set("width", width);
  load.set("height", height);
  load.set("mime", main.mime);
  load.set("html", main.text);
  if (!process.send(load, error)) {
    fail(error);
    return;
  }
  while (!process.stopping()) {
    while (auto command = take()) {
      if (process.stopping())
        return;
      if (!process.send(*command, error)) {
        fail(error);
        return;
      }
    }
    auto message = process.receive(10);
    if (process.stopping())
      break;
    if (!message) {
      if (!process.alive()) {
        auto code = process.exit_code();
        fail("Renderer exited" + (code ? " (code " + std::to_string(*code) + ")" : "") +
             (process.io_error().empty() ? "" : ": " + process.io_error()));
        return;
      }
      continue;
    }
    auto tag = message->str("tag");
    if (tag == "frame" || tag == "image") {
      post_update(hwnd, tag == "frame" ? WM_FRAME : WM_IMAGE,
                  new Update{tab, generation, std::move(*message), {}, {}, {}});
    } else if (tag == "fetch") {
      auto resource_url = parse_url(message->str("url"));
      Resource r;
      if (resource_url)
        r = fetch_url(*resource_url);
      else {
        r.url = message->str("url");
        r.ok = false;
        r.error = "Invalid resource URL";
      }
      if (process.stopping())
        return;
      Json j = Json::make_object();
      j.set("tag", "resource");
      j.set("url", r.url);
      j.set("ok", r.ok);
      j.set("status", r.status);
      j.set("mime", r.mime);
      j.set("error", r.error);
      if (!r.bytes.empty())
        j.set("base64", base64_encode(r.bytes));
      else
        j.set("text", r.text);
      if (!process.send(j, error)) {
        fail(error);
        return;
      }
    } else if (tag == "navigate" || tag == "dialog") {
      post_update(
          hwnd, tag == "navigate" ? WM_NAVIGATE : WM_DIALOG,
          new Update{
              tab, generation, Json(), {}, message->str(tag == "navigate" ? "url" : "text"), {}});
    } else if (tag == "fatal") {
      fail(message->str("message"));
      return;
    }
  }
  post_update(hwnd, WM_DONE, new Update{tab, generation, Json(), {}, {}, {}});
}
struct Browser {
  HWND hwnd = nullptr;
  Profile profile;
  std::vector<Tab> tabs;
  int active = 0, next_id = 1;
  uint64_t generation = 0;
  std::string address, home_search, status = "Ready", profile_error;
  bool address_focus = false, home_focus = false, settings = false, panel_bookmarks = false,
       panel_history = false;
  size_t address_caret = 0, home_search_caret = 0;
  wchar_t high_surrogate = 0;
  int settings_category = 0;
  std::vector<Hit> hits;
  Rect content{44, 84, 1000, 600};
  int width = 1440, height = 940;
  bool private_mode = false;
  std::string capture_path;
  bool smoke = false, expect_image = false;
  DWORD start_tick = 0;
  explicit Browser(bool private_mode = false, const std::string &directory = {})
      : private_mode(private_mode) {
    profile =
        Profile::open(directory.empty() ? default_profile_directory() : directory, private_mode);
    profile_error = profile.error;
    new_tab("psychovault://newtab", false);
  }
  ~Browser() {
    for (auto &tab : tabs)
      if (tab.session)
        tab.session->process.request_stop();
  }
  Tab &current() {
    return tabs[size_t(active)];
  }
  Tab *find_tab(int id) {
    for (auto &tab : tabs)
      if (tab.id == id)
        return &tab;
    return nullptr;
  }
  void stop_session(Tab &tab) {
    if (tab.session) {
      tab.session->process.request_stop();
      tab.session.reset();
    }
  }
  void new_tab(std::string url = "psychovault://newtab", bool focus = true) {
    Tab t;
    t.id = next_id++;
    t.url = url;
    t.title = url == "psychovault://newtab" ? "New Tab" : url;
    t.history.push_back(url);
    t.started = !starts(url, "http://") && !starts(url, "https://");
    tabs.push_back(std::move(t));
    active = int(tabs.size()) - 1;
    if (focus)
      navigate(url, false);
    else {
      sync_address();
      settings = starts(url, "psychovault://settings");
    }
  }
  void close_tab(int index) {
    if (index < 0 || index >= int(tabs.size()))
      return;
    if (tabs.size() <= 1) {
      stop_session(current());
      tabs.clear();
      new_tab();
      save_session();
      return;
    }
    stop_session(tabs[size_t(index)]);
    tabs.erase(tabs.begin() + index);
    if (index < active)
      --active;
    active = std::clamp(active, 0, int(tabs.size()) - 1);
    select_tab(active);
    save_session();
  }
  void select_tab(int index) {
    if (index < 0 || index >= int(tabs.size()))
      return;
    active = index;
    Tab &tab = current();
    sync_address();
    settings = starts(tab.url, "psychovault://settings");
    status = !tab.error.empty() ? tab.error : tab.loading ? "Loading " + tab.url : tab.url;
    if (!tab.started && (starts(tab.url, "http://") || starts(tab.url, "https://"))) {
      navigate(tab.url, false);
    } else if (tab.session) {
      Json j = Json::make_object();
      j.set("tag", "resize");
      j.set("width", content.w);
      j.set("height", content.h);
      tab.session->send(j);
    }
    save_session();
    InvalidateRect(hwnd, nullptr, FALSE);
  }
  void sync_address() {
    address = current().url == "psychovault://newtab" ? "" : current().url;
    address_caret = address.size();
    address_focus = false;
    home_focus = false;
    high_surrogate = 0;
  }
  void navigate(const std::string &input, bool add_history = true) {
    navigate_in(active, input, add_history);
  }
  void navigate_in(int index, const std::string &input, bool add_history = true) {
    if (index < 0 || index >= int(tabs.size()))
      return;
    Tab &tab = tabs[size_t(index)];
    stop_session(tab);
    std::string target = input;
    auto u = navigate_input(input, profile.search_template());
    if (u)
      target = u->href();
    if (add_history) {
      if (tab.history.empty() || tab.history[tab.index] != target) {
        tab.history.resize(tab.index + 1);
        tab.history.push_back(target);
        tab.index = tab.history.size() - 1;
        if (tab.history.size() > 512) {
          tab.history.erase(tab.history.begin(), tab.history.begin() + (tab.history.size() - 512));
          tab.index = tab.history.size() - 1;
        }
      }
    }
    tab.url = target;
    tab.loading = true;
    tab.started = true;
    tab.recorded = false;
    tab.error.clear();
    tab.frame = Json();
    tab.images.clear();
    tab.image_bytes = 0;
    tab.scroll = 0;
    tab.generation = ++generation;
    if (index == active) {
      settings = starts(target, "psychovault://settings");
      sync_address();
    }
    if (starts(target, "psychovault://") || target == "about:blank") {
      tab.loading = false;
      tab.recorded = true;
      tab.title = starts(target, "psychovault://settings") ? "Settings" : "New Tab";
      if (index == active)
        status = "Ready";
      save_session();
      InvalidateRect(hwnd, nullptr, FALSE);
      return;
    }
    if (!u || !u->network()) {
      tab.loading = false;
      tab.error = "Unsupported URL";
      if (index == active)
        status = tab.error;
      InvalidateRect(hwnd, nullptr, FALSE);
      return;
    }
    auto session = std::make_shared<Session>();
    session->hwnd = hwnd;
    session->tab = tab.id;
    session->generation = tab.generation;
    session->url = target;
    session->width = content.w;
    session->height = content.h;
    tab.session = session;
    session->worker = std::thread([session] { session->run(); });
    if (index == active)
      status = "Loading " + target;
    save_session();
    InvalidateRect(hwnd, nullptr, FALSE);
  }
  void stop() {
    Tab &tab = current();
    stop_session(tab);
    tab.loading = false;
    status = "Stopped";
    InvalidateRect(hwnd, nullptr, FALSE);
  }
  void save_session() {
    if (private_mode)
      return;
    profile.session_tabs.clear();
    for (auto &tab : tabs)
      profile.session_tabs.push_back(tab.url);
    profile.session_active = active;
    if (!profile.save_session() && profile_error.empty())
      profile_error = profile.error;
  }
  void back() {
    Tab &t = current();
    if (t.index > 0) {
      --t.index;
      navigate(t.history[t.index], false);
    }
  }
  void forward() {
    Tab &t = current();
    if (t.index + 1 < t.history.size()) {
      ++t.index;
      navigate(t.history[t.index], false);
    }
  }
  void resize(int w, int h) {
    width = w;
    height = h;
    content = {44, 84, double(w - 44), double(h - 106)};
    if (current().session) {
      Json j = Json::make_object();
      j.set("tag", "resize");
      j.set("width", content.w);
      j.set("height", content.h);
      current().session->send(j);
    }
    InvalidateRect(hwnd, nullptr, FALSE);
  }
  void draw(Canvas &c) {
    hits.clear();
    c.fill({0, 0, double(width), double(height)}, BG);
    c.fill({0, 0, 44, double(height)}, RAIL);
    c.line(43, 0, 43, height, BORDER);
    c.fill({44, 0, double(width - 44), 40}, CHROME);
    c.fill({44, 40, double(width - 44), 44}, CHROME);
    c.line(44, 83, width, 83, BORDER);
    double x = 52;
    for (size_t i = 0; i < tabs.size() && x < width - 180; ++i) {
      bool sel = int(i) == active;
      Rect r{x, 6, 180, 32};
      c.fill(r, sel ? PANEL : CHROME);
      if (sel) {
        c.line(r.x, r.y, r.x + r.w, r.y, ACCENT, 2);
      }
      c.text(r.x + 12, r.y + 8, tabs[i].title.substr(0, 22), 13, sel ? TEXT : MUTED, sel);
      hits.push_back({r, "tab", int(i), {}});
      Rect close{r.x + r.w - 24, r.y + 6, 18, 18};
      c.text(close.x + 4, close.y + 1, "x", 12, MUTED);
      hits.push_back({close, "close-tab", int(i), {}});
      x += 184;
    }
    c.text(x + 4, 12, "+", 18, MUTED);
    hits.push_back({{x, 4, 28, 32}, "new-tab", 0, {}});
    c.text(width - 120, 12, "-", 16, MUTED);
    hits.push_back({{double(width - 132), 0, 36, 36}, "minimize", 0, {}});
    c.text(width - 80, 12, "[]", 13, MUTED);
    hits.push_back({{double(width - 92), 0, 36, 36}, "maximize", 0, {}});
    c.text(width - 40, 12, "x", 15, MUTED);
    hits.push_back({{double(width - 52), 0, 40, 36}, "close-window", 0, {}});
    const char *nav_icons[] = {"<", ">", current().loading ? "X" : "R", "H"};
    for (int i = 0; i < 4; ++i) {
      Rect r{double(56 + i * 32), 48, 26, 26};
      c.text(r.x + 6, r.y + 4, nav_icons[i], 14, MUTED);
      hits.push_back({r, std::string("nav-") + std::to_string(i), 0, {}});
    }
    Rect addr{196, 47, double(width - 260), 30};
    c.fill(addr, 0xff050a12);
    c.stroke(addr, address_focus ? ACCENT : BORDER);
    std::string shown = address.empty() ? "Search the web or enter an address" : address;
    c.text(addr.x + 12, addr.y + 6, shown.substr(0, 120), 13, address.empty() ? MUTED : TEXT);
    hits.push_back({addr, "address", 0, {}});
    if (address_focus) {
      double cx = addr.x + 12 + c.text_width(address.substr(0, address_caret), 13);
      c.line(cx, addr.y + 5, cx, addr.y + 24, ACCENT);
    }
    c.text(width - 52, 52, "*", 18, MUTED);
    hits.push_back({{double(width - 60), 44, 32, 36}, "bookmark", 0, {}});
    // Rail: only controls with real behaviour are shown.
    c.text(12, 16, "P", 16, ACCENT, true);
    hits.push_back({{4, 8, 36, 36}, "home", 0, {}});
    c.text(12, 72, "B", 14, MUTED);
    hits.push_back({{4, 64, 36, 36}, "bookmarks", 0, {}});
    c.text(12, 112, "H", 14, MUTED);
    hits.push_back({{4, 104, 36, 36}, "history", 0, {}});
    c.text(12, height - 70, "S", 14, MUTED);
    hits.push_back({{4, double(height - 78), 36, 36}, "settings", 0, {}});
    c.fill(content, BG);
    if (panel_bookmarks || panel_history)
      draw_panel(c);
    else if (settings)
      draw_settings(c);
    else if (current().url == "psychovault://newtab" || current().url == "about:blank")
      draw_home(c);
    else
      draw_content(c);
    c.fill({44, double(height - 22), double(width - 44), 22}, CHROME);
    c.text(52, height - 19, status.substr(0, 180), 11, MUTED);
  }
  void draw_home(Canvas &c) {
    double cx = content.x + content.w / 2, cy = content.y + content.h * 0.36;
    c.line(cx - 16, cy - 40, cx, cy - 50, ACCENT, 2);
    c.line(cx, cy - 50, cx + 16, cy - 40, ACCENT, 2);
    c.line(cx - 16, cy - 40, cx - 12, cy - 16, ACCENT, 2);
    c.line(cx + 16, cy - 40, cx + 12, cy - 16, ACCENT, 2);
    c.line(cx - 12, cy - 16, cx, cy - 8, ACCENT, 2);
    c.line(cx + 12, cy - 16, cx, cy - 8, ACCENT, 2);
    c.text(cx - 90, cy, "PsychoVault", 30, TEXT, true);
    Rect search{cx - 280, cy + 60, 560, 44};
    c.fill(search, 0xff0a111c);
    c.stroke(search, home_focus ? ACCENT : BORDER);
    c.text(search.x + 16, search.y + 12,
           home_search.empty() ? "Search DuckDuckGo or enter an address" : home_search, 14,
           home_search.empty() ? MUTED : TEXT);
    hits.push_back({search, "home-search", 0, {}});
    const char *labels[] = {"GitHub", "YouTube", "Reddit", "Wikipedia", "Mail", "News"};
    const char *urls[] = {"https://github.com/",     "https://www.youtube.com/",
                          "https://www.reddit.com/", "https://www.wikipedia.org/",
                          "https://mail.proton.me/", "https://news.ycombinator.com/"};
    for (int i = 0; i < 6; ++i) {
      Rect tile{cx - 324 + i * 108, cy + 130, 96, 84};
      c.fill(tile, 0xff0d1420);
      c.stroke(tile, BORDER);
      c.text(tile.x + 38, tile.y + 16, std::string(1, labels[i][0]), 18, ACCENT, true);
      c.text(tile.x + 12, tile.y + 52, labels[i], 12, MUTED);
      hits.push_back({tile, "tile", 0, urls[i]});
    }
    c.text(cx - 170, content.y + content.h - 50, "Development engine - not a finished browser", 12,
           MUTED);
  }
  void draw_settings(Canvas &c) {
    c.text(content.x + 80, content.y + 36, "Settings", 28, TEXT, true);
    c.text(content.x + 80, content.y + 76, "Built by Psycho-Core", 13, MUTED);
    const char *cats[] = {"General",    "Privacy & security", "Downloads",
                          "Extensions", "Performance",        "Appearance",
                          "Spelling",   "Passwords",          "Accessibility & language",
                          "About"};
    Rect nav{content.x + 80, content.y + 110, 220, 420};
    c.fill(nav, PANEL);
    c.stroke(nav, BORDER);
    for (int i = 0; i < 10; ++i) {
      Rect r{nav.x + 10, nav.y + 12 + i * 38, 200, 32};
      if (i == settings_category)
        c.stroke(r, ACCENT);
      c.text(r.x + 12, r.y + 7, cats[i], 13, i == settings_category ? TEXT : MUTED);
      hits.push_back({r, "settings-category", i, {}});
    }
    double x = nav.x + nav.w + 32, y = nav.y;
    c.text(x, y, cats[settings_category], 22, TEXT, true);
    y += 44;
    if (settings_category == 0) {
      c.text(x, y, "Search engine", 14, TEXT);
      Rect r{x + 300, y - 4, 220, 30};
      c.fill(r, 0xff050a12);
      c.stroke(r, BORDER);
      c.text(r.x + 10, r.y + 6, profile.search_engine, 13, TEXT);
      hits.push_back({r, "setting-search", 0, {}});
      y += 52;
      c.text(x, y, "Restore tabs on launch", 14, TEXT);
      Rect toggle{x + 300, y - 4, 48, 24};
      c.fill(toggle, profile.restore_tabs ? ACCENT : BORDER);
      c.text(toggle.x + 8, toggle.y + 3, profile.restore_tabs ? "ON" : "OFF", 11,
             profile.restore_tabs ? BG : TEXT, true);
      hits.push_back({toggle, "setting-restore", 0, {}});
      y += 52;
      c.text(x, y, "Home page", 14, TEXT);
      c.text(x + 300, y, profile.home_page, 13, MUTED);
      y += 52;
      c.text(x, y,
             "These controls change real browser behaviour and save to my "
             "profile.",
             12, MUTED);
    } else {
      c.text(x, y,
             "This category is not implemented yet. I will not show pretend "
             "controls.",
             13, MUTED);
    }
    c.text(x, y + 60, "Profile: " + profile.directory, 11, MUTED);
    if (!profile_error.empty())
      c.text(x, y + 82, profile_error, 11, 0xfff87171);
  }
  void draw_panel(Canvas &c) {
    bool bm = panel_bookmarks;
    c.text(content.x + 40, content.y + 30, bm ? "Bookmarks" : "History", 24, TEXT, true);
    size_t y = size_t(content.y) + 80;
    if (bm) {
      for (size_t i = 0; i < profile.bookmarks.size() && i < 30; ++i) {
        auto &b = profile.bookmarks[i];
        Rect r{content.x + 40, double(y), content.w - 80, 36};
        c.text(r.x, r.y + 8, b.title + "  " + b.url, 13, TEXT);
        hits.push_back({r, "open-bookmark", int(i), b.url});
        y += 40;
      }
    } else {
      for (size_t i = 0; i < profile.history.size() && i < 30; ++i) {
        auto &h = profile.history[i];
        Rect r{content.x + 40, double(y), content.w - 80, 36};
        c.text(r.x, r.y + 8, h.title + "  " + h.url, 13, TEXT);
        hits.push_back({r, "open-history", int(i), h.url});
        y += 40;
      }
    }
    if (y == size_t(content.y) + 80)
      c.text(content.x + 40, double(y), "Nothing saved yet.", 13, MUTED);
  }
  void draw_content(Canvas &c) {
    Tab &t = current();
    if (t.loading && t.frame.type == Json::Type::Null) {
      c.text(content.x + 32, content.y + 32, "Loading...", 16, MUTED);
      return;
    }
    if (!t.error.empty()) {
      c.text(content.x + 32, content.y + 32, "Navigation failed", 22, 0xfff87171, true);
      c.text(content.x + 32, content.y + 72, t.error, 13, MUTED);
      return;
    }
    if (t.frame.type != Json::Type::Object)
      return;
    std::vector<PaintCommand> commands;
    for (auto &j : t.frame.at("paint").array) {
      PaintCommand p;
      p.kind = PaintCommand::Kind(int(j.num("kind")));
      p.rect = {j.num("x") + content.x, j.num("y") + content.y - t.scroll, j.num("w"), j.num("h")};
      p.color = uint32_t(j.num("color"));
      p.text = j.str("text");
      p.size = j.num("size");
      p.thickness = j.num("thickness");
      p.bold = j.flag("bold");
      p.node = int(j.num("node"));
      p.url = j.str("url");
      commands.push_back(p);
    }
    Rect old = c.clip;
    c.clip = content;
    paint_commands(c, commands, 0, t.images);
    c.clip = old;
  }
  void click(int x, int y) {
    for (auto it = hits.rbegin(); it != hits.rend(); ++it)
      if (it->rect.contains(x, y)) {
        auto h = *it;
        address_focus = h.action == "address";
        home_focus = h.action == "home-search";
        if (h.action == "tab")
          select_tab(h.index);
        else if (h.action == "close-tab")
          close_tab(h.index);
        else if (h.action == "new-tab")
          new_tab();
        else if (h.action == "nav-0")
          back();
        else if (h.action == "nav-1")
          forward();
        else if (h.action == "nav-2") {
          if (current().loading)
            stop();
          else
            navigate(current().url, false);
        } else if (h.action == "nav-3" || h.action == "home")
          navigate(profile.home_page);
        else if (h.action == "address") {
          address_caret = address.size();
        } else if (h.action == "bookmark") {
          profile.bookmark(current().title, current().url);
          profile.save();
          status = "Bookmark saved";
        } else if (h.action == "bookmarks") {
          panel_bookmarks = !panel_bookmarks;
          panel_history = false;
        } else if (h.action == "history") {
          panel_history = !panel_history;
          panel_bookmarks = false;
        } else if (h.action == "settings")
          navigate("psychovault://settings");
        else if (h.action == "tile")
          navigate(h.value);
        else if (h.action == "open-bookmark" || h.action == "open-history")
          navigate(h.value);
        else if (h.action == "settings-category")
          settings_category = h.index;
        else if (h.action == "setting-search") {
          std::vector<std::string> engines = {"duckduckgo", "brave", "startpage", "wikipedia"};
          auto it2 = std::find(engines.begin(), engines.end(), profile.search_engine);
          profile.search_engine = engines[(it2 - engines.begin() + 1) % engines.size()];
          profile.save();
        } else if (h.action == "setting-restore") {
          profile.restore_tabs = !profile.restore_tabs;
          profile.save();
        } else if (h.action == "minimize")
          ShowWindow(hwnd, SW_MINIMIZE);
        else if (h.action == "maximize")
          ShowWindow(hwnd, IsZoomed(hwnd) ? SW_RESTORE : SW_MAXIMIZE);
        else if (h.action == "close-window")
          PostMessageW(hwnd, WM_CLOSE, 0, 0);
        InvalidateRect(hwnd, nullptr, FALSE);
        return;
      }
    if (content.contains(x, y) && current().session) {
      Json j = Json::make_object();
      j.set("tag", "click");
      j.set("x", x - content.x);
      j.set("y", y - content.y);
      current().session->send(j);
    }
  }
  void key(UINT, WPARAM wp, LPARAM) {
    bool ctrl = GetKeyState(VK_CONTROL) < 0;
    if (ctrl && wp == 'V') {
      paste();
      return;
    }
    if (ctrl && wp == 'L') {
      address_focus = true;
      address_caret = address.size();
      InvalidateRect(hwnd, nullptr, FALSE);
      return;
    }
    if (ctrl && wp == 'T') {
      new_tab();
      return;
    }
    if (ctrl && wp == 'W') {
      close_tab(active);
      return;
    }
    if (ctrl && wp == 'R') {
      navigate(current().url, false);
      return;
    }
    if (ctrl && wp == 'D') {
      profile.bookmark(current().title, current().url);
      profile.save();
      status = "Bookmark saved";
      InvalidateRect(hwnd, nullptr, FALSE);
      return;
    }
    if (wp == VK_F5) {
      navigate(current().url, false);
      return;
    }
    if (wp == VK_ESCAPE) {
      if (current().loading)
        stop();
      address_focus = home_focus = false;
      InvalidateRect(hwnd, nullptr, FALSE);
      return;
    }
    if (address_focus || home_focus) {
      std::string &text = address_focus ? address : home_search;
      size_t &caret = address_focus ? address_caret : home_search_caret;
      if (wp == VK_RETURN) {
        std::string target = text;
        address_focus = home_focus = false;
        if (!target.empty())
          navigate(target);
        return;
      }
      if (wp == VK_BACK) {
        size_t previous = previous_cp(text, caret);
        text.erase(previous, caret - previous);
        caret = previous;
      } else if (wp == VK_DELETE) {
        text.erase(caret, next_cp(text, caret) - caret);
      } else if (wp == VK_LEFT) {
        caret = previous_cp(text, caret);
      } else if (wp == VK_RIGHT) {
        caret = next_cp(text, caret);
      } else if (wp == VK_HOME)
        caret = 0;
      else if (wp == VK_END)
        caret = text.size();
      InvalidateRect(hwnd, nullptr, FALSE);
      return;
    }
    if (wp == VK_PRIOR || wp == VK_NEXT || wp == VK_UP || wp == VK_DOWN) {
      double delta = wp == VK_PRIOR  ? -content.h * 0.8
                     : wp == VK_NEXT ? content.h * 0.8
                     : wp == VK_UP   ? -60
                                     : 60;
      scroll(delta);
      return;
    }
    if (current().session && (wp == VK_RETURN || wp == VK_BACK || wp == VK_TAB)) {
      Json j = Json::make_object();
      j.set("tag", "key");
      j.set("key", wp == VK_RETURN ? "Enter" : wp == VK_BACK ? "Backspace" : "Tab");
      current().session->send(j);
    }
  }
  static size_t previous_cp(const std::string &text, size_t caret) {
    if (!caret)
      return 0;
    --caret;
    while (caret && (uint8_t(text[caret]) & 0xc0) == 0x80)
      --caret;
    return caret;
  }
  static size_t next_cp(const std::string &text, size_t caret) {
    if (caret >= text.size())
      return text.size();
    ++caret;
    while (caret < text.size() && (uint8_t(text[caret]) & 0xc0) == 0x80)
      ++caret;
    return caret;
  }
  void insert_text(const std::string &text) {
    if (text.empty())
      return;
    if (address_focus) {
      if (address.size() + text.size() > 8192)
        return;
      address.insert(address_caret, text);
      address_caret += text.size();
    } else if (home_focus) {
      if (home_search.size() + text.size() > 8192)
        return;
      home_search.insert(home_search_caret, text);
      home_search_caret += text.size();
    } else if (current().session) {
      Json j = Json::make_object();
      j.set("tag", "key");
      j.set("key", "");
      j.set("text", text);
      current().session->send(j);
    }
    InvalidateRect(hwnd, nullptr, FALSE);
  }
  void character(wchar_t ch) {
    if (ch >= 0xd800 && ch <= 0xdbff) {
      high_surrogate = ch;
      return;
    }
    uint32_t cp = ch;
    if (high_surrogate) {
      if (ch >= 0xdc00 && ch <= 0xdfff)
        cp = 0x10000 + ((uint32_t(high_surrogate) - 0xd800) << 10) + (uint32_t(ch) - 0xdc00);
      high_surrogate = 0;
    }
    if (cp < 32 || (cp >= 0xd800 && cp <= 0xdfff))
      return;
    insert_text(utf8_encode(cp));
  }
  void paste() {
    if (!OpenClipboard(hwnd))
      return;
    HANDLE data = GetClipboardData(CF_UNICODETEXT);
    if (data) {
      auto text = static_cast<const wchar_t *>(GlobalLock(data));
      if (text) {
        int n = WideCharToMultiByte(CP_UTF8, 0, text, -1, nullptr, 0, nullptr, nullptr);
        if (n > 0 && n <= 8192) {
          std::string utf8(size_t(n - 1), '\0');
          WideCharToMultiByte(CP_UTF8, 0, text, -1, utf8.data(), n, nullptr, nullptr);
          insert_text(utf8);
        }
        GlobalUnlock(data);
      }
    }
    CloseClipboard();
  }
  void scroll(double delta) {
    Tab &t = current();
    double max = std::max(0.0, t.frame.num("height", content.h) - content.h);
    t.scroll = std::clamp(t.scroll + delta, 0.0, max);
    if (t.session) {
      Json j = Json::make_object();
      j.set("tag", "scroll");
      j.set("y", t.scroll);
      t.session->send(j);
    }
    InvalidateRect(hwnd, nullptr, FALSE);
  }
};
static Browser *browser(HWND hwnd) {
  return reinterpret_cast<Browser *>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));
}
static void paint(HWND hwnd) {
  Browser *b = browser(hwnd);
  if (!b)
    return;
  PAINTSTRUCT ps;
  HDC dc = BeginPaint(hwnd, &ps);
  Canvas canvas(b->width, b->height, BG);
  b->draw(canvas);
  BITMAPINFO bi{};
  bi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
  bi.bmiHeader.biWidth = canvas.width;
  bi.bmiHeader.biHeight = -canvas.height;
  bi.bmiHeader.biPlanes = 1;
  bi.bmiHeader.biBitCount = 32;
  bi.bmiHeader.biCompression = BI_RGB;
  SetDIBitsToDevice(dc, 0, 0, canvas.width, canvas.height, 0, 0, 0, canvas.height,
                    canvas.pixels.data(), &bi, DIB_RGB_COLORS);
  EndPaint(hwnd, &ps);
}
static void capture(HWND hwnd, const std::string &path) {
  Browser *b = browser(hwnd);
  Canvas canvas(b->width, b->height, BG);
  b->draw(canvas);
  auto png = encode_png(canvas.width, canvas.height, canvas.pixels);
  std::ofstream f(path, std::ios::binary);
  f.write(reinterpret_cast<const char *>(png.data()), std::streamsize(png.size()));
}
static LRESULT CALLBACK wndproc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
  switch (msg) {
  case WM_NCCREATE: {
    auto cs = reinterpret_cast<CREATESTRUCTW *>(lp);
    SetWindowLongPtrW(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(cs->lpCreateParams));
    return TRUE;
  }
  case WM_CREATE: {
    auto b = browser(hwnd);
    b->hwnd = hwnd;
    BOOL dark = TRUE;
    DwmSetWindowAttribute(hwnd, 20, &dark, sizeof(dark));
    SetTimer(hwnd, 1, 16, nullptr);
    return 0;
  }
  case WM_SIZE: {
    if (auto b = browser(hwnd))
      b->resize(LOWORD(lp), HIWORD(lp));
    return 0;
  }
  case WM_PAINT:
    paint(hwnd);
    return 0;
  case WM_LBUTTONDOWN: {
    if (auto b = browser(hwnd))
      b->click(GET_X_LPARAM(lp), GET_Y_LPARAM(lp));
    SetFocus(hwnd);
    return 0;
  }
  case WM_MOUSEWHEEL: {
    if (auto b = browser(hwnd))
      b->scroll(-double(GET_WHEEL_DELTA_WPARAM(wp)) / 120 * 60);
    return 0;
  }
  case WM_KEYDOWN:
    if (auto b = browser(hwnd))
      b->key(msg, wp, lp);
    return 0;
  case WM_CHAR:
    if (auto b = browser(hwnd))
      b->character(wchar_t(wp));
    return 0;
  case WM_UNICHAR:
    if (wp == UNICODE_NOCHAR)
      return TRUE;
    if (auto b = browser(hwnd))
      b->insert_text(utf8_encode(uint32_t(wp)));
    return 0;
  case WM_FRAME: {
    std::unique_ptr<Update> up(reinterpret_cast<Update *>(lp));
    auto b = browser(hwnd);
    Tab *tab = b ? b->find_tab(int(wp)) : nullptr;
    if (tab && up->generation == tab->generation) {
      if (!up->error.empty()) {
        tab->error = up->error;
        tab->loading = false;
        if (tab == &b->current())
          b->status = up->error;
      } else {
        tab->frame = std::move(up->frame);
        tab->title = tab->frame.str("title", tab->title);
        tab->loading = false;
        tab->scroll = tab->frame.num("scroll", tab->scroll);
        if (!tab->recorded) {
          tab->recorded = true;
          auto final_url = tab->frame.str("url", tab->url);
          if (!final_url.empty() && final_url != tab->url) {
            tab->url = final_url;
            if (tab->index < tab->history.size())
              tab->history[tab->index] = final_url;
          }
          b->profile.visit(tab->title, tab->url);
          if (!b->profile.save() && b->profile_error.empty())
            b->profile_error = b->profile.error;
          if (tab == &b->current() && !b->address_focus)
            b->sync_address();
        }
        if (tab == &b->current())
          b->status = tab->url;
      }
      if (tab == &b->current())
        InvalidateRect(hwnd, nullptr, FALSE);
    }
    return 0;
  }
  case WM_IMAGE: {
    std::unique_ptr<Update> up(reinterpret_cast<Update *>(lp));
    auto b = browser(hwnd);
    Tab *tab = b ? b->find_tab(int(wp)) : nullptr;
    if (tab && up->generation == tab->generation) {
      try {
        Image image;
        image.width = int(up->frame.num("width"));
        image.height = int(up->frame.num("height"));
        image.pixels = base64_decode(up->frame.str("base64"), kMaxIpcBytes);
        if (image.valid() && tab->image_bytes + image.pixels.size() <= 128u * 1024u * 1024u) {
          tab->image_bytes += image.pixels.size();
          tab->images[up->frame.str("url")] = std::move(image);
          if (tab == &b->current())
            InvalidateRect(hwnd, nullptr, FALSE);
        }
      } catch (const std::exception &e) {
        tab->error = std::string("Image transfer failed: ") + e.what();
        if (tab == &b->current())
          InvalidateRect(hwnd, nullptr, FALSE);
      }
    }
    return 0;
  }
  case WM_NAVIGATE: {
    std::unique_ptr<Update> up(reinterpret_cast<Update *>(lp));
    auto b = browser(hwnd);
    if (b) {
      for (size_t i = 0; i < b->tabs.size(); ++i)
        if (b->tabs[i].id == int(wp) && b->tabs[i].generation == up->generation) {
          b->navigate_in(int(i), up->url);
          break;
        }
    }
    return 0;
  }
  case WM_DIALOG: {
    std::unique_ptr<Update> up(reinterpret_cast<Update *>(lp));
    int n = MultiByteToWideChar(CP_UTF8, 0, up->url.c_str(), int(up->url.size()), nullptr, 0);
    std::wstring text(size_t(n), L'\0');
    MultiByteToWideChar(CP_UTF8, 0, up->url.c_str(), int(up->url.size()), text.data(), n);
    MessageBoxW(hwnd, text.c_str(), L"PsychoVault page", MB_OK);
    return 0;
  }
  case WM_DONE: {
    std::unique_ptr<Update> up(reinterpret_cast<Update *>(lp));
    auto b = browser(hwnd);
    Tab *tab = b ? b->find_tab(int(up->tab)) : nullptr;
    if (tab && up->generation == tab->generation) {
      tab->loading = false;
      if (tab == &b->current()) {
        b->status = !tab->error.empty() ? tab->error : tab->url;
        InvalidateRect(hwnd, nullptr, FALSE);
      }
    }
    return 0;
  }
  case WM_TIMER: {
    auto b = browser(hwnd);
    if (b && b->smoke && GetTickCount() - b->start_tick > 4000) {
      if (!b->capture_path.empty())
        capture(hwnd, b->capture_path);
      bool web = starts(b->current().url, "http://") || starts(b->current().url, "https://");
      bool failed =
          web && (b->current().frame.type != Json::Type::Object || !b->current().error.empty() ||
                  !b->current().session || !b->current().session->process.sandboxed() ||
                  (b->expect_image && b->current().images.empty()));
      PostQuitMessage(failed ? 1 : 0);
    }
    return 0;
  }
  case WM_NCHITTEST: {
    LRESULT hit = DefWindowProcW(hwnd, msg, wp, lp);
    if (hit == HTCLIENT) {
      POINT p{GET_X_LPARAM(lp), GET_Y_LPARAM(lp)};
      ScreenToClient(hwnd, &p);
      auto b = browser(hwnd);
      if (b && p.y < 40 && p.x > 44 && p.x < b->width - 160)
        return HTCAPTION;
    }
    return hit;
  }
  case WM_GETMINMAXINFO: {
    auto m = reinterpret_cast<MINMAXINFO *>(lp);
    m->ptMinTrackSize = {800, 500};
    return 0;
  }
  case WM_DESTROY:
    KillTimer(hwnd, 1);
    PostQuitMessage(0);
    return 0;
  }
  return DefWindowProcW(hwnd, msg, wp, lp);
}
} // namespace pv
static int run_sandbox_test(const std::string &report_path) {
  using namespace pv;
  std::filesystem::path sentinel;
  Json result = Json::make_object();
  bool ok = false;
  try {
    auto suffix = base64_encode(random_bytes(12));
    for (char &c : suffix)
      if (c == '+' || c == '/')
        c = 'x';
    sentinel = std::filesystem::temp_directory_path() /
               ("psychovault-sandbox-sentinel-" + suffix + ".tmp");
    std::string error;
    if (!write_file_atomic(sentinel.string(), "broker-only sandbox sentinel\n", error))
      throw std::runtime_error(error);
    Process renderer;
    bool started = renderer.start(executable_dir() + "\\pv-renderer.exe",
                                  "--sandbox-probe \"" + sentinel.string() + "\"", true, error);
    result.set("started", started);
    result.set("startError", error);
    if (started) {
      auto report = renderer.receive(5000);
      result.set("sandboxed", renderer.sandboxed());
      result.set("ioError", renderer.io_error());
      auto exit = renderer.exit_code();
      if (exit)
        result.set("exitCode", double(*exit));
      if (report) {
        for (auto &entry : report->object)
          result.set(entry.first, entry.second);
        ok = renderer.sandboxed() && report->flag("appContainer") && report->flag("fileDenied") &&
             report->flag("networkTested") && report->flag("networkDenied");
      }
      renderer.terminate();
    }
  } catch (const std::exception &e) {
    result.set("exception", e.what());
  }
  if (!sentinel.empty()) {
    std::error_code ignored;
    std::filesystem::remove(sentinel, ignored);
  }
  result.set("ok", ok);
  std::string error;
  if (!report_path.empty() && !write_file_atomic(report_path, result.dump() + "\n", error))
    return 1;
  return ok ? 0 : 1;
}
int WINAPI wWinMain(HINSTANCE inst, HINSTANCE, LPWSTR, int show) {
  using namespace pv;
  std::string capture, url = "psychovault://newtab", profile_dir;
  bool smoke = false, private_mode = false, expect_image = false, sandbox_test = false;
  std::string sandbox_report;
  std::vector<std::string> args;
  int argc = 0;
  LPWSTR *argv = CommandLineToArgvW(GetCommandLineW(), &argc);
  for (int i = 1; i < argc; ++i) {
    int n = WideCharToMultiByte(CP_UTF8, 0, argv[i], -1, nullptr, 0, nullptr, nullptr);
    std::string a(n ? n - 1 : 0, 0);
    if (n)
      WideCharToMultiByte(CP_UTF8, 0, argv[i], -1, a.data(), n, nullptr, nullptr);
    args.push_back(a);
  }
  if (argv)
    LocalFree(argv);
  for (size_t i = 0; i < args.size(); ++i) {
    if (args[i] == "--smoke")
      smoke = true;
    else if (args[i] == "--private")
      private_mode = true;
    else if (args[i] == "--capture" && i + 1 < args.size())
      capture = args[++i];
    else if (args[i] == "--url" && i + 1 < args.size())
      url = args[++i];
    else if (args[i] == "--profile" && i + 1 < args.size())
      profile_dir = args[++i];
  }
  if (sandbox_test)
    return run_sandbox_test(sandbox_report);
  WNDCLASSEXW wc{};
  wc.cbSize = sizeof(wc);
  wc.lpfnWndProc = wndproc;
  wc.hInstance = inst;
  wc.hCursor = LoadCursor(nullptr, IDC_ARROW);
  wc.hIcon = LoadIcon(nullptr, IDI_APPLICATION);
  wc.lpszClassName = L"PsychoVaultOwnedWindow";
  wc.hbrBackground = nullptr;
  RegisterClassExW(&wc);
  bool explicit_url = false;
  for (size_t i = 0; i < args.size(); ++i)
    if (args[i] == "--url")
      explicit_url = true;
  Browser b(private_mode, profile_dir);
  b.smoke = smoke;
  b.expect_image = expect_image;
  b.capture_path = capture;
  b.start_tick = GetTickCount();
  if (private_mode) {
    b.tabs.clear();
    b.new_tab("psychovault://newtab", false);
  } else if (b.profile.restore_tabs && !b.profile.session_tabs.empty()) {
    b.tabs.clear();
    for (auto &u : b.profile.session_tabs)
      b.new_tab(u, false);
    b.active = std::clamp(b.profile.session_active, 0, int(b.tabs.size()) - 1);
    b.sync_address();
    b.settings = starts(b.current().url, "psychovault://settings");
  }
  HWND hwnd =
      CreateWindowExW(0, wc.lpszClassName, L"PsychoVault",
                      WS_POPUP | WS_THICKFRAME | WS_MINIMIZEBOX | WS_MAXIMIZEBOX | WS_SYSMENU,
                      CW_USEDEFAULT, CW_USEDEFAULT, 1440, 940, nullptr, nullptr, inst, &b);
  if (!hwnd)
    return 1;
  MARGINS margins{0, 0, 1, 0};
  DwmExtendFrameIntoClientArea(hwnd, &margins);
  ShowWindow(hwnd, show);
  UpdateWindow(hwnd);
  b.resize(1440, 940);
  if (explicit_url || smoke)
    b.navigate(url, false);
  else if (!b.current().started)
    b.navigate(b.current().url, false);
  MSG msg;
  while (GetMessageW(&msg, nullptr, 0, 0) > 0) {
    TranslateMessage(&msg);
    DispatchMessageW(&msg);
  }
  b.save_session();
  return int(msg.wParam);
}
