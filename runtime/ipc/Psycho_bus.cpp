// PsychoVault original source. My runtime message bus implementation. See
// Psycho_bus.hpp for the guarantees; here is only how I keep them.
#include "Psycho_bus.hpp"
#include <algorithm>
#include <vector>

namespace pv::rt {

namespace {
bool method_matches_channel(Channel channel, const std::string &method) {
  const size_t separator = method.find(':');
  return separator != std::string::npos &&
         method.compare(0, separator, channel_name(channel)) == 0;
}
} // namespace

std::string channel_name(Channel channel) {
  switch (channel) {
  case Channel::Tab: return "tab";
  case Channel::Nav: return "nav";
  case Channel::Win: return "win";
  case Channel::Bookmarks: return "bookmarks";
  case Channel::History: return "history";
  case Channel::Settings: return "settings";
  case Channel::Find: return "find";
  case Channel::Zoom: return "zoom";
  case Channel::View: return "view";
  case Channel::Downloads: return "downloads";
  case Channel::Ui: return "ui";
  case Channel::Ws: return "ws";
  case Channel::Clip: return "clip";
  case Channel::Rail: return "rail";
  case Channel::Spell: return "spell";
  case Channel::Updates: return "updates";
  case Channel::Proton: return "proton";
  case Channel::Extensions: return "extensions";
  case Channel::Perf: return "perf";
  case Channel::Vault: return "vault";
  case Channel::Translate: return "translate";
  case Channel::Internal: return "internal";
  case Channel::Diagnostics: return "diagnostics";
  case Channel::Lifecycle: return "lifecycle";
  case Channel::Renderer: return "renderer";
  case Channel::Network: return "network";
  }
  return "unknown";
}

// Every stock schema is a list of fields: name, JSON type, required-or-not.
// Unknown fields and missing required fields are both refused: silent field
// tolerance made typos and request-shape bypasses indistinguishable.
using FieldSpec = std::tuple<const char *, Json::Type, bool>;

Schemas Schemas::stock() {
  Schemas s;
  using T = Json::Type;
  auto fields = [](std::vector<FieldSpec> table) {
    return [table](const Json &payload, Error &error) {
      if (payload.type != Json::Type::Object) {
        error = {"schema.payload", "payload must be an object"};
        return false;
      }
      for (const auto &[key, value] : payload.object) {
        const bool known = std::any_of(table.begin(), table.end(),
            [&key](const FieldSpec &field) { return key == std::get<0>(field); });
        if (!known) {
          error = {"schema.unknown-field", std::string("field '") + key + "' is not allowed"};
          return false;
        }
        (void)value;
      }
      for (const auto &[name, type, required] : table) {
        const Json *found = payload.find(name);
        if (!found) {
          if (required) {
            error = {"schema.missing", std::string("field '") + name + "' is required"};
            return false;
          }
          continue;
        }
        if (found->type != type) {
          error = {"schema.type", std::string("field '") + name + "' has the wrong type"};
          return false;
        }
      }
      return true;
    };
  };
  s.add("nav:go", fields({{"url", T::String, true}}));
  s.add("nav:notify",
        fields({{"url", T::String, true}, {"title", T::String, true}, {"loading", T::Bool, true}}));
  s.add("tab:new", fields({{"url", T::String, false}}));
  // S027 WebContents notifications: my navigation controller speaks on the Tab
  // channel, notify-only (request id 0), always naming the contents it speaks for.
  s.add("tab:loading", fields({{"contents", T::Number, true}, {"url", T::String, true},
                               {"kind", T::String, true}}));
  s.add("tab:state", fields({{"contents", T::Number, true}, {"state", T::String, true}}));
  s.add("tab:url", fields({{"contents", T::Number, true}, {"url", T::String, true},
                           {"entry", T::Number, true}}));
  s.add("tab:title", fields({{"contents", T::Number, true}, {"title", T::String, true}}));
  s.add("tab:favicon", fields({{"contents", T::Number, true}, {"favicon", T::String, true}}));
  s.add("tab:error", fields({{"contents", T::Number, true}, {"code", T::String, true},
                             {"message", T::String, true}, {"url", T::String, true}}));
  s.add("tab:activate", fields({{"id", T::Number, true}}));
  s.add("win:focus", fields({{"id", T::Number, true}}));
  s.add("lifecycle:ping", fields({}));
  s.add("lifecycle:pong", fields({{"uptime_ms", T::Number, true}}));
  s.add("lifecycle:echo", fields({{"nonce", T::String, true}}));
  s.add("lifecycle:shutdown", fields({}));
  s.add("renderer:navigate", fields({{"url", T::String, true},
                                     {"ok", T::Bool, false},
                                     {"frame", T::Object, false},
                                     {"detail", T::String, false}}));
  s.add("renderer:isolationProbe", fields({{"path", T::String, true},
                                           {"host", T::String, true},
                                           {"port", T::Number, true},
                                           {"file_opened", T::Bool, false},
                                           {"socket_created", T::Bool, false},
                                           {"socket_connected", T::Bool, false}}));
  s.add("network:fetch", fields({{"url", T::String, true},
                                 {"resource_type", T::Number, true}}));
  s.add("network:fetchResult", fields({{"url", T::String, true},
                                       {"mime", T::String, true},
                                       {"text", T::String, true},
                                       {"bytes_b64", T::String, true},
                                       {"status", T::Number, true},
                                       {"ok", T::Bool, true},
                                       {"refused", T::Bool, true},
                                       {"detail", T::String, false}}));
  s.add("diagnostics:stage",
        fields({{"stage", T::String, true}, {"ok", T::Bool, true}, {"result", T::String, true}}));
  s.add("diagnostics:nettest", fields({}));
  s.add("diagnostics:version", fields({}));
  s.add("history:list", fields({{"query", T::String, false}}));
  // S028: the internal scheme's two conversations - listing my registered
  // pages and requesting one of their resources. Both are Internal-channel
  // only, which by default no renderer capability mask contains.
  s.add("internal:list", fields({}));
  s.add("internal:fetch", fields({{"url", T::String, true}}));
  s.add("settings:get", fields({{"key", T::String, true}}));
  s.add("settings:set", fields({{"key", T::String, true}, {"value", T::Number, true}}));
  s.add("vault:status", fields({}));
  s.add("spell:status", fields({}));
  return s;
}

bool Schemas::validate(const std::string &name, const Json &payload, Error &error,
                       bool is_response) const {
  error = Error{};
  auto it = table_.find(name);
  if (it == table_.end()) {
    error = {"schema.unknown-method", "no registered schema for " + name};
    return false;
  }
  if (is_response && payload.type == Json::Type::Object) {
    const Json *refusal = payload.find("error");
    if (refusal) {
      if (refusal->type == Json::Type::String && payload.object.size() == 1)
        return true;
      error = {"schema.error-response", "an error response must contain only a string 'error'"};
      return false;
    }
  }
  if (!is_response && payload.type == Json::Type::Object && payload.find("error")) {
    error = {"schema.error-request", "error envelopes are only valid in response direction"};
    return false;
  }
  return it->second(payload, error);
}

std::string FrameStream::encode(const Envelope &env) const {
  Json body = Json::make_object();
  body.set("c", Json(static_cast<int>(env.channel)));
  body.set("m", Json(env.method));
  body.set("i", Json(static_cast<size_t>(env.request_id)));
  body.set("r", Json(env.is_response));
  body.set("p", env.payload);
  std::string bytes = body.dump();
  std::string frame(4, '\0');
  uint32_t length = static_cast<uint32_t>(bytes.size());
  for (int i = 0; i < 4; ++i)
    frame[i] = static_cast<char>((length >> (8 * i)) & 0xff);
  return frame + bytes;
}

void FrameStream::feed(const std::string &bytes, std::vector<Envelope> &out, Error &error) {
  buffer_ += bytes;
  for (;;) {
    if (buffer_.size() < 4)
      return;
    uint32_t length = 0;
    for (int i = 0; i < 4; ++i)
      length |= uint32_t(static_cast<uint8_t>(buffer_[size_t(i)])) << (8 * i);
    if (length > (1u << 20) + 64) {
      error = {"wire.oversize", "a frame claimed an impossible length"};
      return;
    }
    if (buffer_.size() < 4 + size_t(length))
      return;
    std::string body = buffer_.substr(4, length);
    buffer_.erase(0, 4 + size_t(length));
    auto parsed = Json::parse(body);
    if (!parsed || parsed->type != Json::Type::Object) {
      error = {"wire.decode", "a frame did not decode to a JSON object"};
      return;
    }
    Envelope env;
    env.channel = static_cast<Channel>(static_cast<uint16_t>(parsed->num("c", 0)));
    env.method = parsed->str("m");
    env.request_id = static_cast<uint32_t>(parsed->num("i", 0));
    if (const Json *response = parsed->find("r")) {
      if (response->type != Json::Type::Bool) {
        error = {"wire.kind", "a frame's response marker must be boolean"};
        return;
      }
      env.is_response = response->boolean;
    }
    if (env.is_response && env.request_id == 0) {
      error = {"wire.kind", "a response must carry a nonzero request id"};
      return;
    }
    const Json *payload = parsed->find("p");
    env.payload = payload ? *payload : Json();
    env.wire_bytes = 4 + size_t(length);
    out.push_back(std::move(env));
    revealed_ += 4 + size_t(length);
  }
}

bool Endpoint::handoff(const std::string &frame, Error &error) {
  // Resident bytes = anything decoded and unread plus any partial frame still
  // forming. The budget refuses growth past the cap with a structured error.
  if (queued_bytes_ + inbound_.buffered() + frame.size() > limits_.max_queued_bytes) {
    error = {"bus.full", "inbound byte budget exhausted on " + label_ + "; my peer must slow down"};
    return false;
  }
  std::vector<Envelope> fresh;
  inbound_.feed(frame, fresh, error);
  if (error)
    return false;
  if (decoded_.size() + fresh.size() > limits_.max_queued_messages) {
    error = {"bus.full", "inbound message budget exhausted on " + label_};
    return false;
  }
  for (auto &env : fresh) {
    if (!method_matches_channel(env.channel, env.method)) {
      error = {"schema.channel", "method does not belong to channel " + channel_name(env.channel)};
      return false;
    }
    if (env.is_response && !accept_responses_) {
      error = {"wire.unexpected-response", label_ + " does not accept peer responses"};
      return false;
    }
    if (env.is_response && env.request_id == 0) {
      error = {"wire.kind", "a response must carry a nonzero request id"};
      return false;
    }
    if (schemas_) {
      // The receiving end validates too. A sender whose validation crashed is
      // not an argument for trusting its payload.
      if (Error e; !schemas_->validate(env.method, env.payload, e, env.is_response)) {
        error = e;
        return false;
      }
    }
    queued_bytes_ += env.wire_bytes;
    decoded_.push_back(std::move(env));
  }
  return true;
}

bool Endpoint::send(const Envelope &env, Error &error) {
  // Callers reuse error objects; success must always leave the slot clean, or a
  // stale refusal looks like it came from me when nothing refused at all.
  error = Error{};
  if (!peer_) {
    error = {"bus.closed", "this endpoint has no peer"};
    return false;
  }
  if (!capability_.allows(env.channel)) {
    error = {"capability.denied",
             label_ + " has no capability for channel " + channel_name(env.channel)};
    return false;
  }
  if (!method_matches_channel(env.channel, env.method)) {
    error = {"schema.channel", "method does not belong to channel " + channel_name(env.channel)};
    return false;
  }
  if (env.is_response && env.request_id == 0) {
    error = {"wire.kind", "a response must carry a nonzero request id"};
    return false;
  }
  if (schemas_) {
    if (Error e; !schemas_->validate(env.method, env.payload, e, env.is_response)) {
      error = e;
      return false;
    }
  }
  // The frame leaves me exactly as it would leave a real process. When S022
  // wraps these endpoints in a windows process pair, encode() stays and only
  // the pipe beneath it changes.
  FrameStream stream;
  return peer_->handoff(stream.encode(env), error);
}

bool Endpoint::recv(Envelope &env, Error &error) {
  if (decoded_.empty()) {
    error = {};
    return false;
  }
  env = std::move(decoded_.front());
  queued_bytes_ -= env.wire_bytes;
  decoded_.pop_front();
  // A privileged response must never leak into an unprivileged inbox.
  if (!capability_.allows(env.channel)) {
    error = {"capability.denied",
             label_ + " may not receive on channel " + channel_name(env.channel)};
    return false;
  }
  return true;
}

} // namespace pv::rt
