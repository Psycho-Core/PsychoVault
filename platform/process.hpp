#pragma once
#include "../engine/base.hpp"
#include <mutex>
namespace pv {
struct Process {
  void *process = nullptr;
  void *thread = nullptr;
  void *job = nullptr;
  void *read = nullptr;
  void *write = nullptr;
  void *sid = nullptr;
  int pid = 0;
  bool restricted = false;
  std::mutex write_mutex;
  Process() = default;
  ~Process();
  Process(const Process &) = delete;
  Process &operator=(const Process &) = delete;
  bool start(const std::string &exe, const std::string &args, bool sandbox,
             std::string &error);
  bool send(const Json &message, std::string &error);
  std::optional<Json> receive(int timeout_ms);
  void terminate();
  bool alive() const;
};
bool write_frame(void *handle, const Json &message, std::string &error);
std::optional<Json> read_frame(void *handle, int timeout_ms);
std::string executable_path();
std::string executable_dir();
} // namespace pv
