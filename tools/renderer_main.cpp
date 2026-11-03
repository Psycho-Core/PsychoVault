#include "../engine/page.hpp"
#include "../platform/process.hpp"
#include <iostream>
#ifdef _WIN32
#define NOMINMAX
#include <windows.h>
#else
#include <unistd.h>
#endif
namespace {
void *in_handle() {
#ifdef _WIN32
  return GetStdHandle(STD_INPUT_HANDLE);
#else
  return reinterpret_cast<void *>(intptr_t(0));
#endif
}
void *out_handle() {
#ifdef _WIN32
  return GetStdHandle(STD_OUTPUT_HANDLE);
#else
  return reinterpret_cast<void *>(intptr_t(1));
#endif
}
bool send(const pv::Json &j) {
  std::string error;
  return pv::write_frame(out_handle(), j, error);
}
void emit(pv::Page &page, double width, double height, double scroll) {
  auto frame = page.render(width, height, scroll);
  send(frame);
  for (auto &url : page.requests) {
    pv::Json j = pv::Json::make_object();
    j.set("tag", "fetch");
    j.set("url", url);
    send(j);
  }
  page.requests.clear();
  for (auto &url : page.navigations) {
    pv::Json j = pv::Json::make_object();
    j.set("tag", "navigate");
    j.set("url", url);
    send(j);
  }
  page.navigations.clear();
  for (auto &text : page.dialogs) {
    pv::Json j = pv::Json::make_object();
    j.set("tag", "dialog");
    j.set("text", text);
    send(j);
  }
  page.dialogs.clear();
  for (auto &d : page.diagnostics) {
    pv::Json j = pv::Json::make_object();
    j.set("tag", "log");
    j.set("subsystem", d.subsystem);
    j.set("message", d.message);
    send(j);
  }
  page.diagnostics.clear();
}
} // namespace
int main(int argc, char **argv) {
  using namespace pv;
#ifdef _WIN32
  if (argc > 2 && std::string(argv[1]) == "--sandbox-probe") {
    HANDLE token = nullptr;
    DWORD is_appcontainer = 0, token_error = 0;
    if (OpenProcessToken(GetCurrentProcess(), TOKEN_QUERY, &token)) {
      DWORD returned = 0;
      if (!GetTokenInformation(token, TokenIsAppContainer, &is_appcontainer,
                               sizeof(is_appcontainer), &returned))
        token_error = GetLastError();
      CloseHandle(token);
    } else
      token_error = GetLastError();
    HANDLE file = CreateFileA(argv[2], GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING,
                              FILE_ATTRIBUTE_NORMAL, nullptr);
    DWORD file_error = file == INVALID_HANDLE_VALUE ? GetLastError() : 0;
    if (file != INVALID_HANDLE_VALUE)
      CloseHandle(file);
    bool denied = file == INVALID_HANDLE_VALUE &&
                  (file_error == ERROR_ACCESS_DENIED || file_error == ERROR_SHARING_VIOLATION);
    std::string out = "{\"appContainer\":" + std::to_string(is_appcontainer) +
                      ",\"tokenError\":" + std::to_string(token_error) +
                      ",\"fileDenied\":" + (denied ? "1" : "0") +
                      ",\"fileError\":" + std::to_string(file_error) +
                      ",\"networkTested\":false}\n";
    DWORD written = 0;
    WriteFile(GetStdHandle(STD_OUTPUT_HANDLE), out.data(), DWORD(out.size()), &written, nullptr);
    return 0;
  }
#else
  (void)argc;
  (void)argv;
#endif
  try {
    Page page;
    double width = 1000, height = 700, scroll = 0;
    bool loaded = false;
    while (true) {
      double delay = page.next_timer_delay();
      int timeout = delay < 0 ? 100 : int(std::clamp(delay, 1.0, 100.0));
      auto message = read_frame(in_handle(), loaded ? timeout : -1);
      if (!message) {
        if (loaded) {
          page.tick();
          emit(page, width, height, scroll);
        }
        continue;
      }
      auto tag = message->str("tag");
      if (tag == "shutdown")
        break;
      if (tag == "load") {
        auto url = parse_url(message->str("url"));
        if (!url)
          continue;
        width = message->num("width", 1000);
        height = message->num("height", 700);
        scroll = 0;
        page.load(*url, message->str("html"), message->str("mime", "text/html"));
        loaded = true;
        emit(page, width, height, scroll);
      } else if (tag == "resource" && loaded) {
        Resource r;
        r.url = message->str("url");
        r.ok = message->flag("ok");
        r.status = int(message->num("status", 200));
        r.mime = message->str("mime");
        r.text = message->str("text");
        r.error = message->str("error");
        page.resource(r);
        emit(page, width, height, scroll);
      } else if (tag == "resize" && loaded) {
        width = message->num("width", width);
        height = message->num("height", height);
        emit(page, width, height, scroll);
      } else if (tag == "scroll" && loaded) {
        scroll = message->num("y", scroll);
        emit(page, width, height, scroll);
      } else if (tag == "click" && loaded) {
        page.click(message->num("x"), message->num("y"));
        emit(page, width, height, scroll);
      } else if (tag == "key" && loaded) {
        page.key(message->str("key"), message->str("text"));
        emit(page, width, height, scroll);
      }
    }
    return 0;
  } catch (std::exception &e) {
    Json j = Json::make_object();
    j.set("tag", "fatal");
    j.set("message", e.what());
    send(j);
    return 1;
  }
}
