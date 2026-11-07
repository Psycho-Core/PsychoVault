// Exercise a persistent renderer worker through the framed process transport.
// This is the portable protocol test for P1/S044 and the app-bridge round trip
// in S045; it does not substitute for a Windows AppContainer or full app-flow run.
#include "../broker/Psycho_broker.hpp"
#include "../process/Psycho_process_transport.hpp"
#include "../renderer/Psycho_renderer_worker.hpp"
#include "../broker/Psycho_renderer_process.hpp"
#include "../../engine/Psycho_raster.hpp"
#include <condition_variable>
#include <deque>
#include <iostream>
#include <memory>
#include <mutex>
#include <stdexcept>
#include <thread>
#include <utility>

using namespace pv;
using namespace pv::rt;

namespace {

class MemoryPipe {
public:
  bool read(std::string &bytes, Error &error) {
    std::unique_lock<std::mutex> lock(mutex_);
    ready_.wait(lock, [&] { return closed_ || !messages_.empty(); });
    if (messages_.empty()) {
      error = {"process.closed", "test pipe closed"};
      return false;
    }
    bytes = std::move(messages_.front());
    messages_.pop_front();
    error = Error{};
    return true;
  }

  bool write(const std::string &bytes, Error &error) {
    std::lock_guard<std::mutex> lock(mutex_);
    if (closed_) {
      error = {"process.closed", "test pipe closed"};
      return false;
    }
    messages_.push_back(bytes);
    ready_.notify_one();
    error = Error{};
    return true;
  }

  void close() {
    std::lock_guard<std::mutex> lock(mutex_);
    closed_ = true;
    ready_.notify_all();
  }

private:
  std::mutex mutex_;
  std::condition_variable ready_;
  std::deque<std::string> messages_;
  bool closed_ = false;
};

void process_check(bool condition, const char *expression, int line, int &checks) {
  if (!condition) {
    std::cerr << "FAIL renderer process test line " << line << ": "
              << expression << "\n";
    throw std::runtime_error("renderer process check failed");
  }
  ++checks;
}
#define PROCESS_CHECK(x) process_check(bool(x), #x, __LINE__, checks)

Json input_command(const std::string &tag) {
  Json payload = Json::make_object();
  payload.set("tag", Json(tag));
  return payload;
}

} // namespace

