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
#include "../platform/Psycho_spell.hpp"
#include "../platform/Psycho_vault.hpp"
#include "../platform/Psycho_extension.hpp"
#include "../platform/Psycho_download.hpp"
#include "../platform/Psycho_sandbox.hpp"
#include "../runtime/commandline/Psycho_commandline.hpp"
#include "../runtime/settings/Psycho_settings.hpp"
#include "../runtime/vault/Psycho_vault_service.hpp"
#include "../runtime/diagnostics/Psycho_diagnostics_page.hpp"
#include "../runtime/content/Psycho_content.hpp"
#include "../runtime/crash/Psycho_crash.hpp"
#include "../runtime/logging/Psycho_logging.hpp"
#include "../runtime/services/Psycho_services.hpp"
#include "../runtime/keys/Psycho_keys.hpp"
#include "../runtime/ime/Psycho_ime.hpp"
#include "../runtime/focus/Psycho_focus.hpp"
#include "../runtime/internal/Psycho_internal.hpp"
#include "../runtime/private/Psycho_private.hpp"
#include "../runtime/tabs/Psycho_tabs.hpp"
#include "../runtime/window/Psycho_window.hpp"
#include "../runtime/app/Psycho_instance.hpp"
#include "../runtime/broker/Psycho_renderer_farm.hpp"
#include "../runtime/renderer/Psycho_renderer_main.hpp"
#include "Psycho_embedded_ui.hpp"
#include <audioclient.h>
#include <atomic>
#include <algorithm>
#include <charconv>
#include <chrono>
#include <cmath>
#include <iterator>
#include <condition_variable>
#include <deque>
#include <dwmapi.h>
#include <cstring>
#include <ctime>
#include <filesystem>
#include <mmdeviceapi.h>
#include <fstream>
#include <mutex>
#include <iostream>
#include <limits>
#include <shellapi.h>
#include <shlobj.h>
#include <commdlg.h>
#include <thread>
#include <windows.h>
#include <windowsx.h>
#include <imm.h>
#pragma comment(lib, "imm32.lib")

namespace pv {
struct ForwardedLaunch {
  std::vector<std::string> urls;
  std::string error;
};
static std::atomic<HWND> primary_window{nullptr};
static std::mutex forwarded_mutex;
static std::deque<ForwardedLaunch *> pending_forwarded;
static rt::AppInstance *app_instance = nullptr;
static rt::RendererProcessFarm *renderer_farm = nullptr;
static constexpr UINT WM_FRAME = WM_APP + 1, WM_NAVIGATE = WM_APP + 2, WM_DIALOG = WM_APP + 3,
                      WM_DONE = WM_APP + 4, WM_IMAGE = WM_APP + 5, WM_URL = WM_APP + 6,
                      WM_FORWARD_LAUNCH = WM_APP + 7, WM_BRIDGE = WM_APP + 8,
                      WM_CLIPBOARD = WM_APP + 9;
static constexpr uint32_t BG = 0xff05080f, RAIL = 0xff080d16, CHROME = 0xff070b12,
                          PANEL = 0xff0d1420, BORDER = 0xff1c2736, TEXT = 0xffe6edf3,
                          MUTED = 0xff8b98a9, ACCENT = 0xff38bdf8;
struct Update {
  int tab;
  uint64_t generation;
  Json frame;
  std::string error, url, title, clipboard;
  // Renderer pixels arrive over bounded IPC chunks and are reconstructed here.
  std::shared_ptr<const Image> image;
};
struct BridgeTask {
  int tab = -1;
  std::string method;
  Json payload;
};
struct Session;
// S027 in production: each tab's session history is a rt::WebContents. The shell
// vets every address before it navigates (navigate_input, the page bridge, the
// renderer allowlist), so this controller's own policy only refuses an empty or
// control-byte address, and its bound is the 512 entries the shell always kept.
static std::unique_ptr<rt::WebContents> make_tab_contents(int tab_id) {
  rt::WebContents::Seams seams;
  seams.history_limit = 512;
  seams.admit = [](const std::string &raw, std::string &canon, rt::Error &error) {
    error = rt::Error{};
    std::string text = raw;
    while (!text.empty() && std::isspace((unsigned char)text.front())) text.erase(0, 1);
    while (!text.empty() && std::isspace((unsigned char)text.back())) text.pop_back();
    if (text.empty()) {
      error = {"nav.empty", "an empty string is not an address"};
      return false;
    }
    for (char c : text)
      if ((unsigned char)c < 0x20 || c == 0x7f) {
        error = {"nav.control", "control bytes do not belong in addresses"};
        return false;
      }
    canon = text;
    return true;
  };
  return std::make_unique<rt::WebContents>(tab_id, tab_id, std::move(seams));
}
struct Tab {
  int id;
  std::string url, title = "New Tab";
  std::unique_ptr<rt::WebContents> web;
  rt::WebContents &nav() {
    if (!web) web = make_tab_contents(id);
    return *web;
  }
  bool can_back() const { return web && web->can_go_back(); }
  bool can_forward() const { return web && web->can_go_forward(); }
  std::vector<std::string> history_urls() const {
    std::vector<std::string> urls;
    if (web)
      for (const auto &entry : web->history()) urls.push_back(entry.url);
    return urls;
  }
  size_t history_index() const { return web && web->index() > 0 ? size_t(web->index()) : 0; }
  bool loading = false, started = false, recorded = false;
  bool pinned = false, muted = false;
  double scroll = 0, zoom = 1.0;
  uint64_t generation = 0;
  Json frame;
  std::string error;
  std::shared_ptr<Session> session;
  std::map<std::string, Image> images;
  size_t image_bytes = 0;
  DWORD frame_tick = 0; // when the first frame of this load arrived
  ULONGLONG last_used = 0; // when this tab was last the one in front, for sleeping idle tabs
  size_t paint_sig = 0; // signature of the last frame's paint list and scroll, to skip identical repaints
};
struct Hit {
  Rect rect;
  std::string action;
  int index = 0;
  std::string value;
};
#ifndef PSYCHO_VAULT_VERSION
#define PSYCHO_VAULT_VERSION "0.0.0-dev"
#endif
struct MenuLine {
  std::string label, action;
};
// S028 in production: which of my pages an address names is decided by the
// registry, not by string matching in the shell.
static const rt::InternalRegistry &internal_registry() {
  static const rt::InternalRegistry registry = rt::InternalRegistry::stock();
  return registry;
}
static bool app_ui_url(const std::string &text) {
  return rt::plain_internal_page(internal_registry(), text) == "newtab";
}
// psychovault://nettest and psychovault://version are served by the broker from
// the S038 diagnostics service (P1). Returns the page name, or "" for any other
// address, including a lookalike with a path, query or fragment.
static std::string diagnostics_page_name(const std::string &text) {
  const std::string name = rt::plain_internal_page(internal_registry(), text);
  const rt::InternalPage *page = name.empty() ? nullptr : internal_registry().find(name);
  return page && page->category == "diagnostics" ? name : std::string();
}
// The settings page is an engine-rendered page like the new-tab page (P3/S083): its
// markup is generated from the legacy inventory and its controls are not wired yet.
static bool settings_page_url(const std::string &text) {
  return rt::plain_internal_page(internal_registry(), text) == "settings";
}
// Every address the shell serves as an engine-rendered document.
// S081: the browser frame (rail, tabs, toolbar) is an engine-rendered page too.
static bool chrome_page_url(const std::string &text) {
  return rt::plain_internal_page(internal_registry(), text) == "chrome";
}
static bool engine_page_url(const std::string &text) {
  return app_ui_url(text) || settings_page_url(text) || chrome_page_url(text) ||
         !diagnostics_page_name(text).empty();
}
static const PVUiAsset *find_ui_asset(const std::string &name) {
  for (const PVUiAsset &asset : kPVUiAssets)
    if (name == asset.name) return &asset;
  return nullptr;
}
// Facts the broker supplies to psychovault://version; set once in wWinMain.
static pv::rt::VersionInfo g_version_info;
static bool g_version_known = false;
// S024 in production: the shell asks WindowRules where the first window may open
// and routes minimize/maximize/restore through it; the OS still owns dragging,
// DPI and fullscreen (see the plan for what stays open).
static std::string utf8_of_wide(std::wstring_view text);
static std::vector<rt::WindowRules::Monitor> enumerate_monitors() {
  std::vector<rt::WindowRules::Monitor> monitors;
  EnumDisplayMonitors(
      nullptr, nullptr,
      [](HMONITOR monitor, HDC, LPRECT, LPARAM data) -> BOOL {
        MONITORINFOEXW info{};
        info.cbSize = sizeof(info);
        if (!GetMonitorInfoW(monitor, &info)) return TRUE;
        auto &out = *reinterpret_cast<std::vector<rt::WindowRules::Monitor> *>(data);
        rt::WindowRules::Monitor entry;
        entry.id = utf8_of_wide(info.szDevice);
        entry.work_area = {info.rcWork.left, info.rcWork.top,
                           info.rcWork.right - info.rcWork.left,
                           info.rcWork.bottom - info.rcWork.top};
        // The primary monitor goes first so it wins ties.
        if (info.dwFlags & MONITORINFOF_PRIMARY) out.insert(out.begin(), std::move(entry));
        else out.push_back(std::move(entry));
        return TRUE;
      },
      reinterpret_cast<LPARAM>(&monitors));
  return monitors;
}
static std::filesystem::path shell_path(const std::string &text) {
  int n = MultiByteToWideChar(CP_UTF8, 0, text.c_str(), int(text.size()),
                              nullptr, 0);
  std::wstring wide(size_t(n > 0 ? n : 0), 0);
  if (n > 0)
    MultiByteToWideChar(CP_UTF8, 0, text.c_str(), int(text.size()), wide.data(), n);
  return std::filesystem::path(wide);
}
static std::wstring utf16_of(const std::string &text) {
  int n = MultiByteToWideChar(CP_UTF8, 0, text.c_str(), int(text.size()),
                              nullptr, 0);
  std::wstring out(size_t(n > 0 ? n : 0), 0);
  if (n > 0)
    MultiByteToWideChar(CP_UTF8, 0, text.c_str(), int(text.size()), out.data(), n);
  return out;
}
static std::string utf8_of_wide(std::wstring_view text) {
  if (text.empty()) return {};
  const int bytes = WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, text.data(),
                                        int(text.size()), nullptr, 0, nullptr, nullptr);
  if (bytes <= 0) return {};
  std::string out(size_t(bytes), '\0');
  if (!WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, text.data(), int(text.size()),
                           out.data(), bytes, nullptr, nullptr)) return {};
  return out;
}
// The result of my tunnel test. A worker fills it and the painting code reads it,
// so neither has to trust the other's lifetime.
struct TunnelTest {
  std::mutex mutex;
  std::atomic<bool> running{false};
  std::string text = "Not tested yet";
};

// What my blocker and my HTTPS policy did while this window has been open. A tab's
// worker thread adds to these and the painting code reads them, so they are atomic
// and neither thread owns the other.
struct BlockReport {
  std::atomic<uint64_t> blocked{0}, allowed{0}, evaluated{0}, upgraded{0}, mixed{0}, forms{0};
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
static void set_resize_media_environment(Json &message, double device_pixel_ratio,
                                         double scrollbar_width, bool reduce_motion,
                                         const std::string &theme, bool high_contrast) {
  message.set("devicePixelRatio", Json(std::clamp(device_pixel_ratio, 0.25, 8.0)));
  message.set("scrollbarWidth", Json(std::clamp(scrollbar_width, 0.0, 64.0)));
  message.set("prefersReducedMotion", Json(reduce_motion ? "reduce" : "no-preference"));
  message.set("prefersColorScheme", Json(theme == "light" ? "light" : "dark"));
  message.set("prefersContrast", Json(high_contrast ? "more" : "no-preference"));
  message.set("forcedColors", Json(high_contrast ? "active" : "none"));
}
struct Session {
  std::thread worker;
  HWND hwnd = nullptr;
  int tab = 0;
  uint64_t generation = 0;
  std::string url;
  std::string start_error;
  NetworkPolicy policy;
  // This tab's filter counters. Only this thread writes them, so the deltas below
  // are exact rather than a snapshot of a shared number.
  FilterCounters counts;
  std::shared_ptr<BlockReport> report;
  std::atomic<uint64_t> page_blocked{0};
  std::string last_block;
  CookieJar *jar = nullptr;
  Storage *storage = nullptr;
  std::mutex *profile_state_mutex = nullptr;
  rt::RendererProcessFarm *farm = nullptr;
  rt::RendererProcessClient::BridgeInvoker bridge_invoke;
  size_t renderer_slot = std::numeric_limits<size_t>::max();
  // The UI thread reads the lease slot to find the tab a dead renderer served;
  // renderer_slot itself is written by the session thread, so this is the copy
  // published for other threads.
  std::atomic<size_t> published_slot{std::numeric_limits<size_t>::max()};
  double width = 1000, height = 700, device_pixel_ratio = 1.0, scrollbar_width = 8.0;
  bool reduce_motion = false, high_contrast = false;
  std::string color_scheme = "dark";
  std::mutex mutex;
  std::condition_variable changed;
  std::deque<Json> outgoing;
  std::atomic<bool> stopping{false};
  std::atomic<bool> ready{false};
  std::atomic<bool> finished{false};
  std::atomic<bool> muted{false};
  // Telemetry for the smoke reports: renderer round trips and the time they took.
  std::atomic<uint64_t> exchanges{0}, exchange_ms{0};
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
  void deliver_renderer_reply(const rt::Envelope &reply);
  void run();
  // One fetch, then a note about it. The deltas are exact because only this thread
  // writes the counters, and the total lives in a block the window can read.
  void note(const FilterCounters &before, const Resource &result);
  uint64_t blocked_in_page() const {
    return page_blocked.load(std::memory_order_relaxed);
  }
  std::string last_block_reason() {
    std::lock_guard lock(mutex);
    return last_block;
  }
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
  } else if (event.tag == "clipboard") {
    update->clipboard = std::move(event.text);
    post_update(hwnd, WM_CLIPBOARD, update);
  } else {
    delete update;
    if (event.tag == "log")
      note(event.subsystem, event.text);
  }
}
void Session::deliver_renderer_reply(const rt::Envelope &reply) {
  if (const Json *images = reply.payload.find("images");
      images && images->type == Json::Type::Array) {
    for (const Json &item : images->array) {
      const double width = item.num("width", 0), height = item.num("height", 0);
      if (width <= 0 || height <= 0 || width > 16384 || height > 16384 ||
          width != double(int(width)) || height != double(int(height)))
        continue;
      try {
        auto image = std::make_shared<Image>();
        image->width = int(width);
        image->height = int(height);
        image->pixels = base64_decode(item.str("pixels_b64"), kMaxImageRawBytes);
        if (!image->valid()) continue;
        HostEvent event;
        event.tag = "image";
        event.url = item.str("url");
        event.image = std::move(image);
        deliver(std::move(event));
      } catch (const std::exception &) {
        note("renderer", "discarded malformed image data from process IPC");
      }
    }
  }
  if (const Json *frame = reply.payload.find("frame");
      frame && frame->type == Json::Type::Object) {
    HostEvent event;
    event.tag = "frame";
    event.frame = *frame;
    deliver(std::move(event));
  }
  const std::string clipboard = reply.payload.str("clipboard");
  if (!clipboard.empty()) {
    HostEvent event;
    event.tag = "clipboard";
    event.text = clipboard;
    deliver(std::move(event));
  }
  const std::string navigation = reply.payload.str("navigation");
  if (!navigation.empty()) {
    auto target = parse_url(navigation);
    if (!target || !target->network()) {
      note("renderer.security", "refused renderer-directed navigation outside HTTP(S)");
    } else {
      HostEvent event;
      event.tag = "navigate";
      event.url = navigation;
      deliver(std::move(event));
    }
  }
  const std::string url_change = reply.payload.str("url_change");
  if (!url_change.empty()) {
    HostEvent event;
    event.tag = "url";
    event.url = url_change;
    deliver(std::move(event));
  }
}

