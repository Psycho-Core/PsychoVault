// PsychoVault original source. The Windows half of my singleton model: a named
// mutex holds the lock, a named pipe carries second-launch arguments to the
// primary. P1/S023.
//
// Windows-only by construction (the file is an empty translation unit off it).
// Written once carefully here; proven on the first authorized manual Windows
// build, as with every platform envelope in this runtime.
#include "Psycho_instance.hpp"

#ifdef _WIN32

#include <atomic>
#include <memory>
#include <utility>
#include <windows.h>

namespace pv::rt {
namespace {

std::wstring widen(const std::string &text) {
  if (text.empty())
    return L"";
  const int wide = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, text.c_str(), -1,
                                       nullptr, 0);
  if (wide <= 0)
    return {};
  std::wstring buffer(size_t(wide), L'\0');
  if (MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, text.c_str(), -1,
                          buffer.data(), wide) != wide)
    return {};
  buffer.resize(size_t(wide - 1));
  return buffer;
}

// The named objects are per-profile: two profiles never block each other, two
// launches of one profile never run side by side.
std::wstring mutex_name(const std::string &profile_key) {
  return L"PsychoVault-mutex-" + widen(profile_key);
}
std::wstring pipe_name(const std::string &profile_key) {
  return L"\\\\.\\pipe\\PsychoVault-launch-" + widen(profile_key);
}

std::string win_error(const char *what) {
  return std::string(what) + " failed: " + std::to_string(GetLastError());
}

bool read_exact(HANDLE handle, void *data, DWORD length) {
  auto *bytes = static_cast<unsigned char *>(data);
  DWORD offset = 0;
  while (offset < length) {
    DWORD got = 0;
    if (!ReadFile(handle, bytes + offset, length - offset, &got, nullptr) || got == 0)
      return false;
    offset += got;
  }
  return true;
}

bool write_exact(HANDLE handle, const void *data, DWORD length) {
  const auto *bytes = static_cast<const unsigned char *>(data);
  DWORD offset = 0;
  while (offset < length) {
    DWORD wrote = 0;
    if (!WriteFile(handle, bytes + offset, length - offset, &wrote, nullptr) || wrote == 0)
      return false;
    offset += wrote;
  }
  return true;
}

struct PrimaryState {
  HANDLE mutex = nullptr;
  HANDLE listener_thread = nullptr;
  std::string profile_key;
  std::atomic<bool> stop{false};
  std::function<void(const LaunchRequest &)> on_forwarded;
};

DWORD WINAPI forward_listener(LPVOID raw) {
  auto *state = static_cast<PrimaryState *>(raw);
  const std::wstring name = pipe_name(state->profile_key);
  while (!state->stop.load()) {
    HANDLE pipe = CreateNamedPipeW(name.c_str(), PIPE_ACCESS_INBOUND,
                                   PIPE_TYPE_BYTE | PIPE_READMODE_BYTE | PIPE_WAIT,
                                   PIPE_UNLIMITED_INSTANCES, 1 << 16, 1 << 16, 200, nullptr);
    if (pipe == INVALID_HANDLE_VALUE)
      return 1;
    // A second instance connects here and delivers its launch request.
    if (!ConnectNamedPipe(pipe, nullptr) && GetLastError() != ERROR_PIPE_CONNECTED) {
      CloseHandle(pipe);
      if (state->stop.load())
        return 0;
      continue;
    }
    uint32_t length = 0;
    if (read_exact(pipe, &length, sizeof(length)) && length > 0 && length <= (1u << 16)) {
      std::string body(length, '\0');
      if (read_exact(pipe, body.data(), length)) {
        auto parsed = Json::parse(body);
        if (parsed && parsed->type == Json::Type::Object && state->on_forwarded) {
          LaunchRequest request;
          const Json *args = parsed->find("args");
          if (args && args->type == Json::Type::Array) {
            bool valid = true;
            for (const Json &arg : args->array) {
              if (arg.type != Json::Type::String) {
                valid = false;
                break;
              }
              request.args.push_back(arg.string);
            }
            if (valid) state->on_forwarded(request);
          }
        }
      }
    }
    DisconnectNamedPipe(pipe);
    CloseHandle(pipe);
  }
  return 0;
}

} // namespace

