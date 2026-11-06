// PsychoVault original source. Framed IPC over a real process byte stream.
// The transport is deliberately separate from the Windows HANDLE adapter so
// the same validation and back-pressure rules can be tested with deterministic
// streams before the named child processes are exercised on Windows.
#pragma once
#include "../ipc/Psycho_bus.hpp"
#include "Psycho_process.hpp"
#include <deque>
#include <memory>
#include <functional>

namespace pv::rt {

class ProcessTransport {
public:
  using Read = std::function<bool(std::string &bytes, Error &error)>;
  using Write = std::function<bool(const std::string &bytes, Error &error)>;

  ProcessTransport(Capability local_capability, const Schemas &schemas,
                   Read read, Write write, bool accept_responses,
                   Limits limits = {});

  // One send is one complete framed envelope. Writes may be buffered by the
  // underlying pipe, but the receiver's FrameStream handles arbitrary chunks.
  bool send(const Envelope &message, Error &error);
  // Reads until one complete validated envelope is available. A closed or
  // malformed stream returns a structured error; it never yields partial JSON.
  bool recv(Envelope &message, Error &error);
  size_t pending() const { return incoming_.size(); }

private:
  bool validate(const Envelope &message, Error &error) const;

  Capability local_capability_;
  const Schemas &schemas_;
  Read read_;
  Write write_;
  bool accept_responses_;
  Limits limits_;
  FrameStream incoming_stream_;
  std::deque<Envelope> incoming_;
  size_t queued_bytes_ = 0;
};

// Binds the transport to the child pipe pair created by spawn_child_platform.
// The WinChildHandle remains the owner of the handles and must outlive the link.
std::unique_ptr<ProcessTransport> windows_child_transport(
    const WinChildHandle &child, Capability local_capability, const Schemas &schemas,
    bool accept_responses, Error &error, Limits limits = {},
    uint32_t read_timeout_ms = 0);

} // namespace pv::rt
