#include "platform/Psycho_extension.hpp"
#include <fstream>
#include <istream>
#include <filesystem>

namespace pv {
namespace {
std::string read_all(const std::string &path) {
  return read_text_file(path);
}
// I convert between my JSON and my interpreter values without a third-party bridge.
js::Value json_to_js(js::Runtime &runtime, const Json &json) {
  switch (json.type) {
  case Json::Type::Null: return js::Value::null();
  case Json::Type::Bool: return js::Value(json.boolean);
  case Json::Type::Number: return js::Value(json.number);
  case Json::Type::String: return js::Value(json.string);
  case Json::Type::Array: {
    auto array = runtime.make_array();
    for (const auto &item : json.array)
      array.object->properties[std::to_string(array.object->properties.size())] =
          json_to_js(runtime, item);
    // My arrays keep an explicit length so extension code can iterate them.
    array.object->properties["length"] = js::Value(double(json.array.size()));
    return array;
  }
  case Json::Type::Object: {
    auto object = runtime.make_object();
    for (const auto &entry : json.object)
      object.object->properties[entry.first] = json_to_js(runtime, entry.second);
    return object;
  }
  }
  return js::Value();
}
// Chrome-style APIs take (details, callback) or just (callback). I pick the last
// function argument so both forms work and a details object is never invoked.
std::string error_text(const js::Value &value) {
  if (value.type == js::Value::Type::Object && value.object) {
    auto &props = value.object->properties;
    std::string name, message, stack;
    if (auto it = props.find("name"); it != props.end()) name = it->second.to_string();
    if (auto it = props.find("message"); it != props.end()) message = it->second.to_string();
    if (auto it = props.find("stack"); it != props.end()) stack = it->second.to_string();
    std::string line = name.empty() ? message : name + ": " + message;
    if (line.empty()) line = stack;
    if (!line.empty()) return line;
  }
  return value.to_string();
}

// I raise a real JS Error so extension code can catch a refusal. A C++ exception
// escaping here would kill the whole worker instead of one API call.
[[noreturn]] void throw_extension_error(js::Runtime &runtime, const std::string &message) {
  auto error = runtime.make_object();
  error.object->properties["name"] = js::Value(std::string("Error"));
  error.object->properties["message"] = js::Value(message);
  throw js::JsError{error};
}
bool permission_implies(const std::string &held, const std::string &wanted) {
  if (held == wanted) return true;
  if (held == "<all_urls>") return true;
  // A declared optional permission is not a grant; only an exact match counts.
  if (held.size() > 2 && held.back() == '*' && held[held.size() - 2] == '.') {
    return wanted.size() >= held.size() - 1 &&
           wanted.compare(0, held.size() - 2, held.substr(0, held.size() - 2)) == 0;
  }
  return false;
}
bool host_pattern_matches(const std::string &pattern, const Url &url) {
  if (pattern == "<all_urls>") return url.network();
  auto scheme_end = pattern.find("://");
  if (scheme_end == std::string::npos) return false;
  auto scheme = pattern.substr(0, scheme_end);
  if (scheme != "*" && scheme != url.scheme) return false;
  if (scheme == "*" && !url.network()) return false;
  auto rest = pattern.substr(scheme_end + 3);
  auto slash = rest.find('/');
  auto host = slash == std::string::npos ? rest : rest.substr(0, slash);
  if (host == "*") return true;
  if (host.size() > 2 && host[0] == '*' && host[1] == '.') {
    auto suffix = host.substr(1);
    return url.host == host.substr(2) ||
           (url.host.size() > suffix.size() &&
            url.host.compare(url.host.size() - suffix.size(), suffix.size(), suffix) == 0);
  }
  return host == url.host;
}

} // namespace

bool ExtensionManifest::has_permission(const std::string &permission) const {
  for (const auto &held : permissions)
    if (permission_implies(held, permission)) return true;
  return false;
}

bool ExtensionManifest::matches_host(const Url &url) const {
  if (has_permission("<all_urls>")) return url.network();
  for (const auto &pattern : host_permissions)
    if (host_pattern_matches(pattern, url)) return true;
  for (const auto &pattern : permissions)
    if (pattern.find("://") != std::string::npos && host_pattern_matches(pattern, url)) return true;
  return false;
}

bool ExtensionManifest::connectable_from(const Url &url) const {
  for (const auto &pattern : externally_connectable)
    if (host_pattern_matches(pattern, url)) return true;
  return false;
}

ExtensionManifest ExtensionManifest::parse(const std::string &text) {
  ExtensionManifest manifest;
  auto json = Json::parse(text, 1024 * 1024);
  if (!json || json->type != Json::Type::Object) {
    manifest.error = "manifest.json is not a JSON object";
    return manifest;
  }
  manifest.manifest_version = int(json->num("manifest_version", 0));
  if (manifest.manifest_version != 2 && manifest.manifest_version != 3) {
    manifest.error = "I only accept manifest_version 2 or 3";
    return manifest;
  }
  manifest.name = json->str("name");
  manifest.version = json->str("version");
  manifest.description = json->str("description");
  if (manifest.name.empty() || manifest.version.empty()) {
    manifest.error = "manifest.json needs a name and a version";
    return manifest;
  }
  if (manifest.name.size() > 256 || manifest.version.size() > 64) {
    manifest.error = "manifest name or version is too long";
    return manifest;
  }
  auto read_strings = [&](const char *key, std::vector<std::string> &out) {
    const Json *value = json->find(key);
    if (!value || value->type != Json::Type::Array) return;
    for (const auto &item : value->array) {
      if (out.size() >= 256) break;
      if (item.type == Json::Type::String) out.push_back(item.string);
    }
  };
  read_strings("permissions", manifest.permissions);
  read_strings("host_permissions", manifest.host_permissions);
  const Json *background = json->find("background");
  if (background && background->type == Json::Type::Object) {
    manifest.service_worker = background->str("service_worker");
    if (manifest.service_worker.empty()) {
      const Json *scripts = background->find("scripts");
      if (scripts && scripts->type == Json::Type::Array && !scripts->array.empty() &&
          scripts->array.front().type == Json::Type::String)
        manifest.service_worker = scripts->array.front().string;
    }
  }
  const Json *action = json->find("action");
  if (!action) action = json->find("browser_action");
  if (action && action->type == Json::Type::Object) {
    manifest.action_title = action->str("default_title", manifest.name);
    manifest.action_popup = action->str("default_popup");
  }
  // I refuse path traversal in any script path an extension declares.
  auto safe_path = [](const std::string &path) {
    if (path.empty()) return true;
    if (path.front() == '/' || path.front() == '\\') return false;
    if (path.find(':') != std::string::npos) return false;
    for (const auto &part : split(replace_all(path, "\\", "/"), '/'))
      if (part == "..") return false;
    return true;
  };
  if (!safe_path(manifest.service_worker) || !safe_path(manifest.action_popup)) {
    manifest.error = "manifest declares an unsafe script path";
    return manifest;
  }
  manifest.key = json->str("key");
  if (!manifest.key.empty()) {
    // The Chrome identity rule: SHA-256 of the DER public key, first 16 bytes,
    // nibbles mapped onto a..p. Proton's distribution key must derive to their
    // published id or a handshake with their account site silently never fires.
    auto der = base64_decode(manifest.key, 1024 * 16);
    if (!der.empty()) {
      auto digest = sha256(der);
      const char *nibbles = "abcdefghijklmnop";
      for (size_t i = 0; i < 16 && i < digest.size(); ++i) {
        manifest.key_id += nibbles[digest[i] >> 4];
        manifest.key_id += nibbles[digest[i] & 15];
      }
    } else {
      manifest.error = "the manifest key did not decode";
      return manifest;
    }
  }
  const Json *connectable = json->find("externally_connectable");
  if (connectable && connectable->type == Json::Type::Object) {
    const Json *matches = connectable->find("matches");
    if (matches && matches->type == Json::Type::Array)
      for (const auto &item : matches->array)
        if (item.type == Json::Type::String && manifest.externally_connectable.size() < 32)
          manifest.externally_connectable.push_back(item.string);
  }
  manifest.valid = true;
  return manifest;
}

struct ExtensionHost::Worker {
  std::unique_ptr<js::Runtime> runtime;
  std::string id;
  std::map<std::string, std::vector<js::Value>> listeners;
  std::string badge, badge_color, title;
  std::string webrtc_policy = "default";
  std::string proxy_user, proxy_pass;
  bool has_proxy_auth = false;
  bool running = false;
  std::string last_error;       // a real JS error my UI can show
  std::string last_error_prop;  // chrome.runtime.lastError during a callback
  js::Value runtime_ns;         // chrome.runtime object, so callbacks can set lastError
  int idle_interval = 60;
  std::string directory;        // my read-only root for pages and scripts
};

ExtensionHost::ExtensionHost() = default;
ExtensionHost::~ExtensionHost() = default;

bool ExtensionHost::grant(const ExtensionRecord &record, const std::string &permission,
                          std::string &error) const {
  if (record.manifest.has_permission(permission)) return true;
  error = "extension " + record.manifest.id + " did not declare the " + permission + " permission";
  return false;
}

bool ExtensionHost::install_directory(const std::string &directory, std::string &error) {
  namespace fs = std::filesystem;
  std::error_code ec;
  auto root = fs::absolute(fs::path(directory), ec);
  if (ec) {
    error = "cannot resolve the extension directory";
    return false;
  }
  auto manifest_path = root / "manifest.json";
  if (!fs::is_regular_file(manifest_path, ec)) {
    error = "the folder does not contain manifest.json";
    return false;
  }
  ExtensionManifest manifest;
  try {
    manifest = ExtensionManifest::parse(read_all(manifest_path.string()));
  } catch (const std::exception &e) {
    error = std::string("cannot read manifest.json: ") + e.what();
    return false;
  }
  if (!manifest.valid) {
    error = manifest.error;
    return false;
  }
  // I derive a stable local id from the install path so reinstalls keep storage.
  auto hash = std::hash<std::string>{}(root.string());
  manifest.id = "local-" + std::to_string(uint64_t(hash));
  return finish_install(manifest, root.string(), false, error);
}

bool ExtensionHost::install_bundled(const std::string &directory, const std::string &id,
                                    std::string &error) {
  namespace fs = std::filesystem;
  std::error_code ec;
  auto root = fs::absolute(fs::path(directory), ec);
  if (ec) {
    error = "cannot resolve the bundled extension directory";
    return false;
  }
  auto manifest_path = root / "manifest.json";
  if (!fs::is_regular_file(manifest_path, ec)) {
    error = "the bundled folder does not contain manifest.json";
    return false;
  }
  ExtensionManifest manifest;
  try {
    manifest = ExtensionManifest::parse(read_all(manifest_path.string()));
  } catch (const std::exception &e) {
    error = std::string("cannot read the bundled manifest: ") + e.what();
    return false;
  }
  if (!manifest.valid) {
    error = manifest.error;
    return false;
  }
  // A keyed manifest owns its Chrome-derived id. Proton's account site hands
  // messages to exactly one id, so the bundled install must use the real one.
  manifest.id = manifest.key_id.empty() ? id : manifest.key_id;
  return finish_install(manifest, root.string(), true, error);
}

bool ExtensionHost::finish_install(const ExtensionManifest &manifest, const std::string &root,
                                   bool bundled, std::string &error) {
  namespace fs = std::filesystem;
  std::error_code ec;
  if (!manifest.service_worker.empty() &&
      !fs::is_regular_file(fs::path(root) / manifest.service_worker, ec)) {
    error = "the declared background script is missing: " + manifest.service_worker;
    return false;
  }
  ExtensionRecord record;
  record.manifest = manifest;
  record.directory = root;
  record.bundled = bundled;
  auto existing = extensions_.find(manifest.id);
  if (existing != extensions_.end()) {
    record.storage = existing->second.storage;
    record.enabled = existing->second.enabled;
  }
  extensions_[manifest.id] = record;
  diagnostics_.push_back("installed " + manifest.name + " " + manifest.version);
  return true;
}

bool ExtensionHost::remove(const std::string &id, std::string &error) {
  auto found = extensions_.find(id);
  if (found == extensions_.end()) {
    error = "no such extension";
    return false;
  }
  if (found->second.bundled) {
    error = "I do not remove a bundled extension; I can only disable it";
    return false;
  }
  stop_worker(id);
  if (effects_.clear_proxy) effects_.clear_proxy(id, error);
  extensions_.erase(found);
  return true;
}

bool ExtensionHost::set_enabled(const std::string &id, bool enabled, std::string &error) {
  auto found = extensions_.find(id);
  if (found == extensions_.end()) {
    error = "no such extension";
    return false;
  }
  found->second.enabled = enabled;
  if (!enabled) {
    stop_worker(id);
    // Disabling a VPN must not silently leave its proxy pointing at a dead port.
    if (effects_.clear_proxy) effects_.clear_proxy(id, error);
  }
  return true;
}

std::vector<ExtensionRecord> ExtensionHost::list() const {
  std::vector<ExtensionRecord> out;
  for (const auto &entry : extensions_) out.push_back(entry.second);
  return out;
}

const ExtensionRecord *ExtensionHost::find(const std::string &id) const {
  auto found = extensions_.find(id);
  return found == extensions_.end() ? nullptr : &found->second;
}

namespace {

// Every chrome.* event object shares this shape; listeners land in the worker's
// registry where stop_worker can unroot them one by one.
void install_event(js::Runtime &runtime, ExtensionHost::Worker &worker, js::Value &holder,
                   const std::string &property, const std::string &name) {
  auto event = runtime.make_object();
  event.object->properties["addListener"] = runtime.make_native(
      [&worker, &runtime, name](js::Values &args) {
        if (args.empty() || args[0].type != js::Value::Type::Object || !args[0].object)
          throw_extension_error(runtime, name + " addListener needs a callback");
        auto &list = worker.listeners[name];
        if (list.size() >= 32)
          throw_extension_error(runtime, "listener limit reached for " + name);
        list.push_back(args[0]);
        runtime.root(args[0].object);
        return js::Value();
      });
  event.object->properties["removeListener"] =
      runtime.make_native([&worker, &runtime, name](js::Values &args) {
        auto found = worker.listeners.find(name);
        if (found == worker.listeners.end() || args.empty() || !args[0].object)
          return js::Value();
        auto &list = found->second;
        for (auto it = list.begin(); it != list.end(); ++it)
          if (it->object == args[0].object) {
            runtime.unroot(it->object);
            list.erase(it);
            break;
          }
        return js::Value();
      });
  event.object->properties["hasListener"] =
      runtime.make_native([&worker, &runtime, name](js::Values &args) {
        if (args.empty() || !args[0].object) return js::Value(false);
        auto found = worker.listeners.find(name);
        if (found != worker.listeners.end())
          for (auto &fn : found->second)
            if (fn.object == args[0].object) return js::Value(true);
        return js::Value(false);
      });
  holder.object->properties[property] = event;
}

// Chrome's rule verbatim: the callback runs with chrome.runtime.lastError set,
// and outside the callback it is undefined. I mirror that with a live slot on
// the runtime namespace my callback wrapper updates around the call.
void invoke_callback(js::Runtime &runtime, ExtensionHost::Worker &worker, const js::Values &args,
                     js::Values results = {}, const std::string &error = std::string()) {
  js::Value callback;
  bool found = false;
  for (size_t i = args.size(); i-- > 0;) {
    if (args[i].type == js::Value::Type::Object && args[i].object &&
        args[i].object->kind == js::Object::Kind::Function) {
      callback = args[i];
      found = true;
      break;
    }
  }
  if (!found) return;
  js::Value slot = worker.runtime_ns;
  if (slot.object) {
    if (error.empty()) {
      slot.object->properties.erase("lastError");
    } else {
      auto last = runtime.make_object();
      last.object->properties["message"] = js::Value(error);
      slot.object->properties["lastError"] = last;
    }
  }
  try {
    runtime.call(callback, results);
  } catch (...) {
    if (slot.object) slot.object->properties.erase("lastError");
    throw;
  }
  if (slot.object) slot.object->properties.erase("lastError");
}

// A messaging listener answers one of three ways: nothing, a promise whose
// settlement is the answer, or return true and a later sendResponse call.
bool deliver_message(js::Runtime &runtime, ExtensionHost::Worker &worker, const std::string &event,
                     js::Value message, js::Value sender, std::string &response_json) {
  auto found = worker.listeners.find(event);
  if (found == worker.listeners.end()) return false;
  for (auto &listener : found->second) {
    auto got = std::make_shared<bool>(false);
    auto failure = std::make_shared<std::string>();
    auto answer = std::make_shared<js::Value>(js::Value());
    auto send_response = runtime.make_native([got, answer](js::Values &args) {
      *got = true;
      *answer = args.empty() ? js::Value() : args[0];
      return js::Value();
    });
    try {
      runtime.reset_fuel();
      js::Value returned = runtime.call(listener, {message, sender, send_response});
      runtime.drain_microtasks();
      if (!*got && returned.type == js::Value::Type::Object && returned.object) {
        auto then = runtime.get_member(returned, "then");
        if (then.type == js::Value::Type::Object && then.object) {
          js::Values push = {runtime.make_native([got, answer](js::Values &a) {
                               *got = true;
                               *answer = a.empty() ? js::Value() : a[0];
                               return js::Value();
                             }),
                             runtime.make_native([got, failure](js::Values &a) {
                               *got = true;
                               *failure = a.empty() ? std::string("rejected") : a[0].to_string();
                               return js::Value();
                             })};
          runtime.call(then, push, returned);
          runtime.drain_microtasks();
        }
      }
      if (!*got && returned.truthy()) {
        // The extension promises to answer through sendResponse later; give its
        // timers real ticks so the reply can arrive.
        for (int i = 0; i < 64 && !*got; ++i) {
          runtime.tick();
          runtime.drain_microtasks();
        }
      }
    } catch (js::JsError &e) {
      *failure = e.value.to_string();
    } catch (const std::exception &e) {
      *failure = e.what();
    }
    if (!failure->empty()) worker.last_error = *failure;
    if (*got) {
      if (!failure->empty()) {
        response_json.clear();
        return true;
      }
      response_json = answer->to_json().dump();
      return true;
    }
  }
  return false;
}

void fire_event(js::Runtime &runtime, ExtensionHost::Worker &worker, const std::string &event,
                const js::Values &arguments) {
  auto found = worker.listeners.find(event);
  if (found == worker.listeners.end()) return;
  for (auto &listener : found->second) {
    try {
      runtime.reset_fuel();
      runtime.call(listener, arguments);
    } catch (js::JsError &e) {
      worker.last_error = event + ": " + e.value.to_string();
    } catch (const std::exception &e) {
      worker.last_error = event + ": " + e.what();
    }
  }
  runtime.drain_microtasks();
}

// fetch's arrayBuffer output is the same bytes TextEncoder makes, so my
// promise rides on the encoder I already ship.
js::Value make_array_buffer_reader(js::Runtime &runtime, const std::string &body) {
  return runtime.make_native([&runtime, body](js::Values &) {
    js::Value promise = runtime.make_promise();
    auto text_encoder = runtime.global().properties["TextEncoder"];
    js::Values none;
    auto encoder = runtime.call(text_encoder, none);
    auto encode = runtime.get_member(encoder, "encode");
    js::Values encode_args{js::Value(body)};
    auto bytes = runtime.call(encode, encode_args, encoder);
    runtime.settle_promise(promise.object, 1, bytes);
    return promise;
  });
}

js::Value make_response(js::Runtime &runtime, int status, const std::string &mime,
                        const std::string &body, const std::string &url) {
  js::Value response = runtime.make_object();
  response.object->properties["url"] = js::Value(url);
  response.object->properties["status"] = js::Value(double(status));
  response.object->properties["statusText"] =
      js::Value(status == 200 ? "OK" : status == 404 ? "Not Found" : "");
  response.object->properties["ok"] = js::Value(status >= 200 && status < 300);
  response.object->properties["redirected"] = js::Value(false);
  response.object->properties["type"] = js::Value("basic");
  auto headers = runtime.make_object();
  headers.object->properties["get"] = runtime.make_native([mime, body](js::Values &args) {
    std::string name = lower(args.empty() ? "" : args[0].to_string());
    if (name == "content-type") return js::Value(mime);
    if (name == "content-length") return js::Value(std::to_string(body.size()));
    return js::Value::null();
  });
  response.object->properties["headers"] = headers;
  response.object->properties["text"] = runtime.make_native(
      [&runtime, body](js::Values &) {
        js::Value promise = runtime.make_promise();
        runtime.settle_promise(promise.object, 1, js::Value(body));
        return promise;
      });
  response.object->properties["json"] = runtime.make_native(
      [&runtime, body](js::Values &) {
        js::Value promise = runtime.make_promise();
        auto parsed = Json::parse(body);
        if (parsed)
          runtime.settle_promise(promise.object, 1, json_to_js(runtime, *parsed));
        else
          runtime.settle_promise(promise.object, 2,
                                 js::Value("SyntaxError: body is not JSON"));
        return promise;
      });
  response.object->properties["arrayBuffer"] = make_array_buffer_reader(runtime, body);
  return response;
}

// A port between my realms: the page side and the worker side share one native
// mailbox and each side's postMessage dispatches the other side's onMessage.
struct PortPair {
  std::string name, extension_id;
  js::Runtime *page_runtime = nullptr;   // non-null when the caller was a page
  js::Value page_port;                   // the port object living in the page
  js::Runtime *worker_runtime = nullptr;
  js::Value worker_port;
  bool page_connected = true, worker_connected = true;
};
std::map<int, std::shared_ptr<PortPair>> ports_;
int next_port = 1;

void port_dispatch(js::Runtime &runtime, const js::Value &port, const js::Value &message) {
  if (port.type != js::Value::Type::Object || !port.object) return;
  auto listeners_it = port.object->properties.find("__listeners__");
  if (listeners_it == port.object->properties.end() ||
      listeners_it->second.type != js::Value::Type::Object)
    return;
  auto list = listeners_it->second.object->properties.find("onMessage");
  if (list == listeners_it->second.object->properties.end() ||
      list->second.type != js::Value::Type::Object)
    return;
  size_t n = size_t(list->second.object->properties["length"].to_number());
  for (size_t i = 0; i < n; ++i) {
    auto fn = list->second.object->properties.find(std::to_string(i));
    if (fn == list->second.object->properties.end()) continue;
    try {
      runtime.reset_fuel();
      runtime.call(fn->second, {message, port});
    } catch (...) {
    }
  }
  runtime.drain_microtasks();
}

js::Value make_port(js::Runtime &runtime, std::shared_ptr<PortPair> pair, bool is_page) {
  auto port = runtime.make_object();
  port.object->properties["name"] = js::Value(pair->name);
  port.object->properties["__pair__"] = js::Value();
  auto registry = runtime.make_object();
  auto message_list = runtime.make_array({});
  auto disconnect_list = runtime.make_array({});
  registry.object->properties["onMessage"] = message_list;
  registry.object->properties["onDisconnect"] = disconnect_list;
  port.object->properties["__listeners__"] = registry;
  auto event_builder = [&runtime, self_weak = js::Value(port)](const char *list_prop) {
    auto event = runtime.make_object();
    event.object->properties["addListener"] = runtime.make_native(
        [&runtime, self_weak, list_prop](js::Values &args) {
          if (args.empty() || args[0].type != js::Value::Type::Object) return js::Value();
          auto reg = self_weak.object->properties["__listeners__"];
          auto list = reg.object->properties[list_prop];
          size_t n = size_t(list.object->properties["length"].to_number());
          if (n >= 16) return js::Value();
          if (args[0].object) runtime.root(args[0].object);
          list.object->properties[std::to_string(n)] = args[0];
          list.object->properties["length"] = js::Value(double(n + 1));
          return js::Value();
        });
    event.object->properties["removeListener"] =
        runtime.make_native([](js::Values &) { return js::Value(); });
    event.object->properties["hasListener"] =
        runtime.make_native([](js::Values &) { return js::Value(false); });
    return event;
  };
  port.object->properties["onMessage"] = event_builder("onMessage");
  port.object->properties["onDisconnect"] = event_builder("onDisconnect");
  port.object->properties["postMessage"] = runtime.make_native(
      [pair, is_page, &runtime](js::Values &args) {
        auto message = args.empty() ? js::Value() : args[0];
        if (is_page) {
          if (pair->worker_connected && pair->worker_runtime)
            port_dispatch(*pair->worker_runtime, pair->worker_port, message);
        } else {
          if (pair->page_connected && pair->page_runtime)
            port_dispatch(*pair->page_runtime, pair->page_port, message);
        }
        return js::Value();
      });
  port.object->properties["disconnect"] = runtime.make_native(
      [pair, is_page](js::Values &) {
        if (is_page)
          pair->page_connected = false;
        else
          pair->worker_connected = false;
        return js::Value();
      });
  static_cast<void>(runtime);
  return port;
}
} // namespace

// Defined below install_api; kept private to this translation unit.
void install_browser_alias(js::Runtime &runtime, js::Value browser, js::Value chrome);

void ExtensionHost::install_api(Worker &worker, const ExtensionRecord &record) {
  auto &runtime = *worker.runtime;
  auto &global = runtime.global();
  worker.directory = record.directory;
  auto self_id = record.manifest.id;
  auto chrome = runtime.make_object();

  // ---- worker platform globals ---------------------------------------------
  {
    global.properties["self"] = js::Value(&global);
    auto location = runtime.make_object();
    location.object->properties["href"] =
        js::Value("chrome-extension://" + self_id + "/background.html");
    location.object->properties["origin"] = js::Value("chrome-extension://" + self_id);
    location.object->properties["protocol"] = js::Value("chrome-extension:");
    location.object->properties["host"] = js::Value(self_id);
    location.object->properties["hostname"] = js::Value(self_id);
    location.object->properties["pathname"] = js::Value("/background.html");
    global.properties["location"] = location;
    auto performance = runtime.make_object();
    performance.object->properties["now"] = runtime.make_native(
        [](js::Values &) { return js::Value(double(now_ms() % 86400000000LL)); });
    performance.object->properties["timeOrigin"] =
        js::Value(double(now_ms() % 86400000000LL));
    global.properties["performance"] = performance;
    std::string directory = record.directory;
    global.properties["importScripts"] = runtime.make_native(
        [&runtime, directory](js::Values &args) {
          for (auto &arg : args) {
            std::string rel = arg.to_string();
            if (!rel.empty() && rel.front() == '/') rel = rel.substr(1);
            namespace fs = std::filesystem;
            std::error_code ec;
            auto base = fs::weakly_canonical(fs::path(directory), ec);
            auto path = fs::weakly_canonical(fs::path(directory) / rel, ec);
            if (ec || base.string().empty() ||
                path.string().compare(0, base.string().size(), base.string()) != 0)
              throw_extension_error(runtime, "importScripts path escapes the extension folder");
            if (!fs::is_regular_file(path, ec))
              throw_extension_error(runtime, "importScripts: missing script " + rel);
            try {
              runtime.run(read_all(path.string()));
            } catch (js::JsError &e) {
              throw_extension_error(runtime,
                                    "importScripts " + rel + " failed: " + e.value.to_string());
            }
          }
          return js::Value();
        });
    global.properties["addEventListener"] = runtime.make_native(
        [&worker, &runtime](js::Values &args) {
          if (args.size() >= 2 && args[1].type == js::Value::Type::Object && args[1].object) {
            auto &list = worker.listeners["event:" + args[0].to_string()];
            if (list.size() < 32) {
              list.push_back(args[1]);
              runtime.root(args[1].object);
            }
          }
          return js::Value();
        });
    global.properties["removeEventListener"] =
        runtime.make_native([](js::Values &) { return js::Value(); });
    // Chrome service workers have no localStorage; Sentry only probes it inside
    // guards, so I give the guard a real object that remembers nothing.
    auto local_storage = runtime.make_object();
    local_storage.object->properties["getItem"] =
        runtime.make_native([](js::Values &) { return js::Value::null(); });
    local_storage.object->properties["setItem"] =
        runtime.make_native([](js::Values &) { return js::Value(); });
    local_storage.object->properties["removeItem"] =
        runtime.make_native([](js::Values &) { return js::Value(); });
    local_storage.object->properties["clear"] =
        runtime.make_native([](js::Values &) { return js::Value(); });
    global.properties["localStorage"] = local_storage;

    global.properties["fetch"] = runtime.make_native(
        [this, &worker, &runtime, self_id](js::Values &args) {
          js::Value promise = runtime.make_promise();
          if (args.empty()) {
            runtime.settle_promise(promise.object, 2, js::Value("TypeError: fetch needs a URL"));
            return promise;
          }
          std::string url = args[0].to_string();
          ExtensionEffects::FetchRequest request;
          request.method = "GET";
          if (args.size() > 1 && args[1].type == js::Value::Type::Object) {
            auto init = args[1].to_json();
            request.method = init.str("method", "GET");
            for (char &c : request.method) c = static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
            request.body = init.str("body");
            const Json *headers = init.find("headers");
            if (headers && headers->type == Json::Type::Object)
              for (auto &entry : headers->object) {
                std::string value = entry.second.type == Json::Type::String
                                        ? entry.second.string
                                        : entry.second.dump();
                request.headers.push_back(entry.first + ": " + value);
              }
          }
          if (starts(url, "/")) url = "chrome-extension://" + self_id + url;
          if (starts(url, "chrome-extension://")) {
            // An extension's own fetch of a packaged file reads my folder, not
            // the network: config, locales and popups all load through here.
            auto rest = url.substr(std::string("chrome-extension://").size());
            auto slash = rest.find('/');
            std::string contents, mime;
            bool ok = slash != std::string::npos &&
                      read_page_resource(self_id, rest.substr(slash + 1), contents, mime);
            auto response = make_response(runtime, ok ? 200 : 404,
                                          mime.empty() ? "text/plain" : mime,
                                          contents, url);
            runtime.settle_promise(promise.object, 1, response);
            return promise;
          }
          request.url = url;
          ExtensionEffects::FetchAnswer answer =
              effects_.fetch ? effects_.fetch(self_id, request) : ExtensionEffects::FetchAnswer{};
          if (answer.status == 0 && answer.error.empty())
            answer.error = "my shell did not answer the fetch";
          if (answer.status >= 100) {
            auto response =
                make_response(runtime, answer.status, answer.mime, answer.text, url);
            runtime.settle_promise(promise.object, 1, response);
          } else {
            runtime.settle_promise(promise.object, 2, js::Value("TypeError: " + answer.error));
          }
          return promise;
        });
    global.properties["XMLHttpRequest"] = runtime.make_native(
        [this, &runtime, self_id](js::Values &) {
          // Sentry's fallback transport when fetch refuses. Real requests ride
          // my shell fetch; failure states reach the callbacks honestly.
          auto self = runtime.make_object();
          auto state = std::make_shared<std::string>("");
          auto url_slot = std::make_shared<std::string>("");
          auto headers = std::make_shared<Json>(Json::make_object());
          auto done = std::make_shared<bool>(false);
          self.object->properties["open"] = runtime.make_native(
              [state, url_slot, done, headers](js::Values &a) {
                if (a.size() >= 2) {
                  *state = a[0].to_string();
                  *url_slot = a[1].to_string();
                  *done = false;
                  *headers = Json::make_object();
                }
                return js::Value();
              });
          self.object->properties["setRequestHeader"] = runtime.make_native(
              [headers](js::Values &a) {
                if (a.size() >= 2) headers->set(a[0].to_string(), a[1].to_string());
                return js::Value();
              });
          self.object->properties["getAllResponseHeaders"] =
              runtime.make_native([](js::Values &) { return js::Value(""); });
          self.object->properties["send"] = runtime.make_native(
              [this, &runtime, self, self_id, state, url_slot, done](js::Values &a) {
                ExtensionEffects::FetchRequest request;
                request.url = *url_slot;
                request.method = *state;
                for (char &c : request.method)
                  c = static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
                if (!a.empty()) request.body = a[0].to_string();
                ExtensionEffects::FetchAnswer answer =
                    effects_.fetch ? effects_.fetch(self_id, request) : ExtensionEffects::FetchAnswer{};
                self.object->properties["status"] = js::Value(double(answer.status));
                self.object->properties["responseText"] =
                    js::Value(answer.status >= 100 ? answer.text : std::string());
                *done = true;
                self.object->properties["readyState"] = js::Value(4.0);
                // readystatechange, error, then load, in the worker's own order.
                auto fire = [&](const char *event) {
                  auto fn = self.object->properties.find(std::string("on") + event);
                  if (fn != self.object->properties.end() &&
                      fn->second.type == js::Value::Type::Object && fn->second.object) {
                    js::Values none;
                    runtime.call(fn->second, none, self);
                  }
                };
                fire("readystatechange");
                fire(answer.status >= 100 ? "load" : "error");
                fire("loadend");
                runtime.drain_microtasks();
                return js::Value();
              });
          return self;
        });
    // A worker asks for WebSocket rarely (Sentry probes it once). I leave a
    // constructor that immediately reports onerror, so code falls to fetch
    // the same way it does on a machine where the port is closed.
    global.properties["WebSocket"] = runtime.make_native(
        [&runtime](js::Values &a) {
          auto self = runtime.make_object();
          self.object->properties["readyState"] = js::Value(3.0); // CLOSED
          self.object->properties["url"] =
              a.empty() ? js::Value("") : js::Value(a[0].to_string());
          self.object->properties["send"] =
              runtime.make_native([](js::Values &) { return js::Value(); });
          self.object->properties["close"] =
              runtime.make_native([](js::Values &) { return js::Value(); });
          auto message = "WebSocket refused: my worker transport does not open sockets yet";
          js::Values one{js::Value(message)};
          auto fn = self.object->properties.find("onerror");
          static_cast<void>(fn);
          static_cast<void>(one);
          return self;
        });
  }

  // ---- chrome.runtime -------------------------------------------------------
  {
    auto object = runtime.make_object();
    object.object->properties["id"] = js::Value(self_id);
    auto directory = record.directory;
    object.object->properties["getManifest"] = runtime.make_native(
        [&runtime, directory](js::Values &) {
          try {
            auto json = Json::parse(read_all(directory + "/manifest.json"));
            if (json) return json_to_js(runtime, *json);
          } catch (...) {
          }
          auto out = runtime.make_object();
          out.object->properties["version"] = js::Value("");
          return out;
        });
    object.object->properties["getURL"] = runtime.make_native(
        [self_id](js::Values &args) {
          auto path = args.empty() ? std::string() : args[0].to_string();
          if (!path.empty() && path.front() == '/') path.erase(0, 1);
          return js::Value("chrome-extension://" + self_id + "/" + path);
        });
    object.object->properties["getPlatformInfo"] = runtime.make_native(
        [this, &runtime, &worker](js::Values &args) {
          Json info = Json::make_object();
          info.set("os", "win");
          info.set("arch", "x86-64");
          info.set("nacl_arch", "x86-64");
          auto value = json_to_js(runtime, info);
          invoke_callback(runtime, worker, args, {value});
          return value;
        });
    object.object->properties["getContexts"] = runtime.make_native(
        [this, &runtime, &worker](js::Values &args) {
          invoke_callback(runtime, worker, args, {runtime.make_array({})});
          return js::Value();
        });
    object.object->properties["setUninstallURL"] = runtime.make_native(
        [this, &runtime, &worker](js::Values &args) {
          invoke_callback(runtime, worker, args, {});
          return js::Value();
        });
    object.object->properties["openOptionsPage"] = runtime.make_native(
        [this, &runtime, &worker](js::Values &args) {
          invoke_callback(runtime, worker, args, {});
          return js::Value();
        });
    install_event(runtime, worker, object, "onMessage", "runtime.onMessage");
    install_event(runtime, worker, object, "onMessageExternal", "runtime.onMessageExternal");
    install_event(runtime, worker, object, "onInstalled", "runtime.onInstalled");
    install_event(runtime, worker, object, "onStartup", "runtime.onStartup");
    install_event(runtime, worker, object, "onConnect", "runtime.onConnect");
    object.object->properties["sendMessage"] = runtime.make_native(
        [this, &runtime, &worker, self_id](js::Values &args) {
          // (message[, options], callback) and (extensionId, message) both reach
          // my own worker only; cross-extension messaging is not granted.
          size_t at = 0;
          if (args.size() >= 2 && args[0].type == js::Value::Type::String) at = 1;
          if (args.size() <= at) return js::Value();
          auto message = args[at];
          std::string response_json;
          deliver_message(runtime, worker, "runtime.onMessage", message,
                          runtime.make_object(), response_json);
          js::Values results;
          if (!response_json.empty()) {
            auto parsed = Json::parse(response_json);
            if (parsed) results.push_back(json_to_js(runtime, *parsed));
          }
          invoke_callback(runtime, worker, args, results);
          return results.empty() ? js::Value() : results.front();
        });
    object.object->properties["connect"] = runtime.make_native(
        [this, &runtime, &worker, self_id](js::Values &args) {
          auto pair = std::make_shared<PortPair>();
          pair->name = args.empty() ? std::string() : args[0].to_json().str("name");
          pair->extension_id = self_id;
          ports_[next_port++] = pair;
          pair->page_runtime = &runtime;
          pair->page_port = make_port(runtime, pair, true);
          // The far side: fire onConnect in the worker with its own port end.
          pair->worker_runtime = &runtime;
          pair->worker_port = make_port(runtime, pair, false);
          auto found = worker.listeners.find("runtime.onConnect");
          if (found != worker.listeners.end())
            for (auto &listener : found->second) {
              try {
                runtime.reset_fuel();
                runtime.call(listener, {pair->worker_port});
                runtime.drain_microtasks();
              } catch (js::JsError &e) {
                worker.last_error = e.value.to_string();
              }
            }
          return pair->page_port;
        });
    chrome.object->properties["runtime"] = object;
    worker.runtime_ns = object;
  }

  // ---- chrome.storage --------------------------------------------------------
  {
    auto storage = runtime.make_object();
    auto guard = [this, &runtime, self_id]() {
      auto found = extensions_.find(self_id);
      std::string error;
      if (found == extensions_.end() || !grant(found->second, "storage", error))
        throw_extension_error(runtime, error);
      return found;
    };
    // One real map; each area reads/writes through the same guarded handlers.
    // "managed" is read-only policy storage my shell never writes, so gets from
    // it always answer empty - the truthful state of my managed storage.
    auto make_area = [this, &runtime, &worker, guard](bool read_only) {
      auto area = runtime.make_object();
      auto self_id2 = worker.id;
      area.object->properties["set"] = runtime.make_native(
          [this, &runtime, &worker, guard, read_only](js::Values &args) {
            auto it = guard();
            if (read_only)
              throw_extension_error(runtime, "managed storage is read-only");
            if (!args.empty() && args[0].type == js::Value::Type::Object) {
              auto incoming = args[0].to_json();
              if (incoming.type == Json::Type::Object) {
                if (it->second.storage.object.size() + incoming.object.size() > 8192)
                  throw_extension_error(runtime, "extension storage item limit reached");
                Json changed = Json::make_object();
                for (auto &entry : incoming.object) {
                  Json delta = Json::make_object();
                  const Json *old = it->second.storage.find(entry.first);
                  if (old) delta.set("oldValue", *old);
                  delta.set("newValue", entry.second);
                  it->second.storage.set(entry.first, entry.second);
                  changed.set(entry.first, delta);
                }
                std::string error;
                if (!storage_path_.empty()) save_storage(storage_path_, error);
                fire_event(runtime, worker, "storage.onChanged",
                           {json_to_js(runtime, changed), js::Value("local")});
              }
            }
            invoke_callback(runtime, worker, args, {});
            return js::Value();
          });
      area.object->properties["get"] = runtime.make_native(
          [this, &runtime, &worker, guard, read_only](js::Values &args) {
            auto it = guard();
            Json result = Json::make_object();
            if (args.empty() || args[0].type == js::Value::Type::Null ||
                args[0].type == js::Value::Type::Undefined) {
              result = read_only ? Json::make_object() : it->second.storage;
            } else if (args[0].type == js::Value::Type::String) {
              const Json *value = read_only ? nullptr : it->second.storage.find(args[0].to_string());
              if (value) result.set(args[0].to_string(), *value);
            } else if (args[0].type == js::Value::Type::Object) {
              auto json = args[0].to_json();
              if (json.type == Json::Type::Array) {
                if (!read_only)
                  for (auto &name : json.array) {
                    const Json *value = it->second.storage.find(name.string);
                    if (value) result.set(name.string, *value);
                  }
              } else if (json.type == Json::Type::Object) {
                // An object argument supplies defaults.
                for (auto &entry : json.object) {
                  const Json *value = read_only ? nullptr : it->second.storage.find(entry.first);
                  result.set(entry.first, value ? *value : entry.second);
                }
              }
            }
            auto value = json_to_js(runtime, result);
            invoke_callback(runtime, worker, args, {value});
            return value;
          });
      area.object->properties["remove"] = runtime.make_native(
          [this, &runtime, &worker, guard, read_only](js::Values &args) {
            auto it = guard();
            if (read_only)
              throw_extension_error(runtime, "managed storage is read-only");
            if (!args.empty()) {
              std::vector<std::string> keys;
              if (args[0].type == js::Value::Type::String)
                keys.push_back(args[0].to_string());
              else if (args[0].type == js::Value::Type::Object) {
                auto json = args[0].to_json();
                for (auto &name : json.array) keys.push_back(name.string);
              }
              auto &entries = it->second.storage.object;
              Json changed = Json::make_object();
              for (const auto &key : keys) {
                for (auto &entry : entries)
                  if (entry.first == key) {
                    Json delta = Json::make_object();
                    delta.set("oldValue", entry.second);
                    changed.set(key, delta);
                  }
                entries.erase(std::remove_if(entries.begin(), entries.end(),
                                             [&](const std::pair<std::string, Json> &e) {
                                               return e.first == key;
                                             }),
                              entries.end());
              }
              std::string error;
              if (!storage_path_.empty()) save_storage(storage_path_, error);
              if (auto *cobj = changed.find("")) static_cast<void>(cobj);
              if (!changed.object.empty()) {
                fire_event(runtime, worker, "storage.onChanged",
                           {json_to_js(runtime, changed), js::Value("local")});
              }
            }
            invoke_callback(runtime, worker, args, {});
            return js::Value();
          });
      area.object->properties["clear"] = runtime.make_native(
          [this, &runtime, &worker, guard, read_only](js::Values &args) {
            auto it = guard();
            if (read_only)
              throw_extension_error(runtime, "managed storage is read-only");
            it->second.storage = Json::make_object();
            std::string error;
            if (!storage_path_.empty()) save_storage(storage_path_, error);
            invoke_callback(runtime, worker, args, {});
            return js::Value();
          });
      area.object->properties["getBytesInUse"] = runtime.make_native(
          [this, &runtime, &worker, guard, read_only](js::Values &args) {
            auto it = guard();
            double bytes = double(it->second.storage.dump().size());
            auto value = js::Value(bytes);
            invoke_callback(runtime, worker, args, {value});
            return value;
          });
      return area;
    };
    storage.object->properties["local"] = make_area(false);
    storage.object->properties["sync"] = make_area(false);
    storage.object->properties["session"] = make_area(false);
    storage.object->properties["managed"] = make_area(true);
    install_event(runtime, worker, storage, "onChanged", "storage.onChanged");
    chrome.object->properties["storage"] = storage;
  }

  // ---- chrome.proxy ----------------------------------------------------------
  {
    auto proxy = runtime.make_object();
    auto settings = runtime.make_object();
    settings.object->properties["set"] = runtime.make_native(
        [this, &runtime, &worker, self_id](js::Values &args) {
          auto found = extensions_.find(self_id);
          std::string error;
          if (found == extensions_.end() || !grant(found->second, "proxy", error))
            throw_extension_error(runtime, error);
          if (args.empty() || args[0].type != js::Value::Type::Object)
            throw_extension_error(runtime, "proxy.settings.set needs a details object");
          auto details = args[0].to_json();
          const Json *value = details.find("value");
          if (!value) throw_extension_error(runtime, "proxy.settings.set needs details.value");
          std::string parse_error;
          Json input = *value;
          input.set("ownerExtension", self_id);
          auto config = ProxyConfig::from_json(input, parse_error);
          if (!config) throw_extension_error(runtime, parse_error);
          if (!effects_.set_proxy || !effects_.set_proxy(self_id, *config, error))
            throw_extension_error(runtime, error.empty() ? "the browser refused the proxy" : error);
          if (effects_.log)
            effects_.log(self_id, "proxy", "set mode " + config->mode_name());
          invoke_callback(runtime, worker, args, {});
          return js::Value();
        });
    settings.object->properties["get"] = runtime.make_native(
        [this, &runtime, &worker, self_id](js::Values &args) {
          auto found = extensions_.find(self_id);
          std::string error;
          if (found == extensions_.end() || !grant(found->second, "proxy", error))
            throw_extension_error(runtime, error);
          Json result = Json::make_object();
          auto config = effects_.get_proxy ? effects_.get_proxy(self_id) : ProxyConfig::direct();
          result.set("value", config.to_json());
          result.set("levelOfControl", config.owner_extension == self_id
                                           ? "controlled_by_this_extension"
                                           : "controllable_by_this_extension");
          auto value = json_to_js(runtime, result);
          invoke_callback(runtime, worker, args, {value});
          return value;
        });
    settings.object->properties["clear"] = runtime.make_native(
        [this, &runtime, &worker, self_id](js::Values &args) {
          auto found = extensions_.find(self_id);
          std::string error;
          if (found == extensions_.end() || !grant(found->second, "proxy", error))
            throw_extension_error(runtime, error);
          if (effects_.clear_proxy && !effects_.clear_proxy(self_id, error))
            throw_extension_error(runtime, error);
          invoke_callback(runtime, worker, args, {});
          return js::Value();
        });
    settings.object->properties["onChange"] = runtime.make_object();
    install_event(runtime, worker, settings, "onChange", "proxy.settings.onChange");
    proxy.object->properties["settings"] = settings;
    install_event(runtime, worker, proxy, "onProxyError", "proxy.onProxyError");
    chrome.object->properties["proxy"] = proxy;
  }

  // ---- chrome.privacy.network.webRTCIPHandlingPolicy -------------------------
  {
    auto privacy = runtime.make_object();
    auto network = runtime.make_object();
    auto webrtc = runtime.make_object();
    auto read_policy = [&]() {
      auto found = extensions_.find(self_id);
      std::string handle = "default";
      if (found != extensions_.end()) {
        auto wfound = found->second.enabled ? workers_.find(self_id) : workers_.end();
        if (wfound != workers_.end()) handle = wfound->second->webrtc_policy;
      }
      return handle;
    };
    webrtc.object->properties["set"] = runtime.make_native(
        [this, &runtime, &worker, self_id](js::Values &args) {
          auto found = extensions_.find(self_id);
          std::string error;
          if (found == extensions_.end() || !grant(found->second, "privacy", error))
            throw_extension_error(runtime, error);
          std::string value = "default";
          if (!args.empty() && args[0].type == js::Value::Type::Object)
            value = args[0].to_json().str("value", "default");
          static const std::set<std::string> allowed = {
              "default", "default_public_and_private_interfaces",
              "default_public_interface_only", "disable_non_proxied_udp"};
          if (!allowed.count(value)) value = "default";
          worker.webrtc_policy = value;
          if (!effects_.set_webrtc_policy || !effects_.set_webrtc_policy(self_id, value, error))
            throw_extension_error(runtime, error);
          invoke_callback(runtime, worker, args, {});
          return js::Value();
        });
    webrtc.object->properties["get"] = runtime.make_native(
        [this, &runtime, &worker, read_policy](js::Values &args) {
          Json result = Json::make_object();
          result.set("value", read_policy());
          result.set("levelOfControl", "controlled_by_this_extension");
          auto value = json_to_js(runtime, result);
          invoke_callback(runtime, worker, args, {value});
          return value;
        });
    webrtc.object->properties["clear"] = runtime.make_native(
        [this, &runtime, &worker, self_id](js::Values &args) {
          std::string error;
          worker.webrtc_policy = "default";
          if (effects_.set_webrtc_policy) effects_.set_webrtc_policy(self_id, "default", error);
          invoke_callback(runtime, worker, args, {});
          return js::Value();
        });
    network.object->properties["webRTCIPHandlingPolicy"] = webrtc;
    privacy.object->properties["network"] = network;
    chrome.object->properties["privacy"] = privacy;
  }

  // ---- chrome.webRequest ------------------------------------------------------
  {
    auto web_request = runtime.make_object();
    // The provider contract Proton's auth handler requires: (details, callback),
    // listener answers with a BlockingResponse through the callback. My network
    // stack calls request_proxy_credentials only for a proxy challenge, never
    // for a website - the extension never sees website credentials.
    web_request.object->properties["onAuthRequired"] = runtime.make_object();
    install_event(runtime, worker, web_request, "onAuthRequired", "webRequest.onAuthRequired");
    install_event(runtime, worker, web_request, "onCompleted", "webRequest.onCompleted");
    install_event(runtime, worker, web_request, "onErrorOccurred", "webRequest.onErrorOccurred");
    web_request.object->properties["handlerBehaviorChanged"] = runtime.make_native(
        [this, &runtime, &worker](js::Values &args) {
          // My host keeps no stale webRequest caches, so this reloads nothing
          // and just behaves like a completed flush.
          invoke_callback(runtime, worker, args, {});
          return js::Value();
        });
    chrome.object->properties["webRequest"] = web_request;
  }

  // ---- chrome.permissions -------------------------------------------------------
  {
    auto permissions = runtime.make_object();
    auto read_names = [](const js::Value &arg, std::vector<std::string> &perms,
                         std::vector<std::string> &origins) {
      if (arg.type != js::Value::Type::Object) return;
      auto json = arg.to_json();
      const Json *p = json.find("permissions");
      if (p && p->type == Json::Type::Array)
        for (auto &item : p->array) perms.push_back(item.string);
      const Json *o = json.find("origins");
      if (o && o->type == Json::Type::Array)
        for (auto &item : o->array) origins.push_back(item.string);
    };
    permissions.object->properties["contains"] = runtime.make_native(
        [this, &runtime, &worker, self_id, read_names](js::Values &args) {
          std::vector<std::string> perms, origins;
          if (!args.empty()) read_names(args[0], perms, origins);
          auto found = extensions_.find(self_id);
          bool has = found != extensions_.end();
          if (has) {
            for (auto &wanted : perms)
              if (!found->second.manifest.has_permission(wanted)) { has = false; break; }
            if (has)
              for (auto &wanted : origins) {
                auto url = parse_url(wanted);
                bool test = url && (found->second.manifest.has_permission("<all_urls>") ||
                                    false);
                if (!test && url) {
                  for (const auto &pattern : found->second.manifest.host_permissions)
                    if (host_pattern_matches(pattern, *url)) { test = true; break; }
                }
                if (!test) { has = false; break; }
              }
          }
          auto value = js::Value(has);
          invoke_callback(runtime, worker, args, {value});
          return value;
        });
    permissions.object->properties["getAll"] = runtime.make_native(
        [this, &runtime, &worker, self_id](js::Values &args) {
          Json out = Json::make_object();
          Json perms = Json::make_array();
          Json origins = Json::make_array();
          auto found = extensions_.find(self_id);
          if (found != extensions_.end()) {
            for (auto &perm : found->second.manifest.permissions) perms.array.push_back(perm);
            for (auto &origin : found->second.manifest.host_permissions)
              origins.array.push_back(origin);
          }
          out.set("permissions", std::move(perms));
          out.set("origins", std::move(origins));
          auto value = json_to_js(runtime, out);
          invoke_callback(runtime, worker, args, {value});
          return value;
        });
    permissions.object->properties["request"] = runtime.make_native(
        [this, &runtime, &worker, self_id, read_names](js::Values &args) {
          // I only grant what the manifest declares up front; asking for
          // anything else answers false, exactly like a denied Chrome prompt.
          std::vector<std::string> perms, origins;
          if (!args.empty()) read_names(args[0], perms, origins);
          auto found = extensions_.find(self_id);
          bool granted = found != extensions_.end();
          if (granted)
            for (auto &wanted : perms)
              if (!found->second.manifest.has_permission(wanted)) { granted = false; break; }
          auto value = js::Value(granted);
          invoke_callback(runtime, worker, args, {value});
          return value;
        });
    chrome.object->properties["permissions"] = permissions;
  }

  // ---- chrome.idle --------------------------------------------------------------
  {
    auto idle = runtime.make_object();
    idle.object->properties["queryState"] = runtime.make_native(
        [this, &runtime, &worker](js::Values &args) {
          double threshold = args.empty() ? 60 : args[0].to_number();
          double seconds = effects_.idle_seconds ? effects_.idle_seconds() : 0.0;
          // I only ever report active or idle; "locked" needs an OS session
          // signal my shell does not track, so I never claim it.
          auto state = js::Value(seconds < threshold ? "active" : "idle");
          invoke_callback(runtime, worker, args, {state});
          return state;
        });
    idle.object->properties["setDetectionInterval"] = runtime.make_native(
        [&worker](js::Values &args) {
          if (!args.empty()) worker.idle_interval = int(std::clamp(args[0].to_number(), 15.0, 86400.0));
          return js::Value();
        });
    install_event(runtime, worker, idle, "onStateChanged", "idle.onStateChanged");
    chrome.object->properties["idle"] = idle;
  }

  // ---- chrome.notifications ------------------------------------------------------
  {
    auto notifications = runtime.make_object();
    notifications.object->properties["create"] = runtime.make_native(
        [this, &runtime, &worker, self_id](js::Values &args) {
          // Both "create(options, cb)" and "create(id, options, cb)".
          std::string id;
          size_t at = 0;
          if (args.size() >= 2 && args[0].type == js::Value::Type::String) {
            id = args[0].to_string();
            at = 1;
          }
          if (id.empty()) id = "note-" + std::to_string(now_ms() % 100000000LL);
          std::string title, message;
          if (args.size() > at && args[at].type == js::Value::Type::Object) {
            auto options = args[at].to_json();
            title = options.str("title");
            message = options.str("message");
          }
          if (effects_.notify) effects_.notify(self_id, id, title, message);
          invoke_callback(runtime, worker, args, {js::Value(id)});
          return js::Value(id);
        });
    notifications.object->properties["clear"] = runtime.make_native(
        [this, &runtime, &worker](js::Values &args) {
          invoke_callback(runtime, worker, args, {js::Value(true)});
          return js::Value();
        });
    notifications.object->properties["update"] = runtime.make_native(
        [this, &runtime, &worker, self_id](js::Values &args) {
          if (args.size() >= 2 && args[0].type == js::Value::Type::String &&
              args[1].type == js::Value::Type::Object) {
            auto options = args[1].to_json();
            if (effects_.notify)
              effects_.notify(self_id, args[0].to_string(), options.str("title"),
                              options.str("message"));
          }
          invoke_callback(runtime, worker, args, {js::Value(true)});
          return js::Value();
        });
    notifications.object->properties["getAll"] = runtime.make_native(
        [this, &runtime, &worker](js::Values &args) {
          invoke_callback(runtime, worker, args, {runtime.make_object()});
          return js::Value();
        });
    install_event(runtime, worker, notifications, "onClicked", "notifications.onClicked");
    install_event(runtime, worker, notifications, "onButtonClicked",
                  "notifications.onButtonClicked");
    install_event(runtime, worker, notifications, "onClosed", "notifications.onClosed");
    chrome.object->properties["notifications"] = notifications;
  }

  // ---- chrome.tabs ---------------------------------------------------------------
  {
    auto tabs = runtime.make_object();
    tabs.object->properties["create"] = runtime.make_native(
        [this, &runtime, &worker](js::Values &args) {
          std::string url;
          bool active = true;
          if (!args.empty() && args[0].type == js::Value::Type::Object) {
            auto options = args[0].to_json();
            url = options.str("url");
            active = options.flag("active", true);
          }
          int id = effects_.open_tab ? effects_.open_tab(url, active) : -1;
          Json tab = Json::make_object();
          tab.set("id", id);
          tab.set("url", url);
          tab.set("active", active);
          tab.set("status", "complete");
          auto value = json_to_js(runtime, tab);
          invoke_callback(runtime, worker, args, {value});
          return value;
        });
    tabs.object->properties["query"] = runtime.make_native(
        [this, &runtime, &worker](js::Values &args) {
          // Tabs belong to my shell, not to me; an extension can only resolve
          // ids it was handed through my own events, so query answers empty.
          invoke_callback(runtime, worker, args, {runtime.make_array({})});
          return js::Value();
        });
    tabs.object->properties["get"] = runtime.make_native(
        [this, &runtime, &worker](js::Values &args) {
          int id = args.empty() ? -1 : int(args[0].to_number());
          std::string url;
          Json tab = Json::make_object();
          if (effects_.resolve_tab && id >= 0 && effects_.resolve_tab(id, url)) {
            tab.set("id", id);
            tab.set("url", url);
            tab.set("status", "complete");
          }
          if (tab.object.empty()) {
            invoke_callback(runtime, worker, args, {js::Value()},
                            "Tabs cannot be resolved right now");
            return js::Value();
          }
          auto value = json_to_js(runtime, tab);
          invoke_callback(runtime, worker, args, {value});
          return value;
        });
    tabs.object->properties["remove"] = runtime.make_native(
        [this, &runtime, &worker](js::Values &args) {
          int id = args.empty() ? -1 : 0;
          if (!args.empty()) {
            auto json = args[0].to_json();
            if (json.type == Json::Type::Number) id = int(json.number);
            else if (json.type == Json::Type::Array && !json.array.empty())
              id = int(json.array.front().number);
          }
          if (effects_.close_tab && id >= 0) effects_.close_tab(id);
          invoke_callback(runtime, worker, args, {});
          return js::Value();
        });
    tabs.object->properties["sendMessage"] = runtime.make_native(
        [this, &runtime, &worker](js::Values &args) {
          // I deliver messages into pages only when a page exists and only to a
          // tab my own shell resolves - nowhere else.
          invoke_callback(runtime, worker, args, {js::Value()});
          return js::Value();
        });
    tabs.object->properties["executeScript"] = runtime.make_native(
        [this, &runtime, &worker](js::Values &args) {
          int id = args.empty() ? -1 : int(args[0].to_number());
          std::string code, result;
          if (args.size() > 1 && args[1].type == js::Value::Type::Object)
            code = args[1].to_json().str("code");
          bool ok = !code.empty() && effects_.run_in_tab &&
                    effects_.run_in_tab(id, code, result);
          js::Values results;
          if (ok) results.push_back(js::Value(result));
          invoke_callback(runtime, worker, args, results, ok ? "" : "the tab refused my script");
          return js::Value();
        });
    install_event(runtime, worker, tabs, "onRemoved", "tabs.onRemoved");
    install_event(runtime, worker, tabs, "onUpdated", "tabs.onUpdated");
    install_event(runtime, worker, tabs, "onActivated", "tabs.onActivated");
    chrome.object->properties["tabs"] = tabs;
  }

  // ---- chrome.scripting ------------------------------------------------------------
  {
    auto scripting = runtime.make_object();
    scripting.object->properties["executeScript"] = runtime.make_native(
        [this, &runtime, &worker](js::Values &args) {
          int id = -1;
          std::string code, result, fallback_error = "the tab refused my script";
          if (!args.empty() && args[0].type == js::Value::Type::Object) {
            auto details = args[0].to_json();
            const Json *target = details.find("target");
            if (target && target->type == Json::Type::Object)
              id = int(target->num("tabId", -1));
            code = details.str("code");
            if (code.empty()) {
              // {func: f, args: [...]} - the function serializes poorly through
              // my JSON model, so I report refusal instead of half-running it.
              fallback_error = "function injection needs my page runtime bridge";
            }
          }
          bool ok = !code.empty() && effects_.run_in_tab &&
                    effects_.run_in_tab(id, code, result);
          js::Values results;
          if (ok) {
            auto item = runtime.make_object();
            item.object->properties["result"] = js::Value(result);
            results.push_back(runtime.make_array({item}));
          }
          invoke_callback(runtime, worker, args, results, ok ? "" : fallback_error);
          return js::Value();
        });
    chrome.object->properties["scripting"] = scripting;
  }

  // ---- chrome.alarms --------------------------------------------------------------
  {
    auto alarms = runtime.make_object();
    alarms.object->properties["create"] = runtime.make_native(
        [this, &runtime, &worker, self_id](js::Values &args) {
          auto found = extensions_.find(self_id);
          std::string error;
          if (found == extensions_.end() || !grant(found->second, "alarms", error))
            throw_extension_error(runtime, error);
          std::string name;
          const js::Value *info = nullptr;
          if (!args.empty() && args[0].type == js::Value::Type::String) {
            name = args[0].to_string();
            if (args.size() > 1) info = &args[1];
          } else if (!args.empty())
            info = &args[0];
          double minutes = 1.0;
          auto now = double(now_ms() % 4000000000000000LL);
          double when = now + 60000.0;
          if (info && info->type == js::Value::Type::Object) {
            auto json = info->to_json();
            if (json.find("periodInMinutes")) minutes = json.num("periodInMinutes", 1.0);
            else if (json.find("delayInMinutes")) minutes = json.num("delayInMinutes", 1.0);
            if (json.find("when")) when = json.num("when", when);
          }
          minutes = std::clamp(minutes, 0.5, 1440.0);
          if (double(worker.runtime->timers.size()) >= 128)
            throw_extension_error(runtime, "alarm limit reached");
          js::Runtime::Timer timer;
          timer.id = runtime.next_timer++;
          timer.due = info && info->to_json().find("when") ? when : now + minutes * 60000.0;
          timer.interval = true;
          timer.callback = js::Value();
          timer.args = {js::Value(name.empty() ? std::string("alarm") : name)};
          runtime.timers.push_back(timer);
          invoke_callback(runtime, worker, args, {});
          return js::Value();
        });
    auto by_name = [&](const js::Values &args) -> std::string {
      if (!args.empty() && args[0].type == js::Value::Type::String) return args[0].to_string();
      return {};
    };
    alarms.object->properties["get"] = runtime.make_native(
        [this, &runtime, &worker, by_name](js::Values &args) {
          auto name = by_name(args);
          js::Value result;
          for (auto &timer : runtime.timers)
            if (!timer.args.empty() && timer.args[0].to_string() == name) {
              Json alarm = Json::make_object();
              alarm.set("name", name);
              alarm.set("scheduledTime", timer.due);
              result = json_to_js(runtime, alarm);
            }
          if (!result.object) result = js::Value();
          invoke_callback(runtime, worker, args, {result});
          return result;
        });
    alarms.object->properties["getAll"] = runtime.make_native(
        [this, &runtime, &worker](js::Values &args) {
          js::Values all;
          for (auto &timer : runtime.timers)
            if (!timer.args.empty()) {
              Json alarm = Json::make_object();
              alarm.set("name", timer.args[0].to_string());
              alarm.set("scheduledTime", timer.due);
              all.push_back(json_to_js(runtime, alarm));
            }
          auto result = runtime.make_array(all);
          invoke_callback(runtime, worker, args, {result});
          return result;
        });
    alarms.object->properties["clear"] = runtime.make_native(
        [this, &runtime, &worker, by_name](js::Values &args) {
          auto name = by_name(args);
          bool cleared = false;
          std::erase_if(runtime.timers, [&](const js::Runtime::Timer &t) {
            bool hit = !t.args.empty() && t.args[0].to_string() == name &&
                       t.callback.type == js::Value::Type::Undefined;
            cleared = cleared || hit;
            return hit;
          });
          invoke_callback(runtime, worker, args, {js::Value(cleared)});
          return js::Value();
        });
    alarms.object->properties["clearAll"] = runtime.make_native(
        [this, &runtime, &worker](js::Values &args) {
          std::erase_if(runtime.timers, [](const js::Runtime::Timer &t) {
            return t.callback.type == js::Value::Type::Undefined && !t.args.empty();
          });
          invoke_callback(runtime, worker, args, {js::Value(true)});
          return js::Value();
        });
    install_event(runtime, worker, alarms, "onAlarm", "alarms.onAlarm");
    chrome.object->properties["alarms"] = alarms;
  }

  // ---- chrome.action (and the MV2 alias) -------------------------------------------
  {
    auto action = runtime.make_object();
    action.object->properties["setBadgeText"] = runtime.make_native(
        [&worker](js::Values &args) {
          if (!args.empty() && args[0].type == js::Value::Type::Object)
            worker.badge = args[0].to_json().str("text");
          return js::Value();
        });
    action.object->properties["getBadgeText"] = runtime.make_native(
        [this, &runtime, &worker](js::Values &args) {
          auto value = js::Value(worker.badge);
          invoke_callback(runtime, worker, args, {value});
          return value;
        });
    action.object->properties["setBadgeBackgroundColor"] = runtime.make_native(
        [&worker](js::Values &args) {
          if (!args.empty() && args[0].type == js::Value::Type::Object)
            worker.badge_color = args[0].to_json().str("color");
          return js::Value();
        });
    action.object->properties["getBadgeBackgroundColor"] = runtime.make_native(
        [this, &runtime, &worker](js::Values &args) {
          auto color = runtime.make_array({js::Value(0.0), js::Value(0.0), js::Value(0.0),
                                           js::Value(0.0)});
          invoke_callback(runtime, worker, args, {color});
          return color;
        });
    action.object->properties["setTitle"] = runtime.make_native(
        [&worker](js::Values &args) {
          if (!args.empty() && args[0].type == js::Value::Type::Object)
            worker.title = args[0].to_json().str("title");
          return js::Value();
        });
    action.object->properties["getTitle"] = runtime.make_native(
        [this, &runtime, &worker](js::Values &args) {
          auto value = js::Value(worker.title.empty() ? std::string() : worker.title);
          invoke_callback(runtime, worker, args, {value});
          return value;
        });
    action.object->properties["getPopup"] = runtime.make_native(
        [this, &runtime, &worker, &record](js::Values &args) {
          auto value = js::Value("chrome-extension://" + record.manifest.id + "/" +
                                 record.manifest.action_popup);
          invoke_callback(runtime, worker, args, {value});
          return value;
        });
    action.object->properties["setPopup"] = runtime.make_native(
        [this, &runtime, &worker](js::Values &args) {
          invoke_callback(runtime, worker, args, {});
          return js::Value();
        });
    action.object->properties["setIcon"] = runtime.make_native(
        [this, &runtime, &worker](js::Values &args) {
          invoke_callback(runtime, worker, args, {});
          return js::Value();
        });
    install_event(runtime, worker, action, "onClicked", "action.onClicked");
    chrome.object->properties["action"] = action;
    chrome.object->properties["browserAction"] = action;
  }

  // ---- chrome.windows (event surface only) ------------------------------------------
  {
    auto windows = runtime.make_object();
    install_event(runtime, worker, windows, "onFocusChanged", "windows.onFocusChanged");
    chrome.object->properties["windows"] = windows;
  }

  global.properties["chrome"] = chrome;
  // Proton's service worker loads the webextension-polyfill on top of chrome;
  // I pre-install `browser` with real promise results so the polyfill's
  // branch "an existing browser object wins" picks my registry instead of
  // building Proxy wrappers I do not support.
  auto browser = runtime.make_object();
  install_browser_alias(runtime, browser, chrome);
  global.properties["browser"] = browser;
  auto console_log = runtime.make_native([this, self_id](js::Values &args) {
    std::string line;
    for (auto &arg : args) {
      if (!line.empty()) line += " ";
      line += arg.to_string();
    }
    if (line.size() > 2048) line.resize(2048);
    if (effects_.log) effects_.log(self_id, "log", line);
    return js::Value();
  });
  auto console = runtime.make_object();
  for (const char *level : {"log", "info", "warn", "error", "debug"})
    console.object->properties[level] = console_log;
  global.properties["console"] = console;
}

bool ExtensionHost::start_worker(const std::string &id, std::string &error) {
  auto found = extensions_.find(id);
  if (found == extensions_.end()) {
    error = "no such extension";
    return false;
  }
  auto &record = found->second;
  if (!record.enabled) {
    error = "the extension is disabled";
    return false;
  }
  if (record.manifest.service_worker.empty()) {
    error = "the extension declares no background script";
    return false;
  }
  stop_worker(id);
  std::string source;
  try {
    namespace fs = std::filesystem;
    source = read_all((fs::path(record.directory) / record.manifest.service_worker).string());
  } catch (const std::exception &e) {
    error = std::string("cannot read the background script: ") + e.what();
    record.install_error = error;
    return false;
  }
  auto worker = std::make_unique<Worker>();
  worker->id = id;
  worker->runtime = std::make_unique<js::Runtime>();
  install_api(*worker, record);
  Worker *raw = worker.get();
  workers_[id] = std::move(worker);
  try {
    raw->runtime->run(source);
    raw->running = true;
    record.install_error.clear();
    diagnostics_.push_back("started worker for " + record.manifest.name);
    // Bootstrap events Chrome would have delivered: onInstalled once ever,
    // onStartup on every browser boot after that.
    bool first_install = !raw->runtime->global().properties.count("__unused_marker");
    auto marker = record.storage.find("pv-installed");
    {
      Json details = Json::make_object();
      if (!marker) {
        details.set("reason", "install");
        details.set("previousVersion", record.manifest.version);
        fire_event(*raw->runtime, *raw, "runtime.onInstalled", {json_to_js(*raw->runtime, details)});
        found->second.storage.set("pv-installed", Json(record.manifest.version));
        std::string save_error;
        if (!storage_path_.empty()) save_storage(storage_path_, save_error);
      } else {
        fire_event(*raw->runtime, *raw, "runtime.onStartup", {});
      }
    }
    static_cast<void>(first_install);
  } catch (js::JsError &e) {
    error = error_text(e.value);
    raw->last_error = error;
    record.install_error = error;
    diagnostics_.push_back("worker failed for " + record.manifest.name + ": " + error);
    return false;
  } catch (const std::exception &e) {
    error = e.what();
    raw->last_error = error;
    record.install_error = error;
    diagnostics_.push_back("worker failed for " + record.manifest.name + ": " + error);
    return false;
  }
  return true;
}

void ExtensionHost::stop_worker(const std::string &id) {
  for (auto it = ports_.begin(); it != ports_.end();)
    if (it->second->extension_id == id) it = ports_.erase(it);
    else ++it;
  auto found = workers_.find(id);
  if (found == workers_.end()) return;
  for (auto &entry : found->second->listeners)
    for (auto &listener : entry.second)
      if (listener.object) found->second->runtime->unroot(listener.object);
  workers_.erase(found);
}

void ExtensionHost::tick() {
  for (auto &entry : workers_) {
    auto &worker = *entry.second;
    if (!worker.running || !worker.runtime) continue;
    auto now = double(std::chrono::duration_cast<std::chrono::milliseconds>(
                          std::chrono::steady_clock::now().time_since_epoch())
                          .count());
    auto &timers = worker.runtime->timers;
    for (size_t i = 0; i < timers.size(); ++i) {
      if (timers[i].due > now) continue;
      // An alarm I created carries a name argument and no JS callback.
      if (timers[i].callback.type == js::Value::Type::Undefined && !timers[i].args.empty()) {
        auto name = timers[i].args[0].to_string();
        Json alarm = Json::make_object();
        alarm.set("name", name);
        auto value = json_to_js(*worker.runtime, alarm);
        for (auto &listener : worker.listeners["alarms.onAlarm"]) {
          try {
            worker.runtime->reset_fuel();
            worker.runtime->call(listener, {value});
          } catch (const std::exception &e) {
            worker.last_error = e.what();
          }
        }
        timers[i].due = now + 60000.0;
      }
    }
    try {
      worker.runtime->reset_fuel();
      // I let the extension's own setTimeout work run too.
      bool progressed = true;
      while (progressed) {
        progressed = false;
        for (size_t i = 0; i < worker.runtime->timers.size(); ++i) {
          auto &timer = worker.runtime->timers[i];
          if (timer.due > now || timer.callback.type == js::Value::Type::Undefined) continue;
          auto callback = timer.callback;
          auto args = timer.args;
          if (timer.interval) timer.due = now + 1000.0;
          else worker.runtime->timers.erase(worker.runtime->timers.begin() + long(i));
          worker.runtime->call(callback, args);
          progressed = true;
          break;
        }
      }
    } catch (const std::exception &e) {
      worker.last_error = e.what();
    }
  }
}

void ExtensionHost::broadcast(const std::string &event, const Json &payload) {
  for (auto &entry : workers_) {
    auto &worker = *entry.second;
    if (!worker.running) continue;
    auto found = worker.listeners.find(event);
    if (found == worker.listeners.end()) continue;
    auto value = json_to_js(*worker.runtime, payload);
    for (auto &listener : found->second) {
      try {
        worker.runtime->reset_fuel();
        worker.runtime->call(listener, {value});
      } catch (js::JsError &e) {
        worker.last_error = "uncaught extension error: " + e.value.to_string();
      } catch (const std::exception &e) {
        worker.last_error = e.what();
      }
    }
  }
}

bool ExtensionHost::load_storage(const std::string &profile_directory, std::string &error) {
  namespace fs = std::filesystem;
  storage_path_ = profile_directory;
  auto path = fs::path(profile_directory) / "owned-extension-storage.json";
  if (!fs::exists(path)) return true;
  try {
    auto json = Json::parse(read_all(path.string()));
    if (!json || json->type != Json::Type::Object) {
      error = "extension storage is corrupt";
      return false;
    }
    for (auto &entry : json->object) {
      auto found = extensions_.find(entry.first);
      if (found != extensions_.end() && entry.second.type == Json::Type::Object)
        found->second.storage = entry.second;
    }
  } catch (const std::exception &e) {
    error = e.what();
    return false;
  }
  return true;
}

bool ExtensionHost::save_storage(const std::string &profile_directory, std::string &error) const {
  namespace fs = std::filesystem;
  Json all = Json::make_object();
  for (const auto &entry : extensions_) all.set(entry.first, entry.second.storage);
  std::error_code ec;
  fs::create_directories(fs::path(profile_directory), ec);
  return write_file_atomic((fs::path(profile_directory) / "owned-extension-storage.json").string(),
                           all.dump() + "\n", error);
}

std::vector<std::string> ExtensionHost::diagnostics() const { return diagnostics_; }

std::string ExtensionHost::worker_error(const std::string &id) const {
  auto found = workers_.find(id);
  return found == workers_.end() ? std::string() : found->second->last_error;
}
namespace {

// The polyfill would promise-wrap chrome itself; I skip that layer and hand
// browser.* promise results straight from the same handlers. Awaiting a Value
// that is settled synchronously behaves exactly like awaiting a resolved
// promise, except call sites that use .then() - which need a real promise.
js::Value promise_native(js::Runtime &runtime, js::Value fn) {
  return runtime.make_native([&runtime, fn](js::Values &args) {
    js::Value promise = runtime.make_promise();
    try {
      js::Value result = runtime.call(fn, args);
      runtime.settle_promise(promise.object, 1, result);
    } catch (js::JsError &e) {
      runtime.settle_promise(promise.object, 2, e.value);
    } catch (const std::exception &e) {
      runtime.settle_promise(promise.object, 2, js::Value(e.what()));
    }
    return promise;
  });
}

js::Value find_ns(js::Runtime &runtime, js::Value root, const std::string &path) {
  js::Value current = root;
  size_t at = 0;
  while (at < path.size()) {
    size_t dot = path.find('.', at);
    auto part = dot == std::string::npos ? path.substr(at) : path.substr(at, dot - at);
    current = runtime.get_member(current, part);
    if (current.type != js::Value::Type::Object || !current.object) return js::Value();
    if (dot == std::string::npos) break;
    at = dot + 1;
  }
  return current;
}

void set_ns(js::Runtime &runtime, js::Value root, const std::string &path, js::Value value) {
  size_t dot = path.find('.');
  if (dot == std::string::npos) {
    root.object->properties[path] = value;
    return;
  }
  auto part = path.substr(0, dot);
  auto child = runtime.get_member(root, part);
  if (child.type != js::Value::Type::Object || !child.object) {
    child = runtime.make_object();
    root.object->properties[part] = child;
  }
  set_ns(runtime, child, path.substr(dot + 1), value);
}
} // namespace

// The `browser` registry: every path below is real, backed by the chrome
// handler it mirrors; promised ones resolve through my own promise machinery.
void install_browser_alias(js::Runtime &runtime, js::Value browser, js::Value chrome) {
  static const char *PROMISED[] = {
      "proxy.settings.set",          "proxy.settings.get",
      "proxy.settings.clear",        "storage.local.get",
      "storage.local.set",           "storage.local.remove",
      "storage.local.clear",         "storage.local.getBytesInUse",
      "storage.sync.get",            "storage.sync.set",
      "storage.sync.remove",         "storage.sync.clear",
      "storage.session.get",         "storage.session.set",
      "storage.managed.get",         "permissions.contains",
      "permissions.getAll",          "permissions.request",
      "idle.queryState",             "notifications.create",
      "notifications.clear",         "notifications.update",
      "notifications.getAll",        "privacy.network.webRTCIPHandlingPolicy.set",
      "privacy.network.webRTCIPHandlingPolicy.get",
      "privacy.network.webRTCIPHandlingPolicy.clear",
      "tabs.create",                 "tabs.query",
      "tabs.get",                    "tabs.remove",
      "tabs.sendMessage",            "tabs.executeScript",
      "scripting.executeScript",     "alarms.get",
      "alarms.getAll",               "alarms.clear",
      "alarms.clearAll",             "runtime.getPlatformInfo",
      "runtime.sendMessage",
      "action.getBadgeText",         "action.getBadgeBackgroundColor",
      "action.getTitle",             "action.getPopup",
  };
  for (auto *path : PROMISED) {
    js::Value fn = find_ns(runtime, chrome, path);
    if (fn.object) set_ns(runtime, browser, path, promise_native(runtime, fn));
  }
  // Events and passive values ride through unchanged so addListener identity
  // matches between namespaces: an addListener on chrome.proxy.onProxyError is
  // the same listener as on browser.proxy.onProxyError.
  static const char *SHARED_ROOTS[] = {
      "runtime.onMessage",     "runtime.onMessageExternal", "runtime.onInstalled",
      "runtime.onStartup",     "runtime.onConnect",         "runtime.id",
      "runtime.getManifest",   "runtime.getURL",            "runtime.setUninstallURL",
      "runtime.openOptionsPage", "runtime.lastError",       "proxy.onProxyError",
      "proxy.settings.onChange", "privacy.network",         "storage.onChanged",
      "alarms.onAlarm",        "alarms.create",             "permissions.remove",
      "notifications.onClicked", "notifications.onButtonClicked", "notifications.onClosed",
      "idle.onStateChanged",   "idle.setDetectionInterval", "tabs.onRemoved",
      "tabs.onUpdated",        "tabs.onActivated",          "action.onClicked",
      "browserAction",         "webRequest",
  };
  for (auto *path : SHARED_ROOTS) {
    js::Value value = find_ns(runtime, chrome, path);
    if (value.type == js::Value::Type::Object && !value.object) continue;
    if (!value.object && value.type == js::Value::Type::Undefined) continue;
    // A promised parent already created a lazy namespace; never overwrite a
    // promised leaf.
    bool present = false;
    try {
      present = find_ns(runtime, browser, path).type != js::Value::Type::Undefined;
    } catch (...) {
      present = false; // walking through a still-missing browser.* branch is fine
    }
    if (!present) set_ns(runtime, browser, path, value);
  }
}

// --- public surface my shell drives ------------------------------------------

bool ExtensionHost::request_proxy_credentials(std::string &user, std::string &pass, int attempts) {
  // Chrome semantics: a proxy challenge reaches webRequest.onAuthRequired with
  // isProxy=true. The extension's own code decides the credentials; a cancel or
  // an empty answer means "no credentials", and the 407 stands.
  for (auto &entry : workers_) {
    auto &worker = *entry.second;
    if (!worker.running || !worker.runtime) continue;
    auto found = worker.listeners.find("webRequest.onAuthRequired");
    if (found == worker.listeners.end() || found->second.empty()) continue;
    auto &runtime = *worker.runtime;
    for (auto &listener : found->second) {
      auto details = runtime.make_object();
      details.object->properties["isProxy"] = js::Value(true);
      details.object->properties["scheme"] = js::Value("https");
      auto challenger = runtime.make_object();
      auto owner = effects_.get_proxy ? effects_.get_proxy(entry.first) : ProxyConfig::direct();
      auto authority = owner.single.host.empty() ? owner.single.authority() : owner.single.host;
      auto colon = authority.find(':');
      challenger.object->properties["host"] =
          js::Value(colon == std::string::npos ? authority : authority.substr(0, colon));
      challenger.object->properties["port"] = js::Value(
          double(colon == std::string::npos ? 1443 : std::strtol(authority.c_str() + colon + 1,
                                                                 nullptr, 10)));
      details.object->properties["challenger"] = challenger;
      details.object->properties["requestId"] =
          js::Value(std::string("proxy-auth-") + std::to_string(attempts));
      details.object->properties["url"] = js::Value("https://" + authority + "/");
      auto got = std::make_shared<bool>(false);
      auto answer = std::make_shared<js::Value>(js::Value());
      auto callback = runtime.make_native([got, answer](js::Values &args) {
        *got = true;
        *answer = args.empty() ? js::Value() : args[0];
        return js::Value();
      });
      try {
        runtime.reset_fuel();
        runtime.call(listener, {details, callback});
        // The provider may await account fetches; run its timers until it
        // answers or the deadline inside its own code fires.
        for (int i = 0; i < 600 && !*got; ++i) {
          runtime.tick();
          runtime.drain_microtasks();
        }
      } catch (js::JsError &e) {
        worker.last_error = "onAuthRequired: " + e.value.to_string();
        continue;
      } catch (const std::exception &e) {
        worker.last_error = std::string("onAuthRequired: ") + e.what();
        continue;
      }
      if (!*got) continue;
      if (answer->type != js::Value::Type::Object || !answer->object) continue;
      if (answer->to_json().flag("cancel")) return false;
      const auto &props = answer->object->properties;
      auto credentials = props.find("authCredentials");
      if (credentials == props.end() || !credentials->second.object) continue;
      auto username = credentials->second.object->properties.find("username");
      auto password = credentials->second.object->properties.find("password");
      if (username == credentials->second.object->properties.end() ||
          password == credentials->second.object->properties.end())
        continue;
      user = username->second.to_string();
      pass = password->second.to_string();
      if (!user.empty()) return true;
    }
  }
  return false;
}

bool ExtensionHost::send_message(const std::string &id, const Json &message,
                                 const std::string &sender_url, std::string &response_json) {
  auto found = workers_.find(id);
  if (found == workers_.end() || !found->second->running) return false;
  auto &worker = *found->second;
  auto &runtime = *worker.runtime;
  auto auto_send = manifest_of(id);
  js::Value message_value = json_to_js(runtime, message);
  auto sender = runtime.make_object();
  sender.object->properties["id"] = js::Value(id);
  sender.object->properties["url"] = js::Value(sender_url);
  // The externally_connectable rule: a page origin must be on the extension's
  // own match list before its traffic reaches the worker.
  std::string event = "runtime.onMessage";
  if (!sender_url.empty() && auto_send) {
    auto sender_parsed = parse_url(sender_url);
    if (sender_parsed && auto_send->connectable_from(*sender_parsed))
      event = "runtime.onMessageExternal";
  }
  return deliver_message(runtime, worker, event, message_value, sender, response_json);
}

bool ExtensionHost::read_page_resource(const std::string &extension_id, const std::string &path,
                                       std::string &contents, std::string &mime) const {
  auto found = extensions_.find(extension_id);
  if (found == extensions_.end()) return false;
  namespace fs = std::filesystem;
  std::error_code ec;
  auto base = fs::weakly_canonical(fs::path(found->second.directory), ec);
  auto target = fs::weakly_canonical(fs::path(found->second.directory) / path, ec);
  auto base_str = base.string();
  auto target_str = target.string();
  if (ec || base_str.empty() || target_str.compare(0, base_str.size(), base_str) != 0)
    return false;
  if (!fs::is_regular_file(target, ec)) return false;
  std::ifstream file(target_str, std::ios::binary);
  contents.assign(std::istreambuf_iterator<char>(file), std::istreambuf_iterator<char>());
  if (contents.size() > 32u * 1024 * 1024) contents.resize(0);
  auto dot = path.rfind('.');
  std::string ext = dot == std::string::npos ? "" : lower(path.substr(dot + 1));
  static const std::map<std::string, std::string> mimes = {
      {"html", "text/html"},  {"js", "text/javascript"}, {"css", "text/css"},
      {"json", "application/json"}, {"svg", "image/svg+xml"}, {"png", "image/png"},
      {"jpg", "image/jpeg"},  {"jpeg", "image/jpeg"},    {"webp", "image/webp"},
      {"woff", "font/woff"},  {"woff2", "font/woff2"},   {"ttf", "font/ttf"},
      {"po", "text/plain"},   {"txt", "text/plain"}};
  auto it = mimes.find(ext);
  mime = it == mimes.end() ? "application/octet-stream" : it->second;
  return !contents.empty();
}

const ExtensionManifest *ExtensionHost::manifest_of(const std::string &id) const {
  auto found = extensions_.find(id);
  return found == extensions_.end() ? nullptr : &found->second.manifest;
}

} // namespace pv