// Primary-facing seams: acquire the named mutex; if it is already held I am not
// primary. On success, my listener thread starts taking forwarded launches. The
// OS state lives on the heap, owned by the seams themselves.
AppInstance::Seams windows_instance_seams(
    const std::string &profile_key,
    std::function<void(const LaunchRequest &)> on_forwarded,
    std::function<bool(std::string &)> save_session) {
  AppInstance::Seams seams;
  auto shared = std::make_shared<PrimaryState>();
  shared->on_forwarded = on_forwarded;
  seams.acquire_lock = [profile_key, shared](std::string &error) -> std::optional<bool> {
    HANDLE mutex = CreateMutexW(nullptr, TRUE, mutex_name(profile_key).c_str());
    if (!mutex) {
      error = win_error("CreateMutexW");
      return std::nullopt;
    }
    if (GetLastError() == ERROR_ALREADY_EXISTS) {
      CloseHandle(mutex);
      return false; // another primary holds it
    }
    shared->mutex = mutex;
    shared->profile_key = profile_key;
    shared->stop.store(false);
    shared->listener_thread = CreateThread(nullptr, 0, forward_listener, shared, 0, nullptr);
    if (!shared->listener_thread) {
      error = win_error("CreateThread(listener)");
      ReleaseMutex(mutex);
      CloseHandle(mutex);
      shared->mutex = nullptr;
      return std::nullopt;
    }
    return true;
  };
  seams.release_lock = [shared]() {
    shared->stop.store(true);
    // Poke the pipe once so a listener parked in ConnectNamedPipe wakes and
    // leaves cleanly instead of hanging the quit path.
    HANDLE poke = CreateFileW(pipe_name(shared->profile_key).c_str(), GENERIC_WRITE, 0, nullptr,
                              OPEN_EXISTING, 0, nullptr);
    if (poke != INVALID_HANDLE_VALUE)
      CloseHandle(poke);
    if (shared->listener_thread) {
      WaitForSingleObject(shared->listener_thread, 500);
      CloseHandle(shared->listener_thread);
      shared->listener_thread = nullptr;
    }
    if (shared->mutex) {
      ReleaseMutex(shared->mutex);
      CloseHandle(shared->mutex);
      shared->mutex = nullptr;
    }
  };
  // A secondary instance uses the same framed named-pipe request shape. The
  // primary listener invokes the supplied callback on its listener thread; the
  // shell adapter must marshal UI work onto its window thread.
  seams.forward = [profile_key](const LaunchRequest &request, std::string &error) {
    return windows_forward_launch(profile_key, request, error);
  };
  seams.save_session = std::move(save_session);
  seams.on_forwarded = std::move(on_forwarded);
  return seams;
}

// Second-instance half: open the primary's pipe, write one framed request,
// leave. Same S021 wire shape: u32 length then one JSON object.
bool windows_forward_launch(const std::string &profile_key, const LaunchRequest &request,
                            std::string &error) {
  Json body = Json::make_object();
  Json args = Json::make_array();
  for (const std::string &arg : request.args)
    args.array.push_back(Json(arg));
  body.set("args", std::move(args));
  std::string text = body.dump();
  if (text.empty() || text.size() > (1u << 16)) {
    error = "instance.forward.payload-too-large";
    return false;
  }
  const std::wstring name = pipe_name(profile_key);
  HANDLE pipe = INVALID_HANDLE_VALUE;
  for (int attempt = 0; attempt < 50; ++attempt) {
    pipe = CreateFileW(name.c_str(), GENERIC_WRITE, 0, nullptr,
                       OPEN_EXISTING, 0, nullptr);
    if (pipe != INVALID_HANDLE_VALUE) break;
    const DWORD code = GetLastError();
    if (code != ERROR_PIPE_BUSY && code != ERROR_FILE_NOT_FOUND) {
      error = "CreateFileW(pipe) failed: " + std::to_string(code);
      return false;
    }
    if (code == ERROR_PIPE_BUSY) WaitNamedPipeW(name.c_str(), 100);
    else Sleep(100); // the primary owns the mutex before its listener is ready
  }
  if (pipe == INVALID_HANDLE_VALUE) {
    error = win_error("CreateFileW(pipe) timeout");
    return false;
  }
  const uint32_t length = static_cast<uint32_t>(text.size());
  const bool ok = write_exact(pipe, &length, sizeof(length)) &&
                  write_exact(pipe, text.data(), length);
  const DWORD write_error = ok ? 0u : GetLastError();
  CloseHandle(pipe);
  if (!ok)
    error = "WriteFile(pipe) failed: " + std::to_string(write_error);
  return ok;
}

} // namespace pv::rt

#endif // _WIN32
