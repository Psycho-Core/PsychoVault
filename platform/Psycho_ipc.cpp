// My length-prefixed JSON framing, shared by my broker and my renderer. It has
// no process, window or UI dependency, so my renderer imports no UI DLLs.
#include "Psycho_ipc.hpp"
#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#else
#include <cerrno>
#include <poll.h>
#include <unistd.h>
#endif
#include <algorithm>

namespace pv {
namespace ipc {
void put32(uint8_t *p, uint32_t n) {
  for (int i = 0; i < 4; ++i)
    p[i] = uint8_t(n >> (8 * i));
}
uint32_t get32(const uint8_t *p) {
  uint32_t n = 0;
  for (int i = 0; i < 4; ++i)
    n |= uint32_t(p[i]) << (8 * i);
  return n;
}
bool write_all(void *h, const uint8_t *data, size_t n) {
  while (n) {
#ifdef _WIN32
    DWORD done = 0;
    if (!WriteFile(h, data, DWORD(std::min<size_t>(n, 1u << 20)), &done, nullptr) || !done)
      return false;
#else
    ssize_t done = ::write(int(intptr_t(h)), data, n);
    if (done < 0 && errno == EINTR)
      continue;
    if (done <= 0)
      return false;
#endif
    data += done;
    n -= size_t(done);
  }
  return true;
}
bool read_all(void *h, uint8_t *data, size_t n) {
  while (n) {
#ifdef _WIN32
    DWORD done = 0;
    if (!ReadFile(h, data, DWORD(std::min<size_t>(n, 1u << 20)), &done, nullptr) || !done)
      return false;
#else
    ssize_t done = ::read(int(intptr_t(h)), data, n);
    if (done < 0 && errno == EINTR)
      continue;
    if (done <= 0)
      return false;
#endif
    data += done;
    n -= size_t(done);
  }
  return true;
}
} // namespace ipc

using ipc::get32;
using ipc::put32;
using ipc::read_all;
using ipc::write_all;

bool write_frame(void *h, const Json &message, std::string &error) {
  if (!h) {
    error = "IPC handle is closed";
    return false;
  }
  auto text = message.dump();
  if (text.size() > kMaxIpcBytes) {
    error = "IPC message too large";
    return false;
  }
  uint8_t header[4];
  put32(header, uint32_t(text.size()));
  if (!write_all(h, header, 4) ||
      !write_all(h, reinterpret_cast<const uint8_t *>(text.data()), text.size())) {
    error = "IPC write failed";
    return false;
  }
  return true;
}
std::optional<Json> read_frame(void *h, int timeout_ms) {
  if (!h)
    return std::nullopt;
#ifdef _WIN32
  DWORD available = 0;
  int waited = 0;
  while (true) {
    if (!PeekNamedPipe(h, nullptr, 0, nullptr, &available, nullptr))
      return std::nullopt;
    if (available >= 4)
      break;
    if (timeout_ms >= 0 && waited >= timeout_ms)
      return std::nullopt;
    Sleep(5);
    waited += 5;
  }
#else
  pollfd p{int(intptr_t(h)), POLLIN, 0};
  if (poll(&p, 1, timeout_ms) <= 0)
    return std::nullopt;
#endif
  uint8_t header[4];
  if (!read_all(h, header, 4))
    return std::nullopt;
  uint32_t n = get32(header);
  if (n > kMaxIpcBytes)
    return std::nullopt;
  std::string text(n, '\0');
  if (n && !read_all(h, reinterpret_cast<uint8_t *>(text.data()), n))
    return std::nullopt;
  auto j = Json::parse(text, kMaxIpcBytes);
  if (!j || j->type != Json::Type::Object)
    return std::nullopt;
  return j;
}
} // namespace pv
