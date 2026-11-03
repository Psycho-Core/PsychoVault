#pragma once
#include "../engine/base.hpp"
#include <atomic>
#include <mutex>
namespace pv {
// The owner joins its I/O worker before destroying this object. request_stop()
// is safe from the UI thread; it never closes a handle another thread is using.
class Process {
public:
  Process();
  ~Process();
  Process(const Process &) = delete;
  Process &operator=(const Process &) = delete;
  bool start(const std::string &exe, const std::string &args, bool sandbox, std::string &error);
  bool send(const Json &message, std::string &error);
  std::optional<Json> receive(int timeout_ms);
  void request_stop();
  bool stopping() const {
    return stop_requested_.load(std::memory_order_acquire);
  }
  void terminate();
  bool alive() const;
  std::optional<uint32_t> exit_code() const;
  int process_id() const {
    return pid;
  }
  bool sandboxed() const {
    return restricted;
  }
  const std::string &io_error() const {
    return io_error_;
  }

private:
  void *process_ = nullptr, *thread_ = nullptr, *job_ = nullptr;
  void *read_ = nullptr, *write_ = nullptr, *sid_ = nullptr;
  void *station_ = nullptr, *desktop_ = nullptr, *stop_event_ = nullptr;
  int pid = 0;
  bool restricted = false, io_broken_ = false;
  std::string io_error_;
  Bytes input_buffer_;
  std::atomic<bool> stop_requested_{false};
  std::mutex write_mutex_;
  bool read_more(int timeout_ms);
  void close_transport();
};
bool write_frame(void *handle, const Json &message, std::string &error);
std::optional<Json> read_frame(void *handle, int timeout_ms);
std::string executable_path();
std::string executable_dir();
} // namespace pv
