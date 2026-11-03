#include "../engine/Psycho_page.hpp"
#include "../platform/Psycho_process.hpp"
#include <iostream>
#ifdef _WIN32
#define NOMINMAX
#include <winsock2.h>
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
void emit(pv::Page &page, double width, double height, double scroll,
          std::set<std::string> &sent_images) {
  auto frame = page.render(width, height, scroll);
  for (const auto &[url, image] : page.images()) {
    if (sent_images.count(url))
      continue;
    pv::Json j = pv::Json::make_object();
    j.set("tag", "image");
    j.set("url", url);
    j.set("width", image.width);
    j.set("height", image.height);
    j.set("base64", pv::base64_encode(image.pixels));
    if (!send(j))
      return;
    sent_images.insert(url);
  }
  if (!send(frame))
    return;
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
    bool file_denied = file == INVALID_HANDLE_VALUE &&
                       (file_error == ERROR_ACCESS_DENIED || file_error == ERROR_SHARING_VIOLATION);
    WSADATA wsa{};
    int startup_error = WSAStartup(MAKEWORD(2, 2), &wsa);
    int tcp_error = 0, udp_error = 0;
    if (!startup_error) {
      SOCKET tcp = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
      if (tcp == INVALID_SOCKET)
        tcp_error = WSAGetLastError();
      else
        closesocket(tcp);
      SOCKET udp = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
      if (udp == INVALID_SOCKET)
        udp_error = WSAGetLastError();
      else
        closesocket(udp);
      WSACleanup();
    }
    bool network_denied = !startup_error && tcp_error == WSAEACCES && udp_error == WSAEACCES;
    Json report = Json::make_object();
    report.set("tag", "sandbox");
    report.set("appContainer", is_appcontainer != 0);
    report.set("fileDenied", file_denied);
    report.set("networkDenied", network_denied);
    report.set("networkTested", true);
    report.set("tokenError", double(token_error));
    report.set("fileError", double(file_error));
    report.set("startupError", double(startup_error));
    report.set("tcpError", double(tcp_error));
    report.set("udpError", double(udp_error));
    bool ok = is_appcontainer && file_denied && network_denied;
    return send(report) && ok ? 0 : 1;
  }
#else
  (void)argc;
  (void)argv;
#endif
  try {
    Page page;
    double width = 1000, height = 700, scroll = 0;
    bool loaded = false;
    std::set<std::string> sent_images;
    while (true) {
      double delay = page.next_timer_delay();
      int timeout = delay < 0 ? 100 : int(std::clamp(delay, 1.0, 100.0));
      auto message = read_frame(in_handle(), loaded ? timeout : -1);
      if (!message) {
        if (loaded) {
          page.tick();
          emit(page, width, height, scroll, sent_images);
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
        sent_images.clear();
        emit(page, width, height, scroll, sent_images);
      } else if (tag == "resource" && loaded) {
        Resource r;
        r.url = message->str("url");
        r.ok = message->flag("ok");
        r.status = int(message->num("status", 200));
        r.mime = message->str("mime");
        const auto *encoded = message->find("base64");
        if (encoded && encoded->type == Json::Type::String) {
          try {
            r.bytes = base64_decode(encoded->string);
          } catch (const std::exception &e) {
            r.ok = false;
            r.error = e.what();
          }
        } else
          r.text = message->str("text");
        r.error = message->str("error");
        page.resource(r);
        emit(page, width, height, scroll, sent_images);
      } else if (tag == "resize" && loaded) {
        width = message->num("width", width);
        height = message->num("height", height);
        emit(page, width, height, scroll, sent_images);
      } else if (tag == "scroll" && loaded) {
        scroll = message->num("y", scroll);
        emit(page, width, height, scroll, sent_images);
      } else if (tag == "click" && loaded) {
        page.click(message->num("x"), message->num("y"));
        emit(page, width, height, scroll, sent_images);
      } else if (tag == "key" && loaded) {
        page.key(message->str("key"), message->str("text"));
        emit(page, width, height, scroll, sent_images);
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
