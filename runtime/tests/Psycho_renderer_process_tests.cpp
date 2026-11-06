// Exercise a persistent renderer worker through the framed process transport.
// This is the portable protocol test for the navigation/input part of P1/S044;
// it does not substitute for a Windows AppContainer or full app-flow run.
#include "../broker/Psycho_broker.hpp"
#include "../process/Psycho_process_transport.hpp"
#include "../renderer/Psycho_renderer_worker.hpp"
#include "../broker/Psycho_renderer_process.hpp"
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
      }, true);
  ProcessTransport child_transport(
      capabilities_for(Role::Renderer), schemas,
      [broker_to_child](std::string &bytes, Error &error) {
        return broker_to_child->read(bytes, error);
      },
      [child_to_broker](const std::string &bytes, Error &error) {
        return child_to_broker->write(bytes, error);
      }, true);

  RendererWorker worker(child_transport, broker_fetcher(child_transport));
  std::thread child([&] { worker.run(); });
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
  Host::Fetcher fetcher = [&](const std::string &url, ResourceType) {
    ++fetches;
    Resource resource;
    resource.url = url;
    resource.mime = "text/html; charset=utf-8";
    if (url == "https://slice.test/") {
      resource.text = "<!doctype html><html><head><title>Persistent page</title></head>"
                      "<body style=\"margin:0;height:2400px\">"
                      "<a href=\"/next\" style=\"display:block;width:300px;height:60px\">"
                      "next page</a><p>Renderer-owned document state</p></body></html>";
    } else if (url == "https://slice.test/next") {
      resource.text = "<!doctype html><html><head><title>Second page</title></head>"
                      "<body><p>second document</p></body></html>";
    } else {
      resource.ok = false;
      resource.status = 404;
      resource.error = "test resource missing";
    }
    return resource;
  };

  Envelope reply;
  Error error;
  PROCESS_CHECK(client.navigate("https://slice.test/", fetcher, reply, error));
  PROCESS_CHECK(!error && reply.payload.flag("ok") &&
                reply.payload.find("frame") && fetches == 1);
  PROCESS_CHECK(reply.payload.find("frame")->str("title") == "Persistent page");

  Json click = input_command("click");
  click.set("x", Json(20));
  click.set("y", Json(20));
  PROCESS_CHECK(client.input(click, fetcher, reply, error));
  PROCESS_CHECK(!error && reply.payload.flag("ok") &&
                reply.payload.str("navigation") == "https://slice.test/next" &&
                fetches == 1);

  Json resize = input_command("resize");
  resize.set("width", Json(640));
  resize.set("height", Json(480));
  PROCESS_CHECK(client.input(resize, fetcher, reply, error));
  PROCESS_CHECK(!error && reply.payload.flag("ok") &&
                reply.payload.find("frame") &&
                reply.payload.find("frame")->str("title") == "Persistent page");

  Json scroll = input_command("scroll");
  scroll.set("y", Json(120));
  PROCESS_CHECK(client.input(scroll, fetcher, reply, error));
  PROCESS_CHECK(!error && reply.payload.flag("ok") && reply.payload.find("frame"));

  Json key = input_command("key");
  key.set("key", Json("Escape"));
  key.set("text", Json(""));
  PROCESS_CHECK(client.input(key, fetcher, reply, error));
  PROCESS_CHECK(!error && reply.payload.flag("ok") && reply.payload.find("frame"));

  Json invalid = input_command("not-an-input");
  PROCESS_CHECK(client.input(invalid, fetcher, reply, error));
  PROCESS_CHECK(!error && !reply.payload.flag("ok") &&
                !reply.payload.str("detail").empty());

  PROCESS_CHECK(client.navigate("https://slice.test/next", fetcher, reply, error));
  PROCESS_CHECK(!error && reply.payload.flag("ok") &&
                reply.payload.find("frame") &&
                reply.payload.find("frame")->str("title") == "Second page" &&
                fetches == 2);
  PROCESS_CHECK(client.shutdown(error) && !error);

  shared += checks;
  return 0;
}
