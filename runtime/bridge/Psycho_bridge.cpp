// PsychoVault original source. See the header for the four laws - this file
// is how I keep them.
#include "Psycho_bridge.hpp"
#include <sstream>

namespace pv::rt {

namespace {

Error refuse(const char *code, const std::string &message) {
  Error error;
  error.code = code;
  error.message = message;
  return error;
}

// Structured cloning, one direction: Json becomes plain page-world objects.
// Only data survives the crossing - no prototypes, no functions, no cycles.
js::Value json_to_value(js::Runtime &world, const Json &json) {
  switch (json.type) {
  case Json::Type::Null: return js::Value::null();
  case Json::Type::Bool: return js::Value(json.boolean);
  case Json::Type::Number: return js::Value(json.number);
  case Json::Type::String: return js::Value(json.string);
  case Json::Type::Array: {
    js::Values items;
    for (const Json &item : json.array) items.push_back(json_to_value(world, item));
    return world.make_array(items);
  }
  case Json::Type::Object: {
    js::Value object = world.make_object();
    for (const auto &[key, value] : json.object)
      world.set_member(object, key, json_to_value(world, value));
    return object;
  }
  }
  return js::Value::undef();
}

// Wire names are colon-shaped on my bus ("settings:get"); the facade is
// dot-shaped in-page (psycho.settings.get). This is the one translation.
std::vector<std::string> split_method(const std::string &method) {
  std::vector<std::string> parts;
  std::string part;
  std::istringstream names(method);
  while (std::getline(names, part, ':')) parts.push_back(part);
  return parts;
}

} // namespace

PreloadBridge::PreloadBridge(Seams seams) : seams_(std::move(seams)) {}

bool PreloadBridge::expose(Channel channel, const std::string &method, Error &error) {
  error = Error{};
  if (!schemas_.knows(method)) {
    error = refuse("bridge.unknown-method",
                   "'" + method + "' is not a registered schema method; only " +
                       "the registry's names can exist on window.psycho");
    return false;
  }
  if (table_.count(method)) {
    error = refuse("bridge.duplicate", "'" + method + "' is already exposed");
    return false;
  }
  table_[method] = ExposedEntry{channel, method};
  return true;
}

bool PreloadBridge::run_preload(std::string_view source, Error &error) {
  error = Error{};
  // The preload world's only privileged door: name + JSON payload in, JSON
  // reply out, curation enforced one layer down in dispatch().
  preload_.global().properties["__psycho_call"] =
      preload_.make_native([this](js::Values &args) -> js::Value {
        const std::string name = args.empty() ? "" : args[0].to_string();
        const Json payload =
            args.size() < 2 ? Json::make_object() : args[1].to_json();
        Json reply;
        Error error;
        if (!dispatch(name, payload, reply, error))
          throw js::JsError{js::Value(std::string("BridgeError: ") + error.code)};
        return json_to_value(preload_, reply);
      });
  try {
    preload_.run(source);
  } catch (js::JsError &thrown) {
    error = refuse("bridge.preload-script",
                   "preload source threw: " + thrown.value.to_string());
    return false;
  }
  return true;
}

js::Value PreloadBridge::trampoline(js::Runtime &page_world,
                                    const std::string &method) {
  return page_world.make_native([this, &page_world, method](js::Values &args) -> js::Value {
    const Json payload = args.empty() ? Json::make_object() : args[0].to_json();
    Json reply;
    Error error;
    if (!dispatch(method, payload, reply, error))
      throw js::JsError{js::Value(std::string("BridgeError: ") + error.code)};
    return json_to_value(page_world, reply);
  });
}

void PreloadBridge::freeze_graph(js::Value object) const {
  if (object.type != js::Value::Type::Object || !object.object) return;
  for (const auto &[name, value] : object.object->properties) freeze_graph(value);
  object.object->frozen = true;
}

bool PreloadBridge::install(js::Runtime &page_world, Error &error) {
  error = Error{};
  js::Value root = page_world.make_object();
  for (const auto &[method, entry] : table_) {
    const std::vector<std::string> parts = split_method(method);
    if (parts.empty()) continue;
    js::Value node = root;
    for (size_t i = 0; i + 1 < parts.size(); ++i) {
      js::Value next = page_world.get_member(node, parts[i]);
      if (next.type != js::Value::Type::Object || !next.object) {
        next = page_world.make_object();
        page_world.set_member(node, parts[i], next);
      }
      node = next;
    }
    page_world.set_member(node, parts.back(), trampoline(page_world, method));
  }
  freeze_graph(root);
  // The page world's global IS its window: scripts see window.psycho.
  page_world.global().properties["psycho"] = root;
  return true;
}

bool PreloadBridge::dispatch(const std::string &method, const Json &payload,
                             Json &reply, Error &error) {
  error = Error{};
  auto entry = table_.find(method);
  if (entry == table_.end()) {
    error = refuse("bridge.unexposed",
                   "'" + method + "' is not exposed; curation is the law");
    return false;
  }
  if (!schemas_.validate(method, payload, error)) return false;
  if (!seams_.invoke) {
    error = refuse("bridge.no-bus", "no broker seam is wired to this bridge");
    return false;
  }
  return seams_.invoke(entry->second.channel, method, payload, reply, error);
}

} // namespace pv::rt
