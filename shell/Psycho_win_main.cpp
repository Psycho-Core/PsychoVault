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
struct Tab {
  int id;
  std::string url, title = "New Tab";
  std::vector<std::string> history;
  size_t index = 0;
  bool loading = false, started = false, recorded = false;
  bool pinned = false, muted = false;
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
#ifndef PSYCHO_VAULT_VERSION
#define PSYCHO_VAULT_VERSION "0.0.0-dev"
#endif
struct MenuLine {
  std::string label, action;
};
static bool app_ui_url(const std::string &text) {
  auto parsed = parse_url(text);
  return parsed && parsed->scheme == "psychovault" && parsed->host == "newtab" &&
         (parsed->path.empty() || parsed->path == "/") && parsed->query.empty() &&
         parsed->fragment.empty();
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
  double width = 1000, height = 700;
  std::mutex mutex;
  std::condition_variable changed;
  std::deque<Json> outgoing;
  std::atomic<bool> stopping{false};
  std::atomic<bool> ready{false};
  std::atomic<bool> finished{false};
  std::atomic<bool> muted{false};
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
  const bool app_page = app_ui_url(url);
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
    if (parsed && parsed->scheme == "psychovault" && parsed->host == "newtab" &&
        parsed->query.empty() && parsed->fragment.empty()) {
      Resource resource;
      resource.url = parsed->href();
      resource.ok = true;
      resource.status = 200;
      if (parsed->path.empty() || parsed->path == "/") {
        resource.mime = "text/html; charset=utf-8";
        resource.text = kPVAppUiHtml;
      } else if (parsed->path == "/app.js") {
        resource.mime = "text/javascript; charset=utf-8";
        resource.text = kPVAppUiJs;
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
      ok = farm->navigate(renderer_slot, url, fetcher, reply, renderer_error,
                          std::move(cookies), std::move(local), poster, bridge_invoke);
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
    if (!exchange(&command, reply)) {
      fail("Renderer input IPC failed: " + renderer_error.message);
      break;
    }
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
struct Browser {
  HWND hwnd = nullptr;
  Profile profile;
  std::vector<Tab> tabs;
  std::vector<std::shared_ptr<Session>> retired_sessions;
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
  bool page_pointer_inside = false, page_pointer_down = false, page_composing = false;
  bool document_scroll_drag = false;
  double document_scroll_drag_start_y = 0, document_scroll_drag_start = 0;
  int width = 1440, height = 940;
  // S045 architecture slice: the browser's page surface is the only drawn
  // interface; the native shell keeps windowing, input dispatch and presentation.
  bool app_ui_slice = true;
  bool private_mode = false;
  std::string capture_path;
  std::string smoke_bridge_url;
  int smoke_bridge_stage = 0;
  int smoke_bridge_source_tab = -1;
  DWORD smoke_bridge_stage_tick = 0;
  bool smoke = false, expect_image = false;
  DWORD start_tick = 0;
  explicit Browser(bool private_mode = false, const std::string &directory = {})
      : private_mode(private_mode) {
    profile =
        Profile::open(directory.empty() ? default_profile_directory() : directory, private_mode);
    profile_error = profile.error;
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
  // A string of secrets is zeroed before it goes out of scope anywhere I can
  // see it, so a passphrase or a copied password does not sit readable in RAM.
  static void scrub(std::string &text) {
    if (!text.empty())
      std::fill(text.begin(), text.end(), '\0');
    text.clear();
  }
  std::string vault_path() const { return profile.directory + "/vault.bin"; }
  bool vault_exists() const {
    DWORD attr = GetFileAttributesW(utf16_of(vault_path()).c_str());
    return attr != INVALID_FILE_ATTRIBUTES && (attr & FILE_ATTRIBUTE_DIRECTORY) == 0;
  }
  bool vault_save() {
    std::string error;
    pv::Bytes blob = vault.seal();
    std::string data(reinterpret_cast<const char *>(blob.data()), blob.size());
    if (!write_file_atomic(vault_path(), data, error)) {
      status = "I could not save my vault: " + error;
      return false;
    }
    vault.dirty = false;
    return true;
  }
  void vault_wipe() {
    std::fill(vault.key.begin(), vault.key.end(), uint8_t(0));
    for (auto &entry : vault.entries)
      scrub(entry.second);
    vault.entries.clear();
  }
  void vault_lock() {
    if (!vault_open)
      return;
    vault_wipe();
    vault_open = false;
    vault_unlocked_at = 0;
    if (hwnd)
      InvalidateRect(hwnd, nullptr, FALSE);
    status = "My vault is locked";
  }
  // A vault row is "site | user | password | note"; copying hands over only
  // the password segment, never the whole row.
  std::string spell_extra_path() const { return profile.directory + "/owned-spell-extra.txt"; }
  void spell_reload_source() {
    try {
      spell.replace_extra(read_text_file(spell_extra_path()));
    } catch (const std::exception &) {
      spell.replace_extra("");
    }
  }
  void spell_reload_users() { spell.set_users(profile.user_words); }

  static std::string vault_password_of(const std::string &row) {
    size_t first = row.find(" | ");
    size_t second = first == std::string::npos ? first : row.find(" | ", first + 3);
    if (second == std::string::npos)
      return row;
    size_t third = row.find(" | ", second + 3);
    return row.substr(second + 3,
                      third == std::string::npos ? third : third - second - 3);
  }

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
      profile.clear_history();
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
  void stop_session(Tab &tab) {
    if (!tab.session) return;
    tab.session->request_stop();
    retired_sessions.push_back(std::move(tab.session));
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
  void new_tab(std::string url = "psychovault://newtab", bool focus = true) {
    Tab t;
    t.id = next_id++;
    t.url = url;
    t.title = app_ui_url(url) ? "New Tab" : url;
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
    if (tabs[size_t(index)].pinned) {
      status = "Unpin the tab before closing it";
      InvalidateRect(hwnd, nullptr, FALSE);
      return;
    }
    if (tabs.size() <= 1) {
      stop_session(current());
      tabs.clear();
      new_tab();
      save_session();
      return;
    }
    if (!tabs[size_t(index)].url.empty()) {
      ClosedTab closed;
      closed.url = tabs[size_t(index)].url;
      closed.title = tabs[size_t(index)].title;
      closed.history = tabs[size_t(index)].history;
      closed.index = tabs[size_t(index)].index;
      closed.pinned = tabs[size_t(index)].pinned;
      closed.muted = tabs[size_t(index)].muted;
      closed_tabs.push_back(std::move(closed));
      if (closed_tabs.size() > 20)
        closed_tabs.erase(closed_tabs.begin());
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
    if ((!tab.started && (starts(tab.url, "http://") || starts(tab.url, "https://"))) ||
        (app_ui_url(tab.url) && !tab.session && tab.frame.type == Json::Type::Null)) {
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
        if (!private_mode &&
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
    tab.images.clear();
    tab.image_bytes = 0;
    tab.scroll = 0;
    tab.generation = ++generation;
    if (index == active) {
      settings = starts(target, "psychovault://settings");
      sync_address();
    }
    const bool app_page = app_ui_url(target);
    if ((starts(target, "psychovault://") && !app_page) || target == "about:blank") {
      tab.loading = false;
      tab.recorded = true;
      tab.title = starts(target, "psychovault://settings") ? "Settings" : "New Tab";
      if (index == active)
        status = "Ready";
      save_session();
      InvalidateRect(hwnd, nullptr, FALSE);
      return;
    }
    if (!u || (!u->network() && !app_page)) {
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
    session->policy.filter_counts = &session->counts;
    session->report = report;
    session->jar = &cookies;
    session->storage = &local_storage;
    session->profile_state_mutex = &profile_state_mutex;
    session->farm = renderer_farm;
    session->bridge_invoke = [window = hwnd, source_tab = tab.id](
        rt::Channel channel, const std::string &method, const Json &payload,
        Json &reply, rt::Error &bridge_error) {
      static const rt::Schemas schemas = rt::Schemas::stock();
      bridge_error = rt::Error{};
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
    content = app_ui_slice ? Rect{0, 0, double(w), double(h)}
                           : Rect{44, 84, double(w - 44), double(h - 106)};
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
    if (app_ui_slice) {
      // Keep the S045 page surface free of the legacy browser chrome, but never
      // leave a user with a featureless black window while a page is starting or
      // when navigation fails. A small shell-owned status card is the fallback
      // until the renderer supplies a page frame.
      c.fill({0, 0, double(width), double(height)}, BG);
      Tab &tab = current();
      if (tab.frame.type == Json::Type::Object && tab.error.empty()) {
        draw_content(c);
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
      return;
    }
    c.fill({0, 0, double(width), double(height)}, BG);
    c.fill({0, 0, 44, double(height)}, RAIL);
    c.line(43, 0, 43, height, BORDER);
    c.fill({44, 0, double(width - 44), 40}, CHROME);
    c.fill({44, 40, double(width - 44), 44}, CHROME);
    c.line(44, 83, width, 83, BORDER);
    double x = 52;
    for (size_t i = 0; i < tabs.size() && x < width - 180; ++i) {
      bool sel = int(i) == active;
      Rect r{x, 6, double(tabs[i].pinned ? 96 : 180), 32};
      c.fill(r, sel ? PANEL : CHROME);
      if (sel) {
        c.line(r.x, r.y, r.x + r.w, r.y, ACCENT, 2);
      }
      // A pinned tab is one letter of its title, and pinning blocks closing so
      // the close control goes away. A muted tab carries a small dot.
      c.text(r.x + 12, r.y + 8,
             tabs[i].pinned ? tabs[i].title.substr(0, 1)
                            : tabs[i].title.substr(0, 22),
             13, sel ? TEXT : MUTED, sel);
      if (tabs[i].muted)
        c.text(r.x + r.w - (tabs[i].pinned ? 22 : 42), r.y + 12, "o", 10, ACCENT);
      hits.push_back({r, "tab", int(i), {}});
      if (!tabs[i].pinned && profile.show_tab_close_buttons) {
        Rect close{r.x + r.w - 24, r.y + 6, 18, 18};
        c.text(close.x + 4, close.y + 1, "x", 12, MUTED);
        hits.push_back({close, "close-tab", int(i), {}});
      }
      x += r.w + 4;
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
    if (address_focus && !address.empty()) {
      // My suggestions come from my own bookmarks and history - a dropdown of
      // things I actually saved, filtered by the text I am typing.
      std::string needle = ascii_lower(address);
      std::vector<std::pair<std::string, std::string>> found;
      auto consider = [&](const std::string &title, const std::string &url) {
        if (found.size() >= 6)
          return;
        if (ascii_lower(title + "  " + url).find(needle) == std::string::npos)
          return;
        for (auto &f : found)
          if (f.second == url)
            return;
        found.push_back({title, url});
      };
      for (auto &b : profile.bookmarks)
        consider(b.title, b.url);
      for (auto &hh : profile.history)
        consider(hh.title, hh.url);
      if (!found.empty()) {
        Rect box{addr.x, addr.y + addr.h + 4, addr.w, double(found.size()) * 28 + 8};
        c.fill(box, PANEL);
        c.stroke(box, BORDER);
        double ry = box.y + 4;
        for (auto &f : found) {
          c.text(box.x + 12, ry + 6, (f.first + "  -  " + f.second).substr(0, 100),
                 12, TEXT);
          hits.push_back({{box.x, ry, box.w, 28}, "suggest", 0, f.second});
          ry += 28;
        }
      }
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
    if (profile.workspaces) {
      c.text(12, 192, "W", 14, panel_workspaces ? ACCENT : MUTED, panel_workspaces);
      hits.push_back({{4, 184, 36, 36}, "workspaces", 0, {}});
    }
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
    else if (panel_workspaces)
      draw_workspaces(c);
    else if (panel_bookmarks || panel_history)
      draw_panel(c);
    else if (settings)
      draw_settings(c);
    else if ((app_ui_url(current().url) && !current().session) ||
             current().url == "about:blank")
      draw_home(c);
    else
      draw_content(c);
    if (palette_open) {
      Rect box{content.x + content.w / 2 - 300, content.y + 20, 600, 38};
      c.fill(box, PANEL);
      c.stroke(box, ACCENT);
      c.text(box.x + 14, box.y + 10,
             (palette_text.empty() ? "Type a command" : palette_text).substr(0, 64), 13,
             palette_text.empty() ? MUTED : TEXT);
      double cx2 = box.x + 14 + c.text_width(palette_text.substr(0, palette_caret), 13);
      c.line(cx2, box.y + 8, cx2, box.y + 30, ACCENT);
      auto matches = palette_matches();
      if (palette_index >= int(matches.size()))
        palette_index = int(matches.size()) - 1;
      double ry = box.y + box.h + 2;
      for (size_t i = 0; i < matches.size() && i < 8; ++i) {
        Rect row{box.x, ry, 600, 30};
        c.fill(row, int(i) == palette_index ? 0xff16233a : PANEL);
        c.stroke(row, BORDER);
        c.text(row.x + 14, row.y + 8, matches[i]->label, 13, TEXT);
        c.text(row.x + row.w - 118, row.y + 8, matches[i]->chord, 12, MUTED);
        hits.push_back({row, "palette-run", int(i), matches[i]->id});
        ry += 32;
      }
    }
    // Two banners can sit on the page area: crash recovery, then update.
    double banner_y = content.y;
    if (crashed_last && !recovery_dismissed) {
      Rect strip{content.x, banner_y, content.w, 34};
      c.fill(strip, 0xff2a2107);
      c.text(strip.x + 14, strip.y + 9,
             "My last session ended unexpectedly. Your tabs were restored.", 12, TEXT);
      Rect keep{strip.x + strip.w - 262, strip.y + 4, 150, 26};
      c.fill(keep, PANEL);
      c.stroke(keep, BORDER);
      c.text(keep.x + 10, keep.y + 6, "Keep restored session", 11, TEXT);
      hits.push_back({keep, "recovery-keep", 0, {}});
      Rect fresh{strip.x + strip.w - 102, strip.y + 4, 94, 26};
      c.fill(fresh, PANEL);
      c.stroke(fresh, BORDER);
      c.text(fresh.x + 10, fresh.y + 6, "Start fresh", 11, TEXT);
      hits.push_back({fresh, "recovery-fresh", 0, {}});
      banner_y += 36;
    }
    if (!next_update.version.empty() && !update_snoozed &&
        profile.dismissed_update != next_update.version) {
      Rect strip{content.x, banner_y, content.w, 34};
      c.fill(strip, 0xff0a2410);
      std::string line = next_update.ready
          ? "Update " + next_update.version + " verified - restart to install"
          : "Update " + next_update.version + " is available";
      c.text(strip.x + 14, strip.y + 9, line, 12, TEXT);
      Rect first{strip.x + strip.w - 302, strip.y + 4, 90, 26};
      c.fill(first, PANEL);
      c.stroke(first, BORDER);
      if (next_update.ready) {
        c.text(first.x + 10, first.y + 6, "Restart now", 11, TEXT);
        hits.push_back({first, "update-restart", 0, {}});
      } else {
        c.text(first.x + 22, first.y + 6, next_update.installing ? "..." : "Install", 11, TEXT);
        hits.push_back({first, "update-install", 0, {}});
      }
      Rect later{strip.x + strip.w - 202, strip.y + 4, 76, 26};
      c.fill(later, PANEL);
      c.stroke(later, BORDER);
      c.text(later.x + 22, later.y + 6, "Later", 11, TEXT);
      hits.push_back({later, "update-later", 0, {}});
      Rect dismiss{strip.x + strip.w - 114, strip.y + 4, 86, 26};
      c.fill(dismiss, PANEL);
      c.stroke(dismiss, BORDER);
      c.text(dismiss.x + 14, dismiss.y + 6, "Dismiss", 11, TEXT);
      hits.push_back({dismiss, "update-dismiss", 0, {}});
      banner_y += 36;
    }
    if (menu_open) {
      const int w = 220, h = int(menu_items.size()) * 30 + 10;
      Rect box{double(menu_x), double(menu_y), double(w), double(h)};
      c.fill(box, PANEL);
      c.stroke(box, BORDER);
      double my = box.y + 5;
      for (auto &item : menu_items) {
        c.text(box.x + 12, my + 7, item.label, 13, TEXT);
        my += 30;
      }
    }
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
    c.text(52, height - 19, status.substr(0, 140), 11, MUTED);
    // What my shield did on the page in front of me. The number is this tab's own
    // count, and the reason is the rule that last refused something.
    if (active < int(tabs.size())) {
      Tab &shown_tab = tabs[size_t(active)];
      if (shown_tab.session) {
        uint64_t blocked = shown_tab.session->blocked_in_page();
        if (blocked) {
          std::string label = "Blocked " + std::to_string(blocked) + " here";
          c.text(double(width) - 320, height - 19, label, 11, ACCENT);
          hits.push_back({{double(width) - 320, double(height - 22), 260, 22}, "shield-note", 0,
                          shown_tab.session->last_block_reason()});
        }
      }
    }
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
    // My spell checker marks words that fail my dictionary with a red squiggle
    // and offers fixes underneath the box. URLs are left alone.
    if (profile.spell_check && !home_search.empty() && home_focus) {
      if (spell_cover != home_search) {
        spell_marks = spell_check_text(spell, home_search);
        spell_cover = home_search;
      }
      constexpr uint32_t SPELL_RED = 0xff0000ff;
      for (const auto &mark : spell_marks) {
        double x0 = search.x + 16 + c.text_width(home_search.substr(0, mark.start), 14);
        double w = c.text_width(home_search.substr(mark.start, mark.length), 14);
        double y0 = search.y + 36;
        for (double sx = x0; sx + 3 <= x0 + w; sx += 3)
          c.line(sx, y0 + (int((sx - x0) / 3) % 2 ? 0.0 : 2.0), sx + 3,
                 y0 + (int((sx - x0) / 3 + 1) % 2 ? 0.0 : 2.0), SPELL_RED, 1);
      }
      const SpellMark *hint = nullptr;
      for (const auto &mark : spell_marks)
        if (home_search_caret >= mark.start && home_search_caret <= mark.start + mark.length)
          hint = &mark;
      if (!hint && !spell_marks.empty())
        hint = &spell_marks.front();
      if (hint) {
        std::string message = "\"" + home_search.substr(hint->start, hint->length) +
                              "\" is not in my dictionary";
        if (!hint->suggestions.empty()) {
          message += " - try ";
          for (size_t i = 0; i < hint->suggestions.size(); ++i)
            message += (i ? ", " : "") + hint->suggestions[i];
          message += " (Tab takes the first)";
        }
        c.text(search.x, search.y + 52, message, 12, MUTED);
      }
    }
    // My speed dial lives in the profile now, so the tiles below are whatever
    // I saved - not a hardcoded row. A right click menu on any page adds it
    // here, and each tile's corner removes it.
    for (size_t i = 0; i < profile.speed_dial.size() && i < 24; ++i) {
      int col = int(i) % 6, rowi = int(i) / 6;
      Rect tile{cx - 324 + col * 108, cy + 130 + rowi * 100, 96, 84};
      c.fill(tile, 0xff0d1420);
      c.stroke(tile, BORDER);
      const std::string &label = profile.speed_dial[i].title;
      c.text(tile.x + 38, tile.y + 16,
             label.empty() ? "?" : label.substr(0, 1), 18, ACCENT, true);
      c.text(tile.x + 10, tile.y + 52, label.substr(0, 12), 12, MUTED);
      hits.push_back({tile, "tile", int(i), profile.speed_dial[i].url});
      Rect remove{tile.x + tile.w - 20, tile.y + 4, 16, 16};
      c.text(remove.x + 4, remove.y, "x", 10, MUTED);
      hits.push_back({remove, "dial-remove", int(i), {}});
    }
    if (profile.speed_dial.size() < 24) {
      size_t i = profile.speed_dial.size();
      int col = int(i) % 6, rowi = int(i) / 6;
      Rect tile{cx - 324 + col * 108, cy + 130 + rowi * 100, 96, 84};
      c.fill(tile, 0xff0d1420);
      c.stroke(tile, BORDER);
      c.text(tile.x + 34, tile.y + 14, "+", 20, MUTED, true);
      c.text(tile.x + 10, tile.y + 52, "Add from box", 11, MUTED);
      hits.push_back({tile, "dial-add", 0, {}});
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
    Rect folder{content.x + content.w - 200, content.y + 60, 150, 30};
    c.fill(folder, PANEL);
    c.stroke(folder, BORDER);
    c.text(folder.x + 12, folder.y + 7, "Open folder", 12, TEXT);
    hits.push_back({folder, "downloads-folder", 0, {}});
    Rect pick{content.x + content.w - 380, content.y + 24, 160, 30};
    c.fill(pick, PANEL);
    c.stroke(pick, BORDER);
    c.text(pick.x + 12, pick.y + 7, "Change folder...", 12, TEXT);
    hits.push_back({pick, "downloads-pickdir", 0, {}});
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
        Rect reveal{row.x + row.w - 180, row.y + 12, 76, 28};
        c.stroke(reveal, BORDER);
        c.text(reveal.x + 14, reveal.y + 6, "Reveal", 12, TEXT);
        hits.push_back({reveal, "download-reveal", item.id, item.path});
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
    auto entry_button = [&](const std::string &label, const char *action, int index) {
      Rect r{x, y - 2, 240, 32};
      c.fill(r, PANEL);
      c.stroke(r, BORDER);
      c.text(r.x + 12, r.y + 8, label, 12, TEXT);
      hits.push_back({r, action, index, {}});
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
      toggle("Panic button", profile.panic_enabled, "setting-panic",
             "Ctrl+Shift+Q hides everything and opens my panic page.");
      value_row("Panic URL", profile.panic_url.empty() ? "my home page" : profile.panic_url,
                "setting-panic-url");
      value_row("Custom search", profile.custom_search.empty() ? "not set" : "configured",
                "setting-custom-search");
      break;
    case 1:
      toggle("Block ads and trackers", profile.block_ads, "setting-block-ads",
             "I refuse the request before a socket opens.");
      toggle("Block autoplay", profile.block_autoplay, "setting-block-autoplay",
             "A video or song only starts after I have clicked the page.");
      toggle("Block third-party cookies", profile.block_third_party_cookies,
             "setting-third-party-cookies", "");
      toggle("Send Do Not Track", profile.send_dnt, "setting-dnt", "I add DNT: 1 to every request.");
      toggle("Strip referrer", profile.strip_referrer, "setting-referrer", "");
      toggle("Clear history when I close", profile.clear_on_exit, "setting-clear-exit", "");
      value_row("WebRTC policy", profile.web_rtc_policy, "");
      button("Clear browsing history now", "setting-clear-history");
      toggle("Always use HTTPS", profile.https_only, "setting-https-only",
             "I upgrade every request, and fall back once when a server cannot answer "
             "over HTTPS.");
      value_row("Filter list file",
                profile.filter_list_path.empty() ? "my starter list only"
                                                 : profile.filter_list_path,
                "setting-filter-file");
      value_row("Memory cap", profile.memory_limit_mb > 0
                                  ? std::to_string(profile.memory_limit_mb) + " MiB"
                                  : std::string("none"),
                "setting-memory-cap");
      note(confinement_summary(confinement));
      if (!confinement_error.empty()) note("My job object said: " + confinement_error);
      note("Blocked hosts: " + std::to_string(profile.blocked_hosts.size()));
      note("Filter rules: " + std::to_string(filters->size()) + " (" +
           std::to_string(filters->exceptions()) + " exceptions)");
      note("Blocked since I started: " + std::to_string(report->blocked.load()) + ", upgraded " +
           std::to_string(report->upgraded.load()) + ", mixed refused " +
           std::to_string(report->mixed.load()) + ", forms refused " +
           std::to_string(report->forms.load()));
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
      button("Test my tunnel", "vpn-test");
      {
        std::string result;
        {
          std::lock_guard lock(tunnel_test->mutex);
          result = tunnel_test->text;
        }
        note(result);
      }
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
      toggle("Spell check my search box", profile.spell_check, "setting-spellcheck", "");
      toggle("Use my built-in dictionary", profile.spell_builtin, "setting-spell-builtin", "");
      note("My dictionary holds " + std::to_string(spell.builtin_count()) +
           " built-in words, " + std::to_string(spell.extra_count()) +
           " from my source, " + std::to_string(profile.user_words.size()) + " of my own.");
      button("Add a word from my search box", "setting-spell-add");
      button("Remove my last added word", "setting-spell-remove");
      value_row("Dictionary source (file or URL)", profile.spell_dict_source,
                "setting-spell-source");
      note("Languages: " + profile.spell_langs + " - English is the one pack I ship.");
      note("A misspelled word in my home search box gets a red squiggle, and");
      note("Tab swaps the word at my cursor for my first suggestion.");
      break;
    case 7: {
      if (!vault_exists()) {
        note("No vault yet - mine keeps logins in one encrypted vault.bin next to my profile.");
        note("Everything inside runs on my own ChaCha20-Poly1305 and PBKDF2 code.");
        note("The passphrase is the only key, so I cannot recover a forgotten one.");
        button("Create my vault with this passphrase", "vault-create");
      } else if (!vault_open) {
        note("My vault is locked - entries wait encrypted on disk.");
        button("Unlock my vault with this passphrase", "vault-unlock");
      } else {
        note("My vault is unlocked and holds " +
             std::to_string(vault.entries.size()) + " login(s).");
        note("Add rows as:  site | user | password | note   (note may be left off).");
        button("Add the row from my search box", "vault-add");
        button("Generate a 20 character password onto my clipboard", "vault-generate");
        int row = 0;
        for (const auto &entry : vault.entries) {
          if (row == 10) {
            note("  ... my list shows the first 10");
            break;
          }
          note("  " + entry.first);
          entry_button("Copy password - " + entry.first, "vault-copy", row);
          entry_button("Remove - " + entry.first, "vault-remove", row);
          row++;
        }
        button("Lock my vault now", "vault-lock");
      }
      value_row("Auto-lock minutes after unlock", std::to_string(profile.vault_lock_minutes),
                "setting-vault-lock");
      break;
    }
    case 8:
      toggle("High contrast", profile.high_contrast, "setting-high-contrast", "");
      toggle("Reduce motion", profile.reduce_motion, "setting-reduce-motion", "");
      toggle("Show tab close buttons", profile.show_tab_close_buttons, "setting-tab-close", "");
      note("Translation is not implemented yet.");
      break;
    default:
      note(std::string("PsychoVault ") + PSYCHO_VAULT_VERSION + " - owned engine");
      note("Profile: " + profile.directory);
      note(vpn_summary());
      note("Extensions: " + std::to_string(extensions.list().size()));
      button("Check for an update", "setting-check-update");
      {
        std::lock_guard lock(update_mutex);
        if (!update_status.empty()) note(update_status);
      }
      y += 10;
      c.text(x, y, "Error log", 16, TEXT, true);
      y += 30;
      for (auto &line : log_cache) {
        c.text(x, y, line.substr(0, 110), 11, MUTED);
        y += 20;
      }
      if (log_cache.empty()) { c.text(x, y, "Nothing logged yet.", 12, MUTED); y += 24; }
      y += 8;
      button("Clear error log", "setting-clear-log");
      note("I verify a release hash before I offer to install it.");
      break;
    }
    if (!profile_error.empty())
      c.text(x, content.y + content.h - 40, profile_error.substr(0, 100), 11, 0xfff87171);
  }
  std::string update_status;
  std::string editing_setting;
  // My vault lives in RAM only while vault_open, and every secret byte is
  // wiped the moment I lock. The passphrase itself is never stored anywhere.
  pv::Vault vault;
  bool vault_open = false;
  DWORD vault_unlocked_at = 0;
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
  std::vector<ClosedTab> closed_tabs;
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
    } else if (key == "vaultCreate") {
      if (value.size() < 8) {
        status = "A vault passphrase needs at least 8 characters";
      } else if (vault_exists()) {
        status = "A vault already exists - unlock it instead";
      } else {
        try {
          vault = pv::Vault::create(value);
          vault_open = true;
          vault_unlocked_at = GetTickCount();
          vault_save();
          status = "My vault is created and unlocked";
        } catch (const std::exception &e) {
          status = std::string("Vault failed: ") + e.what();
        }
      }
      scrub(value);
      scrub(address);
    } else if (key == "vaultUnlock") {
      try {
        std::string raw = read_text_file(vault_path());
        pv::Bytes blob(raw.begin(), raw.end());
        vault = pv::Vault::open(blob, value);
        vault_open = true;
        vault_unlocked_at = GetTickCount();
        status = "My vault is unlocked";
      } catch (const std::runtime_error &) {
        status = "That passphrase does not match my vault";
      }
      scrub(value);
      scrub(address);
    } else if (key == "vaultAdd") {
      if (!vault_open) {
        status = "Unlock my vault first";
      } else {
        size_t pipe = value.find(" | ");
        std::string name = pipe == std::string::npos ? value : value.substr(0, pipe);
        if (name.empty() || pipe == std::string::npos) {
          status = "Use the row form: site | user | password | note";
        } else if (vault.entries.count(name)) {
          status = "I already keep a login named " + name;
        } else {
          vault.entries[name] = value;
          vault.dirty = true;
          if (vault_save())
            status = "Saved " + name + " in my vault";
        }
      }
      scrub(value);
      scrub(address);
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
  void draw_panel(Canvas &c) {
    bool bm = panel_bookmarks;
    c.text(content.x + 40, content.y + 30, bm ? "Bookmarks" : "History", 24, TEXT, true);
    Rect box{content.x + 40, content.y + 62, 420, 30};
    c.fill(box, 0xff0a111c);
    c.stroke(box, panel_focus ? ACCENT : BORDER);
    c.text(box.x + 12, box.y + 7,
           (panel_search.empty() ? "Type to filter" : panel_search).substr(0, 48), 12,
           panel_search.empty() ? MUTED : TEXT);
    hits.push_back({box, "panel-search", 0, {}});
    if (panel_focus) {
      double cx2 = box.x + 12 + c.text_width(panel_search.substr(0, panel_search_caret), 12);
      c.line(cx2, box.y + 6, cx2, box.y + 24, ACCENT);
    }
    std::string needle = ascii_lower(panel_search);
    auto matches = [&](const std::string &title, const std::string &url) {
      return needle.empty() ||
             ascii_lower(title + "  " + url).find(needle) != std::string::npos;
    };
    double y = content.y + 106;
    size_t shown = 0;
    double max_y = content.y + content.h - 30;
    if (bm) {
      for (size_t i = 0; i < profile.bookmarks.size() && shown < 40 &&
                            y < max_y;
           ++i) {
        auto &b = profile.bookmarks[i];
        if (!matches(b.title, b.url))
          continue;
        Rect r{content.x + 40, y, content.w - 80, 36};
        c.text(r.x, r.y + 8, b.title + "  " + b.url, 13, TEXT);
        hits.push_back({r, "open-bookmark", int(i), b.url});
        y += 40;
        ++shown;
      }
    } else {
      // History is grouped by day so the list reads like a diary, newest day
      // on top. Boundary maths is on purpose local-time like mine.
      int64_t last_day = -1;
      std::time_t now = std::time(nullptr);
      std::tm tm_now{};
      localtime_s(&tm_now, &now);
      int64_t today = int64_t(tm_now.tm_year) * 1000 + tm_now.tm_yday;
      for (size_t i = 0; i < profile.history.size() && shown < 40 &&
                            y < max_y;
           ++i) {
        auto &h = profile.history[i];
        if (!matches(h.title, h.url))
          continue;
        std::time_t when = std::time_t(h.visited);
        std::tm tm_v{};
        localtime_s(&tm_v, &when);
        int64_t day = int64_t(tm_v.tm_year) * 1000 + tm_v.tm_yday;
        if (day != last_day) {
          last_day = day;
          std::string label = day == today     ? "Today"
                              : day == today - 1 ? "Yesterday"
                                                 : std::to_string(tm_v.tm_mday) + "/" +
                                                       std::to_string(tm_v.tm_mon + 1) + "/" +
                                                       std::to_string(tm_v.tm_year + 1900);
          c.text(content.x + 40, y + 6, label, 12, ACCENT, true);
          y += 26;
          if (y >= max_y)
            break;
        }
        Rect r{content.x + 40, y, content.w - 80, 36};
        c.text(r.x + 10, r.y + 8, h.title + "  " + h.url, 13, TEXT);
        hits.push_back({r, "open-history", int(i), h.url});
        y += 40;
        ++shown;
      }
    }
    if (!shown)
      c.text(content.x + 40, y, needle.empty() ? "Nothing saved yet."
                                               : "Nothing matches.", 13, MUTED);
  }
  void draw_workspaces(Canvas &c) {
    c.text(content.x + 40, content.y + 30, "Workspaces", 24, TEXT, true);
    c.text(content.x + 40, content.y + 60,
           "Each workspace keeps its own tabs. New and rename are typed into my address bar.",
           12, MUTED);
    Rect add{content.x + content.w - 200, content.y + 24, 150, 30};
    c.fill(add, PANEL);
    c.stroke(add, BORDER);
    c.text(add.x + 12, add.y + 7, "New workspace", 12, TEXT);
    hits.push_back({add, "workspace-new", 0, {}});
    double y = content.y + 100;
    for (size_t i = 0; i < workspaces.size() && y < content.y + content.h - 60; ++i) {
      // The live workspace is shown with the strip as it stands right now.
      Workspace shown = workspaces[i];
      if (i == workspace) {
        shown.tabs.clear();
        for (auto &tab : tabs)
          shown.tabs.push_back(tab.url);
      }
      Rect row{content.x + 40, y, content.w - 80, 48};
      c.fill(row, i == workspace ? PANEL : BG);
      c.stroke(row, i == workspace ? ACCENT : BORDER);
      c.text(row.x + 12, row.y + 7, shown.name, 14, TEXT, i == workspace);
      c.text(row.x + 12, row.y + 26,
             std::to_string(shown.tabs.size()) + " tab" +
                 (shown.tabs.size() == 1 ? "" : "s") +
                 (i == workspace ? " - active" : ""),
             11, MUTED);
      Rect rename{row.x + row.w - 270, row.y + 10, 76, 28};
      c.stroke(rename, BORDER);
      c.text(rename.x + 12, rename.y + 6, "Rename", 12, TEXT);
      hits.push_back({rename, "workspace-rename", int(i), {}});
      Rect close{row.x + row.w - 180, row.y + 10, 76, 28};
      c.stroke(close, BORDER);
      c.text(close.x + 16, close.y + 6, "Close", 12, TEXT);
      hits.push_back({close, "workspace-close", int(i), {}});
      Rect open_b{row.x + row.w - 90, row.y + 10, 76, 28};
      c.stroke(open_b, BORDER);
      c.text(open_b.x + 18, open_b.y + 6, "Open", 12, TEXT);
      hits.push_back({open_b, "workspace-open", int(i), {}});
      y += 56;
    }
  }
  // I turn a frame into the paint list my raster reads, in window coordinates.
  std::vector<PaintCommand> frame_commands(const Tab &t, double origin_x, double origin_y) const {
    std::vector<PaintCommand> commands;
    double zoom = t.zoom;
    for (auto &j : t.frame.at("paint").array) {
      PaintCommand p;
      p.kind = PaintCommand::Kind(int(j.num("kind")));
      const bool fixed = j.flag("fixed");
      p.rect = {j.num("x") * zoom + origin_x,
                j.num("y") * zoom + origin_y - (fixed ? 0 : t.scroll * zoom),
                j.num("w") * zoom, j.num("h") * zoom};
      p.color = uint32_t(j.num("color"));
      p.text = j.str("text");
      p.size = j.num("size") * zoom;
      p.thickness = j.num("thickness");
      p.bold = j.flag("bold");
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
    if (!current().session) return;
    Json command = Json::make_object();
    command.set("tag", "key");
    command.set("key", key);
    current().session->send(command);
  }
  void edit_clipboard(bool cut) {
    if (!current().session) return;
    Json command = Json::make_object();
    command.set("tag", cut ? "cut" : "copy");
    current().session->send(command);
  }
  void composition_start() {
    if (address_focus || home_focus || panel_focus || !current().session) return;
    Json command = Json::make_object(); command.set("tag", "compositionstart");
    current().session->send(command); page_composing = true;
  }
  void composition_update(const std::string &text, size_t caret) {
    if (!page_composing || !current().session) return;
    Json command = Json::make_object(); command.set("tag", "compositionupdate");
    command.set("text", text); command.set("caret", double(caret));
    current().session->send(command);
  }
  void composition_commit(const std::string &text) {
    if (!page_composing || !current().session) return;
    Json command = Json::make_object(); command.set("tag", "compositioncommit");
    command.set("text", text); current().session->send(command); page_composing = false;
  }
  void composition_cancel() {
    if (!page_composing || !current().session) return;
    Json command = Json::make_object(); command.set("tag", "compositioncancel");
    current().session->send(command); page_composing = false;
  }
  void key_up(WPARAM wp) {
    if (address_focus || home_focus || !current().session) return;
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
    current().session->send(command);
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
        else if (h.action == "recovery-keep") {
          recovery_dismissed = true;
        } else if (h.action == "recovery-fresh") {
          // Start fresh means exactly that: everything open goes to the reopen
          // stack, my saved session is cleared, and I land on a clean tab.
          for (auto &tab : tabs)
            stop_session(tab);
          tabs.clear();
          closed_tabs.clear();
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
        } else if (h.action == "vault-create" || h.action == "vault-unlock") {
          begin_setting_edit(h.action == "vault-create" ? "vaultCreate" : "vaultUnlock", "");
        } else if (h.action == "vault-add") {
          begin_setting_edit("vaultAdd", "");
        } else if (h.action == "vault-lock") {
          vault_lock();
        } else if (h.action == "vault-copy") {
          if (vault_open && h.index >= 0 && size_t(h.index) < vault.entries.size()) {
            auto it = vault.entries.begin();
            std::advance(it, h.index);
            std::string name = it->first;
            std::string secret = vault_password_of(it->second);
            write_clipboard(secret);
            scrub(secret);
            status = "Password for " + name + " is on my clipboard";
          }
        } else if (h.action == "vault-remove") {
          if (vault_open && h.index >= 0 && size_t(h.index) < vault.entries.size()) {
            auto it = vault.entries.begin();
            std::advance(it, h.index);
            std::string name = it->first;
            std::fill(it->second.begin(), it->second.end(), '\0');
            vault.entries.erase(it);
            vault.dirty = true;
            if (vault_save())
              status = "Removed " + name + " from my vault";
            InvalidateRect(hwnd, nullptr, FALSE);
          }
        } else if (h.action == "vault-generate") {
          // 64 characters is one clean index per random byte, so every
          // character is equally likely and no modulo leaning creeps in.
          static const char alphabet[] =
              "abcdefghijkmnpqrstuvwxyzABCDEFGHJKLMNPQRSTUVWXYZ23456789!@#$%^&*";
          auto rnd = random_bytes(20);
          std::string fresh;
          for (uint8_t byte : rnd)
            fresh.push_back(alphabet[byte & 63]);
          write_clipboard(fresh);
          scrub(fresh);
          status = "Fresh password on my clipboard - paste it into my add row";
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
  void key(UINT, WPARAM wp, LPARAM) {
    bool ctrl = GetKeyState(VK_CONTROL) < 0;
    bool shift = GetKeyState(VK_SHIFT) < 0;
    if (menu_open && wp == VK_ESCAPE) {
      close_menu();
      InvalidateRect(hwnd, nullptr, FALSE);
      return;
    }
    if (ctrl && shift && wp == 'P') {
      palette_open = !palette_open;
      palette_text.clear();
      palette_caret = 0;
      palette_index = 0;
      InvalidateRect(hwnd, nullptr, FALSE);
      return;
    }
    if (ctrl && shift && wp == 'Q') {
      panic();
      InvalidateRect(hwnd, nullptr, FALSE);
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
    if (ctrl && shift && wp == 'T') {
      reopen_tab();
      InvalidateRect(hwnd, nullptr, FALSE);
      return;
    }
    if (ctrl && wp == VK_TAB && tabs.size() > 1) {
      int step = shift ? -1 : 1;
      select_tab((active + step + int(tabs.size())) % int(tabs.size()));
      return;
    }
    if (ctrl && shift && (wp == VK_LEFT || wp == VK_RIGHT)) {
      move_tab(active, wp == VK_LEFT ? -1 : 1);
      InvalidateRect(hwnd, nullptr, FALSE);
      return;
    }
    if (ctrl && wp == 'M') {
      tab_set_muted(active, !current().muted);
      return;
    }
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
    if (ctrl && (wp == 'C' || wp == 'X') && !address_focus && !home_focus &&
        !panel_focus && !find_open && current().session) {
      edit_clipboard(wp == 'X');
      return;
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
    tabs.clear();
    for (auto &u : w.tabs)
      new_tab(u, false);
    if (tabs.empty())
      new_tab();
    for (size_t i = 0; i < tabs.size(); ++i) {
      if (i < w.pinned.size())
        tabs[i].pinned = w.pinned[i] == '1';
      if (i < w.muted.size())
        tabs[i].muted = w.muted[i] == '1';
    }
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
      if (!tab.url.empty()) {
        ClosedTab closed;
        closed.url = tab.url;
        closed.title = tab.title;
        closed.history = tab.history;
        closed.index = tab.index;
        closed.pinned = tab.pinned;
        closed.muted = tab.muted;
        closed_tabs.push_back(std::move(closed));
      }
      stop_session(tab);
    }
    while (closed_tabs.size() > 20)
      closed_tabs.erase(closed_tabs.begin());
    tabs.clear();
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
      t.id = next_id++;
      t.url = current().url;
      t.title = current().title;
      t.history.push_back(t.url);
      t.started = false;
      tabs.insert(tabs.begin() + active + 1, std::move(t));
      select_tab(active + 1);
    } else if (id == "palette-pin") tab_set_pinned(active, !current().pinned);
    else if (id == "palette-mute") tab_set_muted(active, !current().muted);
    else if (id == "palette-back") back();
    else if (id == "palette-forward") forward();
    else if (id == "palette-reload") navigate(current().url, false);
    else if (id == "palette-stop") stop();
    else if (id == "palette-home") navigate(profile.home_page);
    else if (id == "palette-bookmarks") { panel_bookmarks = true; panel_history = panel_downloads = panel_workspaces = false; }
    else if (id == "palette-history") { panel_history = true; panel_bookmarks = panel_downloads = panel_workspaces = false; }
    else if (id == "palette-downloads") { panel_downloads = true; panel_bookmarks = panel_history = panel_workspaces = false; }
    else if (id == "palette-workspaces") { panel_workspaces = true; panel_bookmarks = panel_history = panel_downloads = false; }
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
      t.id = next_id++;
      t.url = tabs[size_t(menu_tab)].url;
      t.title = tabs[size_t(menu_tab)].title;
      t.history.push_back(t.url);
      t.index = 0;
      t.started = false;
      tabs.insert(tabs.begin() + menu_tab + 1, std::move(t));
      active = menu_tab + 1;
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
    } else if (id == "menu-back" && tabs[size_t(active)].index > 0) {
      back();
    } else if (id == "menu-forward" &&
               tabs[size_t(active)].index + 1 < tabs[size_t(active)].history.size()) {
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
  void swap_tabs(int a, int b) {
    if (a == b || a < 0 || b < 0 || a >= int(tabs.size()) || b >= int(tabs.size()))
      return;
    std::swap(tabs[size_t(a)], tabs[size_t(b)]);
    if (active == a)
      active = b;
    else if (active == b)
      active = a;
    save_session();
  }
  void move_tab(int index, int delta) {
    if (index < 0 || index >= int(tabs.size()))
      return;
    swap_tabs(index, int(std::clamp(index + delta, 0, int(tabs.size()) - 1)));
  }
  // Ctrl+Shift+T undoes the most recent close, back stack and all.
  void reopen_tab() {
    if (closed_tabs.empty()) {
      status = "No closed tab to bring back";
      return;
    }
    ClosedTab restore = std::move(closed_tabs.back());
    closed_tabs.pop_back();
    Tab t;
    t.id = next_id++;
    t.url = restore.url;
    t.title = restore.title;
    t.history = restore.history.empty() ? std::vector<std::string>{restore.url}
                                        : restore.history;
    t.index = std::min(restore.index, t.history.size() - 1);
    t.pinned = restore.pinned;
    t.muted = restore.muted;
    t.started = false;
    tabs.push_back(std::move(t));
    active = int(tabs.size()) - 1;
    select_tab(active);
  }
  void tab_set_pinned(int index, bool pinned) {
    tabs[size_t(index)].pinned = pinned;
    save_session();
    InvalidateRect(hwnd, nullptr, FALSE);
  }
  void tab_set_muted(int index, bool muted) {
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
    const int x = GET_X_LPARAM(lp), y = GET_Y_LPARAM(lp);
    if (auto b = browser(hwnd)) {
      if (b->content.contains(x, y)) {
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
    if (auto b = browser(hwnd))
      b->right_click(GET_X_LPARAM(lp), GET_Y_LPARAM(lp));
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
      if (tab && tab->generation == up->generation) b->write_clipboard(up->clipboard);
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
    if (renderer_farm) {
      rt::Error renderer_error;
      if (!renderer_farm->monitor(renderer_error) && b)
        b->status = "Renderer process recovery failed: " + renderer_error.message;
    }
    if (b) b->reap_sessions();
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
              if (command.str("text") != "Type a web address") continue;
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
      if (b->vault_open && b->profile.vault_lock_minutes > 0 &&
          GetTickCount() - b->vault_unlocked_at >
              DWORD(b->profile.vault_lock_minutes) * 60000)
        b->vault_lock();
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
    if (!b || !task || !b->find_tab(task->tab)) return 0;
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
  b.smoke = smoke;
  b.expect_image = expect_image;
  b.capture_path = capture;
  b.smoke_bridge_url = smoke_bridge_url;
  b.start_tick = GetTickCount();
  if (private_mode) {
    b.tabs.clear();
    b.new_tab("psychovault://newtab", false);
  } else if (b.profile.restore_tabs && !b.profile.session_tabs.empty()) {
    b.tabs.clear();
    // The session file mirrors the active workspace; a workspace store with
    // entries takes precedence because it is the richer record.
    const auto &source_tabs =
        (!b.workspaces.empty() && !b.workspaces[b.workspace].tabs.empty())
            ? b.workspaces[b.workspace].tabs
            : b.profile.session_tabs;
    for (auto &u : source_tabs)
      b.new_tab(u, false);
    for (size_t i = 0; i < b.tabs.size(); ++i) {
      if (i < b.profile.session_pinned.size())
        b.tabs[i].pinned = b.profile.session_pinned[i] == '1';
      if (i < b.profile.session_muted.size())
        b.tabs[i].muted = b.profile.session_muted[i] == '1';
    }
    b.active = std::clamp(b.profile.session_active, 0, int(b.tabs.size()) - 1);
    b.sync_address();
    b.settings = starts(b.current().url, "psychovault://settings");
  }
  HWND hwnd =
      CreateWindowExW(0, wc.lpszClassName, L"PsychoVault",
                      WS_POPUP | WS_THICKFRAME | WS_MINIMIZEBOX | WS_MAXIMIZEBOX | WS_SYSMENU,
                      CW_USEDEFAULT, CW_USEDEFAULT, 1440, 940, nullptr, nullptr, inst, &b);
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
  b.vault_wipe();
  if (!quit_ok) return report_cli_error(quit_error.empty()
      ? "could not save the profile before exit" : quit_error);
  return int(msg.wParam);
}
