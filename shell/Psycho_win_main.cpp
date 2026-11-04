// PsychoVault original Windows shell. I render my own pages inside this one
// executable: no second program, no sibling files, nothing to unpack.
#ifndef _WIN32
#error "The native shell targets Windows. Use pv-engine for headless engine tests."
#endif
#define NOMINMAX
#include "../engine/Psycho_page.hpp"
#include "../platform/Psycho_network.hpp"
#include "../platform/Psycho_host.hpp"
#include "../platform/Psycho_profile.hpp"
#include "../platform/Psycho_extension.hpp"
#include "../platform/Psycho_download.hpp"
#include <audioclient.h>
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <deque>
#include <dwmapi.h>
#include <cstring>
#include <filesystem>
#include <mmdeviceapi.h>
#include <fstream>
#include <iostream>
#include <shellapi.h>
#include <shlobj.h>
#include <thread>
#include <windows.h>
#include <windowsx.h>

namespace pv {
static constexpr UINT WM_FRAME = WM_APP + 1, WM_NAVIGATE = WM_APP + 2, WM_DIALOG = WM_APP + 3,
                      WM_DONE = WM_APP + 4, WM_IMAGE = WM_APP + 5, WM_URL = WM_APP + 6;
static constexpr uint32_t BG = 0xff05080f, RAIL = 0xff080d16, CHROME = 0xff070b12,
                          PANEL = 0xff0d1420, BORDER = 0xff1c2736, TEXT = 0xffe6edf3,
                          MUTED = 0xff8b98a9, ACCENT = 0xff38bdf8;
struct Update {
  int tab;
  uint64_t generation;
  Json frame;
  std::string error, url, title;
  // I hand page images over directly instead of encoding them, because my page
  // now lives in this same process.
  std::shared_ptr<const Image> image;
};
struct Session;
struct Tab {
  int id;
  std::string url, title = "New Tab";
  std::vector<std::string> history;
  size_t index = 0;
  bool loading = false, started = false, recorded = false;
  double scroll = 0, zoom = 1.0;
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
static std::string pick_folder(HWND owner);
// My extensions sit beside my executable, so I resolve that folder myself.
static std::string executable_dir() {
  wchar_t buffer[MAX_PATH * 2];
  DWORD length = GetModuleFileNameW(nullptr, buffer, DWORD(std::size(buffer)));
  if (length == 0 || size_t(length) >= std::size(buffer))
    return std::string();
  int size = WideCharToMultiByte(CP_UTF8, 0, buffer, int(length), nullptr, 0, nullptr, nullptr);
  std::string path(size_t(size), 0);
  if (size > 0)
    WideCharToMultiByte(CP_UTF8, 0, buffer, int(length), path.data(), size, nullptr, nullptr);
  return std::filesystem::path(path).parent_path().string();
}
// I run one page per worker thread. My page host, my engine and my fetches all
// stay on that thread; the window thread only ever reads finished frames.
// My own audio output. WASAPI shared mode at 48 kHz stereo 16-bit, fed from my
// engine's mixer on the thread that runs the page, so no sample crosses threads
// and nothing needs a lock.
static const GUID kMmDeviceEnumeratorGuid = {0xbcde0395,
                                             0xe52f,
                                             0x467c,
                                             {0x8e, 0x3d, 0xc4, 0x57, 0x92, 0x91, 0x69, 0x2e}};
static const GUID kDeviceEnumeratorGuid = {0xa95664d2,
                                           0x9614,
                                           0x4f35,
                                           {0xa7, 0x46, 0xde, 0x8d, 0xb6, 0x36, 0x17, 0xe6}};
static const GUID kAudioClientGuid = {0x1cb9ad4c, 0xdbfa, 0x4c32, {0xb1, 0x78, 0xc2, 0xf5, 0x68, 0xa7, 0x03, 0xb2}};
static const GUID kRenderClientGuid = {0xf294acfc, 0x3146, 0x4483, {0xa7, 0xbf, 0xad, 0xdc, 0xa7, 0xc2, 0x60, 0xe2}};
struct AudioOut {
  IAudioClient *client = nullptr;
  IAudioRenderClient *render = nullptr;
  UINT32 frames = 0;
  int rate = 48000;
  bool started = false, tried = false, com = false;
  ~AudioOut() {
    close();
  }
  bool ready() const {
    return client != nullptr;
  }
  void close() {
    if (client)
      client->Stop();
    if (render) {
      render->Release();
      render = nullptr;
    }
    if (client) {
      client->Release();
      client = nullptr;
    }
    started = false;
    if (com) {
      CoUninitialize();
      com = false;
    }
  }
  // I open the device the first time a page plays something, and I keep it open
  // afterwards so a second play does not pay for it again.
  bool open(std::string &why) {
    if (client)
      return true;
    if (tried)
      return false;
    tried = true;
    HRESULT hr = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
    if (hr == S_OK)
      com = true;
    else if (hr != S_FALSE) {
      why = "COM did not start for audio";
      return false;
    }
    IMMDeviceEnumerator *devices = nullptr;
    hr = CoCreateInstance(kMmDeviceEnumeratorGuid, nullptr, CLSCTX_ALL, kDeviceEnumeratorGuid,
                          reinterpret_cast<void **>(&devices));
    if (FAILED(hr) || !devices) {
      why = "no audio device enumerator on this machine";
      return false;
    }
    IMMDevice *device = nullptr;
    hr = devices->GetDefaultAudioEndpoint(eRender, eConsole, &device);
    devices->Release();
    if (FAILED(hr) || !device) {
      why = "this machine has no default playback device";
      return false;
    }
    hr = device->Activate(kAudioClientGuid, CLSCTX_ALL, nullptr,
                          reinterpret_cast<void **>(&client));
    device->Release();
    if (FAILED(hr) || !client) {
      why = "the audio device did not give me a client";
      client = nullptr;
      return false;
    }
    WAVEFORMATEX format{};
    format.wFormatTag = WAVE_FORMAT_PCM;
    format.nChannels = 2;
    format.nSamplesPerSec = DWORD(rate);
    format.wBitsPerSample = 16;
    format.nBlockAlign = WORD(format.nChannels * format.wBitsPerSample / 8);
    format.nAvgBytesPerSec = format.nSamplesPerSec * format.nBlockAlign;
    // A shared-mode stream that converts my format for me, which is how a 48 kHz
    // mixer reaches a 44.1 kHz device without a second clock of mine.
    hr = client->Initialize(AUDCLNT_SHAREMODE_SHARED,
                            AUDCLNT_STREAMFLAGS_AUTOCONVERTPCM |
                                AUDCLNT_STREAMFLAGS_SRC_DEFAULT_QUALITY,
                            2000000, 0, &format, nullptr);
    if (FAILED(hr)) {
      why = "the audio device refused shared mode";
      close();
      return false;
    }
    hr = client->GetBufferSize(&frames);
    if (FAILED(hr) || frames == 0) {
      why = "the audio device reported no buffer";
      close();
      return false;
    }
    hr = client->GetService(kRenderClientGuid, reinterpret_cast<void **>(&render));
    if (FAILED(hr) || !render) {
      why = "the audio device gave me no render client";
      close();
      return false;
    }
    return true;
  }
  // I top the device up on every tick. Silence goes in when nothing plays, which
  // keeps the stream in step instead of looping whatever was there last.
  void pump(Host &host) {
    if (!client || !render)
      return;
    UINT32 padding = 0;
    if (FAILED(client->GetCurrentPadding(&padding)) || padding > frames)
      return;
    UINT32 available = frames - padding;
    if (available < 32)
      return;
    BYTE *data = nullptr;
    if (FAILED(render->GetBuffer(available, &data)) || !data)
      return;
    std::memset(data, 0, size_t(available) * 4);
    if (host.media_playing())
      host.pull_audio(reinterpret_cast<int16_t *>(data), size_t(available), rate);
    render->ReleaseBuffer(available, 0);
    if (!started && SUCCEEDED(client->Start()))
      started = true;
  }
};
struct Session {
  std::thread worker;
  HWND hwnd = nullptr;
  int tab = 0;
  uint64_t generation = 0;
  std::string url;
  std::string start_error;
  NetworkPolicy policy;
  CookieJar *jar = nullptr;
  Storage *storage = nullptr;
  double width = 1000, height = 700;
  std::mutex mutex;
  std::condition_variable changed;
  std::deque<Json> outgoing;
  std::atomic<bool> stopping{false};
  std::atomic<bool> ready{false};
  std::vector<std::string> diagnostics;
  AudioOut audio;
  ~Session() {
    request_stop();
    // The worker captures a shared_ptr to this Session, so the worker might be
    // the last owner. Detaching avoids a self-join; other owners may join.
    if (worker.joinable()) {
      if (worker.get_id() == std::this_thread::get_id())
        worker.detach();
      else
        worker.join();
    }
  }
  void request_stop() {
    stopping = true;
    changed.notify_all();
  }
  void send(Json j) {
    std::lock_guard lock(mutex);
    if (stopping)
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
  void note(const std::string &subsystem, const std::string &message) {
    std::lock_guard lock(mutex);
    if (diagnostics.size() >= 8)
      return;
    diagnostics.push_back(subsystem + ": " + message.substr(0, 512));
  }
  std::vector<std::string> recent_diagnostics() {
    std::lock_guard lock(mutex);
    return diagnostics;
  }
  void deliver(HostEvent event);
  void apply(Host &host, const Json &command);
  void run();
};
// I build my updates field by field so adding a field can never leave one
// uninitialised in a brace list.
static Update *make_update(int tab, uint64_t generation) {
  auto *update = new Update();
  update->tab = tab;
  update->generation = generation;
  return update;
}
static void post_update(HWND hwnd, UINT message, Update *update) {
  if (!hwnd || !PostMessageW(hwnd, message, WPARAM(update->tab), LPARAM(update)))
    delete update;
}
void Session::deliver(HostEvent event) {
  auto *update = make_update(tab, generation);
  if (event.tag == "frame") {
    update->frame = std::move(event.frame);
    post_update(hwnd, WM_FRAME, update);
  } else if (event.tag == "image") {
    update->url = std::move(event.url);
    update->image = std::move(event.image);
    post_update(hwnd, WM_IMAGE, update);
  } else if (event.tag == "navigate") {
    update->url = std::move(event.url);
    post_update(hwnd, WM_NAVIGATE, update);
  } else if (event.tag == "url") {
    // history.pushState or a form post changed the address without a load.
    update->url = std::move(event.url);
    post_update(hwnd, WM_URL, update);
  } else if (event.tag == "dialog") {
    update->url = std::move(event.text);
    post_update(hwnd, WM_DIALOG, update);
  } else {
    delete update;
    if (event.tag == "log")
      note(event.subsystem, event.text);
  }
}
void Session::apply(Host &host, const Json &command) {
  auto tag = command.str("tag");
  if (tag == "resize") {
    width = command.num("width", width);
    height = command.num("height", height);
    host.resize(width, height);
  } else if (tag == "scroll") {
    host.scroll(command.num("y"));
  } else if (tag == "click") {
    host.click(command.num("x"), command.num("y"));
  } else if (tag == "key") {
    host.key(command.str("key"), command.str("text"));
  }
}
void Session::run() {
  auto fail = [&](const std::string &error) {
    start_error = error;
    auto *update = make_update(tab, generation);
    update->error = error;
    post_update(hwnd, WM_FRAME, update);
    post_update(hwnd, WM_DONE, make_update(tab, generation));
  };
  if (!parse_url(url)) {
    fail("Invalid URL");
    return;
  }
  // My page host fetches through the same policy my downloads and tabs use, and
  // reports straight back to this thread.
  Host host(
      [this](const std::string &link, ResourceType type) {
        auto parsed = parse_url(link);
        if (!parsed) {
          Resource resource;
          resource.url = link;
          resource.ok = false;
          resource.error = "Invalid resource URL";
          return resource;
        }
        // The kind of resource decides which filter rules apply and whether this is
        // a mixed-content request, so it travels with the fetch.
        policy.type = type;
        return fetch_url(*parsed, policy);
      },
      [this](HostEvent event) { deliver(std::move(event)); });
  if (jar) {
    host.use_cookies(jar);
    policy.cookies = jar;
  }
  if (storage) host.use_local_storage(storage);
  // A form post goes out through the same network stack as everything else.
  host.set_poster([this](const std::string &link, const std::string &body,
                         const std::string &content_type) {
    Resource resource;
    auto parsed = parse_url(link);
    if (!parsed) {
      resource.url = link;
      resource.ok = false;
      resource.error = "Invalid form URL";
      return resource;
    }
    RequestOptions options;
    options.method = "POST";
    options.body = body;
    options.content_type = content_type;
    return fetch_url(*parsed, policy, options, 20000, 8u * 1024u * 1024u);
  });
  std::string error;
  if (!host.load(url, error)) {
    if (!stopping)
      fail(error);
    return;
  }
  ready = true;
  std::unique_lock lock(mutex);
  while (!stopping) {
    while (!outgoing.empty() && !stopping) {
      Json command = std::move(outgoing.front());
      outgoing.pop_front();
      lock.unlock();
      apply(host, command);
      lock.lock();
    }
    lock.unlock();
    bool worked = !stopping && host.pump();
    // The device is opened the first time a page actually plays something, and from
    // then on it is topped up on this same thread.
    if (host.media_playing() && !audio.ready()) {
      std::string why;
      if (!audio.open(why) && !why.empty())
        note("media", why);
    }
    audio.pump(host);
    lock.lock();
    if (worked)
      continue;
    changed.wait_for(lock, std::chrono::milliseconds(16),
                     [this] { return stopping.load() || !outgoing.empty(); });
  }
  post_update(hwnd, WM_DONE, make_update(tab, generation));
}
struct Browser {
  HWND hwnd = nullptr;
  Profile profile;
  std::vector<Tab> tabs;
  int active = 0, next_id = 1;
  uint64_t generation = 0;
  std::string address, home_search, status = "Ready", profile_error;
  bool address_focus = false, home_focus = false, settings = false, panel_bookmarks = false,
       panel_history = false, panel_downloads = false;
  ExtensionHost extensions;
  DownloadManager downloads;
  // Every tab shares these, so a session on one tab is a session on the next,
  // and both survive a restart with my settings.
  CookieJar cookies;
  Storage local_storage;
  // Find in page
  bool find_open = false;
  std::string find_text;
  std::vector<Rect> find_marks;
  int find_at = 0;
  std::string extension_error, download_error;
  int settings_scroll = 0;
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
    cookies.from_json(profile.cookies);
    local_storage.from_json(profile.local_storage);
    if (profile.download_directory.empty())
      profile.download_directory = default_download_directory();
    downloads.set_directory(profile.download_directory);
    if (!private_mode) downloads.load(profile.directory);
    install_extension_effects();
    if (!private_mode) {
      std::string storage_error;
      extensions.load_storage(profile.directory, storage_error);
      load_bundled_extensions();
      for (const auto &directory : profile.extension_directories) {
        std::string error;
        if (extensions.install_directory(directory, error)) {
          for (const auto &record : extensions.list()) {
            bool disabled = std::find(profile.disabled_extensions.begin(),
                                      profile.disabled_extensions.end(),
                                      record.manifest.id) != profile.disabled_extensions.end();
            if (disabled) extensions.set_enabled(record.manifest.id, false, error);
            else if (!extensions.start_worker(record.manifest.id, error))
              extension_error = error;
          }
        } else extension_error = error;
      }
    }
    new_tab("psychovault://newtab", false);
  }
  // I persist my settings together with the cookies and local storage, so a
  // restart does not sign the user out of everything.
  bool save_profile() {
    profile.cookies = cookies.to_json();
    profile.local_storage = local_storage.to_json();
    return profile.save();
  }
  // I build the request policy from my persisted settings so every toggle has a
  // real effect on the next request.
  NetworkPolicy policy() {
    NetworkPolicy out;
    out.cookies = &cookies;
    out.proxy = profile.proxy;
    out.send_dnt = profile.send_dnt;
    out.strip_referrer = profile.strip_referrer;
    out.block_third_party_cookies = profile.block_third_party_cookies;
    out.block_ads = profile.block_ads;
    out.blocked_hosts = profile.blocked_hosts;
    out.allowed_hosts = profile.block_allow_sites;
    return out;
  }
  // I ship my VPN bridge beside the browser so a fresh profile has a real
  // extension driving my proxy path. I look in the install folder first, then
  // the source tree so a development run behaves the same way.
  void load_bundled_extensions() {
    namespace fs = std::filesystem;
    std::vector<fs::path> roots;
    roots.push_back(fs::path(executable_dir()) / "extensions");
    roots.push_back(fs::path(executable_dir()) / ".." / "extensions");
    for (const auto &root : roots) {
      std::error_code ec;
      if (!fs::is_directory(root, ec)) continue;
      for (const auto &entry : fs::directory_iterator(root, ec)) {
        if (!entry.is_directory(ec)) continue;
        auto name = entry.path().filename().string();
        std::string error;
        // My bundled id is stable across machines so storage survives an update.
        std::string id = "bundled-" + lower(name);
        if (!extensions.install_bundled(entry.path().string(), id, error)) {
          extension_error = error;
          continue;
        }
        bool disabled = std::find(profile.disabled_extensions.begin(),
                                  profile.disabled_extensions.end(), id) !=
                        profile.disabled_extensions.end();
        if (disabled) extensions.set_enabled(id, false, error);
        else if (!extensions.start_worker(id, error)) extension_error = error;
      }
      break;
    }
  }
  void install_extension_effects() {
    ExtensionEffects effects;
    effects.set_proxy = [this](const std::string &id, const ProxyConfig &config, std::string &error) {
      if (private_mode) {
        error = "I do not let an extension change the proxy in a private window";
        return false;
      }
      if (!profile.proxy.owner_extension.empty() && profile.proxy.owner_extension != id) {
        error = "another extension already controls my proxy";
        return false;
      }
      profile.proxy = config;
      profile.proxy.owner_extension = id;
      if (!save_profile()) { error = profile.error; return false; }
      return true;
    };
    effects.get_proxy = [this](const std::string &) { return profile.proxy; };
    effects.clear_proxy = [this](const std::string &id, std::string &error) {
      if (!profile.proxy.owner_extension.empty() && profile.proxy.owner_extension != id) {
        error = "this extension does not control my proxy";
        return false;
      }
      profile.proxy = ProxyConfig::direct();
      if (!save_profile()) { error = profile.error; return false; }
      return true;
    };
    effects.set_webrtc_policy = [this](const std::string &, const std::string &value, std::string &) {
      profile.web_rtc_policy = value;
      save_profile();
      return true;
    };
    effects.log = [this](const std::string &id, const std::string &level, const std::string &message) {
      if (extension_log.size() >= 200) extension_log.erase(extension_log.begin());
      extension_log.push_back(id + " " + level + ": " + message);
    };
    extensions.set_effects(std::move(effects));
  }
  std::vector<std::string> extension_log;
  std::string vpn_summary() const {
    if (!profile.proxy.active()) return "VPN off - direct connection";
    ProxyServer server;
    std::string reason;
    auto url = parse_url("https://example.com/");
    if (url && !profile.proxy.route(*url, server, reason))
      return "VPN fail-closed - " + reason;
    if (server.empty()) return "VPN " + profile.proxy.mode_name() + " - bypassed";
    return "VPN " + profile.proxy.mode_name() + " via " + server.authority();
  }
  ~Browser() {
    if (profile.clear_on_exit && !private_mode) {
      profile.clear_history();
      save_profile();
    }
    for (auto &tab : tabs)
      if (tab.session)
        tab.session->request_stop();
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
      tab.session->request_stop();
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
    update_find();
    if (!tab.started && (starts(tab.url, "http://") || starts(tab.url, "https://"))) {
      navigate(tab.url, false);
    } else if (tab.session) {
      Json j = Json::make_object();
      j.set("tag", "resize");
      j.set("width", content.w / tab.zoom);
      j.set("height", content.h / tab.zoom);
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
    session->policy = policy();
    session->jar = &cookies;
    session->storage = &local_storage;
    if (auto host_url = parse_url(target)) {
      session->policy.top_level_host = host_url->host;
      session->policy.cookies = &cookies;
    }
    session->width = content.w / tab.zoom;
    session->height = content.h / tab.zoom;
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
      j.set("width", content.w / current().zoom);
      j.set("height", content.h / current().zoom);
      current().session->send(j);
    }
    update_find();
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
    c.text(12, 152, "D", 14, downloads.active_count() ? ACCENT : MUTED);
    hits.push_back({{4, 144, 36, 36}, "downloads", 0, {}});
    c.text(12, height - 158, "A", 14, profile.block_ads ? ACCENT : MUTED, profile.block_ads);
    hits.push_back({{4, double(height - 166), 36, 36}, "toggle-adblock", 0, {}});
    c.text(12, height - 114, "V", 14, profile.proxy.active() ? ACCENT : MUTED,
           profile.proxy.active());
    hits.push_back({{4, double(height - 122), 36, 36}, "vpn", 0, {}});
    c.text(12, height - 70, "S", 14, MUTED);
    hits.push_back({{4, double(height - 78), 36, 36}, "settings", 0, {}});
    c.fill(content, BG);
    if (panel_downloads)
      draw_downloads(c);
    else if (panel_bookmarks || panel_history)
      draw_panel(c);
    else if (settings)
      draw_settings(c);
    else if (current().url == "psychovault://newtab" || current().url == "about:blank")
      draw_home(c);
    else
      draw_content(c);
    if (find_open) {
      Rect bar{content.x + content.w - 330, content.y + 10, 310, 34};
      c.fill(bar, PANEL);
      c.stroke(bar, ACCENT);
      std::string shown = find_text.empty() ? "Find in page" : find_text;
      c.text(bar.x + 12, bar.y + 11, shown.substr(0, 34), 13, find_text.empty() ? MUTED : TEXT);
      std::string counter = find_marks.empty()
                                ? "0/0"
                                : std::to_string(find_at + 1) + "/" + std::to_string(find_marks.size());
      c.text(bar.x + bar.w - 96, bar.y + 12, counter, 12, MUTED);
      Rect close{bar.x + bar.w - 26, bar.y + 8, 18, 18};
      c.text(close.x + 5, close.y, "x", 12, MUTED);
      hits.push_back({bar, "find-bar", 0, {}});
      hits.push_back({close, "find-close", 0, {}});
    }
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
  void draw_downloads(Canvas &c) {
    c.text(content.x + 40, content.y + 30, "Downloads", 24, TEXT, true);
    c.text(content.x + 40, content.y + 62, downloads.directory(), 12, MUTED);
    Rect clear{content.x + content.w - 200, content.y + 24, 150, 30};
    c.fill(clear, PANEL);
    c.stroke(clear, BORDER);
    c.text(clear.x + 12, clear.y + 7, "Clear finished", 12, TEXT);
    hits.push_back({clear, "downloads-clear", 0, {}});
    auto items = downloads.snapshot();
    double y = content.y + 100;
    if (items.empty())
      c.text(content.x + 40, y, "I have not downloaded anything yet.", 13, MUTED);
    for (size_t i = 0; i < items.size() && y < content.y + content.h - 60; ++i) {
      const auto &item = items[i];
      Rect row{content.x + 40, y, content.w - 80, 56};
      c.fill(row, PANEL);
      c.stroke(row, BORDER);
      c.text(row.x + 12, row.y + 8, item.filename.substr(0, 70), 13, TEXT);
      std::string detail = item.state;
      if (item.state == "running") {
        detail += "  " + std::to_string(item.received / 1024) + " KiB";
        if (item.total) detail += " of " + std::to_string(item.total / 1024) + " KiB";
      } else if (!item.error.empty()) detail += " - " + item.error;
      c.text(row.x + 12, row.y + 30, detail.substr(0, 110), 11,
             item.state == "failed" ? 0xfff87171 : MUTED);
      if (item.state == "running" && item.total) {
        double fraction = std::clamp(double(item.received) / double(item.total), 0.0, 1.0);
        c.fill({row.x, row.y + row.h - 3, row.w * fraction, 3}, ACCENT);
      }
      if (item.state == "running") {
        Rect cancel{row.x + row.w - 90, row.y + 12, 76, 28};
        c.stroke(cancel, BORDER);
        c.text(cancel.x + 12, cancel.y + 6, "Cancel", 12, TEXT);
        hits.push_back({cancel, "download-cancel", item.id, {}});
      } else if (item.state == "complete") {
        Rect open{row.x + row.w - 90, row.y + 12, 76, 28};
        c.stroke(open, BORDER);
        c.text(open.x + 20, open.y + 6, "Open", 12, TEXT);
        hits.push_back({open, "download-open", item.id, item.path});
      }
      y += 64;
    }
    if (!download_error.empty())
      c.text(content.x + 40, content.y + content.h - 40, download_error, 12, 0xfff87171);
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
      if (i == settings_category) c.stroke(r, ACCENT);
      c.text(r.x + 22, r.y + 7, cats[i], 13, i == settings_category ? TEXT : MUTED);
      c.text(r.x + 6, r.y + 7, (i < 9 ? "0" : "") + std::to_string(i + 1), 10, MUTED);
      hits.push_back({r, "settings-category", i, {}});
    }
    double x = nav.x + nav.w + 32, y = nav.y;
    c.text(x, y, cats[settings_category], 22, TEXT, true);
    y += 44;
    auto toggle = [&](const char *label, bool value, const char *action, const char *hint) {
      c.text(x, y, label, 14, TEXT);
      Rect t{x + 340, y - 4, 48, 24};
      c.fill(t, value ? ACCENT : BORDER);
      c.text(t.x + 8, t.y + 3, value ? "ON" : "OFF", 11, value ? BG : TEXT, true);
      hits.push_back({t, action, 0, {}});
      if (hint && *hint) { c.text(x, y + 20, hint, 11, MUTED); y += 58; }
      else y += 42;
    };
    auto value_row = [&](const char *label, const std::string &value, const char *action) {
      c.text(x, y, label, 14, TEXT);
      Rect r{x + 340, y - 4, 260, 30};
      c.fill(r, 0xff050a12);
      c.stroke(r, BORDER);
      c.text(r.x + 10, r.y + 6, value.substr(0, 34), 12, TEXT);
      if (action && *action) hits.push_back({r, action, 0, {}});
      y += 46;
    };
    auto button = [&](const char *label, const char *action) {
      Rect r{x, y - 2, 240, 32};
      c.fill(r, PANEL);
      c.stroke(r, BORDER);
      c.text(r.x + 12, r.y + 8, label, 12, TEXT);
      hits.push_back({r, action, 0, {}});
      y += 46;
    };
    auto note = [&](const std::string &text) {
      c.text(x, y, text.substr(0, 96), 12, MUTED);
      y += 26;
    };
    switch (settings_category) {
    case 0:
      value_row("Search engine", profile.search_engine, "setting-search");
      toggle("Restore tabs on launch", profile.restore_tabs, "setting-restore",
             "I reopen my last tabs and the tab I was using.");
      toggle("Workspaces", profile.workspaces, "setting-workspaces", "");
      value_row("Home page", profile.home_page, "setting-home");
      value_row("Custom search", profile.custom_search.empty() ? "not set" : "configured",
                "setting-custom-search");
      break;
    case 1:
      toggle("Block ads and trackers", profile.block_ads, "setting-block-ads",
             "I refuse the request before a socket opens.");
      toggle("Block third-party cookies", profile.block_third_party_cookies,
             "setting-third-party-cookies", "");
      toggle("Send Do Not Track", profile.send_dnt, "setting-dnt", "I add DNT: 1 to every request.");
      toggle("Strip referrer", profile.strip_referrer, "setting-referrer", "");
      toggle("Clear history when I close", profile.clear_on_exit, "setting-clear-exit", "");
      value_row("WebRTC policy", profile.web_rtc_policy, "");
      button("Clear browsing history now", "setting-clear-history");
      note("Blocked hosts: " + std::to_string(profile.blocked_hosts.size()));
      break;
    case 2:
      value_row("Save files to", profile.download_directory, "setting-download-dir");
      toggle("Ask where to save each file", profile.ask_save_location, "setting-ask-save", "");
      toggle("Show download progress", profile.download_notify, "setting-download-notify", "");
      button("Open my downloads", "setting-open-downloads");
      note("Active transfers: " + std::to_string(downloads.active_count()));
      break;
    case 3: {
      note(vpn_summary());
      // The proxy I tunnel through, and the rule that a failed tunnel is the end of
      // the request rather than a quiet direct connection.
      value_row("Server", profile.proxy.active() ? profile.proxy.single.authority() : "none",
                "setting-vpn-server");
      value_row("Scheme", profile.proxy.active() ? profile.proxy.single.scheme : "none",
                "setting-vpn-scheme");
      if (profile.proxy.active() && profile.proxy.single.scheme == "http")
        note("This is a plain HTTP proxy, so my traffic to it is not encrypted. I only "
             "count socks5 and https as a tunnel.");
      value_row("Username", profile.proxy.has_auth() ? profile.proxy.user : "none",
                "setting-vpn-user");
      value_row("Password", profile.proxy.has_auth() ? std::string(8, '*') : "none",
                "setting-vpn-pass");
      toggle("Refuse requests when the tunnel fails", profile.proxy.fail_closed,
             "setting-vpn-fail-closed", "Nothing falls back to a direct socket.");
      button("Turn my tunnel off", "setting-vpn-off");
      note("My own SOCKS5 and HTTPS proxy transport carries this, not WinHTTP.");
      auto records = extensions.list();
      if (records.empty()) {
        note("I have no extensions installed.");
        note("Proton VPN is not bundled yet - see my verification record.");
      }
      for (size_t i = 0; i < records.size() && i < 6; ++i) {
        const auto &record = records[i];
        c.text(x, y, record.manifest.name.substr(0, 40), 14, TEXT);
        c.text(x, y + 18, ("v" + record.manifest.version + "  " +
                          std::to_string(record.manifest.permissions.size()) + " permissions")
                             .substr(0, 60), 11, MUTED);
        Rect t{x + 340, y - 4, 48, 24};
        c.fill(t, record.enabled ? ACCENT : BORDER);
        c.text(t.x + 8, t.y + 3, record.enabled ? "ON" : "OFF", 11, record.enabled ? BG : TEXT, true);
        hits.push_back({t, "extension-toggle", int(i), record.manifest.id});
        y += 50;
        if (!record.install_error.empty()) {
          c.text(x, y - 6, record.install_error.substr(0, 90), 11, 0xfff87171);
          y += 22;
        }
      }
      button("Add an unpacked extension", "extension-add");
      if (!extension_error.empty()) note(extension_error);
      break;
    }
    case 4:
      value_row("CPU limit", profile.cpu_limit ? std::to_string(profile.cpu_limit) + "%" : "off",
                "setting-cpu-limit");
      value_row("Sleep inactive tabs", profile.sleep_tabs_minutes
                    ? std::to_string(profile.sleep_tabs_minutes) + " min" : "off",
                "setting-sleep-tabs");
      toggle("Battery saver", profile.battery_saver, "setting-battery", "");
      note("Live renderers: " + std::to_string(tabs.size()));
      note("Active downloads: " + std::to_string(downloads.active_count()));
      break;
    case 5:
      value_row("Theme", profile.theme, "");
      value_row("Accent", profile.accent, "setting-accent");
      toggle("Hide the sidebar", profile.hide_sidebar, "setting-hide-sidebar", "");
      note("I have not implemented custom mod CSS yet.");
      break;
    case 6:
      toggle("Spell check what I type", profile.spell_check, "setting-spellcheck", "");
      note("Words I added: " + std::to_string(profile.user_words.size()));
      button("Add a word from my search box", "setting-spell-add");
      button("Remove my last added word", "setting-spell-remove");
      break;
    case 7:
      note("My vault is not implemented in this build.");
      note("I will not show a password field that stores nothing.");
      note("I have not migrated any existing vault data.");
      break;
    case 8:
      toggle("High contrast", profile.high_contrast, "setting-high-contrast", "");
      toggle("Reduce motion", profile.reduce_motion, "setting-reduce-motion", "");
      toggle("Show tab close buttons", profile.show_tab_close_buttons, "setting-tab-close", "");
      note("Translation is not implemented yet.");
      break;
    default:
      note("PsychoVault development build - owned engine");
      note("Profile: " + profile.directory);
      note(vpn_summary());
      note("Extensions: " + std::to_string(extensions.list().size()));
      button("Check for an update", "setting-check-update");
      if (!update_status.empty()) note(update_status);
      note("I verify a release hash before I offer to install it.");
      break;
    }
    if (!profile_error.empty())
      c.text(x, content.y + content.h - 40, profile_error.substr(0, 100), 11, 0xfff87171);
  }
  std::string update_status;
  std::string editing_setting;
  void begin_setting_edit(const std::string &key, const std::string &current_value) {
    editing_setting = key;
    address = current_value;
    address_caret = address.size();
    address_focus = true;
    status = "Editing " + key + " - press Enter to save or Escape to cancel";
  }
  void commit_setting_edit() {
    auto key = editing_setting;
    auto value = trim(address);
    editing_setting.clear();
    address_focus = false;
    if (key == "homepage") {
      if (!profile.set_home_page(value)) status = "That is not a URL I can use as a home page";
      else status = "Home page saved";
    } else if (key == "vpnServer") {
      // I take host:port and never guess a scheme, because an HTTPS proxy and a
      // SOCKS5 proxy behave differently and a wrong guess leaks.
      auto colon = value.rfind(':');
      if (value.empty()) {
        profile.proxy = ProxyConfig::direct();
        status = "My tunnel is off";
      } else if (colon == std::string::npos) {
        status = "A tunnel server needs host:port";
      } else {
        std::string host = value.substr(0, colon);
        int port = std::atoi(value.c_str() + colon + 1);
        if (host.empty() || port <= 0 || port > 65535) {
          status = "That is not a host and port I can dial";
        } else {
          if (!profile.proxy.active())
            profile.proxy = ProxyConfig();
          profile.proxy.mode = ProxyConfig::Mode::FixedServers;
          profile.proxy.single.host = host;
          profile.proxy.single.port = port;
          if (profile.proxy.single.scheme != "socks5" &&
              profile.proxy.single.scheme != "https")
            profile.proxy.single.scheme = "socks5";
          profile.proxy.fail_closed = true;
          status = "Tunnel set to " + profile.proxy.single.scheme + " " + host + ":" +
                   std::to_string(port);
        }
      }
    } else if (key == "vpnScheme") {
      // Only the two schemes that protect the connection are offered here. A plain
      // HTTP proxy would put my CONNECT target and my login on the network in clear
      // text, and that is not a VPN whatever the panel calls it.
      const std::string wanted = lower(value);
      if (wanted == "socks5" || wanted == "https") {
        profile.proxy.single.scheme = wanted;
        profile.proxy.fail_closed = true;
        status = "Tunnel scheme " + wanted;
      } else {
        status = "My VPN takes socks5 or https, not " + (wanted.empty() ? "nothing" : wanted);
      }
    } else if (key == "vpnUser") {
      profile.proxy.user = value;
      status = "Tunnel username saved";
    } else if (key == "vpnPass") {
      profile.proxy.password = value;
      status = "Tunnel password saved";
    } else if (key == "customSearch") {
      if (!profile.set_custom_search(value)) status = "A custom search URL must contain %s";
      else { if (!value.empty()) profile.set_search_engine("custom"); status = "Custom search saved"; }
    }
    save_profile();
    sync_address();
  }
  void check_for_update() {
    update_status = "Checking my releases...";
    auto url = parse_url("https://api.github.com/repos/Psycho-Core/PsychoVault/releases/latest");
    if (!url) { update_status = "I could not build the release URL"; return; }
    auto response = fetch_url(*url, policy(), 15000, 1024 * 1024);
    if (!response.ok) { update_status = "Update check failed: " + response.error; return; }
    auto json = Json::parse(response.text);
    if (!json) { update_status = "The release response was not valid JSON"; return; }
    auto tag = json->str("tag_name");
    if (tag.empty()) { update_status = "I have not published a release yet"; return; }
    bool has_manifest = false, has_exe = false;
    const Json *assets = json->find("assets");
    if (assets && assets->type == Json::Type::Array)
      for (const auto &asset : assets->array) {
        auto name = asset.str("name");
        if (name == "PsychoVault-update.json") has_manifest = true;
        if (name == "PsychoVault.exe") has_exe = true;
      }
    update_status = "Latest release " + tag + (has_exe ? " (exe present" : " (no exe");
    update_status += has_manifest ? ", manifest present)" : ", no manifest)";
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
  // I turn a frame into the paint list my raster reads, in window coordinates.
  std::vector<PaintCommand> frame_commands(const Tab &t, double origin_x, double origin_y) const {
    std::vector<PaintCommand> commands;
    double zoom = t.zoom;
    for (auto &j : t.frame.at("paint").array) {
      PaintCommand p;
      p.kind = PaintCommand::Kind(int(j.num("kind")));
      p.rect = {j.num("x") * zoom + origin_x, j.num("y") * zoom + origin_y - t.scroll * zoom,
                j.num("w") * zoom, j.num("h") * zoom};
      p.color = uint32_t(j.num("color"));
      p.text = j.str("text");
      p.size = j.num("size") * zoom;
      p.thickness = j.num("thickness");
      p.bold = j.flag("bold");
      p.node = int(j.num("node"));
      p.url = j.str("url");
      p.gradient = j.str("gradient");
      p.fixed = j.flag("fixed");
      commands.push_back(p);
    }
    return commands;
  }
  static std::string ascii_lower(std::string text) {
    for (char &ch : text)
      if (ch >= 'A' && ch <= 'Z')
        ch = char(ch - 'A' + 'a');
    return text;
  }
  // I build the list of places my query appears by walking the text runs my
  // layout produced, so a find shows what the page actually paints.
  void update_find() {
    find_marks.clear();
    find_at = 0;
    Tab &t = current();
    if (!find_open || find_text.empty() || t.frame.type != Json::Type::Object)
      return;
    // I hand the same paint list to the engine that I hand to my raster, so a
    // highlight can never disagree with what is on screen.
    auto commands = frame_commands(t, 0, 0);
    double zoom = t.zoom > 0 ? t.zoom : 1.0;
    // My frame is already zoomed and scrolled, so I turn each hit back into the
    // page coordinate my scroll position is kept in.
    for (auto &rect : find_in_paint(commands, find_text))
      find_marks.push_back({rect.x / zoom, rect.y / zoom + t.scroll, rect.w / zoom, rect.h / zoom});
  }
  void open_find() {
    find_open = true;
    address_focus = home_focus = false;
    update_find();
    InvalidateRect(hwnd, nullptr, FALSE);
  }
  void close_find() {
    find_open = false;
    find_text.clear();
    find_marks.clear();
    find_at = 0;
    InvalidateRect(hwnd, nullptr, FALSE);
  }
  void find_step(int delta) {
    if (find_marks.empty())
      return;
    find_at = (find_at + delta + int(find_marks.size())) % int(find_marks.size());
    const Rect &m = find_marks[size_t(find_at)];
    Tab &t = current();
    double zoom = t.zoom > 0 ? t.zoom : 1.0;
    // My scroll is kept in page units, so the visible band is the content height
    // divided by the zoom.
    double view = content.h / zoom;
    if (m.y < t.scroll)
      scroll((m.y - t.scroll) * zoom - 24);
    else if (m.y + m.h > t.scroll + view)
      scroll(((m.y + m.h) - (t.scroll + view)) * zoom + 24);
    InvalidateRect(hwnd, nullptr, FALSE);
  }
  void set_zoom(double zoom) {
    Tab &t = current();
    t.zoom = std::clamp(zoom, 0.5, 3.0);
    if (t.session) {
      Json j = Json::make_object();
      j.set("tag", "resize");
      j.set("width", content.w / t.zoom);
      j.set("height", content.h / t.zoom);
      t.session->send(j);
    }
    update_find();
    status = "Zoom " + std::to_string(int(t.zoom * 100 + 0.5)) + "%";
    InvalidateRect(hwnd, nullptr, FALSE);
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
    std::vector<PaintCommand> commands = frame_commands(t, content.x, content.y);
    Rect old = c.clip;
    c.clip = content;
    paint_commands(c, commands, 0, t.images);
    double zoom = t.zoom > 0 ? t.zoom : 1.0;
    for (size_t i = 0; i < find_marks.size(); ++i) {
      const Rect &m = find_marks[i];
      Rect r{m.x * zoom + content.x, m.y * zoom + content.y - t.scroll * zoom, m.w * zoom,
             m.h * zoom};
      if (i == size_t(find_at)) {
        c.stroke(r, ACCENT);
        c.fill({r.x, r.y + r.h - 2, r.w, 2}, ACCENT);
      } else
        c.stroke(r, 0xff2a4a6a);
    }
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
          save_profile();
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
          save_profile();
        } else if (h.action == "setting-restore") {
          profile.restore_tabs = !profile.restore_tabs;
          save_profile();
        } else if (h.action == "downloads") {
          panel_downloads = !panel_downloads;
          panel_bookmarks = panel_history = false;
        } else if (h.action == "toggle-adblock") {
          profile.block_ads = !profile.block_ads;
          save_profile();
          status = profile.block_ads ? "Ad blocker on" : "Ad blocker off";
        } else if (h.action == "vpn") {
          navigate("psychovault://settings");
          settings_category = 3;
        } else if (h.action == "downloads-clear") {
          downloads.clear_finished();
        } else if (h.action == "download-cancel") {
          downloads.cancel(h.index);
        } else if (h.action == "download-open") {
          if (!h.value.empty()) ShellExecuteW(hwnd, L"open", std::wstring(h.value.begin(), h.value.end()).c_str(), nullptr, nullptr, SW_SHOWNORMAL);
        } else if (h.action == "setting-workspaces") {
          profile.workspaces = !profile.workspaces; save_profile();
        } else if (h.action == "setting-home") {
          begin_setting_edit("homepage", profile.home_page);
        } else if (h.action == "setting-custom-search") {
          begin_setting_edit("customSearch", profile.custom_search);
        } else if (h.action == "setting-block-ads") {
          profile.block_ads = !profile.block_ads; save_profile();
        } else if (h.action == "setting-third-party-cookies") {
          profile.block_third_party_cookies = !profile.block_third_party_cookies; save_profile();
        } else if (h.action == "setting-dnt") {
          profile.send_dnt = !profile.send_dnt; save_profile();
        } else if (h.action == "setting-referrer") {
          profile.strip_referrer = !profile.strip_referrer; save_profile();
        } else if (h.action == "setting-clear-exit") {
          profile.clear_on_exit = !profile.clear_on_exit; save_profile();
        } else if (h.action == "setting-vpn-server") {
          begin_setting_edit(
              "vpnServer",
              profile.proxy.active() ? profile.proxy.single.authority() : std::string());
        } else if (h.action == "setting-vpn-scheme") {
          begin_setting_edit("vpnScheme",
                             profile.proxy.active() ? profile.proxy.single.scheme
                                                    : std::string("socks5"));
        } else if (h.action == "setting-vpn-user") {
          begin_setting_edit("vpnUser", profile.proxy.user);
        } else if (h.action == "setting-vpn-pass") {
          begin_setting_edit("vpnPass", profile.proxy.password);
        } else if (h.action == "setting-vpn-fail-closed") {
          profile.proxy.fail_closed = !profile.proxy.fail_closed;
          save_profile();
          status = profile.proxy.fail_closed ? "A failed tunnel stops the request"
                                             : "A failed tunnel may connect direct";
        } else if (h.action == "setting-vpn-off") {
          profile.proxy = ProxyConfig::direct();
          save_profile();
          status = "My tunnel is off";
        } else if (h.action == "setting-clear-history") {
          profile.clear_history(); save_profile(); status = "I cleared my browsing history";
        } else if (h.action == "setting-download-dir") {
          auto chosen = pick_folder(hwnd);
          if (!chosen.empty()) {
            if (profile.set_download_directory(chosen)) {
              downloads.set_directory(profile.download_directory);
              save_profile();
              status = "Downloads will save to " + profile.download_directory;
            } else status = "I cannot write to that folder";
          }
        } else if (h.action == "setting-ask-save") {
          profile.ask_save_location = !profile.ask_save_location; save_profile();
        } else if (h.action == "setting-download-notify") {
          profile.download_notify = !profile.download_notify; save_profile();
        } else if (h.action == "setting-open-downloads") {
          panel_downloads = true; settings = false;
        } else if (h.action == "extension-toggle") {
          std::string error;
          auto records = extensions.list();
          if (h.index >= 0 && size_t(h.index) < records.size()) {
            const auto &record = records[size_t(h.index)];
            bool enable = !record.enabled;
            if (extensions.set_enabled(record.manifest.id, enable, error)) {
              auto &list = profile.disabled_extensions;
              list.erase(std::remove(list.begin(), list.end(), record.manifest.id), list.end());
              if (!enable) list.push_back(record.manifest.id);
              if (enable && !extensions.start_worker(record.manifest.id, error))
                extension_error = error;
              save_profile();
            } else extension_error = error;
          }
        } else if (h.action == "extension-add") {
          auto chosen = pick_folder(hwnd);
          if (!chosen.empty()) {
            std::string error;
            if (extensions.install_directory(chosen, error)) {
              profile.extension_directories.push_back(chosen);
              save_profile();
              for (const auto &record : extensions.list())
                if (record.directory == chosen && !extensions.start_worker(record.manifest.id, error))
                  extension_error = error;
              status = "I installed the extension";
            } else extension_error = error;
          }
        } else if (h.action == "setting-cpu-limit") {
          const int steps[] = {0, 25, 50, 75};
          size_t index = 0;
          for (size_t i = 0; i < 4; ++i) if (profile.cpu_limit == steps[i]) index = i;
          profile.cpu_limit = steps[(index + 1) % 4]; save_profile();
        } else if (h.action == "setting-sleep-tabs") {
          const int steps[] = {0, 5, 15, 30, 60};
          size_t index = 0;
          for (size_t i = 0; i < 5; ++i) if (profile.sleep_tabs_minutes == steps[i]) index = i;
          profile.sleep_tabs_minutes = steps[(index + 1) % 5]; save_profile();
        } else if (h.action == "setting-battery") {
          profile.battery_saver = !profile.battery_saver; save_profile();
        } else if (h.action == "setting-accent") {
          const char *accents[] = {"cyan", "purple", "green", "red"};
          size_t index = 0;
          for (size_t i = 0; i < 4; ++i) if (profile.accent == accents[i]) index = i;
          profile.accent = accents[(index + 1) % 4]; save_profile();
        } else if (h.action == "setting-hide-sidebar") {
          profile.hide_sidebar = !profile.hide_sidebar; save_profile();
        } else if (h.action == "setting-spellcheck") {
          profile.spell_check = !profile.spell_check; save_profile();
        } else if (h.action == "setting-spell-add") {
          if (!home_search.empty()) { profile.add_user_word(home_search); save_profile(); status = "I added the word"; }
          else status = "Type a word in my home search box first";
        } else if (h.action == "setting-spell-remove") {
          if (!profile.user_words.empty()) { profile.remove_user_word(profile.user_words.back()); save_profile(); }
        } else if (h.action == "setting-high-contrast") {
          profile.high_contrast = !profile.high_contrast; save_profile();
        } else if (h.action == "setting-reduce-motion") {
          profile.reduce_motion = !profile.reduce_motion; save_profile();
        } else if (h.action == "setting-tab-close") {
          profile.show_tab_close_buttons = !profile.show_tab_close_buttons; save_profile();
        } else if (h.action == "setting-check-update") {
          check_for_update();
        } else if (h.action == "minimize")
          ShowWindow(hwnd, SW_MINIMIZE);
        else if (h.action == "maximize")
          ShowWindow(hwnd, IsZoomed(hwnd) ? SW_RESTORE : SW_MAXIMIZE);
        else if (h.action == "close-window")
          PostMessageW(hwnd, WM_CLOSE, 0, 0);
        else if (h.action == "find-bar")
          address_focus = home_focus = false;
        else if (h.action == "find-close")
          close_find();
        InvalidateRect(hwnd, nullptr, FALSE);
        return;
      }
    if (content.contains(x, y) && current().session) {
      Json j = Json::make_object();
      j.set("tag", "click");
      // A zoomed page lays out in its own pixels, so I convert back before I
      // hand a click to it.
      j.set("x", (x - content.x) / current().zoom);
      j.set("y", (y - content.y) / current().zoom);
      current().session->send(j);
    }
  }
  void key(UINT, WPARAM wp, LPARAM) {
    bool ctrl = GetKeyState(VK_CONTROL) < 0;
    bool shift = GetKeyState(VK_SHIFT) < 0;
    if (ctrl && wp == 'F') {
      open_find();
      return;
    }
    if (ctrl && (wp == VK_OEM_PLUS || wp == VK_ADD || wp == '=')) {
      set_zoom(current().zoom * 1.1);
      return;
    }
    if (ctrl && (wp == VK_OEM_MINUS || wp == VK_SUBTRACT)) {
      set_zoom(current().zoom / 1.1);
      return;
    }
    if (ctrl && wp == '0') {
      set_zoom(1.0);
      return;
    }
    if (find_open && !address_focus && !home_focus) {
      if (wp == VK_ESCAPE) {
        close_find();
        return;
      }
      if (wp == VK_RETURN) {
        find_step(shift ? -1 : 1);
        return;
      }
      if (wp == VK_BACK) {
        if (!find_text.empty()) {
          size_t previous = previous_cp(find_text, find_text.size());
          find_text.erase(previous);
          update_find();
        }
        InvalidateRect(hwnd, nullptr, FALSE);
        return;
      }
    }
    if (ctrl && wp == 'V') {
      paste();
      return;
    }
    if (ctrl && wp == 'J') {
      panel_downloads = !panel_downloads;
      panel_bookmarks = panel_history = false;
      InvalidateRect(hwnd, nullptr, FALSE);
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
      save_profile();
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
        if (!editing_setting.empty()) { commit_setting_edit(); InvalidateRect(hwnd, nullptr, FALSE); return; }
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
    if (find_open && !address_focus && !home_focus) {
      if (find_text.size() + text.size() <= 512) {
        find_text += text;
        update_find();
      }
      InvalidateRect(hwnd, nullptr, FALSE);
      return;
    }
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
    double zoom = t.zoom > 0 ? t.zoom : 1.0;
    double page_height = t.frame.num("height", content.h / zoom);
    double max = std::max(0.0, page_height - content.h / zoom);
    t.scroll = std::clamp(t.scroll + delta / zoom, 0.0, max);
    if (t.session) {
      Json j = Json::make_object();
      j.set("tag", "scroll");
      j.set("y", t.scroll);
      t.session->send(j);
    }
    InvalidateRect(hwnd, nullptr, FALSE);
  }
};
static std::string pick_folder(HWND owner) {
  BROWSEINFOW info{};
  info.hwndOwner = owner;
  info.lpszTitle = L"Choose a folder";
  info.ulFlags = BIF_RETURNONLYFSDIRS | BIF_NEWDIALOGSTYLE;
  PIDLIST_ABSOLUTE id = SHBrowseForFolderW(&info);
  if (!id) return {};
  wchar_t path[MAX_PATH * 2] = {0};
  std::string out;
  if (SHGetPathFromIDListW(id, path)) {
    int size = WideCharToMultiByte(CP_UTF8, 0, path, -1, nullptr, 0, nullptr, nullptr);
    out.assign(size ? size - 1 : 0, '\0');
    if (size) WideCharToMultiByte(CP_UTF8, 0, path, -1, out.data(), size, nullptr, nullptr);
  }
  CoTaskMemFree(id);
  return out;
}
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
          if (!b->save_profile() && b->profile_error.empty())
            b->profile_error = b->profile.error;
          if (tab == &b->current() && !b->address_focus)
            b->sync_address();
        }
        if (tab == &b->current()) {
          b->status = tab->url;
          b->update_find();
        }
      }
      if (tab == &b->current())
        InvalidateRect(hwnd, nullptr, FALSE);
    }
    return 0;
  }
  case WM_URL: {
    std::unique_ptr<Update> up(reinterpret_cast<Update *>(lp));
    auto b = browser(hwnd);
    Tab *tab = b ? b->find_tab(int(wp)) : nullptr;
    if (tab && up->generation == tab->generation && !up->url.empty()) {
      tab->url = up->url;
      if (tab->index < tab->history.size())
        tab->history[tab->index] = up->url;
      if (tab == &b->current()) {
        if (!b->address_focus)
          b->sync_address();
        b->status = tab->url;
        b->update_find();
        b->save_session();
        InvalidateRect(hwnd, nullptr, FALSE);
      }
    }
    return 0;
  }
  case WM_IMAGE: {
    std::unique_ptr<Update> up(reinterpret_cast<Update *>(lp));
    auto b = browser(hwnd);
    Tab *tab = b ? b->find_tab(int(wp)) : nullptr;
    // My page host already decoded this image in this process, so I only copy
    // the pixels I have not shown yet.
    if (tab && up->generation == tab->generation && up->image && up->image->valid()) {
      const size_t bytes = up->image->pixels.size();
      // A canvas sends a fresh bitmap whenever it is redrawn, so I give the old
      // one back before I count the new one.
      auto previous = tab->images.find(up->url);
      if (previous != tab->images.end())
        tab->image_bytes -= previous->second.pixels.size();
      if (tab->image_bytes + bytes <= 128u * 1024u * 1024u) {
        tab->image_bytes += bytes;
        tab->images[up->url] = *up->image;
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
    if (b && !b->smoke) {
      b->extensions.tick();
      if (b->downloads.active_count() && (GetTickCount() % 8) < 2)
        InvalidateRect(hwnd, nullptr, FALSE);
    }
    // I finish as soon as my page is actually rendered, but I allow a slow
    // first fetch enough time before I call it a failure.
    DWORD elapsed = b ? GetTickCount() - b->start_tick : 0;
    bool settled = b && b->current().frame.type == Json::Type::Object && !b->current().loading;
    if (b && b->smoke && (elapsed > 20000 || (settled && elapsed > 1500))) {
      if (!b->capture_path.empty())
        capture(hwnd, b->capture_path);
      bool web = starts(b->current().url, "http://") || starts(b->current().url, "https://");
      bool failed =
          web && (b->current().frame.type != Json::Type::Object || !b->current().error.empty() ||
                  !b->current().session || !b->current().session->ready ||
                  (b->expect_image && b->current().images.empty()));
      if (failed) {
        // I record exactly why my smoke gate failed so CI shows a real reason
        // instead of a bare exit code.
        Json why = Json::make_object();
        why.set("url", b->current().url);
        why.set("error", b->current().error);
        why.set("hasFrame", b->current().frame.type == Json::Type::Object);
        why.set("hasSession", b->current().session != nullptr);
        why.set("pageReady", b->current().session && b->current().session->ready);
        why.set("imageCount", double(b->current().images.size()));
        why.set("expectImage", b->expect_image);
        why.set("sessionStartError",
                b->current().session ? b->current().session->start_error : std::string());
        why.set("loading", b->current().loading);
        why.set("elapsedMs", double(GetTickCount() - b->start_tick));
        if (b->current().session) {
          Json notes = Json::make_array();
          for (const auto &line : b->current().session->recent_diagnostics())
            notes.array.push_back(Json(line));
          why.set("diagnostics", std::move(notes));
        }
        auto report = b->capture_path.empty() ? std::string("smoke-failure.json")
                                             : b->capture_path + ".failure.json";
        std::string write_error;
        write_file_atomic(report, why.dump() + "\n", write_error);
      }
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
int WINAPI wWinMain(HINSTANCE inst, HINSTANCE, LPWSTR, int show) {
  using namespace pv;
  std::string capture, url = "psychovault://newtab", profile_dir;
  bool smoke = false, private_mode = false, expect_image = false;
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
    else if (args[i] == "--expect-image")
      expect_image = true;
    else if (starts(args[i], "--")) {
      // I refuse an option I do not understand. A silently ignored flag turns a
      // required gate into a pass that proves nothing.
      // I must not show a modal dialog here: a blocked runner would hang the
      // gate instead of failing it. My exit code is the signal.
      std::string message = "I do not understand the option " + args[i] + "\n";
      fwrite(message.data(), 1, message.size(), stderr);
      fflush(stderr);
      return 2;
    }
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