void Session::note(const FilterCounters &before, const Resource &result) {
  uint64_t blocked = counts.blocked - before.blocked;
  uint64_t allowed = counts.allowed_by_exception - before.allowed_by_exception;
  uint64_t evaluated = counts.evaluated - before.evaluated;
  if (blocked)
    page_blocked.fetch_add(blocked, std::memory_order_relaxed);
  if (report) {
    report->blocked.fetch_add(blocked, std::memory_order_relaxed);
    report->allowed.fetch_add(allowed, std::memory_order_relaxed);
    report->evaluated.fetch_add(evaluated, std::memory_order_relaxed);
    if (policy.https) {
      // The HTTPS policy is shared by every tab, so its numbers are already totals
      // and I copy them rather than adding.
      report->upgraded.store(policy.https->upgraded.load(), std::memory_order_relaxed);
      report->mixed.store(policy.https->blocked_mixed.load(), std::memory_order_relaxed);
      report->forms.store(policy.https->blocked_submit.load(), std::memory_order_relaxed);
    }
  }
  if (result.refused) {
    std::lock_guard lock(mutex);
    last_block = result.error;
  }
}
void Session::run() {
  struct Finished {
    Session *session;
    ~Finished() { session->finished = true; }
  } finished_guard{this};
  bool done_posted = false;
  auto fail = [&](const std::string &error) {
    start_error = error;
    ready = false;
    auto *update = make_update(tab, generation);
    update->error = error;
    post_update(hwnd, WM_FRAME, update);
    post_update(hwnd, WM_DONE, make_update(tab, generation));
    done_posted = true;
  };
  if (stopping) {
    post_update(hwnd, WM_DONE, make_update(tab, generation));
    done_posted = true;
    return;
  }
  auto initial_url = parse_url(url);
  const bool app_page = engine_page_url(url);
  if (!initial_url || (!initial_url->network() && !app_page)) {
    fail("Invalid URL");
    return;
  }
  if (!farm || !jar || !storage || !profile_state_mutex) {
    fail("Brokered renderer services are not available");
    return;
  }
  rt::Error renderer_error;
  if (!farm->acquire(renderer_slot, renderer_error)) {
    fail("Could not start tab renderer: " + renderer_error.message);
    return;
  }
  published_slot.store(renderer_slot);
  struct RendererLease {
    rt::RendererProcessFarm *farm;
    size_t slot;
    ~RendererLease() {
      if (farm && slot != std::numeric_limits<size_t>::max()) farm->release(slot);
    }
  } lease{farm, renderer_slot};
  if (stopping) {
    post_update(hwnd, WM_DONE, make_update(tab, generation));
    done_posted = true;
    return;
  }

  Url document_url = *initial_url;
  auto profile_now = [] {
    return std::chrono::duration<double>(
               std::chrono::system_clock::now().time_since_epoch()).count();
  };
  auto update_policy_origin = [&] {
    policy.top_level_host = document_url.host;
    policy.top_level_scheme = document_url.scheme;
  };
  auto attach_profile_state = [&](Resource &resource) {
    if (!resource.url.empty()) {
      auto response_url = parse_url(resource.url);
      if (response_url && response_url->network()) document_url = *response_url;
    }
    update_policy_origin();
    const double now = profile_now();
    if (jar) resource.broker_cookie_state = jar->renderer_state_for(document_url, now);
    if (storage) resource.broker_storage_state = storage->origin_state(document_url.origin());
  };
  Host::Fetcher fetcher = [&](const std::string &link, ResourceType type) {
    auto parsed = parse_url(link);
    if (const std::string diagnostics = diagnostics_page_name(link); !diagnostics.empty()) {
      // S038: no probe adapters are wired in P1, so nettest reports every stage
      // as unavailable. The real network stages arrive with P4.
      Resource resource;
      resource.url = parsed->href();
      resource.mime = "text/html; charset=utf-8";
      resource.ok = true;
      resource.status = 200;
      if (diagnostics == "nettest") {
        resource.text = rt::render_nettest_page(rt::DiagnosticsService({}).run_nettest());
      } else {
        rt::DiagnosticsService::Seams seams;
        if (g_version_known) {
          seams.version_info = [](rt::VersionInfo &out, rt::Error &) {
            out = g_version_info;
            return true;
          };
        }
        rt::DiagnosticsService versioned(std::move(seams));
        rt::VersionInfo info;
        rt::Error version_error;
        const bool known = versioned.version(info, version_error);
        resource.text = rt::render_version_page(known, info, version_error);
      }
      resource.broker_cookie_state = jar->renderer_state_for(document_url, profile_now());
      resource.broker_storage_state = storage->origin_state(document_url.origin());
      return resource;
    }
    if (parsed && parsed->scheme == "psychovault" &&
        (parsed->host == "newtab" || parsed->host == "settings" || parsed->host == "chrome") &&
        parsed->query.empty() && parsed->fragment.empty()) {
      Resource resource;
      resource.url = parsed->href();
      resource.ok = true;
      resource.status = 200;
      // The page itself at "/", and only the shared styles and scripts by name. A page's own
      // markup is never served under another page's origin.
      const bool root = parsed->path.empty() || parsed->path == "/";
      const std::string name = root ? (parsed->host == "newtab"   ? "index.html"
                                       : parsed->host == "chrome" ? "chrome.html"
                                                                  : "settings.html")
                                    : (parsed->path.size() > 1 ? parsed->path.substr(1) : std::string());
      const PVUiAsset *asset = name.empty() || (!root && name.size() > 5 && name.ends_with(".html"))
                                   ? nullptr : find_ui_asset(name);
      if (asset) {
        resource.mime = asset->mime;
        resource.text = asset->data;
      } else {
        resource.ok = false;
        resource.refused = true;
        resource.status = 404;
        resource.error = "internal.resource.unregistered";
      }
      resource.broker_cookie_state = jar->renderer_state_for(document_url, profile_now());
      resource.broker_storage_state = storage->origin_state(document_url.origin());
      return resource;
    }
    if (!parsed || !parsed->network()) {
      Resource resource;
      resource.url = link;
      resource.ok = false;
      resource.refused = true;
      resource.error = "Invalid resource URL";
      return resource;
    }
    policy.type = type;
    FilterCounters before = counts;
    Resource result = fetch_url(*parsed, policy);
    note(before, result);
    if (type == ResourceType::Document)
      attach_profile_state(result);
    else {
      // All renderer snapshots are scoped to the current document origin, not
      // the origin of a cross-site subresource.
      update_policy_origin();
      if (jar) result.broker_cookie_state = jar->renderer_state_for(document_url,
                                                                     profile_now());
      if (storage)
        result.broker_storage_state = storage->origin_state(document_url.origin());
    }
    return result;
  };
  Host::Poster poster = [&](const std::string &link, const std::string &body,
                            const std::string &content_type) {
    Resource result;
    auto parsed = parse_url(link);
    if (!parsed || !parsed->network()) {
      result.url = link;
      result.ok = false;
      result.refused = true;
      result.error = "Invalid form URL";
      return result;
    }
    RequestOptions options;
    options.method = "POST";
    options.body = body;
    options.content_type = content_type;
    policy.type = ResourceType::Document;
    FilterCounters before = counts;
    result = fetch_url(*parsed, policy, options, 20000, 8u * 1024u * 1024u);
    note(before, result);
    attach_profile_state(result);
    return result;
  };

  auto exchange = [&](const Json *command, rt::Envelope &reply) {
    std::unique_lock<std::mutex> state_lock;
    if (profile_state_mutex) state_lock = std::unique_lock<std::mutex>(*profile_state_mutex);
    const double now = profile_now();
    Json cookies = jar->renderer_state_for(document_url, now);
    Json local = storage->origin_state(document_url.origin());
    bool ok = false;
    if (command)
      ok = farm->input(renderer_slot, *command, fetcher, reply, renderer_error,
                       std::move(cookies), std::move(local), poster, bridge_invoke);
    else
      {
        MediaEnvironment environment;
        environment.viewport_width = width;
        environment.viewport_height = height;
        environment.device_pixel_ratio = device_pixel_ratio;
        environment.scrollbar_width = scrollbar_width;
        environment.prefers_reduced_motion = reduce_motion ? "reduce" : "no-preference";
        environment.prefers_color_scheme = color_scheme;
        environment.prefers_contrast = high_contrast ? "more" : "no-preference";
        environment.forced_colors = high_contrast ? "active" : "none";
        ok = farm->navigate(renderer_slot, url, fetcher, reply, renderer_error,
                            std::move(cookies), std::move(local), poster, bridge_invoke,
                            environment);
      }
    if (!ok) return false;
    if (const Json *frame = reply.payload.find("frame");
        frame && frame->type == Json::Type::Object) {
      auto frame_url = parse_url(frame->str("url"));
      if (frame_url && frame_url->network() &&
          frame_url->origin() == document_url.origin())
        document_url = *frame_url;
    }
    if (const Json *state = reply.payload.find("cookie_state"))
      jar->merge_renderer_state(document_url, *state, profile_now());
    if (const Json *state = reply.payload.find("storage_state"))
      storage->replace_origin_state(document_url.origin(), *state);
    return true;
  };

  rt::Envelope reply;
  if (!exchange(nullptr, reply)) {
    fail("Renderer navigation IPC failed: " + renderer_error.message);
    return;
  }
  if (!reply.payload.flag("ok")) {
    fail(reply.payload.str("detail", "Navigation failed"));
    return;
  }
  ready = reply.payload.find("frame") != nullptr;
  if (!ready) {
    fail("Renderer returned no page frame");
    return;
  }
  deliver_renderer_reply(reply);

  // Match the browser viewport before dispatching page input. Resize remains a
  // normal typed renderer message, not a direct call into an engine object.
  Json resize = Json::make_object();
  resize.set("tag", Json("resize"));
  resize.set("width", Json(width));
  resize.set("height", Json(height));
  set_resize_media_environment(resize, device_pixel_ratio, scrollbar_width, reduce_motion,
                                color_scheme, high_contrast);
  if (!exchange(&resize, reply)) {
    fail("Renderer resize IPC failed: " + renderer_error.message);
    return;
  }
  if (!reply.payload.flag("ok")) {
    fail("Renderer refused initial resize: " +
         reply.payload.str("detail", "unknown renderer resize error"));
    return;
  }
  deliver_renderer_reply(reply);

  std::unique_lock lock(mutex);
  auto next_pump = std::chrono::steady_clock::now() + std::chrono::milliseconds(50);
  while (!stopping) {
    if (outgoing.empty())
      changed.wait_until(lock, next_pump,
                         [this] { return stopping.load() || !outgoing.empty(); });
    if (stopping) break;
    Json command;
    if (!outgoing.empty()) {
      command = std::move(outgoing.front());
      outgoing.pop_front();
    } else {
      command = Json::make_object();
      command.set("tag", Json("pump"));
      next_pump = std::chrono::steady_clock::now() + std::chrono::milliseconds(50);
    }
    if (command.str("tag") == "resize") {
      width = command.num("width", width);
      height = command.num("height", height);
    }
    lock.unlock();
    const auto exchange_started = std::chrono::steady_clock::now();
    if (!exchange(&command, reply)) {
      fail("Renderer input IPC failed: " + renderer_error.message);
      break;
    }
    exchanges.fetch_add(1);
    exchange_ms.fetch_add(uint64_t(std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::steady_clock::now() - exchange_started).count()));
    if (!reply.payload.flag("ok")) {
      fail("Renderer refused page input: " +
           reply.payload.str("detail", "unknown renderer input error"));
      break;
    }
    deliver_renderer_reply(reply);
    if (!reply.payload.str("navigation").empty()) {
      stopping = true;
      changed.notify_all();
    }
    lock.lock();
  }
  if (!done_posted) post_update(hwnd, WM_DONE, make_update(tab, generation));
}
static void capture(HWND hwnd, const std::string &path);
// Telemetry the smoke reports carry: how often the window repainted, ticked, and got a frame page.
static std::atomic<uint64_t> g_timer_ticks{0}, g_paints{0}, g_chrome_frames{0}, g_paint_us{0}, g_draw_us{0};
// The settings snapshot the session threads read. Only the UI thread writes it (after every applied
// change), so a settings page reads values without touching the profile from another thread.
struct SettingsHub {
  std::mutex mutex;
  Json snapshot = Json::make_object();
  // Read-only facts the settings page asks for, refreshed by the window every couple of seconds.
  Json usage = Json::make_object();
  std::string diagnostics, update_status;
  Json extensions = Json::make_array(), proxy = Json::make_object();
  std::string extension_error;
  // What the last backup, restore or import did; the page reads it through vault:status.
  std::string vault_notice;
};
struct Browser {
  HWND hwnd = nullptr;
  Profile profile;
  std::vector<Tab> tabs;
  std::vector<std::shared_ptr<Session>> retired_sessions;
  int active = 0;
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
  std::mutex profile_state_mutex;
  // The HTTPS policy, the filter rules and the running totals. A tab holds a share
  // of each, so a tab worker can never outlive what it points at.
  std::shared_ptr<HttpsPolicy> https = std::make_shared<HttpsPolicy>();
  std::shared_ptr<FilterList> filters = std::make_shared<FilterList>();
  std::shared_ptr<BlockReport> report = std::make_shared<BlockReport>();
  // What my confinement layer managed to do. It is reported in the settings page in
  // the same words the platform function gave me, not as a tick.
  ConfinementStatus confinement;
  std::string confinement_error;
  std::shared_ptr<TunnelTest> tunnel_test = std::make_shared<TunnelTest>();
  // Find in page
  bool find_open = false;
  std::string find_text;
  // The bookmarks and history panels share one filter box.
  bool panel_focus = false;
  std::string panel_search;
  size_t panel_search_caret = 0;
  std::vector<Rect> find_marks;
  int find_at = 0;
  std::string extension_error, download_error;
  int settings_scroll = 0;
  size_t address_caret = 0, home_search_caret = 0;
  wchar_t high_surrogate = 0;
  int settings_category = 0;
  std::vector<Hit> hits;
  Rect content{0, 0, 1000, 700};
  // S081: with --chrome the frame around the page is itself an engine page (psychovault://chrome),
  // rendered by its own sandboxed renderer. It owns the window; its middle is transparent and the
  // active tab's page is drawn under it, in `content`. Its buttons reach the shell only through the
  // curated bridge (tab/nav/win methods), never through shell-side hit rectangles.
  static constexpr int kChromeTab = -2;
  static constexpr uint64_t kChromeGeneration = 1;
  bool chrome_enabled = false, chrome_started = false, chrome_focus = false, chrome_overlay = false;
  bool chrome_pointer_down = false, chrome_pointer_inside = false;
  std::shared_ptr<Session> chrome_session;
  Json chrome_frame;
  std::string chrome_error, chrome_state_sent;
  size_t chrome_paint_sig = 0;
  DWORD chrome_state_tick = 0, chrome_frame_tick = 0;
  std::map<std::string, Image> chrome_images;
  bool smoke_chrome = false, smoke_settings = false;
  int smoke_settings_stage = 0;
  bool smoke_settings_before = false;
  DWORD smoke_settings_tick = 0;
  std::string smoke_settings_note;
  std::shared_ptr<SettingsHub> settings_hub = std::make_shared<SettingsHub>();
  std::unique_ptr<rt::SettingsService> settings_service;
  void publish_settings() {
    if (!settings_service) settings_service = std::make_unique<rt::SettingsService>(profile);
    Json snapshot = settings_service->snapshot();
    Json usage = build_usage();
    std::string report = build_diagnostics();
    std::string update_text;
    {
      std::lock_guard update_lock(update_mutex);
      update_text = update_status;
    }
    std::lock_guard lock(settings_hub->mutex);
    settings_hub->snapshot = std::move(snapshot);
    settings_hub->usage = std::move(usage);
    settings_hub->extensions = build_extensions();
    settings_hub->proxy = build_proxy();
    settings_hub->extension_error = extension_error;
    if (const Json *values = settings_hub->snapshot.find("values"))
      if (const Json *services = values->find("services")) services_cache = *services;
    settings_hub->diagnostics = std::move(report);
    settings_hub->update_status = std::move(update_text);
  }
  // What the browser keeps, per kind, with a count: the rows of Settings > Privacy > data table.
  // The installed extensions as the settings page lists them. Nothing from a manifest is shown beyond its
  // name, version and whether it loaded.
  Json build_extensions() {
    Json list = Json::make_array();
    for (const ExtensionRecord &record : extensions.list()) {
      Json item = Json::make_object();
      item.set("id", record.manifest.id);
      item.set("name", record.manifest.name.empty() ? record.manifest.id : record.manifest.name.substr(0, 80));
      item.set("version", record.manifest.version.substr(0, 32));
      item.set("enabled", record.enabled);
      item.set("bundled", record.bundled);
      item.set("error", (record.install_error.empty() ? extensions.worker_error(record.manifest.id) : record.install_error).substr(0, 300));
      list.array.push_back(std::move(item));
    }
    return list;
  }
  Json build_proxy() {
    Json out = Json::make_object();
    out.set("mode", profile.proxy.mode_name());
    out.set("active", profile.proxy.active());
    return out;
  }
  Json services_cache = Json::make_array();
  Json build_usage() {
    Json rows = Json::make_array();
    auto row = [&](const char *id, const char *label, size_t count, const char *unit) {
      Json item = Json::make_object();
      item.set("id", id);
      item.set("label", label);
      item.set("count", double(count));
      item.set("unit", unit);
      rows.array.push_back(std::move(item));
    };
    row("history", "Browsing history", profile.history.size(), "pages");
    row("cookies", "Cookies", cookies.size(), "cookies");
    {
      std::lock_guard<std::mutex> state_lock(profile_state_mutex);
      row("siteData", "Site storage", local_storage.origins(), "sites");
    }
    row("tabs", "Saved tabs", profile.session_tabs.size(), "tabs");
    row("downloads", "Download list", downloads.snapshot().size(), "items");
    Json out = Json::make_object();
    out.set("rows", std::move(rows));
    return out;
  }
  // The troubleshooting report: versions and on/off facts only, no paths, URLs, page content or secrets.
  std::string build_diagnostics() {
    std::string text;
    auto line = [&](const std::string &key, const std::string &value) { text += key + ": " + value + "\n"; };
    line("PsychoVault", g_version_info.runtime_version);
    line("Engine", g_version_info.engine_version);
    line("Profile", g_version_info.profile);
    line("Window", private_mode ? "private" : "normal");
    line("Tabs", std::to_string(tabs.size()));
    line("Blocked requests", std::to_string(report ? report->blocked.load() : 0));
    line("Ad and tracker blocking", profile.block_ads ? "on" : "off");
    line("Vault", vault_service ? (vault_service->status().flag("exists") ? (vault_service->status().flag("locked") ? "locked" : "unlocked") : "none") : "unavailable");
    line("Proxy", profile.proxy.active() ? "set" : "none");
    line("History recording", profile.remember_history ? "on" : "off");
    for (const std::string &flag : g_version_info.flags) line("Flag", flag);
    return text;
  }
  // Tasks the settings page queues for the window thread. Returns true when the method was one of them.
  bool settings_task(const BridgeTask &task) {
    const std::string &method = task.method;
    if (method == "data:clear") {
      const std::string what = task.payload.str("what");
      if (what == "history") profile.clear_history();
      else if (what == "cookies") { cookies.clear(); profile.cookies = cookies.to_json(); }
      else if (what == "siteData") { std::lock_guard<std::mutex> state_lock(profile_state_mutex); local_storage.clear_all(); }
      else if (what == "tabs") { profile.session_tabs.clear(); profile.session_pinned.clear(); profile.session_muted.clear(); }
      else if (what == "downloads") downloads.clear_finished();
      else return true;
      save_profile();
      publish_settings();
      status = "Cleared " + what;
      InvalidateRect(hwnd, nullptr, FALSE);
      return true;
    }
    if (method == "diagnostics:copy") {
      write_clipboard(build_diagnostics());
      status = "The report is on the clipboard";
      return true;
    }
    if (method == "app:openlog") {
      profile.append_error_log("log opened from settings");
      ShellExecuteW(hwnd, L"open", utf16_of(profile.directory + "/owned-errors.log").c_str(), nullptr, nullptr, SW_SHOWNORMAL);
      return true;
    }
    if (method == "app:relaunch") {
      // A helper waits for this window to finish closing, then starts a new copy with the same arguments.
      std::wstring command = L"cmd.exe /c \"ping -n 3 127.0.0.1 >nul & start \"\" ";
      command += GetCommandLineW();
      command += L"\"";
      STARTUPINFOW startup{};
      startup.cb = sizeof(startup);
      PROCESS_INFORMATION info{};
      if (CreateProcessW(nullptr, command.data(), nullptr, nullptr, FALSE, CREATE_NO_WINDOW, nullptr, nullptr, &startup, &info)) {
        CloseHandle(info.hProcess);
        CloseHandle(info.hThread);
        PostMessageW(hwnd, WM_CLOSE, 0, 0);
      } else {
        status = "I could not restart";
      }
      return true;
    }
    if (method == "updates:check") {
      check_for_update();
      return true;
    }
    if (method == "extensions:toggle" || method == "extensions:remove" || method == "extensions:addfolder" ||
        method == "extensions:restore") {
      if (!extension_workers_allowed()) {
        extension_error = "Extensions do not run in a private window";
        return true;
      }
      std::string error;
      const std::string id = task.payload.str("id");
      if (method == "extensions:toggle") {
        const bool enable = task.payload.flag("enabled");
        if (!extensions.set_enabled(id, enable, error)) extension_error = error;
        else {
          auto &off = profile.disabled_extensions;
          off.erase(std::remove(off.begin(), off.end(), id), off.end());
          if (!enable) { off.push_back(id); extensions.stop_worker(id); }
          else if (!extensions.start_worker(id, error)) extension_error = error;
        }
      } else if (method == "extensions:remove") {
        const ExtensionRecord *record = extensions.find(id);
        const std::string directory = record ? record->directory : std::string();
        if (!extensions.remove(id, error)) extension_error = error;
        else {
          auto &dirs = profile.extension_directories;
          dirs.erase(std::remove(dirs.begin(), dirs.end(), directory), dirs.end());
        }
      } else if (method == "extensions:addfolder") {
        const std::string folder = pick_folder(hwnd);
        if (!folder.empty()) {
          if (!extensions.install_directory(folder, error)) extension_error = error;
          else {
            profile.extension_directories.push_back(folder);
            for (const ExtensionRecord &record : extensions.list())
              if (record.directory == folder && record.enabled && !extensions.start_worker(record.manifest.id, error))
                extension_error = error;
          }
        }
      } else {
        load_bundled_extensions();
      }
      if (error.empty() && method != "extensions:restore") extension_error.clear();
      save_profile();
      publish_settings();
      InvalidateRect(hwnd, nullptr, FALSE);
      return true;
    }
    if (method == "downloads:pickdir") {
      const std::string folder = pick_folder(hwnd);
      if (!folder.empty()) {
        apply_setting_value("downloadDir", Json(folder));
      }
      return true;
    }
    return false;
  }
  // Applies one setting the window itself decided on (a picked folder), the way a page's change is applied.
  void apply_setting_value(const std::string &key, const Json &value) {
    if (!settings_service) publish_settings();
    rt::Error error;
    if (!settings_service->set(key, value, error)) {
      status = error.message;
      return;
    }
    save_profile();
    publish_settings();
    status = "Saved " + key;
    InvalidateRect(hwnd, nullptr, FALSE);
  }
  // Applies one settings-page change on the UI thread. The change was validated on the session
  // thread; the profile may still refuse it (a folder that cannot be created), which is reported.
  void apply_setting(const BridgeTask &task) {
    if (!settings_service) publish_settings();
    const std::string key = task.payload.str("key");
    auto value = Json::parse(task.payload.str("value"), 4096);
    rt::Error error;
    if (!value || !settings_service->set(key, *value, error)) {
      status = error.message.empty() ? "Setting refused" : error.message;
      publish_settings();
      InvalidateRect(hwnd, nullptr, FALSE);
      return;
    }
    if (!save_profile() && profile_error.empty()) profile_error = profile.error;
    publish_settings();
    status = "Saved " + key;
    InvalidateRect(hwnd, nullptr, FALSE);
  }
  int smoke_chrome_stage = 0;
  DWORD smoke_chrome_tick = 0;
  std::string smoke_chrome_note;
  size_t smoke_chrome_tabs_before = 0;
  // A dialog the frame page draws over everything: modal, one at a time, answered through the bridge.
  struct FrameDialog {
    int id = 0;
    std::string kind, title, message, value;
    std::vector<std::string> buttons;
    std::function<void(int, const std::string &)> done;
  };
  std::deque<FrameDialog> frame_dialogs;
  int next_frame_dialog = 1;
  bool chrome_captures() const { return chrome_overlay || !frame_dialogs.empty() || palette_open; }
  // The find bar floats over the page's top-right corner (16px in, 8px down, 380x44); a press on it
  // belongs to the frame even though the point is inside the page area.
  Rect find_bar_rect() const { return {content.x + content.w - 396, content.y + 8, 380, 44}; }
  bool over_chrome(int x, int y) const {
    return !content.contains(x, y) || chrome_captures() || (find_open && find_bar_rect().contains(x, y));
  }
  void show_frame_dialog(const std::string &kind, const std::string &title, const std::string &message,
                         std::vector<std::string> buttons, std::function<void(int, const std::string &)> done,
                         const std::string &value = {}) {
    if (frame_dialogs.size() >= 8 || buttons.empty() || buttons.size() > 4) return;
    FrameDialog dialog;
    dialog.id = next_frame_dialog++;
    dialog.kind = kind;
    dialog.title = title.substr(0, 120);
    dialog.message = message.substr(0, 2000);
    dialog.value = value.substr(0, 512);
    dialog.buttons = std::move(buttons);
    dialog.done = std::move(done);
    const bool first = frame_dialogs.empty();
    frame_dialogs.push_back(std::move(dialog));
    if (first) {
      rt::Error focus_error;
      if (focus_service) focus_service->modal_open(kWindowId, focus_error);
      chrome_focus = true;
      chrome_state_sent.clear();
    }
    InvalidateRect(hwnd, nullptr, FALSE);
  }
  void answer_frame_dialog(int id, int button, const std::string &value) {
    if (frame_dialogs.empty() || frame_dialogs.front().id != id ||
        button >= int(frame_dialogs.front().buttons.size()))
      return;
    FrameDialog dialog = std::move(frame_dialogs.front());
    frame_dialogs.pop_front();
    if (frame_dialogs.empty()) {
      rt::Error focus_error;
      if (focus_service) focus_service->modal_close(kWindowId, focus_error);
    }
    chrome_state_sent.clear();
    if (dialog.done) dialog.done(button, value);
    InvalidateRect(hwnd, nullptr, FALSE);
  }
  Session *key_target() {
    return chrome_enabled && chrome_focus && chrome_session ? chrome_session.get()
                                                           : current().session.get();
  }
  void enable_chrome() {
    chrome_enabled = true;
    layout_content();
  }
  // The side panel (history, bookmarks, downloads) takes 330px from the page when it is open.
  // The icon rail is 44px wide unless Settings > Appearance > Hide the sidebar collapses it.
  int rail_width() const { return profile.hide_sidebar ? 0 : 44; }
  bool last_rail_hidden = false;
  // The frame's other panels ("extensions", "performance", "workspaces"); empty when none is open.
  std::string panel_other;
  bool chrome_panel_open() const { return chrome_enabled && (panel_bookmarks || panel_history || panel_downloads || !panel_other.empty()); }
  bool last_panel_open = false;
  void layout_content() {
    const int panel_width = chrome_panel_open() ? 330 : 0;
    const int rail = rail_width();
    content = chrome_enabled ? Rect{double(rail), 84, double(std::max(0, width - rail - panel_width)), double(std::max(0, height - 84 - 22))}
              : app_ui_slice ? Rect{0, 0, double(width), double(height)}
                             : Rect{44, 84, double(width - 44), double(height - 106)};
  }
  bool page_pointer_inside = false, page_pointer_down = false, page_composing = false;
  bool document_scroll_drag = false;
  double document_scroll_drag_start_y = 0, document_scroll_drag_start = 0;
  int width = 1440, height = 940;
  std::unique_ptr<Canvas> paint_surface;
  uint64_t compositor_presented_frames = 0, compositor_dropped_frames = 0;
  std::chrono::steady_clock::time_point compositor_metrics_at{};
  // S045 architecture slice: the browser's page surface is the only drawn
  // interface; the native shell keeps windowing, input dispatch and presentation.
  bool app_ui_slice = true;
  bool private_mode = false;
  std::string capture_path;
  std::string smoke_bridge_url;
  int smoke_bridge_stage = 0;
  int smoke_bridge_source_tab = -1;
  DWORD smoke_bridge_stage_tick = 0;
  bool smoke = false, expect_image = false, smoke_tabs = false, smoke_tabs_done = false, smoke_services = false;
  DWORD start_tick = 0;
  // S037 in production: the private-session policy is consulted before any
  // extension starts. Private windows run none unless the user flips the switch
  // (there is no UI for that yet, so today a private window runs none).
  rt::PrivateSession private_session;
  bool extension_workers_allowed() const {
    return !private_mode || private_session.may_run_extensions();
  }
  explicit Browser(bool private_mode = false, const std::string &directory = {})
      : private_mode(private_mode) {
    {
      rt::Error attach_error;
      tab_model.attach_window(kWindowId, attach_error);
    }
    profile =
        Profile::open(directory.empty() ? default_profile_directory() : directory, private_mode);
    profile_error = profile.error;
    {
      // The vault's file sits in the profile; the service does the reading, writing and locking.
      const std::string vault_file = profile.directory + "/vault.bin";
      rt::VaultService::Seams seams;
      seams.read = [vault_file](std::string &blob) {
        const DWORD attributes = GetFileAttributesW(utf16_of(vault_file).c_str());
        if (attributes == INVALID_FILE_ATTRIBUTES || (attributes & FILE_ATTRIBUTE_DIRECTORY)) return false;
        try { blob = read_text_file(vault_file); } catch (const std::exception &) { return false; }
        return true;
      };
      seams.write = [vault_file](const std::string &blob, std::string &error) {
        return write_file_atomic(vault_file, blob, error);
      };
      seams.now_ms = [] { return uint64_t(GetTickCount64()); };
      vault_service = std::make_shared<rt::VaultService>(std::move(seams));
    }
    spell.builtin_enabled = profile.spell_builtin;
    spell.load_builtin_embedded();
    spell.set_users(profile.user_words);
    spell_reload_source();
    cookies.from_json(profile.cookies);
    local_storage.from_json(profile.local_storage);
    load_security_state();
    // A memory cap and a kill-on-unhandled-exception rule for my own process. Page
    // code shares this process, so this limits a runaway page; it does not isolate
    // one, and the panel says so.
    std::string confinement_failure;
    if (!confine_process(uint64_t(profile.memory_limit_mb) * 1024 * 1024,
                         uint32_t(profile.cpu_limit), confinement_failure))
      confinement_error = confinement_failure;
    confinement = confinement_status();
    if (profile.download_directory.empty())
      profile.download_directory = default_download_directory();
    downloads.set_directory(profile.download_directory);
    if (!private_mode) downloads.load(profile.directory);
    install_extension_effects();
    load_workspaces_state();
    crashed_last = !private_mode && profile.crashed_last_run();
    profile.mark_running();
    reload_log_cache();
    // My launch check is cheap and off-thread; the banner only shows on news.
    if (!private_mode)
      check_for_update();
    if (extension_workers_allowed()) {
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
  // A string of secrets is zeroed before it goes out of scope anywhere I can
  // see it, so a passphrase or a copied password does not sit readable in RAM.
  static void scrub(std::string &text) {
    if (!text.empty())
      std::fill(text.begin(), text.end(), '\0');
    text.clear();
  }
  std::string spell_extra_path() const { return profile.directory + "/owned-spell-extra.txt"; }
  void spell_reload_source() {
    try {
      spell.replace_extra(read_text_file(spell_extra_path()));
    } catch (const std::exception &) {
      spell.replace_extra("");
    }
  }
  void spell_reload_users() { spell.set_users(profile.user_words); }

  bool save_profile() {
    profile.cookies = cookies.to_json();
    {
      std::lock_guard<std::mutex> state_lock(profile_state_mutex);
      profile.local_storage = local_storage.to_json();
    }
    profile.hsts = https->to_json().dump();
    return profile.save();
  }
  // My rules and my HTTPS store, rebuilt from the profile at startup. A saved store
  // is pruned on the way in, because an entry that has expired is not a promise any
  // more and honouring it would be worse than forgetting it.
  void load_security_state() {
    std::string error;
    filters->clear();
    if (!filters->parse(builtin_filter_text(), error) && profile_error.empty())
      profile_error = "my starter filter list: " + error;
    if (!profile.filter_list_path.empty()) {
      std::string list_error;
      if (filters->load_file(profile.filter_list_path, list_error) == 0 && !list_error.empty() &&
          profile_error.empty())
        profile_error = "filter list: " + list_error;
    }
    if (auto parsed = Json::parse(profile.hsts)) https->from_json(*parsed);
    https->prune(int64_t(std::time(nullptr)) * 1000);
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
    out.block_autoplay = profile.block_autoplay;
    out.blocked_hosts = profile.blocked_hosts;
    out.allowed_hosts = profile.block_allow_sites;
    out.https = https.get();
    out.filters = filters.get();
    out.https_only = profile.https_only;
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
  // One real request through the server I have configured, on a worker thread, and
  // the answer kept in words. A tunnel I never dialled is not a connection.
  void start_tunnel_test() {
    if (tunnel_test->running.exchange(true))
      return;
    {
      std::lock_guard lock(tunnel_test->mutex);
      tunnel_test->text = profile.proxy.active()
                              ? "Dialling " + profile.proxy.single.authority() + " ..."
                              : "No tunnel server is set, so there is nothing to dial";
    }
    NetworkPolicy probe_policy = policy();
    auto state = tunnel_test;
    HWND window = hwnd;
    std::thread([probe_policy, state, window]() {
      TunnelProbe probe = probe_tunnel(probe_policy, "https://api.github.com/zen", 20000);
      std::string text;
      if (probe.ok)
        text = "Connected - " + probe.detail;
      else
        text = "Not connected - " + probe.detail;
      {
        std::lock_guard lock(state->mutex);
        state->text = text;
      }
      state->running.store(false);
      // A repaint is all the window needs to pick the new sentence up, and asking
      // for one from another thread cannot free anything the window still owns.
      if (window) InvalidateRect(window, nullptr, FALSE);
    }).detach();
  }
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
    stop_all_sessions();
    profile.clear_running_marker();
    if (profile.clear_on_exit && !private_mode) {
      // "Clear on exit" is a choice: "cache" empties what sites stored (their local storage); "all" also
      // clears history, cookies and the finished downloads.
      const std::string mode = profile.extra_settings.str("clearOnExit", "all");
      { std::lock_guard<std::mutex> state_lock(profile_state_mutex); local_storage.clear_all(); }
      if (mode != "cache") {
        profile.clear_history();
        cookies.clear();
        profile.cookies = cookies.to_json();
        downloads.clear_finished();
      }
      save_profile();
    } else if (!private_mode && profile.history_dirty) {
      profile.history_dirty = false;
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
  // S040/S039 in production. Logs never carry URLs or titles (that would be a
  // second browsing history on disk), and a private session writes nothing.
  std::unique_ptr<rt::StructuredLog> runtime_log;
  std::unique_ptr<rt::CrashService> crash_service;
  // S031 in production: page-originated clipboard writes and message dialogs go
  // through the broker's service law (size cap, writer record, one modal per
  // window) before they reach the OS. Failing closed: no service, no effect.
  std::unique_ptr<rt::ClipboardService> clipboard_service;
  std::unique_ptr<rt::DialogService> dialog_service;
  std::string last_recovery_failure;
  void log_event(const char *service, rt::LogLevel level, const std::string &event,
                 const rt::StructuredLog::Fields &fields = {}) {
    if (!runtime_log) return;
    rt::Error ignored;
    runtime_log->write(service, level, event, fields, ignored);
  }
  void init_runtime_services() {
    if (!private_mode)
      runtime_log = std::make_unique<rt::StructuredLog>(profile.directory + "/logs");
    rt::CrashService::Seams seams;
    seams.now_ms = [] {
      return int64_t(std::chrono::duration_cast<std::chrono::milliseconds>(
                         std::chrono::system_clock::now().time_since_epoch()).count());
    };
    seams.persist_record = [this](const rt::CrashRecord &record, std::string &error) {
      if (!runtime_log) return true; // private session: the record stays in memory
      rt::Error write_error;
      const bool wrote = runtime_log->write(
          "crash", rt::LogLevel::Error, "renderer.failed",
          {{"id", std::to_string(record.id)},
           {"renderer", std::to_string(record.renderer_id)},
           {"tab", std::to_string(record.tab_id)},
           {"kind", record.kind == rt::RendererFailureKind::Hang ? "hang" : "crash"},
           {"reason", record.reason},
           {"state", std::to_string(int(record.state))},
           {"attempts", std::to_string(record.restart_attempts)}},
          write_error);
      if (!wrote) error = write_error.message;
      return wrote;
    };
    seams.show_crash_page = [this](int tab_id, const rt::CrashRecord &, std::string &) {
      Tab *tab = find_tab(tab_id);
      if (!tab) return false;
      tab->loading = false;
      tab->error = "This tab's renderer stopped unexpectedly.";
      if (tab == &current()) status = tab->error;
      InvalidateRect(hwnd, nullptr, FALSE);
      return true;
    };
    seams.restart_tab = [this](int tab_id, const std::string &url, std::string &) {
      for (size_t i = 0; i < tabs.size(); ++i) {
        if (tabs[i].id != tab_id) continue;
        navigate_in(int(i), url, false);
        return true;
      }
      return false;
    };
    bind_shortcuts();
    init_ime();
    focus_service = std::make_unique<rt::FocusService>(rt::FocusService::Seams{});
    {
      rt::Error focus_error;
      focus_service->window_opened(kWindowId, focus_error);
    }
    sync_focus();
    crash_service = std::make_unique<rt::CrashService>(std::move(seams));
    rt::ClipboardService::Seams clip_seams;
    clip_seams.apply = [this](const std::string &text) { write_clipboard(text); };
    clipboard_service = std::make_unique<rt::ClipboardService>(std::move(clip_seams));
    rt::DialogService::Seams dialog_seams;
    dialog_seams.show = [this](const rt::DialogRequest &request) {
      rt::DialogResult result;
      int n = MultiByteToWideChar(CP_UTF8, 0, request.message.c_str(),
                                  int(request.message.size()), nullptr, 0);
      std::wstring text(size_t(n > 0 ? n : 0), L'\0');
      if (n > 0)
        MultiByteToWideChar(CP_UTF8, 0, request.message.c_str(),
                            int(request.message.size()), text.data(), n);
      MessageBoxW(hwnd, text.c_str(), L"PsychoVault page", MB_OK);
      result.cancelled = false;
      result.button = 0;
      return result;
    };
    dialog_service = std::make_unique<rt::DialogService>(std::move(dialog_seams));
  }
  void page_clipboard_write(const Tab &tab, const std::string &text) {
    if (!clipboard_service) return;
    auto parsed = parse_url(tab.url);
    rt::Error error;
    if (!clipboard_service->write_text(parsed ? parsed->origin() : tab.url, text, error))
      log_event("clipboard", rt::LogLevel::Warn, "write.refused", {{"code", error.code}});
  }
  void page_message_dialog(const std::string &message) {
    if (chrome_enabled && chrome_session) {
      // The frame draws the page's alert itself, over everything, and the focus service knows a modal is up.
      show_frame_dialog("alert", "This page says", message, {"OK"}, nullptr);
      return;
    }
    if (!dialog_service) return;
    rt::DialogRequest request;
    request.kind = rt::DialogRequest::Kind::Message;
    request.window_id = 1; // one browser window today; S048 gives windows real ids
    request.message = message;
    request.from_shell = false;
    rt::DialogResult result;
    rt::Error error;
    rt::Error focus_error;
    const bool modal = focus_service && focus_service->modal_open(kWindowId, focus_error);
    if (!dialog_service->run(request, result, error)) {
      ++dialog_refusals;
      log_event("dialog", rt::LogLevel::Warn, "message.refused", {{"code", error.code}});
    }
    if (modal) focus_service->modal_close(kWindowId, focus_error);
  }
  // A leased renderer exited and the farm replaced it. The page it held is gone,
  // so the tab goes through the crash service: record, message, bounded retry.
  void recover_failed_renderers() {
    if (!renderer_farm || !crash_service) return;
    // A replacement counts as stable once it has shown a page for five seconds
    // without failing; rendering a frame alone would let a page that kills its
    // renderer right after load restart forever. (WM_DONE only fires when a
    // session ends, so it cannot be the signal.)
    const DWORD now = GetTickCount();
    for (const auto &tab : tabs)
      if (tab.error.empty() && tab.frame.type == Json::Type::Object && tab.frame_tick != 0 &&
          now - tab.frame_tick >= 5000 && crash_service->restarts_for_tab(tab.id) > 0)
        crash_service->renderer_stable(tab.id);
    for (size_t slot : renderer_farm->take_failed_leases()) {
      log_event("renderer", rt::LogLevel::Warn, "lease.failed",
                {{"slot", std::to_string(slot)}});
      if (chrome_session && chrome_session->published_slot.load() == slot) {
        // The frame's renderer died: start a new one; the next timer tick pushes the state again.
        chrome_session->request_stop();
        retired_sessions.push_back(std::move(chrome_session));
        chrome_session.reset();
        chrome_started = false;
        chrome_frame = Json();
        chrome_state_sent.clear();
        log_event("renderer", rt::LogLevel::Warn, "chrome.restarted", {{"slot", std::to_string(slot)}});
        continue;
      }
      Tab *owner = nullptr;
      for (auto &tab : tabs)
        if (tab.session && tab.session->published_slot.load() == slot) {
          owner = &tab;
          break;
        }
      if (!owner) continue;
      rt::CrashRecord record;
      rt::Error error;
      // Renderer ids are positive in the crash service, slots start at zero.
      if (!crash_service->renderer_failed(int(slot) + 1, owner->id,
                                          rt::RendererFailureKind::Crash, owner->url,
                                          "renderer process exited", record, error)) {
        log_event("crash", rt::LogLevel::Error, "renderer.failed.unrecorded",
                  {{"code", error.code}});
        if (record.id == 0) continue;
      }
      rt::Error restart_error;
      if (!crash_service->restart(record.id, restart_error))
        log_event("crash", rt::LogLevel::Warn, "restart.refused",
                  {{"code", restart_error.code}});
    }
  }
  void stop_session(Tab &tab) {
    if (ime) ime->surface_closed(tab.id);
    if (!tab.session) return;
    tab.session->request_stop();
    retired_sessions.push_back(std::move(tab.session));
  }
  // "Sleep inactive tabs": a web tab that has sat in the background for the chosen number of minutes gives up its
  // renderer and its decoded images. It keeps its address, title and history, and select_tab loads it again
  // (started is false) the next time it is in front. Pinned tabs, the front tab and tabs still loading are left
  // alone. I cannot yet tell that a background tab is playing sound, so pin a tab that must keep playing.
  void sleep_idle_tabs() {
    const int minutes = profile.sleep_tabs_minutes;
    if (minutes <= 0) return;
    const ULONGLONG now = GetTickCount64();
    for (size_t i = 0; i < tabs.size(); ++i) {
      Tab &tab = tabs[i];
      if (int(i) == active || tab.pinned || tab.loading || !tab.session || !tab.started) continue;
      if (!(starts(tab.url, "http://") || starts(tab.url, "https://"))) continue;
      if (tab.last_used == 0) { tab.last_used = now; continue; }
      if (now - tab.last_used < ULONGLONG(minutes) * 60000) continue;
      sleep_tab(tab);
    }
  }
  void sleep_tab(Tab &tab) {
    stop_session(tab);
    tab.started = false;
    tab.frame = Json();
    tab.images.clear();
    tab.image_bytes = 0;
    tab.paint_sig = 0;
  }
  void reap_sessions() {
    for (auto it = retired_sessions.begin(); it != retired_sessions.end();) {
      const auto &session = *it;
      if (!session->finished.load()) {
        ++it;
        continue;
      }
      if (session->worker.joinable() &&
          session->worker.get_id() != std::this_thread::get_id())
        session->worker.join();
      it = retired_sessions.erase(it);
    }
  }
  void stop_all_sessions() {
    for (auto &tab : tabs)
      if (tab.session) tab.session->request_stop();
    for (auto &session : retired_sessions) session->request_stop();
    auto join = [](const std::shared_ptr<Session> &session) {
      if (session && session->worker.joinable() &&
          session->worker.get_id() != std::this_thread::get_id())
        session->worker.join();
    };
    for (auto &tab : tabs) join(tab.session);
    for (auto &session : retired_sessions) join(session);
    retired_sessions.clear();
  }
  // S026 in production: the TabModel is the one authority on tab ids, order,
  // pinning, muting, the active tab and the recently-closed stack. The shell's
  // `tabs` vector holds each tab's own state (session, frame, history) and is
  // put back in the model's order after every change.
  rt::TabModel tab_model;
  rt::WindowRules *window_rules = nullptr;
  int window_rules_id = -1;
  // Window state goes through the rules; the apply seam only touches the real
  // window when it disagrees, so a WM_SIZE that reports the new state cannot loop.
  void window_command(const char *command) {
    if (!window_rules || window_rules_id < 0) return;
    const std::string name = command;
    if (name == "minimize") window_rules->minimize(window_rules_id);
    else if (name == "maximize") window_rules->maximize(window_rules_id);
    else if (name == "restore") window_rules->restore(window_rules_id);
  }
  void window_size_changed(WPARAM kind) {
    if (kind == SIZE_MINIMIZED) window_command("minimize");
    else if (kind == SIZE_MAXIMIZED) window_command("maximize");
    else if (kind == SIZE_RESTORED) window_command("restore");
  }
  int model_open(const std::string &label, int index) {
    rt::Error error;
    return tab_model.open_tab(kWindowId, label, index, error);
  }
  int model_position(int tab_id) const {
    const std::vector<int> order = tab_model.order(kWindowId);
    for (size_t i = 0; i < order.size(); ++i)
      if (order[i] == tab_id) return int(i);
    return -1;
  }
  void apply_model_order() {
    int active_id = (active >= 0 && active < int(tabs.size())) ? tabs[size_t(active)].id : -1;
    std::vector<Tab> ordered;
    ordered.reserve(tabs.size());
    for (int id : tab_model.order(kWindowId)) {
      for (auto &tab : tabs)
        if (tab.id == id) {
          ordered.push_back(std::move(tab));
          break;
        }
    }
    // A tab the model does not know would be lost with its session; keep it last.
    for (auto &tab : tabs) {
      bool placed = false;
      for (const auto &done : ordered) placed = placed || done.id == tab.id;
      if (!placed) ordered.push_back(std::move(tab));
    }
    tabs = std::move(ordered);
    for (auto &tab : tabs)
      if (const rt::Tab *known = tab_model.find(tab.id)) {
        tab.pinned = known->pinned;
        tab.muted = known->muted;
      }
    active = 0;
    for (size_t i = 0; i < tabs.size(); ++i)
      if (tabs[i].id == active_id) active = int(i);
  }
  void forget_all_tabs() {
    rt::Error error;
    tab_model.forget_all(kWindowId, error);
    tabs.clear();
  }
  void trim_closed_data() {
    std::set<int> remembered;
    for (const auto &entry : tab_model.closed()) remembered.insert(entry.tab_id);
    for (auto it = closed_data.begin(); it != closed_data.end();)
      it = remembered.count(it->first) ? std::next(it) : closed_data.erase(it);
  }
  void remember_closed_tab(const Tab &tab) {
    ClosedTab closed;
    closed.url = tab.url;
    closed.title = tab.title;
    closed.history = tab.history_urls();
    closed.index = tab.history_index();
    closed.pinned = tab.pinned;
    closed.muted = tab.muted;
    closed_data[tab.id] = std::move(closed);
    rt::Error error;
    tab_model.close_tab(tab.id, error);
    trim_closed_data();
  }
  void new_tab(std::string url = "psychovault://newtab", bool focus = true) {
    Tab t;
    t.id = model_open(url, -1);
    if (t.id < 0) return;
    {
      rt::Error activate_error;
      tab_model.activate(t.id, activate_error);
    }
    t.url = url;
    t.title = app_ui_url(url) ? "New Tab" : url;
    t.started = !starts(url, "http://") && !starts(url, "https://");
    tabs.push_back(std::move(t));
    active = int(tabs.size()) - 1;
    if (focus)
      navigate(url, false);
    else {
      sync_address();
      settings = starts(url, "psychovault://settings");
    }
    sync_focus();
  }
  void close_tab(int index) {
    if (index < 0 || index >= int(tabs.size()))
      return;
    if (tabs[size_t(index)].pinned) {
      status = "Unpin the tab before closing it";
      InvalidateRect(hwnd, nullptr, FALSE);
      return;
    }
    if (tabs.size() <= 1) {
      stop_session(current());
      forget_all_tabs();
      new_tab();
      save_session();
      return;
    }
    remember_closed_tab(tabs[size_t(index)]);
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
    if (active >= 0 && active < int(tabs.size())) tabs[size_t(active)].last_used = GetTickCount64(); // it goes to the background now
    active = index;
    Tab &tab = current();
    tab.last_used = GetTickCount64();
    {
      rt::Error activate_error;
      tab_model.activate(tab.id, activate_error);
    }
    sync_focus();
    sync_address();
    settings = starts(tab.url, "psychovault://settings");
    status = !tab.error.empty() ? tab.error : tab.loading ? "Loading " + tab.url : tab.url;
    update_find();
    if ((!tab.started && (starts(tab.url, "http://") || starts(tab.url, "https://"))) ||
        (engine_page_url(tab.url) && !tab.session && tab.frame.type == Json::Type::Null)) {
      navigate(tab.url, false);
    } else if (tab.session) {
      Json j = Json::make_object();
      j.set("tag", "resize");
      j.set("width", content.w / tab.zoom);
      j.set("height", content.h / tab.zoom);
      set_resize_media_environment(j, page_device_pixel_ratio(tab.zoom), 8.0 / tab.zoom,
                                   profile.reduce_motion, profile.theme, profile.high_contrast);
      tab.session->send(j);
    }
    save_session();
    InvalidateRect(hwnd, nullptr, FALSE);
  }
  void sync_address() {
    address = app_ui_url(current().url) ? "" : current().url;
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
    if (index == active && page_composing) composition_cancel();
    stop_session(tab);
    std::string target = input;
    std::optional<Url> u;
    if (const std::string via = profile.search_shortcut_url(input); !via.empty()) u = parse_url(via);
    if (!u) u = navigate_input(input, profile.search_template());
    if (u)
      target = u->href();
    {
      const rt::NavKind kind = nav_begin(tab, target, add_history);
      if (add_history && kind == rt::NavKind::New) {
        if (!private_mode && profile.remember_history &&
            (starts(target, "http://") || starts(target, "https://"))) {
          // Both my per-tab stack and the persisted history see the visit;
          // the flush to disk rides my timer so a long session does not lose it.
          if (profile.history.empty() || profile.history.back().url != target) {
            profile.history.push_back({target, target,
                                       double(now_ms() % 4000000000000000LL)});
            if (profile.history.size() > 5000)
              profile.history.erase(profile.history.begin(),
                                    profile.history.begin() + (profile.history.size() - 5000));
            profile.history_dirty = true;
          }
        }
      }
    }
    tab.url = target;
    tab.loading = true;
    tab.started = true;
    tab.recorded = false;
    tab.error.clear();
    tab.frame = Json();
    tab.frame_tick = 0;
    tab.images.clear();
    tab.image_bytes = 0;
    tab.scroll = 0;
    tab.generation = ++generation;
    if (index == active) {
      settings = starts(target, "psychovault://settings");
      sync_address();
    }
    const bool app_page = engine_page_url(target);
    if ((starts(target, "psychovault://") && !app_page) || target == "about:blank") {
      tab.loading = false;
      tab.recorded = true;
      tab.title = starts(target, "psychovault://settings") ? "Settings" : "New Tab";
      nav_commit(tab, tab.title);
      if (index == active)
        status = "Ready";
      save_session();
      InvalidateRect(hwnd, nullptr, FALSE);
      return;
    }
    if (!u || (!u->network() && !app_page)) {
      tab.loading = false;
      tab.error = "Unsupported URL";
      nav_fail(tab, tab.error);
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
    session->policy.filter_counts = &session->counts;
    session->report = report;
    session->jar = &cookies;
    session->storage = &local_storage;
    session->profile_state_mutex = &profile_state_mutex;
    session->farm = renderer_farm;
    publish_settings();
    session->bridge_invoke = [window = hwnd, source_tab = tab.id, hub = settings_hub, vault = vault_service,
                              newtab_origin = app_ui_url(target),
                              settings_origin = settings_page_url(target) && !private_mode](
        rt::Channel channel, const std::string &method, const Json &payload,
        Json &reply, rt::Error &bridge_error) {
      static const rt::Schemas schemas = rt::Schemas::stock();
      bridge_error = rt::Error{};
      if (newtab_origin && method == "ui:tiles") {
        // The new-tab page may read the speed-dial tiles and nothing else about the profile.
        reply = Json::make_object();
        Json tiles = Json::make_array();
        {
          std::lock_guard lock(hub->mutex);
          const Json *values = hub->snapshot.find("values");
          const Json *stored = values ? values->find("speedDial") : nullptr;
          if (stored && stored->type == Json::Type::Array) tiles = *stored;
          for (const char *name : {"theme", "accent"})
            if (values && values->find(name)) reply.set(name, *values->find(name));
          if (values && values->find("highContrast")) reply.set("highContrast", *values->find("highContrast"));
        }
        reply.set("tiles", std::move(tiles));
        return true;
      }
      if (channel == rt::Channel::Settings) {
        // Only the settings page may read or change settings, and only through the catalog.
        if (!settings_origin || (method != "settings:list" && method != "settings:get" &&
                                 method != "settings:set") ||
            !schemas.validate(method, payload, bridge_error)) {
          if (bridge_error.code.empty()) bridge_error = {"bridge.settings.origin", "settings are not available to this page"};
          return false;
        }
        if (method == "settings:list" || method == "settings:get") {
          std::lock_guard lock(hub->mutex);
          if (method == "settings:list") {
            reply = hub->snapshot;
            return true;
          }
          const Json *values = hub->snapshot.find("values");
          const Json *found = values ? values->find(payload.str("key")) : nullptr;
          if (!found) {
            bridge_error = {"setting.unwired", "that setting is not connected to the profile yet"};
            return false;
          }
          reply = Json::make_object();
          reply.set("value", *found);
          return true;
        }
        auto decoded = Json::parse(payload.str("value"), 4096);
        if (!decoded) {
          bridge_error = {"setting.encoding", "the value is not valid JSON text"};
          return false;
        }
        if (!rt::SettingsService::validate(payload.str("key"), *decoded, bridge_error)) return false;
        auto task = std::make_unique<BridgeTask>();
        task->tab = source_tab;
        task->method = method;
        task->payload = payload;
        if (!PostMessageW(window, WM_BRIDGE, 0, reinterpret_cast<LPARAM>(task.get()))) {
          bridge_error = {"bridge.queue.failed", "the browser window refused the change"};
          return false;
        }
        task.release();
        reply = Json::make_object();
        reply.set("queued", Json(true));
        return true;
      }
      {
        // Read-only facts and window tasks the settings page asks for (see settings_task).
        static const char *const reads[] = {"data:usage", "diagnostics:report", "updates:status", "extensions:list", "proxy:status"};
        static const char *const tasks[] = {"data:clear", "diagnostics:copy", "app:openlog", "app:relaunch", "updates:check", "downloads:pickdir", "extensions:toggle", "extensions:remove", "extensions:addfolder", "extensions:restore"};
        const bool is_read = std::any_of(std::begin(reads), std::end(reads), [&](const char *m) { return method == m; });
        const bool is_task = std::any_of(std::begin(tasks), std::end(tasks), [&](const char *m) { return method == m; });
        if (is_read || is_task) {
          if (!settings_origin || !schemas.validate(method, payload, bridge_error)) {
            if (bridge_error.code.empty()) bridge_error = {"bridge.settings.origin", "this is not available to this page"};
            return false;
          }
          reply = Json::make_object();
          if (is_read) {
            std::lock_guard lock(hub->mutex);
            if (method == "data:usage") reply = hub->usage;
            else if (method == "diagnostics:report") reply.set("text", Json(hub->diagnostics));
            else if (method == "extensions:list") { reply.set("extensions", hub->extensions); reply.set("error", Json(hub->extension_error)); }
            else if (method == "proxy:status") reply = hub->proxy;
            else reply.set("text", Json(hub->update_status));
            return true;
          }
          if (method == "data:clear") {
            static const char *const kinds[] = {"history", "cookies", "siteData", "tabs", "downloads"};
            const std::string what = payload.str("what");
            if (std::none_of(std::begin(kinds), std::end(kinds), [&](const char *k) { return what == k; })) {
              bridge_error = {"bridge.data.kind", "that is not something I keep"};
              return false;
            }
          }
          auto task = std::make_unique<BridgeTask>();
          task->tab = source_tab;
          task->method = method;
          task->payload = payload;
          if (!PostMessageW(window, WM_BRIDGE, 0, reinterpret_cast<LPARAM>(task.get()))) {
            bridge_error = {"bridge.queue.failed", "the browser window refused the request"};
            return false;
          }
          task.release();
          reply.set("queued", Json(true));
          return true;
        }
      }
      if (channel == rt::Channel::Vault) {
        // The vault is the settings page's alone, and never reachable from a private window.
        if (!settings_origin || !vault || !schemas.validate(method, payload, bridge_error)) {
          if (bridge_error.code.empty()) bridge_error = {"bridge.vault.origin", "the vault is not available to this page"};
          return false;
        }
        rt::Error failure;
        reply = Json::make_object();
        auto done = [&](bool ok) {
          if (!ok) bridge_error = failure;
          return ok;
        };
        if (method == "vault:status") {
          reply = vault->status();
          std::lock_guard lock(hub->mutex);
          reply.set("notice", Json(hub->vault_notice));
          return true;
        }
        if (method == "vault:rekey")
          return done(vault->rekey(payload.str("current"), payload.str("next"), failure)) && (reply.set("ok", Json(true)), true);
        if (method == "vault:create") return done(vault->create(payload.str("passphrase"), failure)) && (reply.set("ok", Json(true)), true);
        if (method == "vault:unlock") return done(vault->unlock(payload.str("passphrase"), failure)) && (reply.set("ok", Json(true)), true);
        if (method == "vault:lock") { vault->lock(); reply.set("ok", Json(true)); return true; }
        if (method == "vault:list") {
          Json entries;
          if (!done(vault->list(entries, failure))) return false;
          reply.set("entries", std::move(entries));
          return true;
        }
        if (method == "vault:add")
          return done(vault->add(payload.str("site"), payload.str("user"), payload.str("pass"), payload.str("note"), failure)) &&
                 (reply.set("ok", Json(true)), true);
        if (method == "vault:remove") return done(vault->remove(payload.str("site"), failure)) && (reply.set("ok", Json(true)), true);
        if (method == "vault:reveal") {
          std::string password;
          if (!done(vault->reveal(payload.str("site"), password, failure))) return false;
          reply.set("password", Json(password));
          std::fill(password.begin(), password.end(), '\0');
          return true;
        }
        if (method == "vault:generate") { reply.set("password", Json(rt::VaultService::generate())); return true; }
        // vault:copy, backup, restore and import: the window does the clipboard or file dialog on its own thread.
        if (method == "vault:backup" || method == "vault:restore" || method == "vault:import") {
          std::lock_guard lock(hub->mutex);
          hub->vault_notice.clear();
        }
        auto task = std::make_unique<BridgeTask>();
        task->tab = source_tab;
        task->method = method;
        task->payload = payload;
        if (!PostMessageW(window, WM_BRIDGE, 0, reinterpret_cast<LPARAM>(task.get()))) {
          bridge_error = {"bridge.queue.failed", "the browser window refused the request"};
          return false;
        }
        task.release();
        reply.set("queued", Json(true));
        return true;
      }
      if (channel == rt::Channel::History) {
        // Only the settings page may clear history, and only with the whole history cleared.
        if (!settings_origin || method != "history:clear" || !schemas.validate(method, payload, bridge_error)) {
          if (bridge_error.code.empty()) bridge_error = {"bridge.history.origin", "history is not available to this page"};
          return false;
        }
        auto task = std::make_unique<BridgeTask>();
        task->tab = source_tab;
        task->method = method;
        task->payload = payload;
        if (!PostMessageW(window, WM_BRIDGE, 0, reinterpret_cast<LPARAM>(task.get()))) {
          bridge_error = {"bridge.queue.failed", "the browser window refused the request"};
          return false;
        }
        task.release();
        reply = Json::make_object();
        reply.set("queued", Json(true));
        return true;
      }
      if (channel != rt::Channel::Tab ||
          (method != "tab:new" && method != "tab:activate") ||
          !schemas.validate(method, payload, bridge_error))
        return false;
      if (method == "tab:new") {
        std::string target = payload.str("url", "psychovault://newtab");
        if (target.empty()) target = "psychovault://newtab";
        auto parsed_target = parse_url(target);
        if (target.size() > 32768 ||
            (parsed_target && !parsed_target->network() && !app_ui_url(target))) {
          bridge_error = {"bridge.tab.url", "new-tab address uses an unsupported scheme or is too long"};
          return false;
        }
      } else {
        const double raw_id = payload.num("id", 0);
        if (!std::isfinite(raw_id) || raw_id <= 0 ||
            raw_id > double(std::numeric_limits<int>::max()) ||
            raw_id != double(int(raw_id))) {
          bridge_error = {"bridge.tab.id", "tab activation requires a positive integer id"};
          return false;
        }
      }
      auto task = std::make_unique<BridgeTask>();
      task->tab = source_tab;
      task->method = method;
      task->payload = payload;
      if (!PostMessageW(window, WM_BRIDGE, 0,
                        reinterpret_cast<LPARAM>(task.get()))) {
        bridge_error = {"bridge.queue.failed", "the browser window refused the app action"};
        return false;
      }
      task.release();
      reply = Json::make_object();
      reply.set("queued", Json(true));
      return true;
    };
    session->muted.store(tab.muted);
    if (auto host_url = parse_url(target)) {
      session->policy.top_level_host = host_url->host;
      session->policy.top_level_scheme = host_url->scheme;
      session->policy.cookies = &cookies;
    }
    session->width = content.w / tab.zoom;
    session->height = content.h / tab.zoom;
    session->device_pixel_ratio = page_device_pixel_ratio(tab.zoom);
    session->scrollbar_width = 8.0 / tab.zoom;
    session->reduce_motion = profile.reduce_motion;
    session->color_scheme = profile.theme == "light" ? "light" : "dark";
    session->high_contrast = profile.high_contrast;
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
    if (tab.web) {
      rt::Error error;
      tab.web->stop(error);
    }
    tab.loading = false;
    status = "Stopped";
    InvalidateRect(hwnd, nullptr, FALSE);
  }
  void flush_history() {
    if (private_mode || !profile.history_dirty) return;
    if (history_flush_tick && GetTickCount() - history_flush_tick < 10000) return;
    history_flush_tick = GetTickCount();
    profile.history_dirty = false;
    save_profile();
  }
  DWORD history_flush_tick = 0;
  bool save_session() {
    if (private_mode)
      return true;
    profile.session_tabs.clear();
    profile.session_pinned.clear();
    profile.session_muted.clear();
    for (auto &tab : tabs) {
      profile.session_tabs.push_back(tab.url);
      profile.session_pinned.push_back(tab.pinned);
      profile.session_muted.push_back(tab.muted);
    }
    profile.session_active = active;
    const bool session_saved = profile.save_session();
    if (!session_saved && profile_error.empty()) profile_error = profile.error;
    const bool workspaces_saved = save_workspaces();
    return session_saved && workspaces_saved;
  }
  void back() {
    Tab &t = current();
    rt::Error error;
    if (t.nav().go_back(error) > 0) navigate(t.web->pending()->url, false);
  }
  void forward() {
    Tab &t = current();
    rt::Error error;
    if (t.nav().go_forward(error) > 0) navigate(t.web->pending()->url, false);
  }
  // The tab's navigation controller learns what the shell is doing: begin when
  // an address starts loading, commit when its first frame (or an internal page)
  // is on screen, fail when it errors. A failed load never reaches the history.
  rt::NavKind nav_begin(Tab &tab, const std::string &target, bool add_history) {
    rt::WebContents &web = tab.nav();
    rt::Error error;
    const rt::Navigation *pending = web.pending();
    if (!add_history && pending && pending->url == target) return pending->kind; // back/forward
    web.visit(target, error); // same address as on screen reloads, anything else is new
    return web.pending() ? web.pending()->kind : rt::NavKind::Reload;
  }
  void nav_commit(Tab &tab, const std::string &title) {
    if (!tab.web || !tab.web->pending()) return;
    rt::Error error;
    tab.web->commit(title, "", error);
  }
  void nav_fail(Tab &tab, const std::string &message) {
    if (!tab.web || !tab.web->pending()) return;
    rt::Error error;
    tab.web->fail("nav.load", message, error);
  }
  double page_device_pixel_ratio(double zoom) const {
    HDC dc = hwnd ? GetDC(hwnd) : nullptr;
    const int dpi = dc ? GetDeviceCaps(dc, LOGPIXELSX) : 96;
    if (dc) ReleaseDC(hwnd, dc);
    return std::clamp((dpi > 0 ? double(dpi) / 96.0 : 1.0) * zoom, 0.25, 8.0);
  }
  void refresh_viewport_environment() {
    Tab &tab = current();
    if (!tab.session) return;
    Json message = Json::make_object();
    message.set("tag", "resize");
    message.set("width", content.w / tab.zoom);
    message.set("height", content.h / tab.zoom);
    set_resize_media_environment(message, page_device_pixel_ratio(tab.zoom), 8.0 / tab.zoom,
                                 profile.reduce_motion, profile.theme, profile.high_contrast);
    tab.session->send(message);
  }
  void resize(int w, int h) {
    width = w;
    height = h;
    layout_content();
    if (chrome_session) {
      Json j = Json::make_object();
      j.set("tag", "resize");
      j.set("width", double(w));
      j.set("height", double(h));
      set_resize_media_environment(j, page_device_pixel_ratio(1.0), 8.0, profile.reduce_motion,
                                   profile.theme, profile.high_contrast);
      chrome_session->send(j);
    }
    if (current().session) {
      Json j = Json::make_object();
      j.set("tag", "resize");
      j.set("width", content.w / current().zoom);
      j.set("height", content.h / current().zoom);
      set_resize_media_environment(j, page_device_pixel_ratio(current().zoom), 8.0 / current().zoom,
                                   profile.reduce_motion, profile.theme, profile.high_contrast);
      current().session->send(j);
    }
    update_find();
    InvalidateRect(hwnd, nullptr, FALSE);
  }
  void draw(Canvas &c) {
    hits.clear();
    if (app_ui_slice) {
      // Keep the S045 page surface free of the legacy browser chrome, but never
      // leave a user with a featureless black window while a page is starting or
      // when navigation fails. A small shell-owned status card is the fallback
      // until the renderer supplies a page frame.
      c.fill({0, 0, double(width), double(height)}, BG);
      Tab &tab = current();
      if (tab.frame.type == Json::Type::Object && tab.error.empty()) {
        draw_content(c);
      } else if (chrome_enabled) {
        // The frame is drawn below; the page area stays plain until the tab has a frame.
      } else {
        const bool failed = !tab.error.empty();
        const double card_width = std::min(760.0, std::max(320.0, double(width) - 64));
        const double card_height = 176;
        Rect card{(width - card_width) / 2, (height - card_height) / 2,
                  card_width, card_height};
        c.fill(card, PANEL);
        c.stroke(card, BORDER);
        c.text(card.x + 28, card.y + 26,
               failed ? "This page didn't load" : "Starting PsychoVault...",
               22, failed ? 0xfff87171 : TEXT, true);
        if (failed) {
          c.text(card.x + 28, card.y + 66, tab.error, 13, MUTED);
          c.text(card.x + 28, card.y + 112,
                 "Press Ctrl+R to retry, or Ctrl+T to open a new tab.",
                 13, TEXT);
        } else {
          c.text(card.x + 28, card.y + 66,
                 tab.loading ? "Waiting for the page renderer..."
                             : "No page is open yet.",
                 14, MUTED);
          c.text(card.x + 28, card.y + 112,
                 "If this stays here, press Ctrl+T to open a new tab.",
                 13, TEXT);
        }
      }
      if (chrome_enabled) draw_chrome(c);
      return;
    }
}
  std::string update_status;
  std::string editing_setting;
  // My vault lives in RAM only while vault_open, and every secret byte is
  // wiped the moment I lock. The passphrase itself is never stored anywhere.
  // The password vault: one thread-safe owner (see runtime/vault); the settings page reaches it only
  // through the bridge, and only from the settings origin.
  std::shared_ptr<rt::VaultService> vault_service;
  // My spelling engine: the embedded starter vocabulary, words from whatever
  // dictionary source I pointed it at, and the words I taught it myself.
  Spell spell;
  std::string spell_cover; // the text my marks underneath belong to
  std::vector<SpellMark> spell_marks;
  // A right click opens one of my own menus. menu_click() maps a row back to
  // its action string and apply_menu() dispatches it, so a menu entry is read
  // and run in exactly one place.
  bool menu_open = false;
  int menu_tab = -1;
  int menu_x = 0, menu_y = 0;
  std::vector<MenuLine> menu_items;
  // Recently closed tabs keep their whole back stack so Ctrl+Shift+T puts the
  // tab back exactly as it was, not just the address it ended on.
  struct ClosedTab {
    std::string url, title;
    std::vector<std::string> history;
    size_t index = 0;
    bool pinned = false, muted = false;
  };
  std::map<int, ClosedTab> closed_data; // keyed by the TabModel's closed tab id
  // One workspace is one named set of tabs. The live workspace is re-captured
  // from the tab strip every save, so the disk copy is never out of date.
  struct Workspace {
    std::string name = "Main";
    std::vector<std::string> tabs;
    int active = 0;
    std::string pinned, muted;
  };
  std::vector<Workspace> workspaces;
  size_t workspace = 0;
  bool panel_workspaces = false;
  // Ctrl+Shift+P: one box, one filtered command list, Enter or a click runs.
  bool palette_open = false;
  std::string palette_text;
  size_t palette_caret = 0;
  int palette_index = 0;
  // The crash marker said the last run did not exit cleanly; I say so once and
  // the banner below lets me keep or bin the restored tabs.
  bool crashed_last = false, recovery_dismissed = false;
  // Update flow state: what the check found, what the install thread is doing.
  struct PreparedUpdate {
    std::string version, url, sha256;
    uint64_t size = 0;
    bool installing = false, ready = false;
  };
  PreparedUpdate next_update;
  bool update_snoozed = false;
  std::vector<std::string> log_cache;
  void reload_log_cache() { log_cache = profile.read_error_log(10); }
  void log_line(const std::string &subsystem, const std::string &message) {
    profile.append_error_log(subsystem + ": " + message);
    reload_log_cache();
  }
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
    } else if (key == "memoryCap") {
      int megabytes = std::atoi(value.c_str());
      if (!value.empty() && (megabytes < 64 || megabytes > 65536)) {
        status = "A memory cap has to be between 64 and 65536 MiB, or empty for none";
      } else {
        profile.memory_limit_mb = value.empty() ? 0 : megabytes;
        std::string error;
        if (confine_process(uint64_t(profile.memory_limit_mb) * 1024 * 1024,
                            uint32_t(profile.cpu_limit), error)) {
          status = profile.memory_limit_mb > 0
                       ? "Memory cap set to " + std::to_string(profile.memory_limit_mb) + " MiB"
                       : "Memory cap removed";
        } else {
          status = "I could not apply that cap: " + error;
        }
        confinement = confinement_status();
        save_profile();
      }
    } else if (key == "vaultLockMin") {
      int minutes = std::atoi(value.c_str());
      if (value.empty() || minutes < 1 || minutes > 1440) {
        status = "Auto-lock is between 1 and 1440 minutes";
      } else {
        profile.vault_lock_minutes = minutes;
        save_profile();
        status = "My vault locks " + std::to_string(minutes) + " minutes after I unlock it";
      }
    } else if (key == "spellSource") {
      profile.spell_dict_source = value;
      save_profile();
      if (value.empty()) {
        spell.replace_extra("");
        std::string error;
        write_file_atomic(spell_extra_path(), "", error);
        status = "My dictionary source is cleared";
      } else if (starts(value, "http://") || starts(value, "https://")) {
        auto parsed = parse_url(value);
        if (!parsed) {
          status = "That is not a URL I can fetch";
        } else {
          Resource resource = fetch_url(*parsed, 15000, 4u * 1024u * 1024u);
          if (resource.ok && !resource.text.empty()) {
            spell.replace_extra(resource.text);
            std::string error;
            write_file_atomic(spell_extra_path(), resource.text, error);
            status = "My dictionary learned " + std::to_string(spell.extra_count()) +
                     " words from that source";
          } else {
            status = "That dictionary source did not answer: " + resource.error;
          }
        }
      } else {
        try {
          std::string text = read_text_file(value);
          spell.replace_extra(text);
          std::string error;
          write_file_atomic(spell_extra_path(), text, error);
          status = "My dictionary learned " + std::to_string(spell.extra_count()) +
                   " words from that file";
        } catch (const std::exception &) {
          status = "I cannot read that dictionary file";
        }
      }
    } else if (key == "panicUrl") {
      if (value.empty() || starts(value, "http://") || starts(value, "https://")) {
        profile.panic_url = value;
        save_profile();
        status = value.empty() ? "Panic opens my home page" : "Panic opens " + value;
      } else {
        status = "A panic URL must start with http:// or https://";
      }
      return;
    } else if (key == "workspaceNew") {
      workspace_new(value);
      status = "Workspace created: " + workspaces[workspace].name;
      return;
    } else if (starts(key, "workspaceRename:")) {
      int index = std::atoi(key.c_str() + 16);
      if (index >= 0 && size_t(index) < workspaces.size()) {
        workspaces[size_t(index)].name = value.empty() ? workspaces[size_t(index)].name : value;
        save_workspaces();
        status = "Workspace renamed";
      }
      return;
    } else if (key == "filterFile") {
      profile.filter_list_path = value;
      load_security_state();
      if (value.empty())
        status = "I use my starter list only";
      else if (filters->size() == 0)
        status = "That list gave me no rules I can use";
      else
        status = "Filter list loaded: " + std::to_string(filters->size()) + " rules";
    } else if (key == "customSearch") {
      if (!profile.set_custom_search(value)) status = "A custom search URL must contain %s";
      else { if (!value.empty()) profile.set_search_engine("custom"); status = "Custom search saved"; }
    }
    save_profile();
    sync_address();
  }
  static std::vector<int> version_parts(std::string text) {
    if (!text.empty() && (text[0] == 'v' || text[0] == 'V'))
      text.erase(text.begin());
    std::vector<int> out;
    std::string part;
    for (char ch : text) {
      if (ch == '.') {
        out.push_back(std::atoi(part.c_str()));
        part.clear();
      } else if (ch >= '0' && ch <= '9')
        part += ch;
    }
    out.push_back(std::atoi(part.c_str()));
    while (out.size() < 3)
      out.push_back(0);
    return out;
  }
  static bool version_newer(const std::string &candidate, const std::string &current) {
    auto a = version_parts(candidate), b = version_parts(current);
    for (size_t i = 0; i < 3; ++i) {
      if (a[i] != b[i])
        return a[i] > b[i];
    }
    return false;
  }
  // My update check runs off the message thread now: it asks GitHub for the
  // latest release, compares the version against the one baked into this exe,
  // and reads my manifest for the asset hash I will verify on install.
  void check_for_update() {
    {
      std::lock_guard lock(update_mutex);
      if (next_update.installing)
        return;
      update_status = "Checking my releases...";
    }
    NetworkPolicy probe_policy = policy();
    HWND window = hwnd;
    std::thread([this, probe_policy, window] {
      auto report = [this, window](const std::string &text) {
        {
          std::lock_guard lock(update_mutex);
          update_status = text;
        }
        if (window)
          InvalidateRect(window, nullptr, FALSE);
      };
      auto release_url =
          parse_url("https://api.github.com/repos/Psycho-Core/PsychoVault/releases/latest");
      if (!release_url) { report("I could not build the release URL"); return; }
      auto response = fetch_url(*release_url, probe_policy, 15000, 1024 * 1024);
      if (!response.ok) { report("Update check failed: " + response.error); return; }
      auto json = Json::parse(response.text);
      if (!json) { report("The release response was not valid JSON"); return; }
      std::string tag = json->str("tag_name");
      std::string manifest_url;
      if (const Json *assets = json->find("assets"))
        if (assets->type == Json::Type::Array)
          for (const auto &asset : assets->array)
            if (asset.str("name") == "PsychoVault-update.json")
              manifest_url = asset.str("browser_download_url");
      std::string version = tag;
      if (!version.empty() && (version[0] == 'v' || version[0] == 'V'))
        version.erase(version.begin());
      if (version.empty() || !version_newer(version, PSYCHO_VAULT_VERSION)) {
        report("You are on the latest release (" + std::string(PSYCHO_VAULT_VERSION) + ")");
        return;
      }
      if (manifest_url.empty()) {
        report("Release " + tag + " has no manifest, so I will not touch it");
        return;
      }
      auto manifest_link = parse_url(manifest_url);
      if (!manifest_link) { report("The manifest URL is not valid"); return; }
      auto manifest_response = fetch_url(*manifest_link, probe_policy, 15000, 1024 * 1024);
      if (!manifest_response.ok) {
        report("Manifest fetch failed: " + manifest_response.error);
        return;
      }
      auto manifest = Json::parse(manifest_response.text);
      if (!manifest || manifest->str("product") != "PsychoVault") {
        report("The manifest is not mine");
        return;
      }
      // My updater can stage one executable only. A multi-file runtime must be
      // installed from its complete package, never as a shell-only partial update.
      if (const Json *required = manifest->find("requires")) {
        if (required->type == Json::Type::Array)
          for (const Json &name : required->array)
            if (name.string != "PsychoVault.exe") {
              report("This update includes a renderer runtime; install its full package");
              return;
            }
      }
      PreparedUpdate found;
      found.version = manifest->str("version", version);
      if (const Json *assets = manifest->find("assets"))
        if (assets->type == Json::Type::Array)
          for (const auto &asset : assets->array)
            if (asset.str("name") == "PsychoVault.exe") {
              found.url = asset.str("url");
              found.sha256 = lower(asset.str("sha256"));
              found.size = uint64_t(asset.num("size", 0));
            }
      if (found.url.empty() || found.sha256.empty()) {
        report("The manifest does not name my exe");
        return;
      }
      {
        std::lock_guard lock(update_mutex);
        next_update = found;
        update_status = "Update " + found.version + " is available";
      }
      if (window)
        InvalidateRect(window, nullptr, FALSE);
    }).detach();
  }
  // Download the exe, SHA-256 it against the manifest, stage it beside the
  // running exe. A mismatch means the file is deleted and nothing is staged.
  void install_update() {
    PreparedUpdate wanted;
    HWND window = hwnd;
    {
      std::lock_guard lock(update_mutex);
      if (next_update.installing || next_update.ready || next_update.url.empty())
        return;
      next_update.installing = true;
      wanted = next_update;
      update_status = "Downloading update " + wanted.version + " ...";
    }
    NetworkPolicy probe_policy = policy();
    std::string directory = profile.directory;
    wchar_t self_wide[MAX_PATH * 2] = {0};
    GetModuleFileNameW(nullptr, self_wide, DWORD((sizeof(self_wide) / sizeof(wchar_t))));
    int n = WideCharToMultiByte(CP_UTF8, 0, self_wide, -1, nullptr, 0, nullptr, nullptr);
    std::string self(size_t(n > 0 ? n - 1 : 0), 0);
    if (n > 0)
      WideCharToMultiByte(CP_UTF8, 0, self_wide, -1, self.data(), n, nullptr, nullptr);
    std::thread([this, probe_policy, window, wanted, self] {
      auto finish = [this, window](const std::string &text, bool ready) {
        {
          std::lock_guard lock(update_mutex);
          update_status = text;
          next_update.installing = false;
          next_update.ready = ready;
        }
        if (window)
          InvalidateRect(window, nullptr, FALSE);
      };
      auto parsed = parse_url(wanted.url);
      if (!parsed) { finish("The update URL is not valid", false); return; }
      auto response = fetch_url(*parsed, probe_policy, 120000, 384u * 1024u * 1024u);
      if (!response.ok) { finish("Update download failed: " + response.error, false); return; }
      if (wanted.size && response.text.size() != wanted.size) {
        finish("The download size does not match my manifest", false);
        return;
      }
      auto digest = sha256(Bytes(response.text.begin(), response.text.end()));
      static const char digits[] = "0123456789abcdef";
      std::string hex;
      for (uint8_t b : digest) {
        hex += digits[b >> 4];
        hex += digits[b & 15];
      }
      if (hex != wanted.sha256) {
        finish("Update verification failed - I refuse to install it", false);
        return;
      }
      // Only a hash-verified exe gets staged next to the running one.
      std::string staged = self + ".staged";
      std::ofstream out(shell_path(staged), std::ios::binary | std::ios::trunc);
      out.write(response.text.data(), std::streamsize(response.text.size()));
      out.close();
      if (!out) {
        finish("I could not stage the update on disk", false);
        return;
      }
      finish("Update " + wanted.version + " verified - restart to install", true);
    }).detach();
  }
  // Hand off to a freshly spawned copy of myself in --apply-update mode.
  void apply_ready_update() {
    bool ready;
    {
      std::lock_guard lock(update_mutex);
      ready = next_update.ready;
    }
    if (!ready)
      return;
    wchar_t self[MAX_PATH * 2] = {0};
    GetModuleFileNameW(nullptr, self, DWORD((sizeof(self) / sizeof(wchar_t))));
    std::wstring params = L"--apply-update " + std::to_wstring(GetCurrentProcessId()) +
                          L" \"" + self + L"\"";
    ShellExecuteW(nullptr, L"open", self, params.c_str(), nullptr, SW_SHOWNORMAL);
    PostMessageW(hwnd, WM_CLOSE, 0, 0);
  }
  std::mutex update_mutex;
  // I turn a frame into the paint list my raster reads, in window coordinates.
  std::vector<PaintCommand> frame_commands(const Tab &t, double origin_x, double origin_y) const {
    return commands_of(t.frame, t.zoom, t.scroll, origin_x, origin_y);
  }
  static std::vector<PaintCommand> commands_of(const Json &frame, double zoom, double scroll,
                                               double origin_x, double origin_y) {
    std::vector<PaintCommand> commands;
    for (auto &j : frame.at("paint").array) {
      PaintCommand p;
      p.kind = PaintCommand::Kind(int(j.num("kind")));
      const bool fixed = j.flag("fixed");
      p.rect = {j.num("x") * zoom + origin_x,
                j.num("y") * zoom + origin_y - (fixed ? 0 : scroll * zoom),
                j.num("w") * zoom, j.num("h") * zoom};
      p.color = uint32_t(j.num("color"));
      p.text = j.str("text");
      p.size = j.num("size") * zoom;
      p.thickness = j.num("thickness");
      p.bold = j.flag("bold");
      p.font_family = j.str("fontFamily", "Segoe UI");
      p.font_weight = int(j.num("fontWeight"));
      p.node = int(j.num("node"));
      p.url = j.str("url");
      p.gradient = j.str("gradient");
      p.fixed = fixed;
      p.radius = j.num("radius") * zoom;
      p.opacity = j.num("opacity", 1);
      p.blur = j.num("blur") * zoom;
      p.spread = j.num("spread") * zoom;
      p.offset_x = j.num("offsetX") * zoom;
      p.offset_y = j.num("offsetY") * zoom;
      p.rotation = j.num("rotation");
      p.filter = j.str("filter");
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
      set_resize_media_environment(j, page_device_pixel_ratio(t.zoom), 8.0 / t.zoom,
                                   profile.reduce_motion, profile.theme, profile.high_contrast);
      t.session->send(j);
    }
    update_find();
    status = "Zoom " + std::to_string(int(t.zoom * 100 + 0.5)) + "%";
    InvalidateRect(hwnd, nullptr, FALSE);
  }
  std::pair<Rect, Rect> document_scrollbar(double *maximum = nullptr) {
    Tab &t = current();
    const double zoom = t.zoom > 0 ? t.zoom : 1.0;
    const double total = t.frame.num("height", content.h / zoom);
    const double visible = content.h / zoom;
    const double max_scroll = std::max(0.0, total - visible);
    if (maximum) *maximum = max_scroll;
    if (max_scroll <= 0 || content.h <= 0) return {{}, {}};
    const Rect track{content.x + content.w - 8, content.y, 8, content.h};
    const double thumb_height = std::min(track.h,
        std::max(18.0, track.h * visible / std::max(total, visible)));
    const double thumb_y = track.y + (track.h - thumb_height) * t.scroll / max_scroll;
    return {track, {track.x, thumb_y, track.w, thumb_height}};
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
    double document_max = 0;
    const auto [document_track, document_thumb] = document_scrollbar(&document_max);
    if (document_max > 0) {
      c.fill(document_track, 0x30607080u);
      c.fill_rounded(document_thumb, 4, 0xff71869au);
    }
    c.clip = old;
  }
  // The frame page paints over the whole window. Its first paint command is the page background,
  // which would hide the tab underneath, so it is skipped when it covers the viewport.
  void draw_chrome(Canvas &c) {
    if (chrome_frame.type != Json::Type::Object) {
      if (!chrome_error.empty()) c.text(16, 12, "Browser frame failed: " + chrome_error, 13, 0xfff87171);
      return;
    }
    std::vector<PaintCommand> commands = commands_of(chrome_frame, 1.0, 0.0, 0.0, 0.0);
    if (!commands.empty() && commands.front().rect.x <= 0 && commands.front().rect.y <= 0 &&
        commands.front().rect.w >= width && commands.front().rect.h >= height)
      commands.erase(commands.begin());
    Rect old = c.clip;
    c.clip = {0, 0, double(width), double(height)};
    paint_commands(c, commands, 0, chrome_images);
    c.clip = old;
  }
  // A secondary press over the frame (or anywhere while its menu is open) goes to the frame page,
  // which decides what its context menu offers.
  bool chrome_context(int x, int y) {
    if (!chrome_enabled || !chrome_session || !over_chrome(x, y)) return false;
    chrome_focus = true;
    send_chrome_pointer("contextmenu", x, y);
    return true;
  }
  void send_chrome_pointer(const char *tag, int x, int y) {
    if (!chrome_session) return;
    Json command = Json::make_object();
    command.set("tag", tag);
    command.set("x", double(x));
    command.set("y", double(y));
    chrome_session->send(command);
  }
  // A press outside the page area belongs to the frame; the click is sent on release so a drag off
  // a button does not fire it.
  bool chrome_pointer(const char *kind, int x, int y) {
    if (!chrome_enabled || !chrome_session) return false;
    const std::string what = kind;
    if (what == "down") {
      if (!over_chrome(x, y)) {
        chrome_focus = false;
        return false;
      }
      chrome_focus = true;
      chrome_pointer_down = true;
      send_chrome_pointer("pointerdown", x, y);
      return true;
    }
    if (what == "up") {
      if (!chrome_pointer_down) return false;
      chrome_pointer_down = false;
      send_chrome_pointer("pointerup", x, y);
      if (over_chrome(x, y)) send_chrome_pointer("click", x, y);
      return true;
    }
    const bool inside = over_chrome(x, y);
    if (inside || chrome_pointer_inside || chrome_pointer_down) send_chrome_pointer("pointermove", x, y);
    chrome_pointer_inside = inside;
    return inside;
  }
  // The shell tells the frame what it shows; the frame never reads the profile itself.
  void push_chrome_state() {
    if (!chrome_enabled || !chrome_session || chrome_frame.type != Json::Type::Object) return;
    Json state = Json::make_object();
    Json list = Json::make_array();
    for (const Tab &t : tabs) {
      Json entry = Json::make_object();
      entry.set("id", double(t.id));
      entry.set("title", t.title.substr(0, 120));
      entry.set("url", t.url.substr(0, 512));
      entry.set("loading", t.loading);
      entry.set("pinned", t.pinned);
      entry.set("muted", t.muted);
      list.array.push_back(std::move(entry));
    }
    state.set("tabs", std::move(list));
    state.set("activeId", double(current().id));
    state.set("address", app_ui_url(current().url) ? std::string() : current().url.substr(0, 2048));
    state.set("blockAds", profile.block_ads);
    state.set("hideSidebar", profile.hide_sidebar);
    state.set("theme", profile.theme);
    state.set("accent", profile.accent);
    state.set("highContrast", profile.high_contrast);
    state.set("services", services_cache);
    if (chrome_panel_open()) {
      Json panel = Json::make_object();
      Json items = Json::make_array();
      const std::string kind = !panel_other.empty() ? panel_other : panel_bookmarks ? "bookmarks" : panel_history ? "history" : "downloads";
      panel.set("kind", kind);
      if (kind == "extensions") {
        items = build_extensions();
        panel.set("blocked", Json(!extension_workers_allowed()));
      } else if (kind == "performance") {
        for (size_t i = 0; i < tabs.size() && items.array.size() < 100; ++i) {
          const Tab &tab = tabs[i];
          Json item = Json::make_object();
          item.set("id", double(tab.id));
          item.set("title", (tab.title.empty() ? tab.url : tab.title).substr(0, 120));
          const bool web = starts(tab.url, "http://") || starts(tab.url, "https://");
          item.set("state", int(i) == active ? "front" : !web ? "internal" : tab.session ? (tab.pinned ? "pinned" : "running") : "asleep");
          items.array.push_back(std::move(item));
        }
        panel.set("sleepMinutes", double(profile.sleep_tabs_minutes));
      } else if (kind == "workspaces") {
        for (size_t i = 0; i < workspaces.size(); ++i) {
          Json item = Json::make_object();
          item.set("index", double(i));
          item.set("name", workspaces[i].name.substr(0, 60));
          item.set("tabs", double(i == workspace ? tabs.size() : workspaces[i].tabs.size()));
          item.set("current", Json(i == workspace));
          items.array.push_back(std::move(item));
        }
      } else if (kind == "bookmarks") {
        for (const Bookmark &mark : profile.bookmarks) {
          if (items.array.size() >= 300) break;
          Json item = Json::make_object();
          item.set("title", mark.title.substr(0, 120));
          item.set("url", mark.url.substr(0, 512));
          items.array.push_back(std::move(item));
        }
      } else if (kind == "history") {
        for (auto it = profile.history.rbegin(); it != profile.history.rend() && items.array.size() < 300; ++it) {
          Json item = Json::make_object();
          item.set("title", it->title.substr(0, 120));
          item.set("url", it->url.substr(0, 512));
          items.array.push_back(std::move(item));
        }
      } else {
        for (const DownloadItem &download : downloads.snapshot()) {
          if (items.array.size() >= 100) break;
          Json item = Json::make_object();
          item.set("id", double(download.id));
          item.set("filename", download.filename.substr(0, 120));
          item.set("state", download.state);
          item.set("received", double(download.received));
          item.set("total", double(download.total));
          items.array.push_back(std::move(item));
        }
      }
      panel.set("items", std::move(items));
      state.set("panel", std::move(panel));
    }
    if (palette_open) {
      Json palette = Json::make_object();
      palette.set("text", palette_text.substr(0, 120));
      auto matches = palette_matches();
      const int last = int(matches.size()) - 1;
      palette.set("index", double(std::clamp(palette_index, 0, std::max(0, last))));
      Json rows = Json::make_array();
      for (size_t i = 0; i < matches.size() && i < 8; ++i) {
        Json row = Json::make_object();
        row.set("label", matches[i]->label);
        row.set("chord", matches[i]->chord);
        row.set("id", matches[i]->id);
        rows.array.push_back(std::move(row));
      }
      palette.set("matches", std::move(rows));
      state.set("palette", std::move(palette));
    }
    if (find_open) {
      Json find = Json::make_object();
      find.set("text", find_text.substr(0, 200));
      find.set("count", double(find_marks.size()));
      find.set("index", double(find_at));
      state.set("find", std::move(find));
    }
    if (!frame_dialogs.empty()) {
      const FrameDialog &front = frame_dialogs.front();
      Json dialog = Json::make_object();
      dialog.set("id", double(front.id));
      dialog.set("kind", front.kind);
      dialog.set("title", front.title);
      dialog.set("message", front.message);
      dialog.set("value", front.value);
      Json buttons = Json::make_array();
      for (const std::string &label : front.buttons) buttons.array.push_back(Json(label));
      dialog.set("buttons", std::move(buttons));
      state.set("dialog", std::move(dialog));
    }
    state.set("canBack", current().can_back());
    state.set("canForward", current().can_forward());
    state.set("blocked", double(report ? report->blocked.load() : 0));
    state.set("status", status.substr(0, 512));
    const std::string text = state.dump();
    if (text == chrome_state_sent) return;
    chrome_state_sent = text;
    chrome_state_tick = GetTickCount();
    Json message = Json::make_object();
    message.set("tag", "appmessage");
    message.set("state", std::move(state));
    chrome_session->send(std::move(message));
  }
  void start_chrome() {
    if (!chrome_enabled || chrome_started || !hwnd || !renderer_farm) return;
    chrome_started = true;
    auto session = std::make_shared<Session>();
    session->hwnd = hwnd;
    session->tab = kChromeTab;
    session->generation = kChromeGeneration;
    session->url = "psychovault://chrome";
    session->policy = policy();
    session->policy.filter_counts = &session->counts;
    session->report = report;
    session->jar = &cookies;
    session->storage = &local_storage;
    session->profile_state_mutex = &profile_state_mutex;
    session->farm = renderer_farm;
    session->bridge_invoke = [window = hwnd](rt::Channel channel, const std::string &method,
                                             const Json &payload, Json &reply,
                                             rt::Error &bridge_error) {
      static const rt::Schemas schemas = rt::Schemas::stock();
      bridge_error = rt::Error{};
      struct Allowed { const char *method; rt::Channel channel; };
      static const Allowed allowed[] = {
          {"tab:new", rt::Channel::Tab},      {"tab:activate", rt::Channel::Tab},
          {"tab:close", rt::Channel::Tab},    {"nav:go", rt::Channel::Nav},
          {"tab:pin", rt::Channel::Tab},      {"tab:mute", rt::Channel::Tab},
          {"ui:overlay", rt::Channel::Ui},    {"rail:adblock", rt::Channel::Rail},
          {"ui:dialog", rt::Channel::Ui},      {"ui:panel", rt::Channel::Ui},
          {"bookmarks:add", rt::Channel::Bookmarks}, {"bookmarks:remove", rt::Channel::Bookmarks},
          {"history:clear", rt::Channel::History},   {"downloads:cancel", rt::Channel::Downloads},
          {"find:step", rt::Channel::Find},          {"find:close", rt::Channel::Find},
          {"palette:run", rt::Channel::Ui},          {"palette:close", rt::Channel::Ui},
          {"downloads:remove", rt::Channel::Downloads}, {"downloads:clear", rt::Channel::Downloads},
          {"nav:back", rt::Channel::Nav},     {"nav:forward", rt::Channel::Nav},
          {"nav:reload", rt::Channel::Nav},   {"nav:home", rt::Channel::Nav},
          {"win:minimize", rt::Channel::Win}, {"win:maximize", rt::Channel::Win},
          {"win:close", rt::Channel::Win}};
      bool known = false;
      for (const Allowed &entry : allowed)
        if (method == entry.method && channel == entry.channel) known = true;
      if (!known) {
        bridge_error = {"bridge.chrome.method", "the browser frame may not call " + method};
        return false;
      }
      if (!schemas.validate(method, payload, bridge_error)) return false;
      if (method == "tab:activate" || method == "tab:close" || method == "tab:pin" || method == "tab:mute") {
        const double raw_id = payload.num("id", 0);
        if (!std::isfinite(raw_id) || raw_id <= 0 || raw_id > double(std::numeric_limits<int>::max()) ||
            raw_id != double(int(raw_id))) {
          bridge_error = {"bridge.tab.id", "a tab action requires a positive integer id"};
          return false;
        }
      }
      if (method == "ui:panel") {
        const std::string kind = payload.str("kind");
        if (!kind.empty() && kind != "bookmarks" && kind != "history" && kind != "downloads" && kind != "extensions" &&
            kind != "performance" && kind != "workspaces") {
          bridge_error = {"bridge.panel.kind", "unknown panel"};
          return false;
        }
      }
      if (method == "palette:run") {
        const std::string id = payload.str("id");
        const bool plain = !id.empty() && id.size() <= 64 &&
            std::all_of(id.begin(), id.end(), [](unsigned char c) { return std::isalnum(c) || c == '-'; });
        if (!plain) {
          bridge_error = {"bridge.palette.id", "a palette command id is short letters, digits and dashes"};
          return false;
        }
      }
      if (method == "find:step" && std::fabs(payload.num("delta", 0)) != 1) {
        bridge_error = {"bridge.find.step", "find steps by one match, forward or back"};
        return false;
      }
      if (method == "bookmarks:remove" && payload.str("url").size() > 8192) {
        bridge_error = {"bridge.bookmark.url", "address is too long"};
        return false;
      }
      if (method == "downloads:cancel" || method == "downloads:remove") {
        const double id = payload.num("id", 0);
        if (!std::isfinite(id) || id <= 0 || id > 1e9 || id != std::floor(id)) {
          bridge_error = {"bridge.download.id", "a download action needs a positive integer id"};
          return false;
        }
      }
      if (method == "ui:dialog") {
        const double id = payload.num("id", 0), button = payload.num("button", -1);
        if (!std::isfinite(id) || id <= 0 || id > 1e9 || id != std::floor(id) || !std::isfinite(button) ||
            button < 0 || button > 7 || button != std::floor(button) || payload.str("value").size() > 4096) {
          bridge_error = {"bridge.dialog.answer", "a dialog answer needs a positive id, a button 0-7 and short text"};
          return false;
        }
      }
      if (method == "tab:new" || method == "nav:go") {
        const std::string target = payload.str("url", "psychovault://newtab");
        if (target.size() > 32768) {
          bridge_error = {"bridge.url", "address is too long"};
          return false;
        }
        if (method == "tab:new") {
          auto parsed_target = parse_url(target);
          if (parsed_target && !parsed_target->network() && !engine_page_url(target)) {
            bridge_error = {"bridge.tab.url", "new-tab address uses an unsupported scheme"};
            return false;
          }
        }
      }
      auto task = std::make_unique<BridgeTask>();
      task->tab = kChromeTab;
      task->method = method;
      task->payload = payload;
      if (!PostMessageW(window, WM_BRIDGE, 0, reinterpret_cast<LPARAM>(task.get()))) {
        bridge_error = {"bridge.queue.failed", "the browser window refused the app action"};
        return false;
      }
      task.release();
      reply = Json::make_object();
      reply.set("queued", Json(true));
      return true;
    };
    session->width = width;
    session->height = height;
    session->device_pixel_ratio = page_device_pixel_ratio(1.0);
    session->scrollbar_width = 8.0;
    session->reduce_motion = profile.reduce_motion;
    session->color_scheme = profile.theme == "light" ? "light" : "dark";
    session->high_contrast = profile.high_contrast;
    chrome_session = session;
    session->worker = std::thread([session] { session->run(); });
  }
  // Applies one frame action. Every method here arrived from the frame page through the bridge.
  void chrome_action(const BridgeTask &task) {
    const std::string &method = task.method;
    if (method == "tab:new") {
      std::string target = task.payload.str("url", "psychovault://newtab");
      if (target.empty()) target = "psychovault://newtab";
      new_tab(std::move(target));
    } else if (method == "tab:activate" || method == "tab:close" || method == "tab:pin" || method == "tab:mute") {
      const int id = int(task.payload.num("id", 0));
      for (size_t i = 0; i < tabs.size(); ++i) {
        if (tabs[i].id != id) continue;
        if (method == "tab:activate") select_tab(int(i));
        else if (method == "tab:close") close_tab(int(i));
        else if (method == "tab:pin") tab_set_pinned(int(i), !tabs[i].pinned);
        else tab_set_muted(int(i), !tabs[i].muted);
        break;
      }
    } else if (method == "ui:dialog") {
      answer_frame_dialog(int(task.payload.num("id", 0)), int(task.payload.num("button", 0)),
                          task.payload.str("value"));
    } else if (method == "ui:panel") {
      const std::string kind = task.payload.str("kind");
      panel_bookmarks = kind == "bookmarks";
      panel_history = kind == "history";
      panel_downloads = kind == "downloads";
      panel_workspaces = false;
      panel_other = (kind == "extensions" || kind == "performance" || kind == "workspaces") ? kind : std::string();
    } else if (method == "extensions:toggle") {
      BridgeTask forwarded;
      forwarded.tab = task.tab;
      forwarded.method = method;
      forwarded.payload = task.payload;
      settings_task(forwarded);
      publish_settings();
    } else if (method == "perf:sleep") {
      const int id = int(task.payload.num("id", 0));
      for (size_t i = 0; i < tabs.size(); ++i)
        if (tabs[i].id == id && int(i) != active) sleep_tab(tabs[i]);
    } else if (method == "ws:open") {
      workspace_switch(size_t(std::max(0.0, task.payload.num("index", 0))));
    } else if (method == "ws:new") {
      workspace_new("");
    } else if (method == "ws:close") {
      workspace_close(int(task.payload.num("index", -1)));
    } else if (method == "palette:run") {
      run_command(task.payload.str("id"));
    } else if (method == "palette:close") {
      palette_open = false;
    } else if (method == "find:step") {
      find_step(task.payload.num("delta", 1) < 0 ? -1 : 1);
    } else if (method == "find:close") {
      close_find();
    } else if (method == "bookmarks:add") {
      const std::string url = current().url;
      if (!private_mode && !url.empty() && !app_ui_url(url)) {
        profile.bookmark(current().title, url);
        save_profile();
        status = "Bookmark saved";
      }
    } else if (method == "bookmarks:remove") {
      profile.remove_bookmark(task.payload.str("url"));
      save_profile();
    } else if (method == "history:clear") {
      profile.clear_history();
      save_profile();
      status = "I cleared my browsing history";
    } else if (method == "downloads:cancel") {
      downloads.cancel(int(task.payload.num("id", 0)));
    } else if (method == "downloads:remove") {
      downloads.remove(int(task.payload.num("id", 0)));
    } else if (method == "downloads:clear") {
      downloads.clear_finished();
    } else if (method == "ui:overlay") {
      chrome_overlay = task.payload.flag("open");
      if (!chrome_overlay) chrome_pointer_down = false;
    } else if (method == "rail:adblock") {
      if (settings_service) {
        rt::Error error;
        if (settings_service->set("blockAds", Json(!profile.block_ads), error)) {
          save_profile();
          publish_settings();
          status = profile.block_ads ? "Ad and tracker blocking on" : "Ad and tracker blocking off";
        }
      }
    } else if (method == "nav:go") {
      const std::string target = task.payload.str("url");
      if (!target.empty()) navigate(target);
    } else if (method == "nav:back") back();
    else if (method == "nav:forward") forward();
    else if (method == "nav:reload") navigate(current().url, false);
    else if (method == "nav:home") navigate("psychovault://newtab");
    else if (method == "win:minimize") ShowWindow(hwnd, SW_MINIMIZE);
    else if (method == "win:maximize") ShowWindow(hwnd, IsZoomed(hwnd) ? SW_RESTORE : SW_MAXIMIZE);
    else if (method == "win:close") PostMessageW(hwnd, WM_CLOSE, 0, 0);
    InvalidateRect(hwnd, nullptr, FALSE);
  }
  // --smoke-settings: the real settings page, the real pointer path, the real profile. The page
  // lists values through psycho.settings, a press on the "Restore tabs on launch" switch sends
  // psycho.settings.set, the broker validates and the shell applies and saves it.
  bool settings_smoke_step(DWORD now) {
    auto finish = [&](bool passed, const std::string &reason) {
      if (!capture_path.empty()) capture(hwnd, capture_path);
      Json report = Json::make_object();
      report.set("result", passed ? "passed" : "failed");
      report.set("reason", reason);
      report.set("stage", double(smoke_settings_stage));
      report.set("elapsedMs", double(now - start_tick));
      report.set("restoreBefore", smoke_settings_before);
      report.set("restoreAfter", profile.restore_tabs);
      report.set("status", status);
      report.set("note", smoke_settings_note);
      report.set("profileError", profile_error);
      {
        // What the page actually painted, so a missing row can be told from a missing page.
        Json texts = Json::make_array();
        const Json *paint = current().frame.find("paint");
        if (paint && paint->type == Json::Type::Array) {
          report.set("paintCommands", double(paint->array.size()));
          for (const Json &command : paint->array)
            if (!command.str("text").empty() && texts.array.size() < 40)
              texts.array.push_back(Json(command.str("text").substr(0, 60) + " @" +
                                         std::to_string(int(command.num("x"))) + "," +
                                         std::to_string(int(command.num("y")))));
        }
        report.set("texts", std::move(texts));
        report.set("frameTitle", current().frame.str("title"));
      }
      if (current().session) {
        Json notes = Json::make_array();
        for (const auto &line : current().session->recent_diagnostics()) notes.array.push_back(Json(line));
        report.set("diagnostics", std::move(notes));
      }
      std::string write_error;
      write_file_atomic(capture_path + ".settings.json", report.dump() + "\n", write_error);
      smoke_settings_stage = 9;
      PostQuitMessage(passed ? 0 : 1);
    };
    if (smoke_settings_stage >= 9) return true;
    if (now - start_tick >= 45000) {
      finish(false, "the settings switch did not change the profile in time");
      return true;
    }
    Tab &tab = current();
    if (smoke_settings_stage == 0) {
      if (!settings_page_url(tab.url) || tab.frame.type != Json::Type::Object || tab.loading ||
          !tab.session || !tab.session->ready || now - start_tick < 2000)
        return true;
      const Json *paint = tab.frame.find("paint");
      double label_y = -1;
      if (paint && paint->type == Json::Type::Array)
        for (const Json &command : paint->array)
          // The engine paints text word by word, so the row is found by its first word; only the
          // General panel is visible, and "Restore" appears once in it.
          if (command.str("text") == "Restore") {
            label_y = command.num("y") + command.num("h") / 2.0 - tab.scroll + content.y;
            break;
          }
      if (label_y < 0) {
        finish(false, "the settings page painted no 'Restore tabs on launch' row");
        return true;
      }
      int x = 0, y = 0;
      bool found = false;
      for (const Json &command : paint->array) {
        if (std::fabs(command.num("w") - 44) > 1 || std::fabs(command.num("h") - 24) > 1) continue;
        const double cy = command.num("y") + command.num("h") / 2.0 - tab.scroll + content.y;
        if (std::fabs(cy - label_y) > 20) continue;
        x = int(std::lround(command.num("x") + command.num("w") / 2.0 + content.x));
        y = int(std::lround(cy));
        found = true;
        break;
      }
      if (!found) {
        finish(false, "the settings page painted no switch beside 'Restore tabs on launch'");
        return true;
      }
      smoke_settings_before = profile.restore_tabs;
      smoke_settings_note = "pressed the switch at " + std::to_string(x) + "," + std::to_string(y);
      pointer_move(x, y);
      pointer_down(x, y);
      pointer_up(x, y);
      click(x, y);
      smoke_settings_stage = 1;
      smoke_settings_tick = now;
    } else if (smoke_settings_stage == 1) {
      if (profile.restore_tabs != smoke_settings_before) {
        // Flush so the check below reads what a restart would read.
        const bool saved = save_profile();
        Profile reread = Profile::open(profile.directory, false);
        smoke_settings_note += saved ? "; saved" : "; save failed";
        if (reread.restore_tabs == profile.restore_tabs) {
          smoke_settings_stage = 2;
          finish(true, "a switch on the settings page changed the profile and the saved profile");
        } else {
          finish(false, "the change reached memory but not the saved profile");
        }
      } else if (now - smoke_settings_tick > 20000) {
        finish(false, "pressing the switch did not reach the profile");
      }
    }
    return true;
  }
  // --smoke-chrome: finds the frame's own "+" and close glyphs in its paint list and presses them
  // through the same pointer path a mouse uses. Passing means the page, the bridge, the broker and
  // the shell agreed on a new tab and then a close, and the state push redrew the strip between.
  // The leftmost paint command with this text: the tab strip is left of the workspace chip, and
  // paint order alone does not say which of two positioned elements comes first.
  bool chrome_paint_center(const char *glyph, int, int &x, int &y, double max_y = 60, double min_y = 0) const {
    const Json *paint = chrome_frame.find("paint");
    if (!paint || paint->type != Json::Type::Array) return false;
    bool found = false;
    double best = 0;
    for (const Json &command : paint->array) {
      if (command.str("text") != glyph || command.num("y") > max_y || command.num("y") < min_y) continue;
      if (found && command.num("x") >= best) continue;
      found = true;
      best = command.num("x");
      x = int(std::lround(command.num("x") + command.num("w") / 2.0));
      y = int(std::lround(command.num("y") + command.num("h") / 2.0));
    }
    return found;
  }
  void chrome_press(int x, int y) {
    chrome_pointer("move", x, y);
    chrome_pointer("down", x, y);
    chrome_pointer("up", x, y);
  }
  bool chrome_smoke_step(DWORD now) {
    auto finish = [&](bool passed, const std::string &reason) {
      if (!capture_path.empty()) capture(hwnd, capture_path);
      Json report = Json::make_object();
      report.set("result", passed ? "passed" : "failed");
      report.set("reason", reason);
      report.set("stage", double(smoke_chrome_stage));
      report.set("elapsedMs", double(now - start_tick));
      report.set("tabCount", double(tabs.size()));
      report.set("activeUrl", current().url);
      report.set("chromeError", chrome_error);
      report.set("chromeFrame", chrome_frame.type == Json::Type::Object);
      report.set("chromeReady", chrome_session && chrome_session->ready.load());
      report.set("note", smoke_chrome_note);
      report.set("timerTicks", double(g_timer_ticks.load()));
      report.set("paints", double(g_paints.load()));
      report.set("chromeFrames", double(g_chrome_frames.load()));
      report.set("paintMsTotal", double(g_paint_us.load()) / 1000.0);
      report.set("drawMsTotal", double(g_draw_us.load()) / 1000.0);
      if (chrome_session) {
        report.set("chromeExchanges", double(chrome_session->exchanges.load()));
        report.set("chromeExchangeMs", double(chrome_session->exchange_ms.load()));
      }
      if (current().session) {
        report.set("tabExchanges", double(current().session->exchanges.load()));
        report.set("tabExchangeMs", double(current().session->exchange_ms.load()));
      }
      {
        // What the frame painted in its top band and dialog area, to tell a missing item from a late frame.
        Json texts = Json::make_array();
        const Json *paint = chrome_frame.find("paint");
        if (paint && paint->type == Json::Type::Array)
          for (const Json &command : paint->array)
            if (!command.str("text").empty() && command.num("y") < 300 && command.num("x") > 60 &&
                texts.array.size() < 30)
              texts.array.push_back(Json(command.str("text").substr(0, 40) + " @" +
                                         std::to_string(int(command.num("x"))) + "," +
                                         std::to_string(int(command.num("y")))));
        report.set("chromeTexts", std::move(texts));
      }
      report.set("activeLoading", current().loading);
      report.set("activeHasFrame", current().frame.type == Json::Type::Object);
      report.set("activeError", current().error);
      if (chrome_session) {
        Json notes = Json::make_array();
        for (const auto &line : chrome_session->recent_diagnostics()) notes.array.push_back(Json(line));
        report.set("diagnostics", std::move(notes));
        report.set("startError", chrome_session->start_error);
      }
      std::string write_error;
      write_file_atomic(capture_path + ".chrome.json", report.dump() + "\n", write_error);
      smoke_chrome_stage = 99;
      PostQuitMessage(passed ? 0 : 1);
    };
    if (smoke_chrome_stage >= 99) return true;
    if (now - start_tick >= 45000) {
      finish(false, "the browser frame did not complete its tab actions in time");
      return true;
    }
    if (!chrome_error.empty()) {
      finish(false, "the browser frame failed to load: " + chrome_error);
      return true;
    }
    // Ready means the strip on screen was drawn from the current tab list, judged from the frame itself.
    // The strip is current when its "+" sits where chrome.js puts it for this many tabs
    // (44px rail + 36px mark gap + 126px per tab + 2px, centre of the 26px button).
    int plus_x = 0, plus_y = 0;
    const bool settled = chrome_paint_center("+", 0, plus_x, plus_y) &&
                         std::abs(plus_x - (44 + 36 + int(tabs.size()) * 126 + 2 + 13)) <= 2;
    const bool ready = chrome_frame.type == Json::Type::Object && settled &&
                       current().frame.type == Json::Type::Object && !current().loading &&
                       now - start_tick >= 1500;
    int x = 0, y = 0;
    if (smoke_chrome_stage == 0) {
      if (!ready) return true;
      if (!chrome_paint_center("+", 0, x, y)) {
        finish(false, "the frame painted no new-tab button");
        return true;
      }
      smoke_chrome_note = std::to_string(tabs.size()) + " tabs before the click at " +
                          std::to_string(x) + "," + std::to_string(y) + " content " +
                          std::to_string(int(content.x)) + "," + std::to_string(int(content.y));
      smoke_chrome_tick = now;
      chrome_press(x, y);
      smoke_chrome_stage = 1;
    } else if (smoke_chrome_stage == 1) {
      if (tabs.size() >= 2) {
        smoke_chrome_stage = 2;
        smoke_chrome_tick = now;
        chrome_state_sent.clear();
      }
    } else if (smoke_chrome_stage == 2) {
      // Wait for the strip to be redrawn with a second tab before pressing its close glyph.
      if (now - smoke_chrome_tick < 600 || !settled || chrome_frame.type != Json::Type::Object) return true;
      if (!chrome_paint_center("\u00d7", 0, x, y)) {
        finish(false, "the frame painted no tab close button");
        return true;
      }
      smoke_chrome_tick = now;
      chrome_press(x, y);
      smoke_chrome_stage = 3;
    } else if (smoke_chrome_stage == 3) {
      if (tabs.size() == 1 && current().frame.type == Json::Type::Object && !current().loading) {
        smoke_chrome_stage = 4;
        smoke_chrome_tick = now;
        smoke_chrome_note += "; closed one";
      }
    } else if (smoke_chrome_stage == 4) {
      // Right-click the remaining tab through the window procedure's path; the frame opens its menu
      // over the page and tells the shell (ui.overlay), which then sends it every press.
      if (now - smoke_chrome_tick < 600 || !settled) return true;
      chrome_context(140, 20);
      smoke_chrome_stage = 5;
      smoke_chrome_tick = now;
    } else if (smoke_chrome_stage == 5) {
      if (!chrome_overlay || now - smoke_chrome_tick < 600) return true;
      if (!chrome_paint_center("Pin tab", 0, x, y)) {
        finish(false, "the tab menu painted no Pin item");
        return true;
      }
      smoke_chrome_note += "; menu open, pressing Pin at " + std::to_string(x) + "," + std::to_string(y);
      chrome_press(x, y);
      smoke_chrome_stage = 6;
      smoke_chrome_tick = now;
    } else if (smoke_chrome_stage == 6) {
      if (!tabs.empty() && tabs[0].pinned && !chrome_overlay) {
        smoke_chrome_note += "; pinned";
        // A page alert, drawn by the frame over everything: press OK through the pointer path.
        show_frame_dialog("alert", "This page says", "Smoke alert", {"OK"}, [this](int, const std::string &) {
          smoke_chrome_note += "; alert answered";
        });
        smoke_chrome_stage = 7;
        smoke_chrome_tick = now;
      }
    } else if (smoke_chrome_stage == 7) {
      if (now - smoke_chrome_tick < 600 || frame_dialogs.empty()) {
        if (frame_dialogs.empty() && now - smoke_chrome_tick >= 600)
          finish(false, "the alert disappeared before it was pressed");
        return true;
      }
      if (!chrome_paint_center("OK", 0, x, y, 2000)) return true;
      chrome_press(x, y);
      smoke_chrome_stage = 8;
      smoke_chrome_tick = now;
    } else if (smoke_chrome_stage == 8) {
      if (frame_dialogs.empty() && smoke_chrome_note.find("alert answered") != std::string::npos) {
        // Open the history panel (what Ctrl+H or the menu does) with one visit in it.
        profile.history.push_back({"Smoke visit", "https://smoke.test/", double(now_ms() % 4000000000000000LL)});
        panel_history = true;
        smoke_chrome_note += "; alert done, panel opened";
        smoke_chrome_stage = 10;
        smoke_chrome_tick = now;
      }
    } else if (smoke_chrome_stage == 10) {
      // The page area must have shrunk by the 330px panel and the row must be painted in it.
      if (now - smoke_chrome_tick < 600) return true;
      int row_x = 0, row_y = 0;
      if (!chrome_paint_center("Smoke", 0, row_x, row_y, 2000) || row_x < int(content.x + content.w)) return true;
      if (std::fabs(content.w - (width - 44 - 330)) > 1) {
        finish(false, "the page area did not shrink for the panel");
        return true;
      }
      smoke_chrome_note += "; pressing the history row at " + std::to_string(row_x) + "," + std::to_string(row_y);
      chrome_press(row_x, row_y);
      smoke_chrome_stage = 11;
      smoke_chrome_tick = now;
    } else if (smoke_chrome_stage == 11) {
      if (current().url == "https://smoke.test/") {
        // The find bar floats over the page's corner; Ctrl+F opens it, the frame draws the shell's
        // find state, and its Done button must be pressable even though it is inside the page area.
        panel_history = false;
        find_text = "smoke";
        open_find();
        smoke_chrome_note += "; history row opened a page";
        smoke_chrome_stage = 12;
        smoke_chrome_tick = now;
      }
    } else if (smoke_chrome_stage == 12) {
      if (now - smoke_chrome_tick < 600) return true;
      int done_x = 0, done_y = 0;
      if (!chrome_paint_center("Done", 0, done_x, done_y, 2000)) return true;
      if (!find_bar_rect().contains(done_x, done_y)) {
        finish(false, "the find bar's Done button is outside the rectangle the shell routes to the frame");
        return true;
      }
      chrome_press(done_x, done_y);
      smoke_chrome_stage = 13;
      smoke_chrome_tick = now;
    } else if (smoke_chrome_stage == 13) {
      if (!find_open) {
        // The command palette: the shell owns the text and the matches, the frame draws the rows.
        palette_open = true;
        palette_text = "New tab";
        palette_caret = palette_text.size();
        palette_index = 0;
        smoke_chrome_note += "; find closed";
        smoke_chrome_stage = 14;
        smoke_chrome_tick = now;
      }
    } else if (smoke_chrome_stage == 14) {
      if (now - smoke_chrome_tick < 600) return true;
      int row_x = 0, row_y = 0;
      if (!chrome_paint_center("New", 0, row_x, row_y, 2000, 130)) return true; // below the text line (y~105)
      smoke_chrome_note += "; pressing palette row at " + std::to_string(row_x) + "," + std::to_string(row_y);
      smoke_chrome_tabs_before = tabs.size();
      chrome_press(row_x, row_y);
      smoke_chrome_stage = 15;
      smoke_chrome_tick = now;
    } else if (smoke_chrome_stage == 15) {
      if (!palette_open && tabs.size() == smoke_chrome_tabs_before + 1) {
        finish(true, "the frame opened a tab, closed one, pinned one, answered an alert, opened a history row, closed the find bar, and ran a palette command");
        return true;
      }
    }
    return true;
  }
  void send_page_pointer(const char *tag, int x, int y) {
    if (!current().session) return;
    Json command = Json::make_object();
    command.set("tag", tag);
    const double zoom = current().zoom > 0 ? current().zoom : 1.0;
    command.set("x", (x - content.x) / zoom);
    command.set("y", (y - content.y) / zoom);
    current().session->send(command);
  }
  void pointer_move(int x, int y) {
    if (document_scroll_drag) {
      double maximum = 0;
      const auto [track, thumb] = document_scrollbar(&maximum);
      const double travel = std::max(1.0, track.h - thumb.h);
      const double position = document_scroll_drag_start +
          (y - document_scroll_drag_start_y) * maximum / travel;
      scroll_to(position);
      return;
    }
    chrome_pointer("move", x, y);
    const bool inside = content.contains(x, y);
    if (inside || page_pointer_inside || page_pointer_down) send_page_pointer("pointermove", x, y);
    page_pointer_inside = inside;
  }
  void wheel(int x, int y, double delta) {
    if (!content.contains(x, y) || !current().session) return;
    Json command = Json::make_object();
    command.set("tag", "wheel");
    const double zoom = current().zoom > 0 ? current().zoom : 1.0;
    command.set("x", (x - content.x) / zoom);
    command.set("y", (y - content.y) / zoom);
    command.set("delta", delta / zoom);
    current().session->send(command);
  }
  void scroll_key(const char *key) {
    if (!current().session) return;
    Json command = Json::make_object();
    command.set("tag", "scrollkey");
    command.set("key", key);
    current().session->send(command);
  }
  void page_key(const std::string &key) {
    Session *target = key_target();
    if (!target) return;
    Json command = Json::make_object();
    command.set("tag", "key");
    command.set("key", key);
    target->send(command);
  }
  void edit_clipboard(bool cut) {
    Session *target = key_target();
    if (!target) return;
    Json command = Json::make_object();
    command.set("tag", cut ? "cut" : "copy");
    target->send(command);
  }
  // S033 in production for page text fields: every platform composition string
  // passes the ImeService (valid UTF-8, bounded, caret on a character boundary,
  // one composition per tab, commit delivered exactly once) before the page sees
  // it. Windows still composes dead keys itself, so those reach me as WM_CHAR,
  // and the system draws the candidate window, so S033's placement law is not
  // exercised here. The address and search fields are not routed through it yet.
  // S034 in production: the FocusService is the one record of which window is
  // active, which tab is focused and whether a dialog holds the window modal.
  // The shell keeps its own tab vector in step with it (S048 replaces that).
  static constexpr int kWindowId = 1;
  std::unique_ptr<rt::FocusService> focus_service;
  std::set<int> focus_tabs;
  void sync_focus() {
    if (!focus_service) return;
    rt::Error error;
    std::set<int> live;
    for (const auto &tab : tabs) live.insert(tab.id);
    for (auto it = focus_tabs.begin(); it != focus_tabs.end();) {
      if (!live.count(*it)) {
        focus_service->tab_closed(kWindowId, *it, error);
        it = focus_tabs.erase(it);
      } else {
        ++it;
      }
    }
    for (const auto &tab : tabs)
      if (!focus_tabs.count(tab.id) && focus_service->tab_opened(kWindowId, tab.id, false, error))
        focus_tabs.insert(tab.id);
    if (!tabs.empty() && active >= 0 && active < int(tabs.size()))
      focus_service->focus_tab(kWindowId, current().id, error); // refused while a dialog is up
  }
  void window_activated(bool activated) {
    if (!focus_service || !activated) return;
    rt::Error error;
    focus_service->focus_window(kWindowId, error);
  }
  void window_minimized(bool minimized) {
    if (!focus_service) return;
    rt::Error error;
    focus_service->set_occluded(kWindowId, minimized, error);
  }
  std::unique_ptr<rt::ImeService> ime;
  bool ime_forward_update = false;
  std::string ime_commit_text;
  void init_ime() {
    rt::ImeService::Seams seams;
    seams.on_preedit = [this](int, const rt::Composition &composition) {
      if (!ime_forward_update || !page_composing || !current().session) return;
      Json command = Json::make_object();
      command.set("tag", "compositionupdate");
      command.set("text", composition.preedit);
      command.set("caret", double(composition.caret));
      current().session->send(command);
    };
    seams.on_commit = [this](int, const std::string &text) { ime_commit_text = text; };
    ime = std::make_unique<rt::ImeService>(std::move(seams));
  }
  void send_composition_tag(const char *tag, const std::string &text = {}) {
    if (!current().session) return;
    Json command = Json::make_object();
    command.set("tag", tag);
    if (std::string(tag) == "compositioncommit") command.set("text", text);
    current().session->send(command);
  }
  void composition_start() {
    if (address_focus || home_focus || panel_focus || !current().session || !ime) return;
    rt::Error error;
    if (!ime->begin(current().id, error)) {
      log_event("ime", rt::LogLevel::Warn, "begin.refused", {{"code", error.code}});
      return;
    }
    send_composition_tag("compositionstart");
    page_composing = true;
  }
  void composition_update(const std::string &text, size_t caret) {
    if (!page_composing || !current().session || !ime) return;
    rt::Error error;
    ime_forward_update = true;
    const bool ok = ime->update(current().id, text, caret, error);
    ime_forward_update = false;
    if (!ok) log_event("ime", rt::LogLevel::Warn, "update.refused", {{"code", error.code}});
  }
  void composition_commit(const std::string &text) {
    if (!page_composing || !current().session || !ime) return;
    ime_commit_text.clear();
    rt::Error error;
    if (!ime->commit(current().id, text, error)) {
      // A malformed or oversized result never reaches the page; end the
      // composition as a cancel so both sides agree it is over.
      log_event("ime", rt::LogLevel::Warn, "commit.refused", {{"code", error.code}});
      rt::Error ignored;
      ime->cancel(current().id, ignored);
      send_composition_tag("compositioncancel");
    } else {
      send_composition_tag("compositioncommit", ime_commit_text);
    }
    ime_commit_text.clear();
    page_composing = false;
  }
  void composition_cancel() {
    if (!page_composing || !current().session || !ime) return;
    rt::Error error;
    if (ime->composing(current().id)) ime->cancel(current().id, error);
    send_composition_tag("compositioncancel");
    page_composing = false;
  }
  void key_up(WPARAM wp) {
    if (address_focus || home_focus || !key_target()) return;
    std::string key = wp == VK_RETURN ? "Enter" : wp == VK_BACK ? "Backspace"
                      : wp == VK_TAB ? "Tab" : wp == VK_UP ? "ArrowUp"
                      : wp == VK_DOWN ? "ArrowDown" : wp == VK_PRIOR ? "PageUp"
                      : wp == VK_NEXT ? "PageDown" : wp == VK_HOME ? "Home"
                      : wp == VK_END ? "End" : wp == VK_SPACE ? " "
                      : wp == VK_ESCAPE ? "Escape" : wp == VK_DELETE ? "Delete"
                      : wp == VK_LEFT ? "ArrowLeft" : wp == VK_RIGHT ? "ArrowRight"
                      : wp == VK_SHIFT ? "Shift" : wp == VK_CONTROL ? "Control"
                      : wp == VK_MENU ? "Alt" : std::string();
    if (key.empty() && wp >= 'A' && wp <= 'Z') {
      char letter = char(wp);
      const bool shifted = GetKeyState(VK_SHIFT) < 0;
      const bool caps = (GetKeyState(VK_CAPITAL) & 1) != 0;
      if (!(shifted ^ caps)) letter = char(letter - 'A' + 'a');
      key.assign(1, letter);
    } else if (key.empty() && wp >= '0' && wp <= '9') {
      key.assign(1, char(wp));
    }
    if (key.empty()) return;
    Json command = Json::make_object();
    command.set("tag", "keyup");
    command.set("key", key);
    key_target()->send(command);
  }
  void pointer_down(int x, int y) {
    if (!content.contains(x, y)) return;
    double maximum = 0;
    const auto [track, thumb] = document_scrollbar(&maximum);
    if (maximum > 0 && track.contains(x, y)) {
      page_pointer_down = false;
      if (thumb.contains(x, y)) {
        document_scroll_drag = true;
        document_scroll_drag_start_y = y;
        document_scroll_drag_start = current().scroll;
      } else {
        const double travel = std::max(1.0, track.h - thumb.h);
        const double ratio = std::clamp((y - track.y - thumb.h / 2) / travel, 0.0, 1.0);
        scroll_to(ratio * maximum);
      }
      return;
    }
    page_pointer_down = true;
    send_page_pointer("pointerdown", x, y);
  }
  void pointer_up(int x, int y) {
    if (document_scroll_drag) { document_scroll_drag = false; return; }
    if (!page_pointer_down) return;
    send_page_pointer("pointerup", x, y);
    page_pointer_down = false;
  }
  void click(int x, int y) {
    if (palette_open) {
      bool ran = false;
      for (auto it = hits.rbegin(); it != hits.rend(); ++it)
        if (it->rect.contains(x, y) && it->action == "palette-run") {
          run_command(it->value);
          ran = true;
          break;
        }
      if (!ran)
        palette_open = false;
      InvalidateRect(hwnd, nullptr, FALSE);
      return;
    }
    if (menu_click(x, y)) {
      InvalidateRect(hwnd, nullptr, FALSE);
      return;
    }
    for (auto it = hits.rbegin(); it != hits.rend(); ++it)
      if (it->rect.contains(x, y)) {
        auto h = *it;
        address_focus = h.action == "address";
        home_focus = h.action == "home-search";
        panel_focus = h.action == "panel-search";
        if (panel_focus)
          panel_search_caret = panel_search.size();
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
          panel_search.clear();
          panel_search_caret = 0;
        } else if (h.action == "history") {
          panel_history = !panel_history;
          panel_bookmarks = false;
          panel_search.clear();
          panel_search_caret = 0;
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
        } else if (h.action == "setting-block-autoplay") {
          profile.block_autoplay = !profile.block_autoplay;
          save_profile();
          status = profile.block_autoplay ? "Autoplay needs my click first"
                                          : "Pages may start media on their own";
        } else if (h.action == "setting-https-only") {
          profile.https_only = !profile.https_only; save_profile();
          status = profile.https_only ? "I upgrade every request to HTTPS"
                                      : "I upgrade only the hosts that asked me to";
        } else if (h.action == "setting-memory-cap") {
          begin_setting_edit("memoryCap",
                             profile.memory_limit_mb > 0
                                 ? std::to_string(profile.memory_limit_mb)
                                 : std::string());
        } else if (h.action == "setting-filter-file") {
          begin_setting_edit("filterFile", profile.filter_list_path);
        } else if (h.action == "shield-note") {
          status = h.value.empty() ? "Nothing blocked on this page yet" : h.value;
        } else if (h.action == "setting-third-party-cookies") {
          profile.block_third_party_cookies = !profile.block_third_party_cookies; save_profile();
        } else if (h.action == "setting-dnt") {
          profile.send_dnt = !profile.send_dnt; save_profile();
        } else if (h.action == "setting-referrer") {
          profile.strip_referrer = !profile.strip_referrer; save_profile();
        } else if (h.action == "setting-clear-exit") {
          profile.clear_on_exit = !profile.clear_on_exit; save_profile();
        } else if (h.action == "vpn-test") {
          start_tunnel_test();
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
        } else if (h.action == "setting-spell-builtin") {
          profile.spell_builtin = !profile.spell_builtin;
          spell.builtin_enabled = profile.spell_builtin;
          save_profile();
        } else if (h.action == "setting-spell-source") {
          begin_setting_edit("spellSource", profile.spell_dict_source);
        } else if (h.action == "setting-spell-add") {
          if (!home_search.empty()) { profile.add_user_word(home_search); spell_reload_users(); save_profile(); status = "I added the word"; }
          else status = "Type a word in my home search box first";
        } else if (h.action == "setting-spell-remove") {
          if (!profile.user_words.empty()) { profile.remove_user_word(profile.user_words.back()); spell_reload_users(); save_profile(); }
        } else if (h.action == "setting-high-contrast") {
          profile.high_contrast = !profile.high_contrast; save_profile();
          refresh_viewport_environment();
        } else if (h.action == "setting-reduce-motion") {
          profile.reduce_motion = !profile.reduce_motion; save_profile();
          refresh_viewport_environment();
        } else if (h.action == "setting-tab-close") {
          profile.show_tab_close_buttons = !profile.show_tab_close_buttons; save_profile();
        } else if (h.action == "setting-check-update") {
          check_for_update();
        } else if (h.action == "minimize")
          window_command("minimize");
        else if (h.action == "maximize")
          window_command(IsZoomed(hwnd) ? "restore" : "maximize");
        else if (h.action == "close-window")
          PostMessageW(hwnd, WM_CLOSE, 0, 0);
        else if (h.action == "find-bar")
          address_focus = home_focus = false;
        else if (h.action == "find-close")
          close_find();
        else if (h.action == "recovery-keep") {
          recovery_dismissed = true;
        } else if (h.action == "recovery-fresh") {
          // Start fresh means exactly that: everything open goes to the reopen
          // stack, my saved session is cleared, and I land on a clean tab.
          for (auto &tab : tabs)
            stop_session(tab);
          forget_all_tabs();
          closed_data.clear();
          tab_model.clear_closed();
          profile.session_tabs.clear();
          save_session();
          crashed_last = recovery_dismissed = false;
          new_tab();
        } else if (h.action == "update-install") {
          install_update();
        } else if (h.action == "update-restart") {
          apply_ready_update();
        } else if (h.action == "update-later") {
          update_snoozed = true;
        } else if (h.action == "update-dismiss") {
          profile.dismissed_update = next_update.version;
          save_profile();
        } else if (h.action == "setting-clear-log") {
          profile.append_error_log("log cleared by the owner");
          std::error_code ec;
          std::filesystem::remove(profile.directory + "/owned-errors.log", ec);
          reload_log_cache();
        } else if (h.action == "setting-panic") {
          profile.panic_enabled = !profile.panic_enabled;
          save_profile();
        } else if (h.action == "setting-panic-url") {
          begin_setting_edit("panicUrl", profile.panic_url);
        } else if (h.action == "setting-vault-lock") {
          begin_setting_edit("vaultLockMin", std::to_string(profile.vault_lock_minutes));
        } else if (h.action == "workspaces") {
          panel_workspaces = !panel_workspaces;
          panel_bookmarks = panel_history = panel_downloads = false;
        } else if (h.action == "workspace-new") {
          begin_setting_edit("workspaceNew", "");
        } else if (h.action == "workspace-rename") {
          if (h.index >= 0 && size_t(h.index) < workspaces.size())
            begin_setting_edit("workspaceRename:" + std::to_string(h.index),
                               workspaces[size_t(h.index)].name);
        } else if (h.action == "workspace-close") {
          workspace_close(h.index);
        } else if (h.action == "workspace-open") {
          workspace_switch(size_t(h.index));
        } else if (h.action == "suggest") {
          navigate(h.value);
        } else if (h.action == "dial-remove") {
          if (h.index >= 0 && size_t(h.index) < profile.speed_dial.size()) {
            profile.speed_dial.erase(profile.speed_dial.begin() + h.index);
            save_profile();
          }
        } else if (h.action == "dial-add") {
          // The + tile takes whatever is in my home search box as the address,
          // so typing a site and clicking + is all it takes to pin it.
          auto target = navigate_input(home_search, profile.search_template());
          if (target && target->network()) {
            bool exists = false;
            for (auto &d : profile.speed_dial)
              exists = exists || d.url == target->href();
            if (!exists) {
              profile.speed_dial.push_back({target->host, target->href()});
              save_profile();
              status = "I added it to my speed dial";
            } else
              status = "That site is already on my speed dial";
          } else
            status = "Type an address in my search box first";
        } else if (h.action == "download-reveal") {
          if (!h.value.empty()) {
            // Explorer selects the file for a /select switch with a quoted
            // path, built as a named string so the call itself stays simple.
            std::wstring select = L"/select,\"" + utf16_of(h.value) + L"\"";
            ShellExecuteW(hwnd, L"open", L"explorer.exe", select.c_str(),
                          nullptr, SW_SHOWNORMAL);
          }
        } else if (h.action == "downloads-folder") {
          ShellExecuteW(hwnd, L"open", utf16_of(downloads.directory()).c_str(),
                        nullptr, nullptr, SW_SHOWNORMAL);
        } else if (h.action == "downloads-pickdir") {
          auto chosen = pick_folder(hwnd);
          if (!chosen.empty()) {
            if (profile.set_download_directory(chosen)) {
              downloads.set_directory(profile.download_directory);
              save_profile();
              status = "Downloads will save to " + profile.download_directory;
            } else
              status = "I cannot write to that folder";
          }
        }
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
  // S032 in production. One registry, bound once; the key handler asks it first.
  rt::ShortcutRegistry shortcuts;
  void bind_shortcuts() {
    rt::Error error;
    if (!rt::bind_stock_shortcuts(shortcuts, error))
      log_event("keys", rt::LogLevel::Error, "bind.failed", {{"code", error.code}});
  }
  std::string resolve_shortcut(WPARAM wp, bool ctrl, bool shift, bool alt) const {
    std::string token;
    if ((wp >= 'A' && wp <= 'Z') || (wp >= '0' && wp <= '9')) token = std::string(1, char(wp));
    else if (wp == VK_OEM_PLUS || wp == VK_ADD) token = "=";
    else if (wp == VK_OEM_MINUS || wp == VK_SUBTRACT) token = "-";
    else if (wp == VK_TAB) token = "Tab";
    else if (wp == VK_LEFT) token = "Left";
    else if (wp == VK_RIGHT) token = "Right";
    else if (wp == VK_F5) token = "F5";
    if (token.empty()) return "";
    rt::KeyEvent event;
    event.ctrl = ctrl;
    event.shift = shift;
    event.alt = alt;
    event.key = token;
    rt::KeyContext context;
    context.editable_focused = address_focus || home_focus || panel_focus || find_open;
    return shortcuts.resolve(event, context);
  }
  // Returns false when the command does not apply right now, so the key falls
  // through to ordinary handling exactly as an unbound key would.
  bool run_shortcut(const std::string &command) {
    if (command == "app.palette") {
      palette_open = !palette_open;
      palette_text.clear();
      palette_caret = 0;
      palette_index = 0;
    } else if (command == "app.panic") {
      panic();
    } else if (command == "tab.reopen") {
      reopen_tab();
    } else if (command == "tab.next" || command == "tab.previous") {
      if (tabs.size() <= 1) return false;
      const int step = command == "tab.next" ? 1 : -1;
      select_tab((active + step + int(tabs.size())) % int(tabs.size()));
      return true;
    } else if (command == "tab.move-left" || command == "tab.move-right") {
      move_tab(active, command == "tab.move-left" ? -1 : 1);
    } else if (command == "tab.mute") {
      tab_set_muted(active, !current().muted);
      return true;
    } else if (command == "find.open") {
      open_find();
      return true;
    } else if (command == "zoom.in") {
      set_zoom(current().zoom * 1.1);
      return true;
    } else if (command == "zoom.out") {
      set_zoom(current().zoom / 1.1);
      return true;
    } else if (command == "zoom.reset") {
      set_zoom(1.0);
      return true;
    } else if (command == "panel.downloads") {
      panel_downloads = !panel_downloads;
      panel_bookmarks = panel_history = false;
    } else if (command == "address.focus") {
      address_focus = true;
      address_caret = address.size();
    } else if (command == "tab.new") {
      new_tab();
      return true;
    } else if (command == "tab.close") {
      close_tab(active);
      return true;
    } else if (command == "page.reload") {
      navigate(current().url, false);
      return true;
    } else if (command == "bookmark.add") {
      profile.bookmark(current().title, current().url);
      save_profile();
      status = "Bookmark saved";
    } else {
      return false;
    }
    InvalidateRect(hwnd, nullptr, FALSE);
    return true;
  }
  void key(UINT, WPARAM wp, LPARAM) {
    if (!frame_dialogs.empty()) {
      // A modal dialog owns the keyboard: no shortcut, address or page sees a key until it is answered.
      chrome_focus = true;
      const char *name = wp == VK_RETURN ? "Enter" : wp == VK_ESCAPE ? "Escape" : wp == VK_TAB ? "Tab"
                         : wp == VK_LEFT ? "ArrowLeft" : wp == VK_RIGHT ? "ArrowRight"
                         : wp == VK_BACK ? "Backspace" : wp == VK_DELETE ? "Delete" : nullptr;
      if (name) page_key(name);
      return;
    }
    bool ctrl = GetKeyState(VK_CONTROL) < 0;
    bool shift = GetKeyState(VK_SHIFT) < 0;
    if (menu_open && wp == VK_ESCAPE) {
      close_menu();
      InvalidateRect(hwnd, nullptr, FALSE);
      return;
    }
    // S032: the broker's registry claims a chord before any other handling. While
    // the command palette is open only the palette and panic chords are claimed,
    // as before; every other key is the palette's to consume.
    {
      const std::string command = resolve_shortcut(wp, ctrl, shift, GetKeyState(VK_MENU) < 0);
      if (!command.empty() && (!palette_open || command == "app.palette" || command == "app.panic") &&
          run_shortcut(command))
        return;
    }
    if (palette_open) {
      auto matches = palette_matches();
      if (wp == VK_ESCAPE) {
        palette_open = false;
      } else if (wp == VK_RETURN) {
        if (palette_index >= 0 && palette_index < int(matches.size()))
          run_command(matches[size_t(palette_index)]->id);
      } else if (wp == VK_UP) {
        palette_index = std::max(0, palette_index - 1);
      } else if (wp == VK_DOWN) {
        palette_index = std::min(int(matches.size()) - 1, palette_index + 1);
      } else if (wp == VK_BACK) {
        size_t previous = previous_cp(palette_text, palette_caret);
        palette_text.erase(previous, palette_caret - previous);
        palette_caret = previous;
        palette_index = 0;
      } else if (wp == VK_LEFT) {
        palette_caret = previous_cp(palette_text, palette_caret);
      } else if (wp == VK_RIGHT) {
        palette_caret = next_cp(palette_text, palette_caret);
      } else {
        InvalidateRect(hwnd, nullptr, FALSE);
        return;
      }
      InvalidateRect(hwnd, nullptr, FALSE);
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
    if (ctrl && (wp == 'C' || wp == 'X') && !address_focus && !home_focus &&
        !panel_focus && !find_open && current().session) {
      edit_clipboard(wp == 'X');
      return;
    }
    if (ctrl && wp == 'V') {
      paste();
      return;
    }
    if (wp == VK_ESCAPE) {
      if (current().loading)
        stop();
      address_focus = home_focus = false;
      InvalidateRect(hwnd, nullptr, FALSE);
      return;
    }
    if (panel_focus) {
      if (wp == VK_ESCAPE) {
        panel_focus = false;
      } else if (wp == VK_RETURN) {
        // Enter opens the first match - bookmarks first on the bookmark panel.
        std::string needle = ascii_lower(panel_search);
        auto matches = [&](const std::string &title, const std::string &url) {
          return needle.empty() || ascii_lower(title + "  " + url).find(needle) !=
                                       std::string::npos;
        };
        std::string open_url;
        if (panel_bookmarks) {
          for (auto &b : profile.bookmarks)
            if (matches(b.title, b.url)) { open_url = b.url; break; }
        } else {
          for (auto &hh : profile.history)
            if (matches(hh.title, hh.url)) { open_url = hh.url; break; }
        }
        if (!open_url.empty()) {
          panel_bookmarks = panel_history = false;
          panel_focus = false;
          navigate(open_url);
        }
      } else if (wp == VK_BACK) {
        size_t previous = previous_cp(panel_search, panel_search_caret);
        panel_search.erase(previous, panel_search_caret - previous);
        panel_search_caret = previous;
      } else if (wp == VK_DELETE) {
        panel_search.erase(panel_search_caret,
                           next_cp(panel_search, panel_search_caret) - panel_search_caret);
      } else if (wp == VK_LEFT) {
        panel_search_caret = previous_cp(panel_search, panel_search_caret);
      } else if (wp == VK_RIGHT) {
        panel_search_caret = next_cp(panel_search, panel_search_caret);
      } else if (wp == VK_HOME)
        panel_search_caret = 0;
      else if (wp == VK_END)
        panel_search_caret = panel_search.size();
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
      if (wp == VK_TAB && home_focus && profile.spell_check) {
        // The squiggle at my cursor takes my first suggestion when I press Tab.
        for (const auto &mark : spell_marks) {
          if (caret >= mark.start && caret <= mark.start + mark.length &&
              !mark.suggestions.empty()) {
            text.replace(mark.start, mark.length, mark.suggestions[0]);
            caret = mark.start + mark.suggestions[0].size();
            spell_cover.clear();
            break;
          }
        }
        InvalidateRect(hwnd, nullptr, FALSE);
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
    if (wp == VK_PRIOR || wp == VK_NEXT || wp == VK_UP || wp == VK_DOWN ||
        wp == VK_HOME || wp == VK_END) {
      const char *key = wp == VK_PRIOR ? "PageUp" : wp == VK_NEXT ? "PageDown"
                        : wp == VK_UP ? "ArrowUp" : wp == VK_DOWN ? "ArrowDown"
                        : wp == VK_HOME ? "Home" : "End";
      page_key(key);
      return;
    }
    std::string page_key_name = wp == VK_RETURN ? "Enter" : wp == VK_BACK ? "Backspace"
        : wp == VK_TAB ? "Tab" : wp == VK_DELETE ? "Delete"
        : wp == VK_LEFT ? "ArrowLeft" : wp == VK_RIGHT ? "ArrowRight"
        : wp == VK_CONTROL ? "Control" : wp == VK_SHIFT ? "Shift"
        : wp == VK_MENU ? "Alt" : wp == VK_SPACE ? "Space" : std::string();
    if (page_key_name.empty() && wp >= 'A' && wp <= 'Z') {
      char letter = char(wp);
      if (!(shift ^ ((GetKeyState(VK_CAPITAL) & 1) != 0))) letter = char(letter - 'A' + 'a');
      page_key_name.assign(1, letter);
    } else if (page_key_name.empty() && wp >= '0' && wp <= '9') {
      page_key_name.assign(1, char(wp));
    }
    if (!page_key_name.empty() && !address_focus && !home_focus && !panel_focus && !find_open)
      page_key(page_key_name);
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
    if (palette_open) {
      if (palette_text.size() + text.size() <= 120) {
        palette_text.insert(palette_caret, text);
        palette_caret += text.size();
        palette_index = 0;
      }
      InvalidateRect(hwnd, nullptr, FALSE);
      return;
    }
    if (panel_focus) {
      if (panel_search.size() + text.size() <= 200) {
        panel_search.insert(panel_search_caret, text);
        panel_search_caret += text.size();
      }
      InvalidateRect(hwnd, nullptr, FALSE);
      return;
    }
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
    } else if (key_target()) {
      Json j = Json::make_object();
      j.set("tag", "key");
      j.set("key", "");
      j.set("text", text);
      key_target()->send(j);
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
  void load_workspaces_state() {
    if (private_mode)
      return;
    Json state = profile.load_workspaces();
    if (state.type != Json::Type::Object)
      return;
    if (const Json *list = state.find("list"))
      if (list->type == Json::Type::Array)
        for (auto &entry : list->array) {
          if (workspaces.size() >= 32)
            break;
          Workspace w;
          w.name = entry.str("name", "Main");
          w.active = int(entry.num("active", 0));
          w.pinned = entry.str("pinned");
          w.muted = entry.str("muted");
          if (const Json *tabs_json = entry.find("tabs"))
            if (tabs_json->type == Json::Type::Array)
              for (auto &t : tabs_json->array) {
                if (w.tabs.size() >= 64)
                  break;
                if (t.type == Json::Type::String)
                  if (auto parsed = parse_url(t.string))
                    if (parsed->network())
                      w.tabs.push_back(t.string);
              }
          workspaces.push_back(std::move(w));
        }
    workspace = size_t(std::clamp(int(state.num("active", 0)), 0,
                                  int(workspaces.empty() ? 0 : workspaces.size() - 1)));
  }
  void capture_workspace() {
    if (workspaces.empty())
      workspaces.push_back(Workspace{});
    if (workspace >= workspaces.size())
      workspace = 0;
    Workspace &w = workspaces[workspace];
    w.tabs.clear();
    w.pinned.clear();
    w.muted.clear();
    for (auto &tab : tabs) {
      w.tabs.push_back(tab.url);
      w.pinned += tab.pinned ? '1' : '0';
      w.muted += tab.muted ? '1' : '0';
    }
    w.active = active;
  }
  bool save_workspaces() {
    if (private_mode || !profile.workspaces)
      return true;
    capture_workspace();
    Json list = Json::make_array();
    for (auto &w : workspaces) {
      Json entry = Json::make_object();
      entry.set("name", w.name);
      Json urls = Json::make_array();
      for (auto &u : w.tabs)
        urls.array.push_back(Json(u));
      entry.set("tabs", urls);
      entry.set("active", double(w.active));
      entry.set("pinned", w.pinned);
      entry.set("muted", w.muted);
      list.array.push_back(std::move(entry));
    }
    Json state = Json::make_object();
    state.set("active", double(workspace));
    state.set("list", list);
    if (!profile.save_workspaces(state)) {
      if (profile_error.empty()) profile_error = profile.error;
      return false;
    }
    return true;
  }
  // A workspace swap stops every live session and rebuilds the strip from the
  // stored list, exactly like my launch restore does.
  void load_workspace_tabs(Workspace &w) {
    for (auto &tab : tabs)
      stop_session(tab);
    forget_all_tabs();
    for (auto &u : w.tabs)
      new_tab(u, false);
    if (tabs.empty())
      new_tab();
    for (size_t i = 0; i < tabs.size(); ++i) {
      rt::Error flag_error;
      if (i < w.pinned.size()) tab_model.set_pinned(tabs[i].id, w.pinned[i] == '1', flag_error);
      if (i < w.muted.size()) tab_model.set_muted(tabs[i].id, w.muted[i] == '1', flag_error);
    }
    apply_model_order();
    active = std::clamp(w.active, 0, int(tabs.size()) - 1);
    select_tab(active);
  }
  void workspace_switch(size_t index) {
    if (index == workspace || index >= workspaces.size())
      return;
    capture_workspace();
    workspace = index;
    load_workspace_tabs(workspaces[workspace]);
    status = "Workspace: " + workspaces[workspace].name;
    save_session();
  }
  void workspace_new(const std::string &name) {
    if (workspaces.size() >= 32)
      return;
    capture_workspace();
    Workspace w;
    w.name = name.empty() ? "Workspace " + std::to_string(workspaces.size() + 1)
                          : name;
    workspaces.push_back(w);
    workspace = workspaces.size() - 1;
    load_workspace_tabs(workspaces[workspace]);
    save_session();
  }
  void workspace_close(int index) {
    if (index < 0 || index >= int(workspaces.size()))
      return;
    if (workspaces.size() <= 1) {
      status = "My last workspace cannot be closed";
      return;
    }
    workspaces.erase(workspaces.begin() + index);
    if (size_t(index) < workspace)
      --workspace;
    else if (size_t(index) == workspace) {
      workspace = std::min(size_t(index), workspaces.size() - 1);
      load_workspace_tabs(workspaces[workspace]);
    }
    save_session();
  }
  // The panic button is my one-way escape hatch: every tab goes onto the
  // reopen stack, a harmless page opens, and Ctrl+Shift+T still brings things
  // back if I pressed it by mistake.
  void panic() {
    if (!profile.panic_enabled) {
      status = "Panic is off - turn it on in settings first";
      InvalidateRect(hwnd, nullptr, FALSE);
      return;
    }
    for (auto &tab : tabs) {
      remember_closed_tab(tab);
      stop_session(tab);
    }
    forget_all_tabs();
    std::string target = profile.panic_url.empty() ? profile.home_page : profile.panic_url;
    new_tab(target.empty() ? "psychovault://newtab" : target);
    status = "Panic - Ctrl+Shift+T brings my tabs back";
  }
  // The command palette is a box that runs named interface actions. Each row
  // lists its chord when there is one, so the palette doubles as a cheat sheet.
  struct PaletteCommand {
    const char *label;
    const char *chord;
    const char *id;
  };
  static const std::vector<PaletteCommand> &palette_commands() {
    static const std::vector<PaletteCommand> commands = {
        {"New tab", "Ctrl+T", "palette-new-tab"},
        {"Close this tab", "Ctrl+W", "palette-close-tab"},
        {"Reopen closed tab", "Ctrl+Shift+T", "palette-reopen"},
        {"Duplicate tab", "", "palette-duplicate"},
        {"Pin or unpin tab", "", "palette-pin"},
        {"Mute or unmute tab", "Ctrl+M", "palette-mute"},
        {"Back", "", "palette-back"},
        {"Forward", "", "palette-forward"},
        {"Reload", "Ctrl+R", "palette-reload"},
        {"Stop loading", "Esc", "palette-stop"},
        {"Go home", "", "palette-home"},
        {"Bookmarks", "", "palette-bookmarks"},
        {"History", "", "palette-history"},
        {"Downloads", "Ctrl+J", "palette-downloads"},
        {"Workspaces", "", "palette-workspaces"},
        {"Settings", "", "palette-settings"},
        {"Find in page", "Ctrl+F", "palette-find"},
        {"Zoom in", "Ctrl +", "palette-zoom-in"},
        {"Zoom out", "Ctrl -", "palette-zoom-out"},
        {"Reset zoom", "Ctrl+0", "palette-zoom-reset"},
        {"Toggle ad blocker", "", "palette-adblock"},
        {"Clear browsing history", "", "palette-clear-history"},
        {"Check for an update", "", "palette-update"},
        {"Panic", "Ctrl+Shift+Q", "palette-panic"},
    };
    return commands;
  }
  std::vector<const PaletteCommand *> palette_matches() const {
    std::vector<const PaletteCommand *> out;
    std::string needle = ascii_lower(palette_text);
    for (auto &command : palette_commands())
      if (needle.empty() || ascii_lower(command.label).find(needle) != std::string::npos)
        out.push_back(&command);
    return out;
  }
  void run_command(const std::string &id) {
    palette_open = false;
    if (id == "palette-new-tab") new_tab();
    else if (id == "palette-close-tab") close_tab(active);
    else if (id == "palette-reopen") reopen_tab();
    else if (id == "palette-duplicate") {
      Tab t;
      t.id = model_open(current().url, active + 1);
      if (t.id < 0) return;
      t.url = current().url;
      t.title = current().title;
      t.started = false;
      const int place = model_position(t.id);
      tabs.insert(tabs.begin() + place, std::move(t));
      select_tab(place);
    } else if (id == "palette-pin") tab_set_pinned(active, !current().pinned);
    else if (id == "palette-mute") tab_set_muted(active, !current().muted);
    else if (id == "palette-back") back();
    else if (id == "palette-forward") forward();
    else if (id == "palette-reload") navigate(current().url, false);
    else if (id == "palette-stop") stop();
    else if (id == "palette-home") navigate(profile.home_page);
    else if (id == "palette-bookmarks") { panel_bookmarks = true; panel_history = panel_downloads = panel_workspaces = false; panel_other.clear(); }
    else if (id == "palette-history") { panel_history = true; panel_bookmarks = panel_downloads = panel_workspaces = false; panel_other.clear(); }
    else if (id == "palette-downloads") { panel_downloads = true; panel_bookmarks = panel_history = panel_workspaces = false; panel_other.clear(); }
    else if (id == "palette-workspaces") { panel_bookmarks = panel_history = panel_downloads = panel_workspaces = false; panel_other = "workspaces"; }
    else if (id == "palette-settings") navigate("psychovault://settings");
    else if (id == "palette-find") open_find();
    else if (id == "palette-zoom-in") set_zoom(current().zoom * 1.1);
    else if (id == "palette-zoom-out") set_zoom(current().zoom / 1.1);
    else if (id == "palette-zoom-reset") set_zoom(1.0);
    else if (id == "palette-adblock") {
      profile.block_ads = !profile.block_ads;
      save_profile();
      status = profile.block_ads ? "Ad blocker on" : "Ad blocker off";
    } else if (id == "palette-clear-history") {
      profile.clear_history();
      save_profile();
      status = "I cleared my browsing history";
    } else if (id == "palette-update") check_for_update();
    else if (id == "palette-panic") panic();
    InvalidateRect(hwnd, nullptr, FALSE);
  }
  // Copying a vault password: it goes to the clipboard, and a minute later the clipboard is emptied unless
  // something else has been copied since (the clipboard sequence number is how I know).
  ULONGLONG hub_refreshed_at = 0;
  DWORD vault_clipboard_sequence = 0;
  ULONGLONG vault_clipboard_clear_at = 0;
  void vault_copy(const std::string &site) {
    if (!vault_service) return;
    std::string password;
    rt::Error failure;
    if (!vault_service->reveal(site, password, failure)) {
      status = failure.message;
      return;
    }
    write_clipboard(password);
    std::fill(password.begin(), password.end(), '\0');
    vault_clipboard_sequence = GetClipboardSequenceNumber();
    vault_clipboard_clear_at = GetTickCount64() + 60000;
    status = "Password for " + site + " is on the clipboard for a minute";
    InvalidateRect(hwnd, nullptr, FALSE);
  }
  // Backup, restore and CSV import. The file is chosen in a standard dialog and read or written here, on the
  // window thread; the vault service does the sealing and merging. The page reads the outcome as the notice.
  static std::string file_dialog(HWND owner, bool save, const wchar_t *filter, const wchar_t *title,
                                 const wchar_t *default_name, const wchar_t *extension) {
    wchar_t path[MAX_PATH * 2] = {0};
    if (default_name) wcsncpy_s(path, default_name, _TRUNCATE);
    OPENFILENAMEW ofn{};
    ofn.lStructSize = sizeof(ofn);
    ofn.hwndOwner = owner;
    ofn.lpstrFilter = filter;
    ofn.lpstrFile = path;
    ofn.nMaxFile = DWORD(std::size(path));
    ofn.lpstrTitle = title;
    ofn.lpstrDefExt = extension;
    ofn.Flags = OFN_NOCHANGEDIR | OFN_HIDEREADONLY | (save ? OFN_OVERWRITEPROMPT : OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST);
    if (!(save ? GetSaveFileNameW(&ofn) : GetOpenFileNameW(&ofn))) return {};
    const int size = WideCharToMultiByte(CP_UTF8, 0, path, -1, nullptr, 0, nullptr, nullptr);
    std::string out(size > 0 ? size_t(size - 1) : 0, '\0');
    if (size > 0) WideCharToMultiByte(CP_UTF8, 0, path, -1, out.data(), size, nullptr, nullptr);
    return out;
  }
  void vault_file_task(const BridgeTask &task) {
    if (!vault_service) return;
    std::string notice;
    rt::Error failure;
    int added = 0, skipped = 0;
    if (task.method == "vault:backup") {
      std::string blob;
      if (!vault_service->backup(task.payload.str("password"), blob, failure)) notice = failure.message;
      else {
        const std::string file = file_dialog(hwnd, true, L"PsychoVault backup (*.pvbackup)\0*.pvbackup\0All files\0*.*\0",
                                             L"Save a vault backup", L"psychovault.pvbackup", L"pvbackup");
        std::string write_error;
        if (file.empty()) notice = "Backup cancelled";
        else if (!write_file_atomic(file, blob, write_error)) notice = "The backup could not be written: " + write_error;
        else notice = "Backup saved. Keep its password safe - it cannot be recovered.";
      }
    } else if (task.method == "vault:restore") {
      const std::string file = file_dialog(hwnd, false, L"PsychoVault backup (*.pvbackup)\0*.pvbackup\0All files\0*.*\0",
                                           L"Restore a vault backup", nullptr, nullptr);
      if (file.empty()) notice = "Restore cancelled";
      else {
        std::string blob;
        try { blob = read_text_file(file); } catch (const std::exception &) { blob.clear(); }
        if (blob.empty()) notice = "That file could not be read";
        else if (!vault_service->restore(blob, task.payload.str("password"), added, skipped, failure)) notice = failure.message;
        else notice = "Restored " + std::to_string(added) + " login(s); skipped " + std::to_string(skipped) + " already in the vault.";
      }
    } else {
      const std::string file = file_dialog(hwnd, false, L"CSV files (*.csv)\0*.csv\0All files\0*.*\0",
                                           L"Import logins from a CSV file", nullptr, nullptr);
      if (file.empty()) notice = "Import cancelled";
      else {
        std::string text;
        try { text = read_text_file(file); } catch (const std::exception &) { text.clear(); }
        if (text.empty()) notice = "That file could not be read";
        else if (!vault_service->import_csv(text, added, skipped, failure)) notice = failure.message;
        else notice = "Imported " + std::to_string(added) + " login(s); skipped " + std::to_string(skipped) + ".";
        std::fill(text.begin(), text.end(), '\0');
      }
    }
    {
      std::lock_guard lock(settings_hub->mutex);
      settings_hub->vault_notice = notice;
    }
    status = notice;
    InvalidateRect(hwnd, nullptr, FALSE);
  }
  void vault_clipboard_tick() {
    if (!vault_clipboard_clear_at || GetTickCount64() < vault_clipboard_clear_at) return;
    vault_clipboard_clear_at = 0;
    if (GetClipboardSequenceNumber() == vault_clipboard_sequence && OpenClipboard(hwnd)) {
      EmptyClipboard();
      CloseClipboard();
      status = "The vault emptied the clipboard";
    }
  }
  void write_clipboard(const std::string &text) {
    int n = MultiByteToWideChar(CP_UTF8, 0, text.c_str(), int(text.size()),
                                nullptr, 0);
    if (n <= 0 || !OpenClipboard(hwnd))
      return;
    EmptyClipboard();
    HGLOBAL handle = GlobalAlloc(GMEM_MOVEABLE, (n + 1) * sizeof(wchar_t));
    if (handle) {
      auto *data = static_cast<wchar_t *>(GlobalLock(handle));
      MultiByteToWideChar(CP_UTF8, 0, text.c_str(), int(text.size()), data, n);
      data[n] = 0;
      GlobalUnlock(handle);
      SetClipboardData(CF_UNICODETEXT, handle);
    }
    CloseClipboard();
  }
  void open_menu(int tab, int x, int y, std::vector<MenuLine> items) {
    menu_open = true;
    menu_tab = tab;
    menu_x = std::clamp(x, 44, width - 230);
    menu_y = std::clamp(y, 40, height - int(items.size()) * 30 - 40);
    menu_items = std::move(items);
    InvalidateRect(hwnd, nullptr, FALSE);
  }
  void close_menu() {
    menu_open = false;
    menu_tab = -1;
    menu_items.clear();
  }
  // True when the click was consumed by the menu - either running a row or
  // closing it. The menu owns its own geometry so it never needs hits.
  bool menu_click(int x, int y) {
    if (!menu_open)
      return false;
    const int w = 220, h = int(menu_items.size()) * 30 + 10;
    if (x < menu_x || x > menu_x + w || y < menu_y || y > menu_y + h ||
        y < menu_y + 5) {
      close_menu();
      return false;
    }
    int index = (y - menu_y - 5) / 30;
    if (index >= 0 && index < int(menu_items.size()))
      apply_menu(menu_items[size_t(index)].action);
    close_menu();
    return true;
  }
  void apply_menu(const std::string &id) {
    if (id == "menu-pin" && menu_tab >= 0 && menu_tab < int(tabs.size()))
      tab_set_pinned(menu_tab, !tabs[size_t(menu_tab)].pinned);
    else if (id == "menu-mute" && menu_tab >= 0 && menu_tab < int(tabs.size()))
      tab_set_muted(menu_tab, !tabs[size_t(menu_tab)].muted);
    else if (id == "menu-move-left")
      move_tab(menu_tab, -1);
    else if (id == "menu-move-right")
      move_tab(menu_tab, 1);
    else if (id == "menu-duplicate" && menu_tab >= 0 &&
             menu_tab < int(tabs.size())) {
      Tab t;
      t.id = model_open(tabs[size_t(menu_tab)].url, menu_tab + 1);
      if (t.id < 0) return;
      t.url = tabs[size_t(menu_tab)].url;
      t.title = tabs[size_t(menu_tab)].title;
      t.started = false;
      const int place = model_position(t.id);
      tabs.insert(tabs.begin() + place, std::move(t));
      active = place;
      select_tab(active);
    } else if (id == "menu-close-others") {
      for (int i = int(tabs.size()) - 1; i >= 0; --i)
        if (i != menu_tab && !tabs[size_t(i)].pinned)
          close_tab(i);
    } else if (id == "menu-create") {
      new_tab();
    } else if (id == "menu-reopen") {
      reopen_tab();
    } else if (id == "menu-close") {
      close_tab(menu_tab);
    } else if (id == "menu-back" && tabs[size_t(active)].can_back()) {
      back();
    } else if (id == "menu-forward" && tabs[size_t(active)].can_forward()) {
      forward();
    } else if (id == "menu-reload") {
      navigate(current().url, false);
    } else if (id == "menu-copy-url") {
      write_clipboard(current().url);
    } else if (id == "menu-view-source") {
      navigate("view-source:" + current().url);
    } else if (id == "menu-dial-add") {
      // Only a real address becomes a tile - internal pages stay off the dial.
      if (starts(current().url, "http://") || starts(current().url, "https://")) {
        bool exists = false;
        for (auto &d : profile.speed_dial)
          exists = exists || d.url == current().url;
        if (!exists) {
          profile.speed_dial.push_back({current().title, current().url});
          save_profile();
          status = "I added this page to my speed dial";
        } else
          status = "This page is already on my speed dial";
      }
    }
  }
  // --smoke-services: the checks the other smokes cannot make. It drives the
  // broker services through the shell's own code (clipboard, dialog and modality,
  // IME, shortcuts, focus, window state, taskbar identity), then kills the
  // renderer a tab holds and requires the crash path to record it, log it and
  // bring the tab back. Phases are driven from the timer; failures accumulate.
  int services_phase = 0;
  DWORD services_tick = 0;
  int services_home_tab = -1;
  std::vector<std::string> services_failures;
  void services_check(bool ok, const std::string &what) {
    if (!ok) services_failures.push_back(what);
  }
  static inline bool g_dialog_seen = false, g_dialog_modal = false, g_dialog_second_refused = false;
  static inline Browser *g_services_browser = nullptr;
  static void CALLBACK services_dialog_timer(HWND, UINT, UINT_PTR id, DWORD) {
    Browser *b = g_services_browser;
    HWND dialog = FindWindowW(L"#32770", L"PsychoVault page");
    if (!b || !dialog) return;
    g_dialog_seen = true;
    g_dialog_modal = b->focus_service && b->focus_service->is_modal(kWindowId) &&
                     b->dialog_service && b->dialog_service->busy(kWindowId);
    // A second dialog while one is up must be refused, not stacked or blocked.
    const size_t refused_before = b->dialog_refusals;
    b->page_message_dialog("second dialog");
    g_dialog_second_refused = b->dialog_refusals == refused_before + 1;
    KillTimer(nullptr, id);
    PostMessageW(dialog, WM_CLOSE, 0, 0);
  }
  size_t dialog_refusals = 0;
  void run_services_sync() {
    Tab *home = find_tab(services_home_tab);
    services_check(home && home->session && home->session->ready.load(), "home tab has a ready renderer");
    // --- taskbar identity
    {
      PWSTR identity = nullptr;
      const HRESULT result = GetCurrentProcessExplicitAppUserModelID(&identity);
      services_check(SUCCEEDED(result) && identity &&
                         std::wstring(identity) == L"PsychoCore.PsychoVault.Browser",
                     "the taskbar identity is set");
      if (identity) CoTaskMemFree(identity);
    }
    // --- shortcuts: registry resolution and command effects
    services_check(resolve_shortcut('T', true, false, false) == "tab.new", "Ctrl+T resolves to tab.new");
    services_check(resolve_shortcut('T', true, true, false) == "tab.reopen", "Ctrl+Shift+T resolves to tab.reopen");
    services_check(resolve_shortcut(VK_F5, false, false, false) == "page.reload", "F5 resolves to page.reload");
    services_check(resolve_shortcut('Z', true, false, false).empty(), "an unbound chord resolves to nothing");
    {
      const size_t before = tabs.size();
      services_check(run_shortcut("tab.new") && tabs.size() == before + 1, "tab.new opens a tab");
      services_check(run_shortcut("tab.close") && tabs.size() == before, "tab.close closes it");
      const bool muted = current().muted;
      services_check(run_shortcut("tab.mute") && current().muted != muted, "tab.mute toggles");
      run_shortcut("tab.mute");
      const double zoom = current().zoom;
      services_check(run_shortcut("zoom.in") && current().zoom > zoom, "zoom.in zooms");
      services_check(run_shortcut("zoom.reset") && std::abs(current().zoom - 1.0) < 1e-9, "zoom.reset resets");
    }
    if (Tab *back = find_tab(services_home_tab)) {
      const size_t index = size_t(back - tabs.data());
      select_tab(int(index));
    }
    // --- vault: the service over the real profile directory: create, seal to disk, lock, refuse a wrong
    // passphrase, unlock, keep and reveal an entry, and never write the password in the clear
    if (vault_service) {
      rt::Error vault_error;
      const std::string vault_file = profile.directory + "/vault.bin";
      std::string leftover;
      services_check(!vault_service->status().flag("exists") || true, "vault status answers");
      services_check(vault_service->create("services smoke passphrase", vault_error), "the vault is created");
      services_check(vault_service->add("smoke.test", "tester", "Pl4in-Marker-9137", "", vault_error), "an entry is added");
      vault_service->lock();
      services_check(vault_service->status().flag("locked") && vault_service->status().flag("exists"),
                     "a locked vault says so and still exists on disk");
      services_check(!vault_service->unlock("not the passphrase", vault_error) && vault_error.code == "vault.passphrase",
                     "a wrong passphrase is refused");
      services_check(vault_service->unlock("services smoke passphrase", vault_error), "the right passphrase unlocks");
      std::string revealed;
      services_check(vault_service->reveal("smoke.test", revealed, vault_error) && revealed == "Pl4in-Marker-9137",
                     "the entry comes back");
      try { leftover = read_text_file(vault_file); } catch (const std::exception &) {}
      services_check(!leftover.empty() && leftover.find("Pl4in-Marker-9137") == std::string::npos &&
                         leftover.find("smoke.test") == std::string::npos,
                     "the vault file holds no password or site in the clear");
      vault_service->lock();
      std::string ignore_error;
      DeleteFileW(utf16_of(vault_file).c_str());
    }
    // --- clipboard: accepted, recorded, bounded, and on the OS clipboard
    if (clipboard_service) {
      const uint64_t before = clipboard_service->generation();
      page_clipboard_write(current(), "psychovault smoke");
      services_check(clipboard_service->generation() == before + 1 &&
                         !clipboard_service->last_writer().empty(),
                     "a page clipboard write is accepted and attributed");
      bool read_back = false;
      if (OpenClipboard(hwnd)) {
        if (HANDLE data = GetClipboardData(CF_UNICODETEXT)) {
          if (auto *text = static_cast<const wchar_t *>(GlobalLock(data))) {
            read_back = std::wstring(text) == L"psychovault smoke";
            GlobalUnlock(data);
          }
        }
        CloseClipboard();
        services_check(read_back, "the OS clipboard holds the written text");
      }
      page_clipboard_write(current(), std::string(size_t(2) << 20, 'x'));
      services_check(clipboard_service->generation() == before + 1, "an oversized write is refused");
    } else {
      services_check(false, "clipboard service exists");
    }
    // --- IME: valid text passes, malformed text never reaches the page
    if (ime && current().session) {
      const int id = current().id;
      composition_start();
      services_check(page_composing && ime->composing(id), "composition starts");
      composition_update("ab", 2);
      services_check(ime->composition(id) && ime->composition(id)->preedit == "ab", "preedit is accepted");
      composition_update(std::string("\xff\xfe"), 0);
      services_check(ime->composition(id) && ime->composition(id)->preedit == "ab", "malformed preedit is refused");
      composition_commit("xy");
      services_check(!page_composing && !ime->composing(id), "commit ends the composition");
      composition_start();
      composition_commit(std::string("\xc3\x28"));
      services_check(!page_composing && !ime->composing(id), "a malformed commit ends as a cancel");
    } else {
      services_check(false, "IME service and a live session exist");
    }
    // --- focus
    if (focus_service) {
      services_check(focus_service->focused_tab(kWindowId) == current().id, "the focused tab is the current tab");
      services_check(focus_service->active_window() == kWindowId || focus_service->active_window() == -1,
                     "the window is known to the focus service");
    }
    // --- dialog + modality: the timer closes the box once it is up
    if (dialog_service && focus_service) {
      g_services_browser = this;
      g_dialog_seen = g_dialog_modal = g_dialog_second_refused = false;
      SetTimer(nullptr, 0, 150, services_dialog_timer);
      page_message_dialog("psychovault smoke dialog");
      services_check(g_dialog_seen, "the message dialog appeared");
      services_check(g_dialog_modal, "the window was modal while the dialog was up");
      services_check(g_dialog_second_refused, "a second dialog was refused");
      services_check(!focus_service->is_modal(kWindowId) && !dialog_service->busy(kWindowId),
                     "modality ends with the dialog");
    }
  }
  void run_services_window() {
    // --- window state through WindowRules, and occlusion through FocusService
    if (window_rules && window_rules_id >= 0 && focus_service) {
      ShowWindow(hwnd, SW_MINIMIZE);
      const rt::WindowRecord *record = window_rules->find(window_rules_id);
      services_check(record && record->state == rt::WindowState::Minimized, "minimize reaches WindowRules");
      services_check(focus_service->visibility_of(current().id) == rt::PageVisibility::Hidden,
                     "a minimized window hides its tabs");
      ShowWindow(hwnd, SW_RESTORE);
      record = window_rules->find(window_rules_id);
      services_check(record && record->state == rt::WindowState::Normal, "restore reaches WindowRules");
      services_check(focus_service->visibility_of(current().id) != rt::PageVisibility::Hidden,
                     "a restored window shows its tabs");
    } else {
      services_check(false, "window rules and focus service exist");
    }
  }
  // Returns true when finished (report ready).
  bool services_busy = false; // a modal dialog pumps messages, so the timer can re-enter
  bool services_step(DWORD now) {
    if (services_busy) return false;
    struct Busy {
      bool &flag;
      explicit Busy(bool &f) : flag(f) { flag = true; }
      ~Busy() { flag = false; }
    } busy(services_busy);
    Tab *home = find_tab(services_home_tab);
    switch (services_phase) {
    case 0: { // wait for the app page to be live
      const Tab &t = current();
      if (app_ui_url(t.url) && !t.loading && t.error.empty() && t.frame.type == Json::Type::Object &&
          t.session && t.session->ready.load() && now - start_tick >= 1500) {
        services_home_tab = t.id;
        run_services_sync();
        services_tick = GetTickCount();
        services_phase = 1;
      } else if (now - start_tick >= 30000) {
        services_check(false, "the app page did not become ready");
        return true;
      }
      return false;
    }
    case 1: { // kill the renderer this tab holds
      const size_t slot = home && home->session ? home->session->published_slot.load()
                                                 : std::numeric_limits<size_t>::max();
      const unsigned long pid = (renderer_farm && slot != std::numeric_limits<size_t>::max())
                                    ? renderer_farm->process_id(slot) : 0;
      if (pid == 0 && now - services_tick < 10000) return false; // the session may be restarting
      services_check(pid != 0, "the home tab holds a renderer process (tab " +
                                   std::to_string(services_home_tab) + ", found " +
                                   std::string(home ? "yes" : "no") + ", session " +
                                   std::string(home && home->session ? "yes" : "no") + ", slot " +
                                   (slot == std::numeric_limits<size_t>::max() ? std::string("none")
                                                                               : std::to_string(slot)) +
                                   ", url " + (home ? home->url : std::string()) + ", error " +
                                   (home ? home->error : std::string()) + ")");
      if (pid == 0) return true;
      HANDLE victim = OpenProcess(PROCESS_TERMINATE | SYNCHRONIZE, FALSE, DWORD(pid));
      services_check(victim != nullptr, "the renderer process can be opened");
      if (!victim) return true;
      TerminateProcess(victim, 0);
      WaitForSingleObject(victim, 5000);
      CloseHandle(victim);
      services_tick = now;
      services_phase = 2;
      return false;
    }
    case 2: { // the crash path must record it and bring the tab back
      const bool recorded = crash_service && !crash_service->records().empty();
      const Tab *t = find_tab(services_home_tab);
      const bool back = recorded && t && !t->loading && t->error.empty() &&
                        t->frame.type == Json::Type::Object && t->session &&
                        t->session->ready.load() && crash_service->records().back().restart_attempts >= 1 &&
                        crash_service->restarts_for_tab(services_home_tab) == 0; // WM_DONE marks it stable
      if (back) {
        const rt::CrashRecord &record = crash_service->records().back();
        services_check(record.tab_id == services_home_tab, "the crash record names the tab");
        services_check(record.kind == rt::RendererFailureKind::Crash, "it is recorded as a crash");
        namespace fs = std::filesystem;
        std::error_code ec;
        const fs::path crash_log = fs::path(shell_path(profile.directory)) / "logs" / "crash.log";
        services_check(fs::exists(crash_log, ec) && fs::file_size(crash_log, ec) > 0,
                       "the crash was written to crash.log");
        services_phase = 3;
      } else if (now - services_tick >= 40000) {
        services_check(false, recorded ? "the crashed tab did not come back and become stable"
                                       : "no crash was recorded");
        return true;
      }
      return false;
    }
    default:
      run_services_window();
      return true;
    }
  }
  Json services_report() const {
    Json report = Json::make_object();
    report.set("result", services_failures.empty() ? "passed" : "failed");
    Json list = Json::make_array();
    for (const auto &failure : services_failures) list.array.push_back(Json(failure));
    report.set("failures", std::move(list));
    report.set("phase", double(services_phase));
    return report;
  }
  // --smoke-tabs: drives the tab and history models through the real shell code
  // paths (pin, move, close, reopen, history) and reports every disagreement
  // between the strip, the TabModel and the per-tab WebContents. No network.
  Json run_tab_model_smoke() {
    std::vector<std::string> failures;
    auto check = [&](bool ok, const std::string &what) {
      if (!ok) failures.push_back(what);
    };
    auto consistent = [&](const std::string &when) {
      const std::vector<int> order = tab_model.order(kWindowId);
      bool same = order.size() == tabs.size();
      for (size_t i = 0; same && i < tabs.size(); ++i) same = order[i] == tabs[i].id;
      check(same, when + ": strip order differs from the model");
      std::set<int> ids;
      for (const auto &tab : tabs) ids.insert(tab.id);
      check(ids.size() == tabs.size(), when + ": duplicate tab ids");
      const bool in_range = active >= 0 && active < int(tabs.size());
      check(in_range, when + ": active index out of range");
      const rt::Tab *model_active = tab_model.active_in(kWindowId);
      check(in_range && model_active && model_active->id == tabs[size_t(active)].id,
            when + ": active tab differs from the model");
    };
    const size_t base = tabs.size();
    consistent("start");
    for (const char *name : {"one", "two", "three"}) {
      (void)name;
      new_tab("psychovault://settings", false);
    }
    consistent("open");
    check(tabs.size() == base + 3, "three tabs were not opened");
    const int target_id = tabs.back().id;
    {
      rt::Error e;
      rt::WebContents &web = tabs.back().nav();
      check(web.visit("http://three.test/a", e) > 0 && web.commit("a", "", e), "first commit");
      check(web.visit("http://three.test/b", e) > 0 && web.commit("b", "", e), "second commit");
      check(web.history().size() == 2 && web.index() == 1, "two commits make two entries");
      check(web.visit("http://three.test/b", e) > 0 && web.pending() &&
                web.pending()->kind == rt::NavKind::Reload,
            "the address on screen reloads");
      check(web.commit("b", "", e) && web.history().size() == 2, "a reload does not grow history");
      check(web.go_back(e) > 0 && web.commit("a", "", e) && web.index() == 0, "back lands on the first entry");
      check(web.go_forward(e) > 0 && web.commit("b", "", e) && web.index() == 1, "forward returns");
      check(web.visit("http://three.test/c", e) > 0 && web.fail("nav.load", "refused", e) &&
                web.history().size() == 2 && web.index() == 1,
            "a failed load leaves history alone");
    }
    tab_set_pinned(int(tabs.size()) - 1, true);
    consistent("pin");
    check(!tabs.empty() && tabs.front().id == target_id && tabs.front().pinned,
          "a pinned tab moves to the front");
    {
      const int mover = tabs.back().id;
      move_tab(int(tabs.size()) - 1, -int(tabs.size()));
      consistent("move");
      check(tabs.front().id == target_id && tabs.front().pinned,
            "an unpinned tab was dragged in front of a pinned one");
      check(mover > 0, "mover id");
    }
    tab_set_pinned(0, false);
    consistent("unpin");
    int victim = -1;
    for (size_t i = 0; i < tabs.size(); ++i)
      if (tabs[i].id == target_id) victim = int(i);
    check(victim >= 0, "target tab still exists");
    const size_t before_close = tabs.size();
    if (victim >= 0) close_tab(victim);
    consistent("close");
    check(tabs.size() == before_close - 1 && tab_model.closed().size() >= 1, "close removes one tab and remembers it");
    reopen_tab();
    consistent("reopen");
    check(tabs.size() == before_close, "reopen restores the tab");
    const int reopened_id = tabs.empty() ? -1 : tabs[size_t(active)].id;
    check(reopened_id > 0 && reopened_id != target_id, "a reopened tab gets a fresh id");
    if (active >= 0 && active < int(tabs.size())) {
      const Tab &back = tabs[size_t(active)];
      check(back.web && back.web->history().size() == 2 && back.web->index() == 1,
            "reopen restores the tab's history and position");
    }
    check(tab_model.closed().empty(), "reopen consumes the closed entry");
    Json report = Json::make_object();
    report.set("result", failures.empty() ? "passed" : "failed");
    Json list = Json::make_array();
    for (const auto &failure : failures) list.array.push_back(Json(failure));
    report.set("failures", std::move(list));
    report.set("tabs", double(tabs.size()));
    return report;
  }
  void swap_tabs(int a, int b) {
    if (a == b || a < 0 || b < 0 || a >= int(tabs.size()) || b >= int(tabs.size()))
      return;
    // The model decides where the tab may land (pinned tabs stay a prefix).
    rt::Error move_error;
    tab_model.move_within_window(kWindowId, tabs[size_t(a)].id, b, move_error);
    apply_model_order();
    save_session();
  }
  void move_tab(int index, int delta) {
    if (index < 0 || index >= int(tabs.size()))
      return;
    swap_tabs(index, int(std::clamp(index + delta, 0, int(tabs.size()) - 1)));
  }
  // Ctrl+Shift+T undoes the most recent close, back stack and all.
  void reopen_tab() {
    if (tab_model.closed().empty()) {
      status = "No closed tab to bring back";
      return;
    }
    const int closed_id = tab_model.closed().back().tab_id;
    rt::Error reopen_error;
    const int id = tab_model.reopen_last(reopen_error);
    if (id < 0) {
      status = "I could not bring that tab back";
      return;
    }
    ClosedTab restore;
    if (auto found = closed_data.find(closed_id); found != closed_data.end()) {
      restore = std::move(found->second);
      closed_data.erase(found);
    }
    Tab t;
    t.id = id;
    t.url = restore.url.empty() ? std::string("psychovault://newtab") : restore.url;
    t.title = restore.title.empty() ? std::string("New Tab") : restore.title;
    {
      std::vector<rt::HistoryEntry> entries;
      const std::vector<std::string> urls =
          restore.history.empty() ? std::vector<std::string>{restore.url} : restore.history;
      for (const auto &entry_url : urls) {
        rt::HistoryEntry entry;
        entry.url = entry_url;
        entries.push_back(std::move(entry));
      }
      rt::Error restore_error;
      t.nav().restore(entries, int(std::min(restore.index, entries.size() - 1)), restore_error);
    }
    if (const rt::Tab *known = tab_model.find(id)) {
      t.pinned = known->pinned;
      t.muted = known->muted;
    }
    t.started = false;
    const int place = std::clamp(model_position(id), 0, int(tabs.size()));
    tabs.insert(tabs.begin() + place, std::move(t));
    active = place;
    select_tab(active);
  }
  void tab_set_pinned(int index, bool pinned) {
    rt::Error pin_error;
    tab_model.set_pinned(tabs[size_t(index)].id, pinned, pin_error);
    apply_model_order(); // pinned tabs are a prefix, so the order can change
    save_session();
    InvalidateRect(hwnd, nullptr, FALSE);
  }
  void tab_set_muted(int index, bool muted) {
    rt::Error mute_error;
    tab_model.set_muted(tabs[size_t(index)].id, muted, mute_error);
    tabs[size_t(index)].muted = muted;
    if (tabs[size_t(index)].session)
      tabs[size_t(index)].session->muted.store(muted);
    save_session();
    InvalidateRect(hwnd, nullptr, FALSE);
  }
  void right_click(int x, int y) {
    for (auto it = hits.rbegin(); it != hits.rend(); ++it)
      if (it->rect.contains(x, y) &&
          (it->action == "tab" || it->action == "close-tab")) {
        int index = it->index;
        Tab &t = tabs[size_t(index)];
        std::vector<MenuLine> items;
        items.push_back({t.pinned ? "Unpin tab" : "Pin tab", "menu-pin"});
        items.push_back({t.muted ? "Unmute tab" : "Mute tab", "menu-mute"});
        items.push_back({"Duplicate tab", "menu-duplicate"});
        items.push_back({"Move tab left", "menu-move-left"});
        items.push_back({"Move tab right", "menu-move-right"});
        items.push_back({"Close other tabs", "menu-close-others"});
        items.push_back({"New tab", "menu-create"});
        items.push_back({"Reopen closed tab", "menu-reopen"});
        items.push_back({"Close tab", "menu-close"});
        open_menu(index, x, y, std::move(items));
        return;
      }
    if (content.contains(x, y) && !menu_open && !panel_bookmarks &&
        !panel_history && !panel_downloads && !settings) {
      std::vector<MenuLine> items;
      items.push_back({"Back", "menu-back"});
      items.push_back({"Forward", "menu-forward"});
      items.push_back({"Reload", "menu-reload"});
      items.push_back({"Copy page URL", "menu-copy-url"});
      items.push_back({"Add page to speed dial", "menu-dial-add"});
      items.push_back({"View page source", "menu-view-source"});
      open_menu(active, x, y, std::move(items));
    }
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
          std::string utf8(size_t(n), '\0');
          WideCharToMultiByte(CP_UTF8, 0, text, -1, utf8.data(), n, nullptr, nullptr);
          utf8.pop_back();
          if (!address_focus && !home_focus && !panel_focus && !find_open && current().session) {
            Json command = Json::make_object();
            command.set("tag", "paste");
            command.set("text", utf8);
            current().session->send(command);
          } else insert_text(utf8);
        }
        GlobalUnlock(data);
      }
    }
    CloseClipboard();
  }
  void scroll_to(double position) {
    Tab &t = current();
    const double zoom = t.zoom > 0 ? t.zoom : 1.0;
    const double page_height = t.frame.num("height", content.h / zoom);
    const double maximum = std::max(0.0, page_height - content.h / zoom);
    t.scroll = std::clamp(position, 0.0, maximum);
    if (t.session) {
      Json j = Json::make_object();
      j.set("tag", "scroll");
      j.set("y", t.scroll);
      t.session->send(j);
    }
    InvalidateRect(hwnd, nullptr, FALSE);
  }
  void scroll(double delta) {
    const double zoom = current().zoom > 0 ? current().zoom : 1.0;
    scroll_to(current().scroll + delta / zoom);
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
  const auto paint_started = std::chrono::steady_clock::now();
  if (!b->paint_surface || b->paint_surface->width != std::max(1, b->width) ||
      b->paint_surface->height != std::max(1, b->height))
    b->paint_surface = std::make_unique<Canvas>(b->width, b->height, BG);
  Canvas &canvas = *b->paint_surface;
  canvas.clip = {double(ps.rcPaint.left), double(ps.rcPaint.top),
                 double(std::max(0L, ps.rcPaint.right - ps.rcPaint.left)),
                 double(std::max(0L, ps.rcPaint.bottom - ps.rcPaint.top))};
  const auto draw_started = std::chrono::steady_clock::now();
  b->draw(canvas);
  g_draw_us.fetch_add(uint64_t(std::chrono::duration_cast<std::chrono::microseconds>(
      std::chrono::steady_clock::now() - draw_started).count()));
  BITMAPINFO bi{};
  bi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
  bi.bmiHeader.biWidth = canvas.width;
  bi.bmiHeader.biHeight = -canvas.height;
  bi.bmiHeader.biPlanes = 1;
  bi.bmiHeader.biBitCount = 32;
  bi.bmiHeader.biCompression = BI_RGB;
  const int dirty_x = std::clamp(ps.rcPaint.left, 0L, LONG(canvas.width));
  const int dirty_y = std::clamp(ps.rcPaint.top, 0L, LONG(canvas.height));
  const int dirty_right = std::clamp(ps.rcPaint.right, LONG(dirty_x), LONG(canvas.width));
  const int dirty_bottom = std::clamp(ps.rcPaint.bottom, LONG(dirty_y), LONG(canvas.height));
  if (dirty_right > dirty_x && dirty_bottom > dirty_y)
    StretchDIBits(dc, dirty_x, dirty_y, dirty_right - dirty_x, dirty_bottom - dirty_y,
                  dirty_x, dirty_y, dirty_right - dirty_x, dirty_bottom - dirty_y,
                  canvas.pixels.data(), &bi, DIB_RGB_COLORS, SRCCOPY);
  canvas.clip = {0, 0, double(canvas.width), double(canvas.height)};
  const int reported_refresh_hz = GetDeviceCaps(dc, VREFRESH);
  const int refresh_hz = reported_refresh_hz >= 20 ? reported_refresh_hz : 60;
  EndPaint(hwnd, &ps);
  GdiFlush();
  const double elapsed = std::chrono::duration<double>(
      std::chrono::steady_clock::now() - paint_started).count();
  DwmFlush();
  g_paint_us.fetch_add(uint64_t(std::chrono::duration_cast<std::chrono::microseconds>(
      std::chrono::steady_clock::now() - paint_started).count()));
  ++b->compositor_presented_frames;
  if (elapsed > 1.0 / refresh_hz)
    b->compositor_dropped_frames += uint64_t(std::max(1.0, std::ceil(elapsed * refresh_hz) - 1));
  const auto metrics_now = std::chrono::steady_clock::now();
  if (b->compositor_metrics_at.time_since_epoch().count() == 0)
    b->compositor_metrics_at = metrics_now;
  if (metrics_now - b->compositor_metrics_at >= std::chrono::seconds(5)) {
    const std::string metrics = "PsychoVault compositor: paints=" +
        std::to_string(b->compositor_presented_frames) + " estimated-dropped=" +
        std::to_string(b->compositor_dropped_frames) + "\n";
    OutputDebugStringA(metrics.c_str());
    b->compositor_presented_frames = b->compositor_dropped_frames = 0;
    b->compositor_metrics_at = metrics_now;
  }
}
static void capture(HWND hwnd, const std::string &path) {
  Browser *b = browser(hwnd);
  Canvas canvas(b->width, b->height, BG);
  b->draw(canvas);
  auto png = encode_png(canvas.width, canvas.height, canvas.pixels);
  std::ofstream f(path, std::ios::binary);
  f.write(reinterpret_cast<const char *>(png.data()), std::streamsize(png.size()));
}
// Smoke watchdog: the last message the window procedure saw and when. If a smoke never finishes, the
// watchdog thread writes these beside the capture and ends the process, so a hang leaves evidence.
static std::atomic<DWORD> g_last_message_tick{0};
static std::atomic<UINT> g_last_message{0};
static std::atomic<UINT> g_in_message{0};
static LRESULT wndproc_inner(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp);
static LRESULT CALLBACK wndproc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
  g_last_message.store(msg);
  if (msg == WM_PAINT) g_paints.fetch_add(1);
  else if (msg == WM_TIMER) g_timer_ticks.fetch_add(1);
  g_last_message_tick.store(GetTickCount());
  g_in_message.store(msg);
  const LRESULT result = wndproc_inner(hwnd, msg, wp, lp);
  g_in_message.store(0);
  return result;
}
static LRESULT wndproc_inner(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
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
    if (auto b = browser(hwnd)) {
      b->window_minimized(wp == SIZE_MINIMIZED);
      b->window_size_changed(wp);
      b->resize(LOWORD(lp), HIWORD(lp));
    }
    return 0;
  }
  case WM_ACTIVATE:
    if (auto b = browser(hwnd)) b->window_activated(LOWORD(wp) != WA_INACTIVE);
    return DefWindowProcW(hwnd, msg, wp, lp);
  case WM_PAINT:
    paint(hwnd);
    return 0;
  case WM_LBUTTONDOWN: {
    const int x = GET_X_LPARAM(lp), y = GET_Y_LPARAM(lp);
    if (auto b = browser(hwnd)) {
      if (b->chrome_pointer("down", x, y)) {
        SetCapture(hwnd);
      } else if (b->content.contains(x, y)) {
        SetCapture(hwnd);
        b->pointer_down(x, y);
      } else b->click(x, y);
    }
    SetFocus(hwnd);
    return 0;
  }
  case WM_LBUTTONUP: {
    const int x = GET_X_LPARAM(lp), y = GET_Y_LPARAM(lp);
    if (auto b = browser(hwnd)) {
      b->chrome_pointer("up", x, y);
      const bool page_click = b->page_pointer_down && b->content.contains(x, y);
      b->pointer_up(x, y);
      if (page_click) b->click(x, y);
    }
    if (GetCapture() == hwnd) ReleaseCapture();
    return 0;
  }
  case WM_MOUSEMOVE: {
    TRACKMOUSEEVENT tracking{sizeof(TRACKMOUSEEVENT), TME_LEAVE, hwnd, 0};
    TrackMouseEvent(&tracking);
    if (auto b = browser(hwnd)) b->pointer_move(GET_X_LPARAM(lp), GET_Y_LPARAM(lp));
    return 0;
  }
  case WM_MOUSELEAVE:
    if (auto b = browser(hwnd))
      b->pointer_move(int(b->content.x) - 1, int(b->content.y) - 1);
    return 0;
  case WM_RBUTTONDOWN: {
    if (auto b = browser(hwnd)) {
      const int x = GET_X_LPARAM(lp), y = GET_Y_LPARAM(lp);
      if (!b->chrome_context(x, y)) b->right_click(x, y);
    }
    return 0;
  }
  case WM_MOUSEWHEEL: {
    POINT point{GET_X_LPARAM(lp), GET_Y_LPARAM(lp)};
    ScreenToClient(hwnd, &point);
    if (auto b = browser(hwnd))
      b->wheel(point.x, point.y, -double(GET_WHEEL_DELTA_WPARAM(wp)) / 120 * 60);
    return 0;
  }
  case WM_IME_STARTCOMPOSITION:
    if (auto b = browser(hwnd)) b->composition_start();
    return DefWindowProcW(hwnd, msg, wp, lp);
  case WM_IME_COMPOSITION: {
    auto b = browser(hwnd);
    HIMC context = ImmGetContext(hwnd);
    if (!b || !context) return DefWindowProcW(hwnd, msg, wp, lp);
    auto read_composition = [&](DWORD index) {
      LONG bytes = ImmGetCompositionStringW(context, index, nullptr, 0);
      if (bytes <= 0 || bytes > 8192 * LONG(sizeof(wchar_t)) || bytes % LONG(sizeof(wchar_t)))
        return std::wstring();
      std::wstring result(size_t(bytes / LONG(sizeof(wchar_t))), L'\0');
      if (ImmGetCompositionStringW(context, index, result.data(), DWORD(bytes)) < 0) return std::wstring();
      return result;
    };
    if (lp & GCS_RESULTSTR) {
      const std::string committed = utf8_of_wide(read_composition(GCS_RESULTSTR));
      b->composition_commit(committed);
    } else if (lp & GCS_COMPSTR) {
      const std::wstring preedit = read_composition(GCS_COMPSTR);
      LONG caret_units = ImmGetCompositionStringW(context, GCS_CURSORPOS, nullptr, 0);
      const size_t caret = size_t(std::clamp<LONG>(caret_units, 0, LONG(preedit.size())));
      b->composition_update(utf8_of_wide(preedit), utf8_of_wide(preedit.substr(0, caret)).size());
    }
    ImmReleaseContext(hwnd, context);
    return 0;
  }
  case WM_IME_ENDCOMPOSITION:
    if (auto b = browser(hwnd)) b->composition_cancel();
    return DefWindowProcW(hwnd, msg, wp, lp);
  case WM_KEYDOWN:
    if (auto b = browser(hwnd))
      b->key(msg, wp, lp);
    return 0;
  case WM_KEYUP:
    if (auto b = browser(hwnd)) b->key_up(wp);
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
    if (b && int(wp) == Browser::kChromeTab) {
      if (up->generation == Browser::kChromeGeneration) {
        if (!up->error.empty()) b->chrome_error = up->error;
        else {
          const bool first = b->chrome_frame.type != Json::Type::Object;
          b->chrome_frame = std::move(up->frame);
          b->chrome_frame_tick = GetTickCount();
          g_chrome_frames.fetch_add(1);
          if (first) b->chrome_state_sent.clear();
          // The frame page is pumped continuously and most frames change nothing; repainting the
          // whole window for each one starves the timer. Repaint only what the frame says changed.
          const Json &f = b->chrome_frame;
          const Json *painted = f.find("paint");
          const size_t signature = std::hash<std::string>{}(painted ? painted->dump() : std::string());
          const bool same = signature == b->chrome_paint_sig;
          b->chrome_paint_sig = signature;
          const Json *tiles = f.find("damageTiles"), *damage = f.find("damage");
          if (same && !first) {
            // Identical to what is on screen.
          } else if (first || f.flag("fullDamage") ||
              (!(tiles && tiles->type == Json::Type::Array) && !(damage && damage->type == Json::Type::Array))) {
            InvalidateRect(hwnd, nullptr, FALSE);
          } else if (tiles && tiles->type == Json::Type::Array && !tiles->array.empty() &&
                     tiles->array.size() <= 256) {
            for (const Json &tile : tiles->array) {
              const double size = tile.num("size", 128);
              RECT dirty{LONG(std::floor(tile.num("x"))), LONG(std::floor(tile.num("y"))),
                         LONG(std::ceil(tile.num("x") + size)), LONG(std::ceil(tile.num("y") + size))};
              InvalidateRect(hwnd, &dirty, FALSE);
            }
          } else if (damage && damage->type == Json::Type::Array && !damage->array.empty()) {
            if (damage->array.size() > 64) InvalidateRect(hwnd, nullptr, FALSE);
            else
              for (const Json &region : damage->array) {
                RECT dirty{LONG(std::floor(region.num("x"))), LONG(std::floor(region.num("y"))),
                           LONG(std::ceil(region.num("x") + region.num("w"))),
                           LONG(std::ceil(region.num("y") + region.num("h")))};
                InvalidateRect(hwnd, &dirty, FALSE);
              }
          }
        }
      }
      return 0;
    }
    Tab *tab = b ? b->find_tab(int(wp)) : nullptr;
    if (tab && up->generation == tab->generation) {
      bool full_repaint = !up->error.empty();
      bool frame_unchanged = false;
      const std::string old_title = tab->title, old_url = tab->url;
      const bool old_loading = tab->loading, old_recorded = tab->recorded;
      if (!up->error.empty()) {
        tab->error = up->error;
        b->nav_fail(*tab, up->error);
        tab->loading = false;
        if (tab == &b->current())
          b->status = up->error;
      } else {
        tab->frame = std::move(up->frame);
        {
          // The renderer is pumped many times a second and most frames repeat the last one;
          // repainting the window for each would keep the UI thread painting forever.
          const Json *painted = tab->frame.find("paint");
          const size_t signature = std::hash<std::string>{}(
              (painted ? painted->dump() : std::string()) + "|" + std::to_string(tab->frame.num("scroll")));
          frame_unchanged = signature == tab->paint_sig;
          tab->paint_sig = signature;
        }
        tab->title = tab->frame.str("title", tab->title);
        tab->loading = false;
        tab->scroll = tab->frame.num("scroll", tab->scroll);
        if (!tab->recorded) {
          tab->recorded = true;
          auto final_url = tab->frame.str("url", tab->url);
          b->nav_commit(*tab, tab->title);
          tab->frame_tick = GetTickCount();
          if (!final_url.empty() && final_url != tab->url) {
            tab->url = final_url;
            rt::Error replace_error;
            if (tab->web) tab->web->replace_url(final_url, replace_error);
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
        full_repaint = !old_recorded || old_loading || old_title != tab->title || old_url != tab->url;
      }
      if (tab == &b->current()) {
        if (full_repaint || b->find_open) {
          InvalidateRect(hwnd, nullptr, FALSE);
        } else if (frame_unchanged) {
          // Same paint list and scroll as the frame already on screen.
        } else {
          const Json *tiles = tab->frame.find("damageTiles");
          if (tab->frame.flag("fullDamage")) {
            RECT page{LONG(std::floor(b->content.x)), LONG(std::floor(b->content.y)),
                      LONG(std::ceil(b->content.x + b->content.w)),
                      LONG(std::ceil(b->content.y + b->content.h))};
            InvalidateRect(hwnd, &page, FALSE);
          } else if (tiles && tiles->type == Json::Type::Array && tiles->array.size() <= 256) {
            const double zoom = tab->zoom > 0 ? tab->zoom : 1.0;
            for (const Json &tile : tiles->array) {
              const double x = b->content.x + tile.num("x") * zoom;
              const double y = b->content.y +
                  (tile.flag("fixed") ? tile.num("y") : tile.num("y") - tab->scroll) * zoom;
              const double size = tile.num("size", 128) * zoom;
              const double left = std::max(x, b->content.x);
              const double top = std::max(y, b->content.y);
              const double right = std::min(x + size, b->content.x + b->content.w);
              const double bottom = std::min(y + size, b->content.y + b->content.h);
              if (right <= left || bottom <= top) continue;
              RECT dirty{LONG(std::floor(left)), LONG(std::floor(top)),
                         LONG(std::ceil(right)), LONG(std::ceil(bottom))};
              InvalidateRect(hwnd, &dirty, FALSE);
            }
          } else {
            const Json *damage = tab->frame.find("damage");
            if (!damage || damage->type != Json::Type::Array || damage->array.size() > 64) {
              InvalidateRect(hwnd, nullptr, FALSE);
            } else {
              const double zoom = tab->zoom > 0 ? tab->zoom : 1.0;
              for (const Json &region : damage->array) {
                const double x = b->content.x + region.num("x") * zoom;
                const double y = b->content.y +
                    (region.flag("fixed") ? region.num("y") : region.num("y") - tab->scroll) * zoom;
                Rect clipped{x, y, region.num("w") * zoom, region.num("h") * zoom};
                const double left = std::max(clipped.x, b->content.x);
                const double top = std::max(clipped.y, b->content.y);
                const double right = std::min(clipped.x + clipped.w, b->content.x + b->content.w);
                const double bottom = std::min(clipped.y + clipped.h, b->content.y + b->content.h);
                if (right <= left || bottom <= top) continue;
                RECT dirty{LONG(std::floor(left)), LONG(std::floor(top)),
                           LONG(std::ceil(right)), LONG(std::ceil(bottom))};
                InvalidateRect(hwnd, &dirty, FALSE);
              }
            }
          }
        }
      }
    }
    return 0;
  }
  case WM_URL: {
    std::unique_ptr<Update> up(reinterpret_cast<Update *>(lp));
    auto b = browser(hwnd);
    Tab *tab = b ? b->find_tab(int(wp)) : nullptr;
    if (tab && up->generation == tab->generation && !up->url.empty()) {
      tab->url = up->url;
      rt::Error replace_error;
      if (tab->web) tab->web->replace_url(up->url, replace_error);
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
    if (b && int(wp) == Browser::kChromeTab) {
      if (up->generation == Browser::kChromeGeneration && up->image && up->image->valid() &&
          b->chrome_images.size() < 64) {
        b->chrome_images[up->url] = *up->image;
        InvalidateRect(hwnd, nullptr, FALSE);
      }
      return 0;
    }
    Tab *tab = b ? b->find_tab(int(wp)) : nullptr;
    // The broker reconstructed renderer pixels from the chunked process reply;
    // keep the per-tab cache bounded before presenting them in the shell.
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
    auto target = parse_url(up->url);
    if (b) {
      for (size_t i = 0; i < b->tabs.size(); ++i)
        if (b->tabs[i].id == int(wp) && b->tabs[i].generation == up->generation) {
          if (target && target->network())
            b->navigate_in(int(i), up->url);
          else {
            b->tabs[i].error = "Renderer navigation was refused";
            b->tabs[i].loading = false;
            b->status = b->tabs[i].error;
            InvalidateRect(hwnd, nullptr, FALSE);
          }
          break;
        }
    }
    return 0;
  }
  case WM_CLIPBOARD: {
    std::unique_ptr<Update> up(reinterpret_cast<Update *>(lp));
    if (auto b = browser(hwnd)) {
      Tab *tab = b->find_tab(int(wp));
      if (tab && tab->generation == up->generation) b->page_clipboard_write(*tab, up->clipboard);
    }
    return 0;
  }
  case WM_DIALOG: {
    std::unique_ptr<Update> up(reinterpret_cast<Update *>(lp));
    if (auto b = browser(hwnd)) b->page_message_dialog(up->url);
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
    if (renderer_farm) {
      rt::Error renderer_error;
      if (!renderer_farm->monitor(renderer_error) && b) {
        b->status = "Renderer process recovery failed: " + renderer_error.message;
        if (b->last_recovery_failure != renderer_error.code + renderer_error.message) {
          b->last_recovery_failure = renderer_error.code + renderer_error.message;
          b->log_event("renderer", rt::LogLevel::Error, "recovery.failed",
                       {{"code", renderer_error.code}});
        }
      }
      if (b) b->recover_failed_renderers();
      if (b) b->sync_focus();
    }
    if (b) b->reap_sessions();
    if (b) b->sleep_idle_tabs();
    if (b && b->chrome_enabled) {
      b->start_chrome();
      if (b->chrome_panel_open() != b->last_panel_open || b->profile.hide_sidebar != b->last_rail_hidden) {
        b->last_panel_open = b->chrome_panel_open();
        b->last_rail_hidden = b->profile.hide_sidebar;
        b->resize(b->width, b->height);
      }
      b->push_chrome_state();
    }
    if (b && b->smoke && b->smoke_settings) {
      if (b->settings_smoke_step(GetTickCount())) return 0;
    }
    if (b && b->smoke && b->smoke_chrome) {
      if (b->chrome_smoke_step(GetTickCount())) return 0;
    }
    if (b && b->smoke && b->smoke_services) {
      if (!b->smoke_tabs_done && b->services_step(GetTickCount())) {
        b->smoke_tabs_done = true;
        const Json report = b->services_report();
        std::string write_error;
        write_file_atomic(b->capture_path + ".services.json", report.dump() + "\n", write_error);
        PostQuitMessage(report.str("result") == "passed" ? 0 : 1);
      }
      return 0;
    }
    if (b && b->smoke && b->smoke_tabs) {
      if (!b->smoke_tabs_done && GetTickCount() - b->start_tick >= 1500) {
        b->smoke_tabs_done = true;
        const Json report = b->run_tab_model_smoke();
        std::string write_error;
        write_file_atomic(b->capture_path + ".tabs.json", report.dump() + "\n", write_error);
        PostQuitMessage(report.str("result") == "passed" ? 0 : 1);
      }
      return 0;
    }
    if (b && b->smoke && !b->smoke_bridge_url.empty()) {
      const DWORD now = GetTickCount();
      auto write_flow_report = [&](bool passed, const std::string &reason) {
        const Tab *source = b->find_tab(b->smoke_bridge_source_tab);
        const Tab &target = b->current();
        Json report = Json::make_object();
        report.set("result", passed ? "passed" : "failed");
        report.set("reason", reason);
        report.set("stage", double(b->smoke_bridge_stage));
        report.set("elapsedMs", double(GetTickCount() - b->start_tick));
        report.set("processId", double(GetCurrentProcessId()));
        report.set("sourceTabId", double(b->smoke_bridge_source_tab));
        report.set("sourceUrl", source ? source->url : std::string());
        report.set("sourceTabRetained", source != nullptr);
        report.set("sourceRendererReady", source && source->session && source->session->ready);
        report.set("targetUrl", b->smoke_bridge_url);
        report.set("actualUrl", target.url);
        report.set("targetTabId", double(target.id));
        report.set("tabCount", double(b->tabs.size()));
        report.set("targetRendererReady", target.session && target.session->ready);
        report.set("targetLoading", target.loading);
        report.set("targetError", target.error);
        report.set("targetTitle", target.frame.str("title"));
        report.set("targetImageCount", double(target.images.size()));
        report.set("appScreenshot", b->capture_path + ".app.png");
        report.set("fixtureScreenshot", b->capture_path);
        std::string report_error;
        write_file_atomic(b->capture_path + ".flow.json", report.dump() + "\n",
                          report_error);
        if (!report_error.empty())
          OutputDebugStringA(("S045 smoke flow report failed: " + report_error + "\n").c_str());
      };
      auto fail_flow = [&](const std::string &reason) {
        if (!b->capture_path.empty()) capture(hwnd, b->capture_path);
        write_flow_report(false, reason);
        b->smoke_bridge_stage = 3;
        PostQuitMessage(1);
      };

      if (b->smoke_bridge_stage == 0) {
        const Tab &home = b->current();
        const bool app_ready = app_ui_url(home.url) && !home.loading &&
            home.error.empty() && home.frame.type == Json::Type::Object &&
            home.session && home.session->ready;
        if (app_ready && now - b->start_tick >= 1500) {
          b->smoke_bridge_source_tab = home.id;
          const std::string app_capture = b->capture_path + ".app.png";
          capture(hwnd, app_capture);
          // Locate the input from its visible placeholder paint command instead
          // of assuming a viewport-specific coordinate. The click, text and
          // Enter all traverse the renderer event/IPC route as a user would.
          double address_x = 0, address_y = 0;
          bool address_found = false;
          if (const Json *paint = home.frame.find("paint");
              paint && paint->type == Json::Type::Array) {
            for (const Json &command : paint->array) {
              if (command.str("text") != "Search DuckDuckGo or enter an address") continue;
              address_x = command.num("x") + 2.0;
              address_y = command.num("y") + command.num("h") / 2.0;
              address_found = true;
              break;
            }
          }
          if (!address_found) {
            fail_flow("the app page did not paint its address input placeholder");
          } else {
            b->click(int(std::lround(address_x)), int(std::lround(address_y)));
            b->smoke_bridge_stage = 1;
            b->smoke_bridge_stage_tick = now;
          }
        } else if (now - b->start_tick >= 20000) {
          fail_flow("the app UI did not become ready for address entry");
        }
      } else if (b->smoke_bridge_stage == 1 &&
                 now - b->smoke_bridge_stage_tick >= 150) {
        for (unsigned char ch : b->smoke_bridge_url)
          b->character(wchar_t(ch));
        b->key(WM_KEYDOWN, VK_RETURN, 0);
        b->smoke_bridge_stage = 2;
        b->smoke_bridge_stage_tick = now;
      } else if (b->smoke_bridge_stage == 2) {
        Tab *source = b->find_tab(b->smoke_bridge_source_tab);
        Tab &target = b->current();
        const bool target_loaded = target.url == b->smoke_bridge_url &&
            !target.loading && target.error.empty() &&
            target.frame.type == Json::Type::Object && target.session &&
            target.session->ready && (!b->expect_image || !target.images.empty());
        if (target_loaded && source && app_ui_url(source->url) &&
            source->session && source->session->ready && b->tabs.size() >= 2) {
          capture(hwnd, b->capture_path);
          write_flow_report(true, "address entry used psycho.tab.new in the running app");
          b->smoke_bridge_stage = 3;
          PostQuitMessage(0);
        } else if (target.url == b->smoke_bridge_url && !target.error.empty()) {
          fail_flow("the address bridge opened a page with an error: " + target.error);
        } else if (now - b->smoke_bridge_stage_tick >= 30000) {
          fail_flow("the app address bridge did not open and render the local fixture");
        }
      }
      return 0;
    }
    if (b && !b->smoke) {
      b->extensions.tick();
      b->flush_history();
      b->vault_clipboard_tick();
      if (GetTickCount64() - b->hub_refreshed_at > 2000) {
        b->hub_refreshed_at = GetTickCount64();
        b->publish_settings();
      }
      if (b->vault_service && b->vault_service->auto_lock(b->profile.vault_lock_minutes)) {
        b->status = "The vault locked itself";
        InvalidateRect(hwnd, nullptr, FALSE);
      }
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
      bool failed_internal = false;
      // An internal page that never produced a frame is a failed smoke too.
      const bool internal_page = engine_page_url(b->current().url);
      if (internal_page && (b->current().frame.type != Json::Type::Object ||
                            !b->current().error.empty()))
        failed_internal = true;
      bool failed =
          failed_internal ||
          (web && (b->current().frame.type != Json::Type::Object || !b->current().error.empty() ||
                  !b->current().session || !b->current().session->ready ||
                  (b->expect_image && b->current().images.empty())));
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
  case WM_FORWARD_LAUNCH: {
    std::unique_ptr<ForwardedLaunch> launch(reinterpret_cast<ForwardedLaunch *>(lp));
    auto b = browser(hwnd);
    if (!b || !launch) return 0;
    if (!launch->error.empty()) {
      b->profile_error = launch->error;
      b->status = "Could not open forwarded launch";
      InvalidateRect(hwnd, nullptr, FALSE);
      return 0;
    }
    ShowWindow(hwnd, SW_RESTORE);
    SetForegroundWindow(hwnd);
    if (!launch->urls.empty()) {
      b->navigate(launch->urls.front(), false);
      for (size_t i = 1; i < launch->urls.size(); ++i)
        b->new_tab(launch->urls[i], false);
    }
    return 0;
  }
  case WM_BRIDGE: {
    std::unique_ptr<BridgeTask> task(reinterpret_cast<BridgeTask *>(lp));
    auto b = browser(hwnd);
    if (b && task && task->tab == Browser::kChromeTab) {
      if (b->chrome_enabled) b->chrome_action(*task);
      return 0;
    }
    if (!b || !task || !b->find_tab(task->tab)) return 0;
    // Only the browser frame may close tabs, navigate or drive the window; a page session reaches
    // this point with tab:new / tab:activate alone (its bridge lambda enforces that).
    if (task->method == "settings:set") {
      b->apply_setting(*task);
      return 0;
    }
    if (b->settings_task(*task)) return 0;
    if (task->method == "vault:backup" || task->method == "vault:restore" || task->method == "vault:import") {
      b->vault_file_task(*task);
      return 0;
    }
    if (task->method == "vault:copy") {
      b->vault_copy(task->payload.str("site"));
      return 0;
    }
    if (task->method == "history:clear") {
      b->profile.clear_history();
      b->save_profile();
      b->status = "I cleared my browsing history";
      InvalidateRect(hwnd, nullptr, FALSE);
      return 0;
    }
    if (task->method != "tab:new" && task->method != "tab:activate") return 0;
    if (task->method == "tab:new") {
      std::string target = task->payload.str("url", "psychovault://newtab");
      if (target.empty()) target = "psychovault://newtab";
      b->new_tab(std::move(target));
    } else if (task->method == "tab:activate") {
      const int id = int(task->payload.num("id", 0));
      for (size_t i = 0; i < b->tabs.size(); ++i) {
        if (b->tabs[i].id == id) {
          b->select_tab(int(i));
          break;
        }
      }
    }
    return 0;
  }
  case WM_CLOSE: {
    auto b = browser(hwnd);
    if (app_instance) {
      std::string error;
      if (!app_instance->quit(error)) {
        if (b) {
          b->profile_error = error.empty() ? "Could not save the profile before closing" : error;
          b->status = "Close cancelled: profile could not be saved";
          InvalidateRect(hwnd, nullptr, FALSE);
        }
        return 0;
      }
      app_instance = nullptr;
    }
    DestroyWindow(hwnd);
    return 0;
  }
  case WM_NCHITTEST: {
    LRESULT hit = DefWindowProcW(hwnd, msg, wp, lp);
    if (hit == HTCLIENT) {
      POINT p{GET_X_LPARAM(lp), GET_Y_LPARAM(lp)};
      ScreenToClient(hwnd, &p);
      auto b = browser(hwnd);
      if (b && !b->chrome_enabled && p.y < 40 && p.x > 44 && p.x < b->width - 160)
        return HTCAPTION;
      // With the engine frame the empty part of the tab strip drags the window; the tabs and the
      // buttons are inside the strip's left and right edges.
      if (b && b->chrome_enabled && p.y < 40 && p.x > 44 + 36 + (int(b->tabs.size()) * 126) + 96 &&
          p.x < b->width - 160)
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
  auto report_cli_error = [](const std::string &message) {
    const std::string line = "PsychoVault: " + message + "\n";
    fwrite(line.data(), 1, line.size(), stderr);
    fflush(stderr);
    return 2;
  };
  std::vector<std::string> args;
  int argc = 0;
  LPWSTR *argv = CommandLineToArgvW(GetCommandLineW(), &argc);
  if (!argv) return report_cli_error("cannot parse the Windows command line");
  for (int i = 1; i < argc; ++i) {
    const int n = WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, argv[i], -1,
                                      nullptr, 0, nullptr, nullptr);
    if (n <= 0) {
      LocalFree(argv);
      return report_cli_error("an argument is not valid Unicode");
    }
    std::string value(size_t(n), '\0'); // include the terminator in capacity
    if (WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, argv[i], -1,
                            value.data(), n, nullptr, nullptr) != n) {
      LocalFree(argv);
      return report_cli_error("cannot convert an argument to UTF-8");
    }
    value.resize(size_t(n - 1));
    args.push_back(std::move(value));
  }
  LocalFree(argv);

  // Renderer workers are separate sandboxed processes but reuse this single
  // installed executable; the private verb never enters the browser UI path.
  if (args.size() == 1 && args.front() == "--pv-renderer-child")
    return rt::run_renderer_child();

  // The updater's private verb is deliberately separate from ordinary argv.
  // Its target must be this executable, never a caller-selected file path.
  if (!args.empty() && args.front() == "--apply-update") {
    if (args.size() != 3)
      return report_cli_error("invalid internal update arguments");
    uint32_t parent_pid = 0;
    const auto parsed_pid = std::from_chars(args[1].data(),
                                            args[1].data() + args[1].size(),
                                            parent_pid);
    if (parsed_pid.ec != std::errc{} ||
        parsed_pid.ptr != args[1].data() + args[1].size() || parent_pid == 0)
      return report_cli_error("invalid internal update process id");
    wchar_t module_path[32768] = {};
    const DWORD module_length = GetModuleFileNameW(
        nullptr, module_path, DWORD(std::size(module_path)));
    if (module_length == 0 || module_length >= std::size(module_path))
      return report_cli_error("cannot identify the running executable");
    const std::wstring requested_target = utf16_of(args[2]);
    auto absolute_path = [](const std::wstring &path) {
      std::vector<wchar_t> buffer(32768);
      const DWORD length = GetFullPathNameW(path.c_str(), DWORD(buffer.size()),
                                            buffer.data(), nullptr);
      if (length == 0 || length >= buffer.size()) return std::wstring{};
      return std::wstring(buffer.data(), length);
    };
    const std::wstring current_target = absolute_path(module_path);
    const std::wstring supplied_target = absolute_path(requested_target);
    if (current_target.empty() || supplied_target.empty() ||
        CompareStringOrdinal(current_target.data(), int(current_target.size()),
                             supplied_target.data(), int(supplied_target.size()),
                             TRUE) != CSTR_EQUAL)
      return report_cli_error("update target must be the running executable");

    HANDLE parent = OpenProcess(SYNCHRONIZE, FALSE, DWORD(parent_pid));
    if (!parent) return report_cli_error("cannot wait for the update parent");
    const DWORD wait = WaitForSingleObject(parent, 30000);
    CloseHandle(parent);
    if (wait != WAIT_OBJECT_0)
      return report_cli_error("update parent did not exit before the timeout");
    Sleep(400);
    const std::wstring staged = current_target + L".staged";
    bool installed = false;
    for (int attempt = 0; attempt < 20; ++attempt) {
      if (CopyFileW(staged.c_str(), current_target.c_str(), FALSE)) {
        installed = true;
        break;
      }
      Sleep(500);
    }
    if (!installed) return report_cli_error("could not install the verified update");
    DeleteFileW(staged.c_str());
    const HINSTANCE launched = ShellExecuteW(nullptr, L"open", current_target.c_str(),
                                              L"", nullptr, SW_SHOWNORMAL);
    if (reinterpret_cast<INT_PTR>(launched) <= 32)
      return report_cli_error("could not relaunch the updated executable");
    return 0;
  }

  rt::CommandLineOptions command;
  rt::Error command_error;
  if (!rt::parse_command_line(args, command, command_error))
    return report_cli_error(command_error.message);
  const std::string capture = command.capture_path;
  const bool smoke = command.smoke;
  const bool private_mode = command.private_mode;
  const bool expect_image = command.expect_image;
  const std::string smoke_bridge_url = command.smoke_bridge_url;
  const std::string profile_dir = command.profile_directory.empty()
      ? default_profile_directory() : command.profile_directory;
  const std::vector<std::string> startup_urls =
      rt::command_line_startup_urls(command);
  // Facts for psychovault://version. Only things I actually know are listed; no
  // build hashes are computed yet, so the page says none were provided.
  g_version_info.runtime_version = PSYCHO_VAULT_VERSION;
  g_version_info.engine_version = PSYCHO_VAULT_VERSION;
  g_version_info.profile = command.profile_directory.empty() ? "default" : "custom";
  if (private_mode) g_version_info.profile = "private";
  if (private_mode) g_version_info.flags.push_back("--private");
  if (command.diagnostics) g_version_info.flags.push_back("--diagnostics");
  g_version_known = true;

  auto dispatch_forwarded = [](const rt::LaunchRequest &request) {
    auto *launch = new ForwardedLaunch;
    rt::CommandLineOptions forwarded;
    rt::Error error;
    if (rt::parse_command_line(request.args, forwarded, error))
      launch->urls = rt::command_line_startup_urls(forwarded);
    else
      launch->error = error.message;
    std::lock_guard<std::mutex> lock(forwarded_mutex);
    HWND target = primary_window.load();
    if (target) {
      if (!PostMessageW(target, WM_FORWARD_LAUNCH, 0,
                        reinterpret_cast<LPARAM>(launch)))
        delete launch;
    } else {
      pending_forwarded.push_back(launch);
    }
  };
  Browser *browser_instance = nullptr;
  auto save_before_quit = [&browser_instance, private_mode](std::string &error) {
    if (!browser_instance) return true;
    if (!private_mode) {
      const bool session_saved = browser_instance->save_session();
      const bool profile_saved = browser_instance->save_profile();
      if (!session_saved || !profile_saved) {
        error = browser_instance->profile.error.empty()
            ? browser_instance->profile_error : browser_instance->profile.error;
        if (error.empty()) error = "profile or session save failed";
        return false; // a refused close must leave the live pages running
      }
    }
    for (auto &tab : browser_instance->tabs)
      browser_instance->stop_session(tab);
    return true;
  };
  uint64_t profile_hash = 14695981039346656037ull;
  for (unsigned char ch : profile_dir) {
    profile_hash ^= ch;
    profile_hash *= 1099511628211ull;
  }
  const std::string instance_mode = private_mode ? "\\private" : "\\normal";
  for (unsigned char ch : instance_mode) {
    profile_hash ^= ch;
    profile_hash *= 1099511628211ull;
  }
  const std::string profile_key = "profile-" + std::to_string(profile_hash);
  rt::AppInstance instance(rt::windows_instance_seams(
      profile_key, dispatch_forwarded, save_before_quit));
  app_instance = &instance;
  std::string instance_error;
  const auto instance_start = instance.start(rt::LaunchRequest{args}, instance_error);
  if (instance_start == rt::AppInstance::Start::ForwardedAndExit) {
    app_instance = nullptr;
    return 0;
  }
  if (instance_start == rt::AppInstance::Start::LockFailed) {
    app_instance = nullptr;
    return report_cli_error(instance_error.empty()
        ? "could not acquire the profile instance lock" : instance_error);
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
  Browser b(private_mode, profile_dir);
  browser_instance = &b;
  rt::RendererProcessFarm hosted_renderers;
  rt::Error renderer_start_error;
  const std::u8string renderer_path_utf8 =
      (std::filesystem::path(executable_dir()) / "PsychoVault.exe").u8string();
  const std::string renderer_executable(
      reinterpret_cast<const char *>(renderer_path_utf8.data()),
      renderer_path_utf8.size());
  if (!hosted_renderers.start(renderer_executable, 2, renderer_start_error)) {
    std::string ignored;
    instance.quit(ignored);
    app_instance = nullptr;
    return report_cli_error("could not start the isolated renderer pool: " +
                            renderer_start_error.message);
  }
  auto startup_fetch = [](const std::string &url, ResourceType) {
    Resource resource;
    resource.url = url;
    resource.mime = "text/html; charset=utf-8";
    resource.status = 200;
    resource.ok = true;
    resource.text = "<!doctype html><html><body><h1>Renderer process ready</h1></body></html>";
    return resource;
  };
  for (size_t i = 0; i < hosted_renderers.size(); ++i) {
    rt::Envelope probe_reply;
    rt::Error probe_error;
    if (!hosted_renderers.navigate(i, "about:blank", startup_fetch,
                                   probe_reply, probe_error) ||
        !probe_reply.payload.flag("ok") ||
        !probe_reply.payload.find("frame")) {
      hosted_renderers.shutdown();
      std::string ignored;
      instance.quit(ignored);
      app_instance = nullptr;
      return report_cli_error("isolated renderer startup probe failed: " +
                              probe_error.message);
    }
  }
  renderer_farm = &hosted_renderers;
  b.init_runtime_services();
  b.log_event("app", rt::LogLevel::Info, "start",
              {{"version", PSYCHO_VAULT_VERSION},
               {"mode", private_mode ? "private" : "normal"}});
  b.log_event("renderer", rt::LogLevel::Info, "pool.started",
              {{"count", std::to_string(hosted_renderers.size())}});
  b.smoke = smoke;
  b.expect_image = expect_image;
  b.capture_path = capture;
  b.smoke_bridge_url = smoke_bridge_url;
  b.smoke_tabs = command.smoke_tabs;
  b.smoke_services = command.smoke_services;
  b.smoke_chrome = command.smoke_chrome;
  b.smoke_settings = command.smoke_settings;
  // The engine-rendered frame is the browser's chrome. The older smokes drive the page alone, so they
  // keep it off unless they ask for it; --no-chrome turns it off for debugging.
  if (!command.no_chrome && (command.chrome || !command.smoke || command.smoke_settings)) b.enable_chrome();
  b.start_tick = GetTickCount();
  if (smoke && !capture.empty() && (command.smoke_chrome || command.smoke_settings)) {
    std::thread([path = capture + ".watchdog.json", started = b.start_tick] {
      Sleep(75000);
      Json report = Json::make_object();
      report.set("result", "hung");
      report.set("lastMessage", double(g_last_message.load()));
      report.set("insideMessage", double(g_in_message.load()));
      report.set("msSinceLastMessage", double(GetTickCount() - g_last_message_tick.load()));
      report.set("msSinceStart", double(GetTickCount() - started));
      report.set("timerTicks", double(g_timer_ticks.load()));
      report.set("paints", double(g_paints.load()));
      report.set("chromeFrames", double(g_chrome_frames.load()));
      report.set("paintMsTotal", double(g_paint_us.load()) / 1000.0);
      report.set("drawMsTotal", double(g_draw_us.load()) / 1000.0);
      std::string ignored;
      write_file_atomic(path, report.dump() + "\n", ignored);
      TerminateProcess(GetCurrentProcess(), 3);
    }).detach();
  }
  if (private_mode) {
    b.forget_all_tabs();
    b.new_tab("psychovault://newtab", false);
  } else if (b.profile.restore_tabs && !b.profile.session_tabs.empty()) {
    b.forget_all_tabs();
    // The session file mirrors the active workspace; a workspace store with
    // entries takes precedence because it is the richer record.
    const auto &source_tabs =
        (!b.workspaces.empty() && !b.workspaces[b.workspace].tabs.empty())
            ? b.workspaces[b.workspace].tabs
            : b.profile.session_tabs;
    for (auto &u : source_tabs)
      b.new_tab(u, false);
    for (size_t i = 0; i < b.tabs.size(); ++i) {
      rt::Error flag_error;
      if (i < b.profile.session_pinned.size())
        b.tab_model.set_pinned(b.tabs[i].id, b.profile.session_pinned[i] == '1', flag_error);
      if (i < b.profile.session_muted.size())
        b.tab_model.set_muted(b.tabs[i].id, b.profile.session_muted[i] == '1', flag_error);
    }
    b.apply_model_order();
    b.active = std::clamp(b.profile.session_active, 0, int(b.tabs.size()) - 1);
    b.sync_address();
    b.settings = starts(b.current().url, "psychovault://settings");
  }
  {
    std::string identity_error;
    if (!rt::windows_taskbar_identity(identity_error))
      b.log_event("window", rt::LogLevel::Warn, "taskbar-identity.failed",
                  {{"detail", identity_error}});
  }
  static HWND rules_window = nullptr; // the apply seam below runs after creation
  rt::WindowRules window_rules(rt::WindowRules::Seams{
      enumerate_monitors,
      [](const rt::WindowRecord &record) {
        HWND target = rules_window;
        if (!target) return;
        switch (record.state) {
        case rt::WindowState::Maximized:
          if (!IsZoomed(target)) ShowWindow(target, SW_MAXIMIZE);
          break;
        case rt::WindowState::Minimized:
          if (!IsIconic(target)) ShowWindow(target, SW_MINIMIZE);
          break;
        case rt::WindowState::Normal:
          if (IsZoomed(target) || IsIconic(target)) ShowWindow(target, SW_RESTORE);
          break;
        case rt::WindowState::Fullscreen:
          break; // not wired: the shell has no fullscreen mode yet
        }
      }});
  // First placement: centred on the primary work area and clamped inside it, so
  // a smaller or rearranged display never opens the window off screen.
  rt::WinRect first{0, 0, 1440, 940};
  {
    const auto monitors = enumerate_monitors();
    if (!monitors.empty()) {
      const rt::WinRect &work = monitors.front().work_area;
      first.x = work.x + (work.w - first.w) / 2;
      first.y = work.y + (work.h - first.h) / 2;
    }
  }
  std::string rules_error;
  const int rules_id = window_rules.create("PsychoVault", first, rules_error);
  const rt::WinRect placed = window_rules.find(rules_id)->bounds;
  HWND hwnd =
      CreateWindowExW(0, wc.lpszClassName, L"PsychoVault",
                      WS_POPUP | WS_THICKFRAME | WS_MINIMIZEBOX | WS_MAXIMIZEBOX | WS_SYSMENU,
                      placed.x, placed.y, placed.w, placed.h, nullptr, nullptr, inst, &b);
  rules_window = hwnd;
  b.window_rules = &window_rules;
  b.window_rules_id = rules_id;
  if (!hwnd) {
    renderer_farm = nullptr;
    hosted_renderers.shutdown();
    std::string ignored;
    instance.quit(ignored);
    app_instance = nullptr;
    return 1;
  }
  {
    std::lock_guard<std::mutex> lock(forwarded_mutex);
    primary_window.store(hwnd);
    while (!pending_forwarded.empty()) {
      ForwardedLaunch *launch = pending_forwarded.front();
      pending_forwarded.pop_front();
      if (!PostMessageW(hwnd, WM_FORWARD_LAUNCH, 0,
                        reinterpret_cast<LPARAM>(launch)))
        delete launch;
    }
  }
  MARGINS margins{0, 0, 1, 0};
  DwmExtendFrameIntoClientArea(hwnd, &margins);
  ShowWindow(hwnd, show);
  UpdateWindow(hwnd);
  b.resize(1440, 940);
  if (!startup_urls.empty()) {
    b.navigate(startup_urls.front(), false);
    for (size_t i = 1; i < startup_urls.size(); ++i)
      b.new_tab(startup_urls[i], false);
  } else if (smoke) {
    b.navigate("psychovault://newtab", false);
  } else if (!b.current().started || app_ui_url(b.current().url)) {
    b.navigate(b.current().url, false);
  }
  MSG msg;
  while (GetMessageW(&msg, nullptr, 0, 0) > 0) {
    TranslateMessage(&msg);
    DispatchMessageW(&msg);
  }
  b.stop_all_sessions();
  renderer_farm = nullptr;
  hosted_renderers.shutdown();
  std::string quit_error;
  const bool quit_ok = instance.quit(quit_error);
  app_instance = nullptr;
  {
    std::lock_guard<std::mutex> lock(forwarded_mutex);
    primary_window.store(nullptr);
    while (!pending_forwarded.empty()) {
      delete pending_forwarded.front();
      pending_forwarded.pop_front();
    }
  }
  if (b.vault_service) b.vault_service->lock();
  if (!quit_ok) return report_cli_error(quit_error.empty()
      ? "could not save the profile before exit" : quit_error);
  return int(msg.wParam);
}