int run_renderer_process_tests(int &shared) {
  int checks = 0;
  Schemas schemas = Schemas::stock();
  auto broker_to_child = std::make_shared<MemoryPipe>();
  auto child_to_broker = std::make_shared<MemoryPipe>();

  ProcessTransport broker_transport(
      Capability::all(), schemas,
      [child_to_broker](std::string &bytes, Error &error) {
        return child_to_broker->read(bytes, error);
      },
      [broker_to_child](const std::string &bytes, Error &error) {
        return broker_to_child->write(bytes, error);
      }, true, renderer_process_limits());
  ProcessTransport child_transport(
      capabilities_for(Role::Renderer), schemas,
      [broker_to_child](std::string &bytes, Error &error) {
        return broker_to_child->read(bytes, error);
      },
      [child_to_broker](const std::string &bytes, Error &error) {
        return child_to_broker->write(bytes, error);
      }, true, renderer_process_limits());

  RendererWorker worker(child_transport, broker_fetcher(child_transport));
  std::string child_exception;
  std::thread child([&] {
    try {
      worker.run();
    } catch (const js::JsError &thrown) {
      child_exception = thrown.value.to_string();
      std::cerr << "renderer child JS exception: " << child_exception << "\n";
    } catch (const std::exception &thrown) {
      child_exception = thrown.what();
      std::cerr << "renderer child exception: " << child_exception << "\n";
    }
  });
  struct JoinChild {
    std::shared_ptr<MemoryPipe> input;
    std::thread &thread;
    ~JoinChild() {
      input->close();
      if (thread.joinable()) thread.join();
    }
  } join{broker_to_child, child};

  RendererProcessClient client(broker_transport);
  int fetches = 0;
  Json refreshed_cookies = Json::make_array();
  Json cookie = Json::make_object();
  cookie.set("name", Json("broker"));
  cookie.set("value", Json("updated"));
  cookie.set("domain", Json("slice.test"));
  cookie.set("path", Json("/"));
  cookie.set("expires", Json(-1));
  cookie.set("hostOnly", Json(true));
  refreshed_cookies.array.push_back(cookie);
  Json hidden_cookie = Json::make_object();
  hidden_cookie.set("name", Json("auth"));
  hidden_cookie.set("value", Json("secret"));
  hidden_cookie.set("domain", Json("slice.test"));
  hidden_cookie.set("path", Json("/"));
  hidden_cookie.set("expires", Json(-1));
  hidden_cookie.set("hostOnly", Json(true));
  hidden_cookie.set("httpOnly", Json(true));
  refreshed_cookies.array.push_back(hidden_cookie);
  Bytes image_pixels(400u * 300u * 4u);
  uint32_t pixel_seed = 0x12345678u;
  for (uint8_t &channel : image_pixels) {
    pixel_seed = pixel_seed * 1664525u + 1013904223u;
    channel = uint8_t(pixel_seed >> 24);
  }
  const Bytes image_png = encode_png(400, 300, image_pixels);
  const std::string large_title(1200u * 1024u, 'x');
  Host::Fetcher fetcher = [&](const std::string &url, ResourceType) {
    ++fetches;
    Resource resource;
    resource.url = url;
    resource.mime = "text/html; charset=utf-8";
    resource.broker_cookie_state = refreshed_cookies;
    if (url == "https://slice.test/") {
      resource.text = "<!doctype html><html><head><title>Persistent page</title></head>"
                      "<body style=\"margin:0;height:2400px\">"
                      "<script>localStorage.setItem('fromPage','renderer');"
                      "localStorage.setItem('cookie',document.cookie)</script>"
                      "<a href=\"/next\" style=\"display:block;width:300px;height:60px\">"
                      "next page</a><p>Renderer-owned document state</p></body></html>";
    } else if (url == "https://slice.test/next") {
      resource.text = "<!doctype html><html><head><title>Second page</title></head>"
                      "<body><p>second document</p></body></html>";
    } else if (url == "https://slice.test/form") {
      resource.text = "<!doctype html><html><head><title>Form page</title></head>"
                      "<body><form method=\"post\" action=\"/submit\">"
                      "<input name=\"q\" value=\"from renderer\">"
                      "<button>Send</button></form></body></html>";
    } else if (url == "https://slice.test/image") {
      resource.text = "<!doctype html><html><head><title>Image page</title></head>"
                      "<body><img src=\"/pixel.png\" width=\"64\" height=\"64\"></body></html>";
    } else if (url == "https://slice.test/large") {
      resource.text = "<!doctype html><html><head><title>" + large_title +
                      "</title></head><body><p>Large page passed</p></body></html>";
    } else if (url == "https://slice.test/pixel.png") {
      resource.mime = "image/png";
      resource.bytes = image_png;
    } else {
      resource.ok = false;
      resource.status = 404;
      resource.error = "test resource missing";
    }
    return resource;
  };
  int posts = 0;
  Host::Poster poster = [&](const std::string &url, const std::string &body,
                            const std::string &content_type) {
    ++posts;
    Resource resource;
    resource.url = "https://slice.test/posted";
    resource.status = 200;
    resource.ok = true;
    resource.mime = "text/html; charset=utf-8";
    resource.text = "<!doctype html><html><head><title>Posted through broker</title></head>"
                    "<body><p>post complete</p></body></html>";
    resource.broker_cookie_state = refreshed_cookies;
    if (url != "https://slice.test/submit" ||
        body.find("q=from+renderer") == std::string::npos ||
        content_type.find("application/x-www-form-urlencoded") == std::string::npos) {
      resource.ok = false;
      resource.status = 400;
      resource.error = "form POST did not preserve its URL-encoded request";
    }
    return resource;
  };

  Json initial_storage = Json::make_object();
  Json site_storage = Json::make_object();
  site_storage.set("fromBroker", Json("present"));
  initial_storage.set("https://slice.test", std::move(site_storage));
  Json cookie_state = Json::make_array();
  cookie_state.array.push_back(hidden_cookie);
  Json storage_state = initial_storage;
  Envelope reply;
  Error error;
  PROCESS_CHECK(client.navigate("https://slice.test/", fetcher, reply, error,
                                cookie_state, storage_state));
  PROCESS_CHECK(!error && reply.payload.flag("ok") &&
                reply.payload.find("frame") && fetches == 1);
  PROCESS_CHECK(reply.payload.find("frame")->str("title") == "Persistent page");
  PROCESS_CHECK(reply.payload.find("cookie_state") &&
                reply.payload.find("cookie_state")->array.size() == 1);
  PROCESS_CHECK(reply.payload.find("storage_state") &&
                reply.payload.find("storage_state")->find("https://slice.test") &&
                reply.payload.find("storage_state")->find("https://slice.test")
                        ->str("fromBroker") == "present" &&
                reply.payload.find("storage_state")->find("https://slice.test")
                        ->str("fromPage") == "renderer" &&
                reply.payload.find("storage_state")->find("https://slice.test")
                        ->str("cookie") == "broker=updated");
  cookie_state = *reply.payload.find("cookie_state");
  storage_state = *reply.payload.find("storage_state");
  auto send_input = [&](const Json &command) {
    const bool sent = client.input(command, fetcher, reply, error,
                                   cookie_state, storage_state, poster);
    if (sent && reply.payload.find("cookie_state"))
      cookie_state = *reply.payload.find("cookie_state");
    if (sent && reply.payload.find("storage_state"))
      storage_state = *reply.payload.find("storage_state");
    return sent;
  };

  Json click = input_command("click");
  click.set("x", Json(20));
  click.set("y", Json(20));
  PROCESS_CHECK(send_input(click));
  PROCESS_CHECK(!error && reply.payload.flag("ok") &&
                reply.payload.str("navigation") == "https://slice.test/next" &&
                fetches == 1);

  Json resize = input_command("resize");
  resize.set("width", Json(640));
  resize.set("height", Json(480));
  PROCESS_CHECK(send_input(resize));
  PROCESS_CHECK(!error && reply.payload.flag("ok") &&
                reply.payload.find("frame") &&
                reply.payload.find("frame")->str("title") == "Persistent page");

  Json pump = input_command("pump");
  PROCESS_CHECK(send_input(pump));
  PROCESS_CHECK(!error && reply.payload.flag("ok") && reply.payload.find("frame"));

  Json scroll = input_command("scroll");
  scroll.set("y", Json(120));
  PROCESS_CHECK(send_input(scroll));
  PROCESS_CHECK(!error && reply.payload.flag("ok") && reply.payload.find("frame"));

  Json key = input_command("key");
  key.set("key", Json("Escape"));
  key.set("text", Json(""));
  PROCESS_CHECK(send_input(key));
  PROCESS_CHECK(!error && reply.payload.flag("ok") && reply.payload.find("frame"));

  // Shell text entry sends Unicode text without a physical key name; it must
  // cross process IPC rather than being rejected as an empty key.
  Json typed_text = input_command("key");
  typed_text.set("key", Json(""));
  typed_text.set("text", Json("address entry"));
  PROCESS_CHECK(send_input(typed_text));
  PROCESS_CHECK(!error && reply.payload.flag("ok") && reply.payload.find("frame"));

  Json invalid = input_command("not-an-input");
  PROCESS_CHECK(send_input(invalid));
  PROCESS_CHECK(!error && !reply.payload.flag("ok") &&
                !reply.payload.str("detail").empty());

  PROCESS_CHECK(client.navigate("https://slice.test/next", fetcher, reply, error,
                                cookie_state, storage_state));
  PROCESS_CHECK(!error && reply.payload.flag("ok") &&
                reply.payload.find("frame") &&
                reply.payload.find("frame")->str("title") == "Second page" &&
                fetches == 2);
  cookie_state = *reply.payload.find("cookie_state");
  storage_state = *reply.payload.find("storage_state");
  PROCESS_CHECK(client.navigate("https://slice.test/form", fetcher, reply, error,
                                cookie_state, storage_state, poster));
  PROCESS_CHECK(!error && reply.payload.flag("ok") &&
                reply.payload.find("frame") &&
                reply.payload.find("frame")->str("title") == "Form page" &&
                fetches == 3);
  cookie_state = *reply.payload.find("cookie_state");
  storage_state = *reply.payload.find("storage_state");
  Json focus_form = input_command("click");
  focus_form.set("x", Json(10));
  focus_form.set("y", Json(10));
  PROCESS_CHECK(send_input(focus_form));
  Json submit_form = input_command("key");
  submit_form.set("key", Json("Enter"));
  PROCESS_CHECK(send_input(submit_form));
  PROCESS_CHECK(!error && reply.payload.flag("ok") && posts == 1 &&
                reply.payload.find("frame") &&
                reply.payload.find("frame")->str("title") == "Posted through broker");
  PROCESS_CHECK(client.navigate("https://slice.test/image", fetcher, reply, error,
                                cookie_state, storage_state, poster));
  PROCESS_CHECK(!error && reply.payload.flag("ok") &&
                reply.payload.find("frame") &&
                reply.payload.find("frame")->str("title") == "Image page" &&
                fetches == 5);
  const Json *images = reply.payload.find("images");
  PROCESS_CHECK(images && images->type == Json::Type::Array &&
                images->array.size() == 1 &&
                images->array.front().str("url") == "https://slice.test/pixel.png");
  if (images && images->type == Json::Type::Array && !images->array.empty()) {
    Bytes pixels = base64_decode(images->array.front().str("pixels_b64"),
                                 kMaxImageRawBytes);
    PROCESS_CHECK(images->array.front().num("width") == 400 &&
                  images->array.front().num("height") == 300 &&
                  pixels == image_pixels);
  }
  // S045: the registered new-tab page receives only its curated bridge and
  // can request a broker-owned tab action over renderer process IPC.
  Host::Fetcher app_fetcher = [](const std::string &url, ResourceType) {
    Resource resource;
    resource.url = url;
    resource.ok = url == "psychovault://newtab" ||
                  url == "psychovault://newtab/";
    resource.mime = "text/html; charset=utf-8";
    resource.text = "<!doctype html><html><head><title>App bridge slice</title></head>"
                    "<body><div>App page</div>"
                    "<script>psycho.tab.new({url:'https://local.test/'})</script>"
                    "</body></html>";
    if (!resource.ok) resource.error = "internal test resource not registered";
    return resource;
  };
  bool app_bridge_called = false;
  RendererProcessClient::BridgeInvoker app_bridge =
      [&](Channel channel, const std::string &method, Json payload, Json &result,
          Error &) {
        app_bridge_called = channel == Channel::Tab && method == "tab:new" &&
                           payload.str("url") == "https://local.test/";
        result = Json::make_object();
        result.set("queued", Json(true));
        return app_bridge_called;
      };
  const bool app_navigation_ok = client.navigate(
      "psychovault://newtab", app_fetcher, reply, error, Json::make_array(),
      Json::make_object(), {}, app_bridge);
  if (!app_navigation_ok || error || !reply.payload.flag("ok") ||
      !reply.payload.find("frame") ||
      reply.payload.find("frame")->str("title") != "App bridge slice")
    std::cerr << "S045 bridge navigation: sent=" << app_navigation_ok
              << " error=" << error.code << ": " << error.message
              << " response=" << reply.payload.dump() << "\n";
  PROCESS_CHECK(app_navigation_ok);
  PROCESS_CHECK(!error && reply.payload.flag("ok") && reply.payload.find("frame") &&
                reply.payload.find("frame")->str("title") == "App bridge slice");
  PROCESS_CHECK(app_bridge_called);
  const std::string app_frame_url = reply.payload.find("frame")->str("url");
  PROCESS_CHECK(app_frame_url == "psychovault://newtab" ||
                app_frame_url == "psychovault://newtab/");

  // A real document can exceed the historical 1 MiB process-frame cap. The
  // renderer transport must carry the broker response and the resulting frame,
  // while still enforcing its explicit bounded 32 MiB allowance.
  PROCESS_CHECK(client.navigate("https://slice.test/large", fetcher, reply, error,
                                cookie_state, storage_state));
  PROCESS_CHECK(!error && reply.payload.flag("ok") && reply.payload.find("frame") &&
                reply.payload.find("frame")->str("title").size() == large_title.size() &&
                fetches == 6);

  PROCESS_CHECK(client.shutdown(error) && !error);

  shared += checks;
  return 0;
}
