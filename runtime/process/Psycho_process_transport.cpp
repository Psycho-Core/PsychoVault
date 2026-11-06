// PsychoVault original source. Process-boundary IPC validation and framing.
#include "Psycho_process_transport.hpp"
#include <utility>

namespace pv::rt {
namespace {
bool method_matches_channel(Channel channel, const std::string &method) {
  const size_t separator = method.find(':');
  return separator != std::string::npos &&
         method.compare(0, separator, channel_name(channel)) == 0;
}
} // namespace

ProcessTransport::ProcessTransport(Capability local_capability, const Schemas &schemas,
                                   Read read, Write write, bool accept_responses,
                                   Limits limits)
    : local_capability_(local_capability), schemas_(schemas), read_(std::move(read)),
      write_(std::move(write)), accept_responses_(accept_responses), limits_(limits) {}

bool ProcessTransport::validate(const Envelope &message, Error &error) const {
  if (!local_capability_.allows(message.channel)) {
    error = {"capability.denied", "process transport has no capability for channel " +
                                     channel_name(message.channel)};
    return false;
  }
  if (!method_matches_channel(message.channel, message.method)) {
    error = {"schema.channel", "method does not belong to channel " +
                                  channel_name(message.channel)};
    return false;
  }
  if (message.is_response && message.request_id == 0) {
    error = {"wire.kind", "a response must carry a nonzero request id"};
    return false;
  }
  if (!schemas_.validate(message.method, message.payload, error, message.is_response))
    return false;
  return true;
}

bool ProcessTransport::send(const Envelope &message, Error &error) {
  error = Error{};
  if (!write_) {
    error = {"process.write.unavailable", "process transport has no write endpoint"};
    return false;
  }
  if (!validate(message, error))
    return false;
  FrameStream frames;
  const std::string frame = frames.encode(message);
  if (frame.size() > limits_.max_queued_bytes) {
    error = {"bus.oversize", "one process IPC frame exceeds the configured byte limit"};
    return false;
  }
  if (!write_(frame, error)) {
    if (!error) error = {"process.write.failed", "process IPC write failed"};
    return false;
  }
  error = Error{};
  return true;
}

bool ProcessTransport::recv(Envelope &message, Error &error) {
  error = Error{};
  if (!read_) {
    error = {"process.read.unavailable", "process transport has no read endpoint"};
    return false;
  }
  while (incoming_.empty()) {
    std::string bytes;
    if (!read_(bytes, error)) {
      if (!error) error = {"process.read.failed", "process IPC read failed"};
      return false;
    }
    if (bytes.empty()) {
      error = {"process.closed", "process IPC stream closed before a full envelope arrived"};
      return false;
    }
    if (bytes.size() > limits_.max_queued_bytes ||
        incoming_stream_.buffered() > limits_.max_queued_bytes - bytes.size()) {
      error = {"bus.full", "process IPC receive buffer exceeded its byte limit"};
      return false;
    }
    std::vector<Envelope> decoded;
    incoming_stream_.feed(bytes, decoded, error);
    if (error) return false;
    if (incoming_.size() + decoded.size() > limits_.max_queued_messages) {
      error = {"bus.full", "process IPC receive queue exceeded its message limit"};
      return false;
    }
    for (Envelope &candidate : decoded) {
      if (candidate.is_response && !accept_responses_) {
        error = {"wire.unexpected-response", "process transport does not accept peer responses"};
        return false;
      }
      if (!validate(candidate, error)) return false;
      queued_bytes_ += candidate.wire_bytes;
      if (queued_bytes_ > limits_.max_queued_bytes) {
        error = {"bus.full", "process IPC decoded queue exceeded its byte limit"};
        return false;
      }
      incoming_.push_back(std::move(candidate));
    }
  }
  message = std::move(incoming_.front());
  incoming_.pop_front();
  queued_bytes_ -= message.wire_bytes;
  return true;
}

} // namespace pv::rt
