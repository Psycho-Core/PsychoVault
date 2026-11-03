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
int main() {
  using namespace pv;
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
