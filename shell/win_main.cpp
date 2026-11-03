// PsychoVault original Windows shell. Website code runs in pv-renderer, not
// here.
#ifndef _WIN32
#error "The native shell targets Windows. Use pv-engine for headless engine tests."
#endif
#define NOMINMAX
#include "../engine/page.hpp"
#include "../platform/network.hpp"
#include "../platform/process.hpp"
#include "../platform/profile.hpp"
#include <atomic>
#include <chrono>
#include <dwmapi.h>
#include <fstream>
#include <iostream>
#include <shellapi.h>
#include <thread>
#include <windows.h>
#include <windowsx.h>

namespace pv {
static constexpr UINT WM_FRAME = WM_APP + 1, WM_NAVIGATE = WM_APP + 2, WM_DIALOG = WM_APP + 3,
                      WM_DONE = WM_APP + 4;
static constexpr uint32_t BG = 0xff05080f, RAIL = 0xff080d16, CHROME = 0xff070b12,
                          PANEL = 0xff0d1420, BORDER = 0xff1c2736, TEXT = 0xffe6edf3,
                          MUTED = 0xff8b98a9, ACCENT = 0xff38bdf8;
struct Update {
  int tab;
  uint64_t generation;
  Json frame;
  std::string error, url, title;
};
struct Tab {
  int id;
  std::string url, title = "New Tab";
  std::vector<std::string> history;
  size_t index = 0;
  bool loading = false;
  double scroll = 0;
  Json frame;
  std::string error;
};
struct Hit {
  Rect rect;
  std::string action;
  int index = 0;
  std::string value;
};
struct Session {
  Process process;
  std::atomic<bool> stop{false};
  std::thread worker;
  HWND hwnd;
  int tab;
  uint64_t generation;
  std::string url;
  double width, height;
  ~Session() {
    stop = true;
    process.terminate();
    if (worker.joinable())
      worker.detach();
  }
  void send(Json j) {
    if (stop)
      return;
    std::string error;
    process.send(j, error);
  }
};
struct Browser {
  HWND hwnd = nullptr;
  Profile profile;
  std::vector<Tab> tabs;
  int active = 0, next_id = 1;
  uint64_t generation = 0;
  std::shared_ptr<Session> session;
  std::string address, home_search, status = "Ready", profile_error;
  bool address_focus = false, home_focus = false, settings = false, panel_bookmarks = false,
       panel_history = false;
  size_t address_caret = 0;
  int settings_category = 0;
  std::vector<Hit> hits;
  Rect content{44, 84, 1000, 600};
  int width = 1440, height = 940;
  bool private_mode = false;
  std::string capture_path;
  bool smoke = false;
  DWORD start_tick = 0;
  Browser() {
    profile = Profile::open(default_profile_directory(), false);
    profile_error = profile.error;
    new_tab("psychovault://newtab", false);
  }
  Tab &current() {
    return tabs[size_t(active)];
  }
  void new_tab(std::string url = "psychovault://newtab", bool focus = true) {
    Tab t;
    t.id = next_id++;
    t.url = url;
    t.title = url == "psychovault://newtab" ? "New Tab" : url;
    t.history.push_back(url);
    tabs.push_back(t);
    active = int(tabs.size()) - 1;
    if (focus)
      navigate(url, false);
  }
  void close_tab(int index) {
    if (tabs.size() <= 1) {
      new_tab();
      return;
    }
    tabs.erase(tabs.begin() + index);
    active = std::clamp(active, 0, int(tabs.size()) - 1);
    navigate(current().url, false);
  }
  void select_tab(int index) {
    if (index < 0 || index >= int(tabs.size()))
      return;
    active = index;
    navigate(current().url, false);
  }
  void sync_address() {
    address = current().url == "psychovault://newtab" ? "" : current().url;
    address_caret = address.size();
    address_focus = false;
    home_focus = false;
  }
  void navigate(const std::string &input, bool add_history = true) {
    if (session) {
      session->stop = true;
      session->process.terminate();
      session.reset();
    }
    std::string target = input;
    auto u = navigate_input(input, profile.search_template());
    if (u)
      target = u->href();
    Tab &t = current();
    if (add_history) {
      if (t.history.empty() || t.history[t.index] != target) {
        t.history.resize(t.index + 1);
        t.history.push_back(target);
        t.index = t.history.size() - 1;
      }
    }
    t.url = target;
    t.loading = true;
    t.error.clear();
    t.frame = Json();
    t.scroll = 0;
    settings = starts(target, "psychovault://settings");
    sync_address();
    if (starts(target, "psychovault://") || target == "about:blank") {
      t.loading = false;
      t.title = settings ? "Settings" : "New Tab";
      status = "Ready";
      save_session();
      InvalidateRect(hwnd, nullptr, FALSE);
      return;
    }
    if (!u || !u->network()) {
      t.loading = false;
      t.error = "Unsupported URL";
      InvalidateRect(hwnd, nullptr, FALSE);
      return;
    }
    profile.visit(t.title, target);
    profile.save();
    auto s = std::make_shared<Session>();
    s->hwnd = hwnd;
    s->tab = t.id;
    s->generation = ++generation;
    s->url = target;
    s->width = content.w;
    s->height = content.h;
    session = s;
    s->worker = std::thread([s] {
      std::string renderer = executable_dir() + "\\pv-renderer.exe";
      std::string error;
      if (!s->process.start(renderer, "", true, error)) {
        {
          auto *up = new Update{
              s->tab, s->generation, Json(), "Renderer sandbox failed: " + error, {}, {}};
          PostMessageW(s->hwnd, WM_FRAME, 0, reinterpret_cast<LPARAM>(up));
        }
        return;
      }
      Resource main = fetch_url(*parse_url(s->url));
      if (s->stop)
        return;
      Json load = Json::make_object();
      load.set("tag", "load");
      load.set("url", s->url);
      load.set("width", s->width);
      load.set("height", s->height);
      load.set("mime", main.mime);
      if (main.ok)
        load.set("html", main.text);
      else
        load.set("html", "<!doctype html><html><head><title>Navigation "
                         "failed</"
                         "title><style>body{background:#05080f;color:#e6edf3;"
                         "font-family:Segoe "
                         "UI;padding:40px}h1{color:#38bdf8}</style></"
                         "head><body><h1>Navigation failed</h1><p>" +
                             html_escape(main.error) + "</p></body></html>");
      if (!s->process.send(load, error)) {
        {
          auto *up = new Update{s->tab, s->generation, Json(), error, {}, {}};
          PostMessageW(s->hwnd, WM_FRAME, 0, reinterpret_cast<LPARAM>(up));
        }
        return;
      }
      while (!s->stop) {
        auto msg = s->process.receive(25);
        if (s->stop)
          break;
        if (!msg) {
          if (!s->process.alive())
            break;
          continue;
        }
        auto tag = msg->str("tag");
        if (tag == "frame") {
          auto *up = new Update{s->tab, s->generation, *msg, {}, {}, {}};
          PostMessageW(s->hwnd, WM_FRAME, 0, reinterpret_cast<LPARAM>(up));
        } else if (tag == "fetch") {
          Resource r = fetch_url(*parse_url(msg->str("url")));
          if (s->stop)
            break;
          Json j = Json::make_object();
          j.set("tag", "resource");
          j.set("url", r.url);
          j.set("ok", r.ok);
          j.set("status", r.status);
          j.set("mime", r.mime);
          j.set("text", r.text);
          j.set("error", r.error);
          s->send(j);
        } else if (tag == "navigate") {
          auto *up = new Update{s->tab, s->generation, Json(), {}, msg->str("url"), {}};
          PostMessageW(s->hwnd, WM_NAVIGATE, 0, reinterpret_cast<LPARAM>(up));
        } else if (tag == "dialog") {
          auto *up = new Update{s->tab, s->generation, Json(), {}, msg->str("text"), {}};
          PostMessageW(s->hwnd, WM_DIALOG, 0, reinterpret_cast<LPARAM>(up));
        } else if (tag == "fatal") {
          {
            auto *up = new Update{s->tab, s->generation, Json(), msg->str("message"), {}, {}};
            PostMessageW(s->hwnd, WM_FRAME, 0, reinterpret_cast<LPARAM>(up));
          }
          break;
        }
      }
      PostMessageW(s->hwnd, WM_DONE, s->tab, s->generation);
    });
    status = "Loading " + target;
    save_session();
    InvalidateRect(hwnd, nullptr, FALSE);
  }
  void post_error(std::shared_ptr<Session> s, const std::string &error) {
    auto *up = new Update{s->tab, s->generation, Json(), error, {}, {}};
    PostMessageW(hwnd, WM_FRAME, 0, reinterpret_cast<LPARAM>(up));
  }
  void save_session() {
    if (private_mode)
      return;
    profile.session_tabs.clear();
    for (auto &t : tabs)
      profile.session_tabs.push_back(t.url);
    profile.save();
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
    if (session) {
      Json j = Json::make_object();
      j.set("tag", "resize");
      j.set("width", content.w);
      j.set("height", content.h);
      session->send(j);
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
    const char *nav_icons[] = {"<", ">", "R", "H"};
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
      c.text(content.x + 40, y, "Nothing saved yet.", 13, MUTED);
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
    paint_commands(c, commands, 0, {});
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
        else if (h.action == "nav-2")
          navigate(current().url, false);
        else if (h.action == "nav-3" || h.action == "home")
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
    if (content.contains(x, y) && session) {
      Json j = Json::make_object();
      j.set("tag", "click");
      j.set("x", x - content.x);
      j.set("y", y - content.y);
      session->send(j);
    }
  }
  void key(UINT msg, WPARAM wp, LPARAM) {
    bool ctrl = GetKeyState(VK_CONTROL) < 0;
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
        if (caret > 0) {
          text.erase(caret - 1, 1);
          --caret;
        }
      } else if (wp == VK_DELETE) {
        if (caret < text.size())
          text.erase(caret, 1);
      } else if (wp == VK_LEFT) {
        if (caret > 0)
          --caret;
      } else if (wp == VK_RIGHT) {
        if (caret < text.size())
          ++caret;
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
    if (session && (wp == VK_RETURN || wp == VK_BACK)) {
      Json j = Json::make_object();
      j.set("tag", "key");
      j.set("key", wp == VK_RETURN ? "Enter" : "Backspace");
      session->send(j);
    }
  }
  size_t home_search_caret = 0;
  void character(wchar_t ch) {
    if (ch < 32)
      return;
    std::string s;
    for (auto c : utf8_decode(std::string(1, char(ch))))
      s += utf8_encode(c);
    if (address_focus) {
      address.insert(address_caret, s);
      address_caret += s.size();
    } else if (home_focus) {
      home_search.insert(home_search_caret, s);
      home_search_caret += s.size();
    } else if (session) {
      Json j = Json::make_object();
      j.set("tag", "key");
      j.set("key", "");
      j.set("text", s);
      session->send(j);
    }
    InvalidateRect(hwnd, nullptr, FALSE);
  }
  void scroll(double delta) {
    Tab &t = current();
    double max = std::max(0.0, t.frame.num("height", content.h) - content.h);
    t.scroll = std::clamp(t.scroll + delta, 0.0, max);
    if (session) {
      Json j = Json::make_object();
      j.set("tag", "scroll");
      j.set("y", t.scroll);
      session->send(j);
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
  case WM_CHAR: {
    if (auto b = browser(hwnd)) {
      wchar_t ch = wchar_t(wp);
      if (ch >= 32) {
        char buf[8];
        int n = WideCharToMultiByte(CP_UTF8, 0, &ch, 1, buf, sizeof(buf), nullptr, nullptr);
        std::string s(buf, n);
        if (b->address_focus) {
          b->address.insert(b->address_caret, s);
          b->address_caret += s.size();
        } else if (b->home_focus) {
          b->home_search.insert(b->home_search_caret, s);
          b->home_search_caret += s.size();
        } else if (b->session) {
          Json j = Json::make_object();
          j.set("tag", "key");
          j.set("key", "");
          j.set("text", s);
          b->session->send(j);
        }
        InvalidateRect(hwnd, nullptr, FALSE);
      }
    }
    return 0;
  }
  case WM_FRAME: {
    std::unique_ptr<Update> up(reinterpret_cast<Update *>(lp));
    auto b = browser(hwnd);
    if (b && up->tab == b->current().id && up->generation == b->generation) {
      if (!up->error.empty()) {
        b->current().error = up->error;
        b->status = up->error;
      } else {
        b->current().frame = up->frame;
        b->current().title = up->frame.str("title", b->current().title);
        b->current().loading = false;
        b->status = b->current().url;
        b->profile.visit(b->current().title, b->current().url);
        b->profile.save();
      }
      InvalidateRect(hwnd, nullptr, FALSE);
    }
    return 0;
  }
  case WM_NAVIGATE: {
    std::unique_ptr<Update> up(reinterpret_cast<Update *>(lp));
    auto b = browser(hwnd);
    if (b && up->tab == b->current().id && up->generation == b->generation)
      b->navigate(up->url);
    return 0;
  }
  case WM_DIALOG: {
    std::unique_ptr<Update> up(reinterpret_cast<Update *>(lp));
    MessageBoxW(hwnd, std::wstring(up->url.begin(), up->url.end()).c_str(), L"PsychoVault page",
                MB_OK);
    return 0;
  }
  case WM_DONE: {
    auto b = browser(hwnd);
    if (b && wp == b->current().id && lp == b->generation) {
      b->current().loading = false;
      InvalidateRect(hwnd, nullptr, FALSE);
    }
    return 0;
  }
  case WM_TIMER: {
    auto b = browser(hwnd);
    if (b && b->smoke && GetTickCount() - b->start_tick > 4000) {
      if (!b->capture_path.empty())
        capture(hwnd, b->capture_path);
      PostMessageW(hwnd, WM_CLOSE, 0, 0);
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
int WINAPI wWinMain(HINSTANCE inst, HINSTANCE, LPWSTR cmd, int show) {
  using namespace pv;
  std::string capture, url = "psychovault://newtab", profile_dir;
  bool smoke = false, private_mode = false;
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
  WNDCLASSEXW wc{};
  wc.cbSize = sizeof(wc);
  wc.lpfnWndProc = wndproc;
  wc.hInstance = inst;
  wc.hCursor = LoadCursor(nullptr, IDC_ARROW);
  wc.hIcon = LoadIcon(nullptr, IDI_APPLICATION);
  wc.lpszClassName = L"PsychoVaultOwnedWindow";
  wc.hbrBackground = nullptr;
  RegisterClassExW(&wc);
  Browser b;
  b.private_mode = private_mode;
  if (!profile_dir.empty())
    b.profile = Profile::open(profile_dir, private_mode);
  b.smoke = smoke;
  b.capture_path = capture;
  b.start_tick = GetTickCount();
  if (private_mode) {
    b.tabs.clear();
    b.new_tab(url, false);
  } else if (b.profile.restore_tabs && !b.profile.session_tabs.empty()) {
    b.tabs.clear();
    for (auto &u : b.profile.session_tabs)
      b.new_tab(u, false);
    b.active = 0;
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
  b.navigate(url, false);
  MSG msg;
  while (GetMessageW(&msg, nullptr, 0, 0) > 0) {
    TranslateMessage(&msg);
    DispatchMessageW(&msg);
  }
  b.session.reset();
  b.save_session();
  return int(msg.wParam);
}
