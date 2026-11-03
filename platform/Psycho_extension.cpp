#include "Psycho_extension.hpp"
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
void call_callback(js::Runtime &runtime, js::Values &args, const js::Values &result = {}) {
  for (size_t i = args.size(); i-- > 0;) {
    if (args[i].type == js::Value::Type::Object && args[i].object &&
        args[i].object->kind == js::Object::Kind::Function) {
      runtime.call(args[i], result);
      return;
    }
  }
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
  manifest.valid = true;
  return manifest;
}

struct ExtensionHost::Worker {
  std::unique_ptr<js::Runtime> runtime;
  std::string id;
  std::map<std::string, std::vector<js::Value>> listeners;
  std::string badge, title;
  std::string webrtc_policy = "default";
  std::string proxy_user, proxy_pass;
  bool has_proxy_auth = false;
  bool running = false;
  std::string last_error;
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
  manifest.id = id;
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

void ExtensionHost::install_api(Worker &worker, const ExtensionRecord &record) {
  auto &runtime = *worker.runtime;
  auto &global = runtime.global();
  auto chrome = runtime.make_object();
  auto self_id = record.manifest.id;
  auto manifest_json = [&]() {
    Json j = Json::make_object();
    j.set("manifest_version", record.manifest.manifest_version);
    j.set("name", record.manifest.name);
    j.set("version", record.manifest.version);
    j.set("description", record.manifest.description);
    Json perms = Json::make_array();
    for (const auto &p : record.manifest.permissions) perms.array.push_back(Json(p));
    j.set("permissions", std::move(perms));
    return j;
  }();


  // ---- chrome.runtime -----------------------------------------------------
  {
    auto object = runtime.make_object();
    object.object->properties["id"] = js::Value(self_id);
    object.object->properties["getManifest"] =
        runtime.make_native([&runtime, manifest_json](js::Values &) {
          return json_to_js(runtime, manifest_json);
        });
    object.object->properties["getURL"] =
        runtime.make_native([self_id](js::Values &args) {
          auto path = args.empty() ? std::string() : args[0].to_string();
          if (!path.empty() && path.front() == '/') path.erase(0, 1);
          return js::Value("chrome-extension://" + self_id + "/" + path);
        });
    object.object->properties["getPlatformInfo"] =
        runtime.make_native([&runtime](js::Values &args) {
          Json info = Json::make_object();
          info.set("os", "win");
          info.set("arch", "x86-64");
          info.set("nacl_arch", "x86-64");
          auto value = json_to_js(runtime, info);
          if (!args.empty()) runtime.call(args[0], {value});
          return value;
        });
    auto on_message = runtime.make_object();
    on_message.object->properties["addListener"] = runtime.make_native(
        [&worker, &runtime](js::Values &args) {
          if (!args.empty() && worker.listeners["runtime.onMessage"].size() < 32) {
            worker.listeners["runtime.onMessage"].push_back(args[0]);
            runtime.root(args[0].object);
          }
          return js::Value();
        });
    object.object->properties["onMessage"] = on_message;
    object.object->properties["sendMessage"] = runtime.make_native(
        [this, &runtime, self_id](js::Values &args) {
          if (args.empty()) return js::Value();
          auto payload = args[0].to_json();
          // I only deliver within the same extension. Cross-extension messaging
          // needs an explicit externally_connectable grant I have not added.
          auto found = workers_.find(self_id);
          if (found != workers_.end()) {
            auto message = json_to_js(runtime, payload);
            for (auto &listener : found->second->listeners["runtime.onMessage"])
              runtime.call(listener, {message});
          }
          return js::Value();
        });
    chrome.object->properties["runtime"] = object;
  }

  // ---- chrome.storage.local ----------------------------------------------
  {
    auto storage = runtime.make_object();
    auto local = runtime.make_object();
    auto guard = [this, &runtime, self_id](void) {
      auto found = extensions_.find(self_id);
      std::string error;
      if (found == extensions_.end() || !grant(found->second, "storage", error))
        throw_extension_error(runtime, error);
      return found;
    };
    local.object->properties["set"] = runtime.make_native(
        [this, &runtime, guard](js::Values &args) {
          auto it = guard();
          if (args.empty() || args[0].type != js::Value::Type::Object) return js::Value();
          auto incoming = args[0].to_json();
          if (incoming.type != Json::Type::Object) return js::Value();
          size_t total = it->second.storage.object.size();
          if (total + incoming.object.size() > 8192)
            throw_extension_error(runtime, "extension storage item limit reached");
          for (auto &entry : incoming.object) it->second.storage.set(entry.first, entry.second);
          std::string error;
          if (!storage_path_.empty()) save_storage(storage_path_, error);
          call_callback(runtime, args);
          return js::Value();
        });
    local.object->properties["get"] = runtime.make_native(
        [&runtime, guard](js::Values &args) {
          auto it = guard();
          Json result = Json::make_object();
          if (args.empty() || args[0].type == js::Value::Type::Null) {
            result = it->second.storage;
          } else if (args[0].type == js::Value::Type::String) {
            auto key = args[0].to_string();
            const Json *value = it->second.storage.find(key);
            if (value) result.set(key, *value);
          } else if (args[0].type == js::Value::Type::Object) {
            // An object argument supplies defaults.
            auto defaults = args[0].to_json();
            for (auto &entry : defaults.object) {
              const Json *value = it->second.storage.find(entry.first);
              result.set(entry.first, value ? *value : entry.second);
            }
          }
          auto value = json_to_js(runtime, result);
          call_callback(runtime, args, {value});
          return value;
        });
    local.object->properties["remove"] = runtime.make_native(
        [this, &runtime, guard](js::Values &args) {
          auto it = guard();
          if (!args.empty()) {
            auto keys = args[0].type == js::Value::Type::String
                            ? std::vector<std::string>{args[0].to_string()}
                            : std::vector<std::string>{};
            if (args[0].type == js::Value::Type::Object) {
              auto json = args[0].to_json();
              for (size_t i = 0; i < json.array.size(); ++i)
                keys.push_back(json.array[i].string);
            }
            auto &entries = it->second.storage.object;
            for (const auto &key : keys)
              entries.erase(std::remove_if(entries.begin(), entries.end(),
                                           [&](const std::pair<std::string, Json> &e) {
                                             return e.first == key;
                                           }),
                            entries.end());
          }
          std::string error;
          if (!storage_path_.empty()) save_storage(storage_path_, error);
          call_callback(runtime, args);
          return js::Value();
        });
    local.object->properties["clear"] = runtime.make_native(
        [this, &runtime, guard](js::Values &args) {
          auto it = guard();
          it->second.storage = Json::make_object();
          std::string error;
          if (!storage_path_.empty()) save_storage(storage_path_, error);
          call_callback(runtime, args);
          return js::Value();
        });
    storage.object->properties["local"] = local;
    storage.object->properties["session"] = local;
    chrome.object->properties["storage"] = storage;
  }

  // ---- chrome.proxy -------------------------------------------------------
  {
    auto proxy = runtime.make_object();
    auto settings = runtime.make_object();
    settings.object->properties["set"] = runtime.make_native(
        [this, &runtime, self_id](js::Values &args) {
          auto found = extensions_.find(self_id);
          std::string error;
          if (found == extensions_.end() || !grant(found->second, "proxy", error))
            throw_extension_error(runtime, error);
          if (args.empty() || args[0].type != js::Value::Type::Object)
            throw_extension_error(runtime, "proxy.settings.set needs a details object");
          auto details = args[0].to_json();
          const Json *value = details.find("value");
          if (!value) throw_extension_error(runtime, "proxy.settings.set needs details.value");
          Json input = *value;
          input.set("ownerExtension", self_id);
          auto config = ProxyConfig::from_json(input, error);
          if (!config) throw_extension_error(runtime, error);
          if (!effects_.set_proxy || !effects_.set_proxy(self_id, *config, error))
            throw_extension_error(runtime, error.empty() ? "the browser refused the proxy" : error);
          if (effects_.log)
            effects_.log(self_id, "proxy", "set mode " + config->mode_name());
          return js::Value();
        });
    settings.object->properties["get"] = runtime.make_native(
        [this, &runtime, self_id](js::Values &args) {
          auto found = extensions_.find(self_id);
          std::string error;
          if (found == extensions_.end() || !grant(found->second, "proxy", error))
            throw_extension_error(runtime, error);
          Json result = Json::make_object();
          auto config = effects_.get_proxy ? effects_.get_proxy(self_id) : ProxyConfig::direct();
          result.set("value", config.to_json());
          result.set("levelOfControl",
                     config.owner_extension == self_id ? "controlled_by_this_extension"
                                                       : "controllable_by_this_extension");
          auto value = json_to_js(runtime, result);
          call_callback(runtime, args, {value});
          return value;
        });
    settings.object->properties["clear"] = runtime.make_native(
        [this, &runtime, self_id](js::Values &args) {
          auto found = extensions_.find(self_id);
          std::string error;
          if (found == extensions_.end() || !grant(found->second, "proxy", error))
            throw_extension_error(runtime, error);
          if (effects_.clear_proxy && !effects_.clear_proxy(self_id, error))
            throw_extension_error(runtime, error);
          call_callback(runtime, args);
          return js::Value();
        });
    proxy.object->properties["settings"] = settings;
    auto on_error = runtime.make_object();
    on_error.object->properties["addListener"] = runtime.make_native(
        [&worker, &runtime](js::Values &args) {
          if (!args.empty() && worker.listeners["proxy.onProxyError"].size() < 8) {
            worker.listeners["proxy.onProxyError"].push_back(args[0]);
            runtime.root(args[0].object);
          }
          return js::Value();
        });
    proxy.object->properties["onProxyError"] = on_error;
    chrome.object->properties["proxy"] = proxy;
  }

  // ---- chrome.webRequest.onAuthRequired ----------------------------------
  {
    auto web_request = runtime.make_object();
    auto on_auth = runtime.make_object();
    on_auth.object->properties["addListener"] = runtime.make_native(
        [this, &worker, &runtime, self_id](js::Values &args) {
          auto found = extensions_.find(self_id);
          std::string error;
          if (found == extensions_.end() || !grant(found->second, "webRequest", error))
            throw_extension_error(runtime, error);
          if (!grant(found->second, "webRequestAuthProvider", error))
            throw_extension_error(runtime, error);
          if (args.empty()) return js::Value();
          // I keep the provider and call it only for a proxy challenge. I never
          // hand website credentials to an extension.
          worker.listeners["webRequest.onAuthRequired"].clear();
          worker.listeners["webRequest.onAuthRequired"].push_back(args[0]);
          runtime.root(args[0].object);
          return js::Value();
        });
    web_request.object->properties["onAuthRequired"] = on_auth;
    chrome.object->properties["webRequest"] = web_request;
  }

  // ---- chrome.privacy -----------------------------------------------------
  {
    auto privacy = runtime.make_object();
    auto network = runtime.make_object();
    auto policy = runtime.make_object();
    policy.object->properties["set"] = runtime.make_native(
        [this, &worker, &runtime, self_id](js::Values &args) {
          auto found = extensions_.find(self_id);
          std::string error;
          if (found == extensions_.end() || !grant(found->second, "privacy", error))
            throw_extension_error(runtime, error);
          if (args.empty() || args[0].type != js::Value::Type::Object)
            throw_extension_error(runtime, "webRTCIPHandlingPolicy.set needs a details object");
          auto details = args[0].to_json();
          auto value = details.str("value");
          static const std::set<std::string> allowed = {
              "default", "default_public_and_private_interfaces",
              "default_public_interface_only", "disable_non_proxied_udp",
              "proxy_only"};
          if (!allowed.count(value))
            throw_extension_error(runtime, "unsupported WebRTC policy: " + value);
          worker.webrtc_policy = value;
          if (effects_.set_webrtc_policy &&
              !effects_.set_webrtc_policy(self_id, value, error))
            throw_extension_error(runtime, error);
          return js::Value();
        });
    policy.object->properties["get"] = runtime.make_native(
        [&worker, &runtime](js::Values &args) {
          Json result = Json::make_object();
          result.set("value", worker.webrtc_policy);
          auto value = json_to_js(runtime, result);
          call_callback(runtime, args, {value});
          return value;
        });
    network.object->properties["webRTCIPHandlingPolicy"] = policy;
    privacy.object->properties["network"] = network;
    chrome.object->properties["privacy"] = privacy;
  }

  // ---- chrome.alarms ------------------------------------------------------
  {
    auto alarms = runtime.make_object();
    alarms.object->properties["create"] = runtime.make_native(
        [this, &runtime, self_id](js::Values &args) {
          auto found = extensions_.find(self_id);
          std::string error;
          if (found == extensions_.end() || !grant(found->second, "alarms", error))
            throw_extension_error(runtime, error);
          if (args.empty()) return js::Value();
          std::string name;
          const js::Value *info = nullptr;
          if (args[0].type == js::Value::Type::String) {
            name = args[0].to_string();
            if (args.size() > 1) info = &args[1];
          } else {
            info = &args[0];
          }
          double minutes = 1.0;
          if (info && info->type == js::Value::Type::Object) {
            auto json = info->to_json();
            if (json.find("periodInMinutes")) minutes = json.num("periodInMinutes", 1.0);
            else if (json.find("delayInMinutes")) minutes = json.num("delayInMinutes", 1.0);
          }
          // I floor alarms at 30 seconds so an extension cannot spin my broker.
          minutes = std::clamp(minutes, 0.5, 1440.0);
          js::Runtime::Timer timer;
          timer.id = runtime.next_timer++;
          timer.due = double(std::chrono::duration_cast<std::chrono::milliseconds>(
                            std::chrono::steady_clock::now().time_since_epoch())
                            .count()) + minutes * 60000.0;
          timer.interval = true;
          // I dispatch through the registered onAlarm listeners rather than a
          // closure, so the extension sees a real chrome.alarms.onAlarm event.
          timer.callback = js::Value();
          timer.args = {js::Value(name)};
          runtime.timers.push_back(timer);
          return js::Value();
        });
    auto on_alarm = runtime.make_object();
    on_alarm.object->properties["addListener"] = runtime.make_native(
        [&worker, &runtime](js::Values &args) {
          if (!args.empty() && worker.listeners["alarms.onAlarm"].size() < 8) {
            worker.listeners["alarms.onAlarm"].push_back(args[0]);
            runtime.root(args[0].object);
          }
          return js::Value();
        });
    alarms.object->properties["onAlarm"] = on_alarm;
    chrome.object->properties["alarms"] = alarms;
  }

  // ---- chrome.action ------------------------------------------------------
  {
    auto action = runtime.make_object();
    action.object->properties["setBadgeText"] = runtime.make_native(
        [&worker, &runtime](js::Values &args) {
          if (!args.empty() && args[0].type == js::Value::Type::Object)
            worker.badge = args[0].to_json().str("text");
          call_callback(runtime, args);
          return js::Value();
        });
    action.object->properties["setTitle"] = runtime.make_native(
        [&worker, &runtime](js::Values &args) {
          if (!args.empty() && args[0].type == js::Value::Type::Object)
            worker.title = args[0].to_json().str("title");
          call_callback(runtime, args);
          return js::Value();
        });
    action.object->properties["setIcon"] = runtime.make_native(
        [&runtime](js::Values &args) {
          // I have not implemented extension icon rasterisation yet, so I accept
          // the call without claiming the icon changed.
          call_callback(runtime, args);
          return js::Value();
        });
    auto on_clicked = runtime.make_object();
    on_clicked.object->properties["addListener"] = runtime.make_native(
        [&worker, &runtime](js::Values &args) {
          if (!args.empty() && worker.listeners["action.onClicked"].size() < 8) {
            worker.listeners["action.onClicked"].push_back(args[0]);
            runtime.root(args[0].object);
          }
          return js::Value();
        });
    action.object->properties["onClicked"] = on_clicked;
    chrome.object->properties["action"] = action;
    chrome.object->properties["browserAction"] = action;
  }

  global.properties["chrome"] = chrome;
  global.properties["browser"] = chrome;
  auto console = runtime.make_object();
  auto log_fn = runtime.make_native([this, self_id](js::Values &args) {
    std::string message;
    for (auto &arg : args) {
      if (!message.empty()) message += " ";
      message += arg.to_string();
    }
    if (message.size() > 2048) message.resize(2048);
    if (effects_.log) effects_.log(self_id, "log", message);
    return js::Value();
  });
  console.object->properties["log"] = log_fn;
  console.object->properties["warn"] = log_fn;
  console.object->properties["error"] = log_fn;
  console.object->properties["info"] = log_fn;
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
  } catch (js::JsError &e) {
    error = e.value.to_string();
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
} // namespace pv
