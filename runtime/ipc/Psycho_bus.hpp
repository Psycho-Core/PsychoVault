// PsychoVault original source. My runtime message bus - the framed, typed,
// capability-checked spine of my own application runtime. P1/S021 of
// docs/Psycho_MASTER-PLAN.md.
//
// What this guarantees, concretely:
//  - framed, length-prefixed messages on the wire (u32 length + one JSON object),
//  - typed channel IDs from the 21 legacy channel groups, never free-form strings,
//  - per-channel schema validation before a byte moves and after it arrives,
//  - capability handles: an endpoint may only speak on channels its mask allows,
//  - back-pressure: a bounded inbound queue, so a busy peer fails structured
//    sends with "bus.full" instead of growing without limit,
//  - structured errors everywhere: a machine code plus a human message.
//
// This is S021 finished with in-process links. S022 gives renderer endpoints real
// process envelopes with job objects; the wire format and validation below do not
// change when that lands, which is the point of separating them.
#pragma once
#include "../../engine/Psycho_base.hpp"
#include <cstdint>
#include <deque>
#include <functional>
#include <map>
#include <memory>

namespace pv::rt {

// The channel groups from my legacy inventory (21 groups), plus two runtime-
// internal channels. IDs are stable and never reassigned: they ride the wire.
enum class Channel : uint16_t {
  Tab = 1,
  Nav = 2,
  Win = 3,
  Bookmarks = 4,
  History = 5,
  Settings = 6,
  Find = 7,
  Zoom = 8,
  View = 9,
  Downloads = 10,
  Ui = 11,
  Ws = 12,
  Clip = 13,
  Rail = 14,
  Spell = 15,
  Updates = 16,
  Proton = 17,
  Extensions = 18,
  Perf = 19,
  Vault = 20,
  Translate = 21,
  Internal = 22, // my psychovault:// scheme surface: pages, resources, diagnostics
  Diagnostics = 30,
  Lifecycle = 31,
};

std::string channel_name(Channel channel);

// Structured errors. A code is machine-readable and stable; the message is for
// the person holding the support bundle.
struct Error {
  std::string code, message;
  explicit operator bool() const { return !code.empty(); }
};

struct Envelope {
  Channel channel = Channel::Lifecycle;
  std::string method;      // e.g. "nav:go" - one of the names my schema registry knows.
  uint32_t request_id = 0; // 0 means "notify": no response is expected.
  Json payload;            // Always an object; the schema validator enforces it.
  // How many wire bytes this envelope occupied, filled in by the frame parser so
  // my byte budgets measure what is actually resident in a queue.
  size_t wire_bytes = 0;
};

// A capability handle is exactly a bit mask over channel IDs.
struct Capability {
  uint64_t allow = 0;
  bool allows(Channel c) const {
    uint16_t id = static_cast<uint16_t>(c);
    return id < 64 && (allow & (uint64_t(1) << id)) != 0;
  }
  static Capability all() {
    Capability cap;
    cap.allow = ~uint64_t(0);
    return cap;
  }
};

// One validated method signature: the required and optional fields of its
// payload object, with types. Unknown payloads are rejected outright - a typo in
// a method name is a bug I surface, not a packet I slip through.
class Schemas {
public:
  using Validator = std::function<bool(const Json &payload, Error &error)>;
  void add(const std::string &name, Validator validate) { table_[name] = std::move(validate); }
  bool validate(const std::string &name, const Json &payload, Error &error) const;
  // Whether a method name is registered at all - the preload bridge's
  // curation law asks this before any name may exist on window.psycho.
  bool knows(const std::string &name) const { return table_.count(name) != 0; }
  // My stock registry: the channels that already have a contract today.
  static Schemas stock();

private:
  std::map<std::string, Validator> table_;
};

// Framing: u32 length (little endian) + that many bytes of one JSON object. The
// parser is incremental on purpose: real sockets deliver halves, and I refuse to
// assume frames arrive whole.
class FrameStream {
public:
  std::string encode(const Envelope &env) const;
  // I append bytes as they arrive and hand back any complete envelopes decoded
  // this call. Partial frames sit in my buffer until their length arrives.
  void feed(const std::string &bytes, std::vector<Envelope> &out, Error &error);
  size_t buffered() const { return buffer_.size(); }

private:
  std::string buffer_;
  size_t revealed_ = 0; // bytes decoded so far, for debugging surfaces only
};

struct Limits {
  // Back-pressure numbers, deliberately small enough to trip in tests.
  size_t max_queued_bytes = 1u << 20; // 1 MiB inbound per endpoint
  size_t max_queued_messages = 512;   // decoded, awaiting pickup
};

class Link;

// One end of a link. My peer writes into my inbound stream; I read from it.
// Sending means: capability check, schema check, encode, bounded handoff.
class Endpoint {
public:
  Endpoint(Capability capability, std::string label) : capability_(capability), label_(std::move(label)) {}

  const std::string &label() const { return label_; }
  Capability capability() const { return capability_; }
  void set_schemas(const Schemas *schemas) { schemas_ = schemas; }
  void set_limits(Limits limits) { limits_ = limits; }

  bool send(const Envelope &env, Error &error);
  bool recv(Envelope &env, Error &error);
  size_t pending() const { return decoded_.size(); }
  size_t pending_bytes() const { return inbound_.buffered(); }

private:
  friend class Link;
  // Link wiring: my peer calls these when it sends.
  bool handoff(const std::string &frame, Error &error);

  Capability capability_;
  std::string label_;
  const Schemas *schemas_ = nullptr;
  Limits limits_;
  FrameStream inbound_;              // byte stream coming to me
  std::deque<Envelope> decoded_;     // complete messages awaiting pickup
  size_t queued_bytes_ = 0;          // wire bytes still resident, decoded or partial
  Endpoint *peer_ = nullptr;         // who I am connected to
};

// A full-duplex, in-process link between two endpoints. Both directions enforce
// the sender's capabilities and the wire schema; receiving side validates again,
// because a corrupted peer should never get the benefit of the doubt.
class Link {
public:
  Link(Capability a_caps, std::string a_label, Capability b_caps, std::string b_label)
      : a_(a_caps, std::move(a_label)), b_(b_caps, std::move(b_label)) {
    a_.peer_ = &b_;
    b_.peer_ = &a_;
  }
  Endpoint &a() { return a_; }
  Endpoint &b() { return b_; }
  void set_schemas(const Schemas *schemas) {
    a_.set_schemas(schemas);
    b_.set_schemas(schemas);
  }
  void set_limits(Limits limits) {
    a_.set_limits(limits);
    b_.set_limits(limits);
  }

private:
  Endpoint a_, b_;
};

} // namespace pv::rt
