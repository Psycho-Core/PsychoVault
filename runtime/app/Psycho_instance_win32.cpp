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
#include <windows.h>

namespace pv::rt {
namespace {

std::wstring widen(const std::string &text) {
  if (text.empty())
    return L"";
  int wide = MultiByteToWideChar(CP_UTF8, 0, text.c_str(), -1, nullptr, 0);
  std::wstring out(size_t(wide - 1), L'\0');
  MultiByteToWideChar(CP_UTF8, 0, text.c_str(), -1, out.data(), wide);
  return out;
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
    DWORD got = 0;
    if (ReadFile(pipe, &length, 4, &got, nullptr) && got == 4 && length <= (1u << 16)) {
      std::string body(length, '\0');
      if (length == 0 || ReadFile(pipe, body.data(), length, &got, nullptr)) {
        auto parsed = Json::parse(body);
        if (parsed && parsed->type == Json::Type::Object && state->on_forwarded) {
          LaunchRequest request;
          const Json *args = parsed->find("args");
          if (args && args->type == Json::Type::Array)
            for (const Json &arg : args->array)
              if (arg.type == Json::Type::String)
                request.args.push_back(arg.string);
          state->on_forwarded(request);
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
AppInstance::Seams windows_instance_seams(const std::string &profile_key) {
  AppInstance::Seams seams;
  auto shared = std::make_shared<PrimaryState>();
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
  seams.on_forwarded = [shared](const LaunchRequest &request) {
    if (shared->on_forwarded)
      shared->on_forwarded(request);
  };
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
  HANDLE pipe = CreateFileW(pipe_name(profile_key).c_str(), GENERIC_WRITE, 0, nullptr,
                            OPEN_EXISTING, 0, nullptr);
  if (pipe == INVALID_HANDLE_VALUE) {
    error = win_error("CreateFileW(pipe)");
    return false;
  }
  uint32_t length = static_cast<uint32_t>(text.size());
  DWORD wrote = 0;
  bool ok = WriteFile(pipe, &length, 4, &wrote, nullptr) &&
            WriteFile(pipe, text.data(), length, &wrote, nullptr);
  DWORD write_error = ok ? 0u : GetLastError();
  CloseHandle(pipe);
  if (!ok)
    error = "WriteFile(pipe) failed: " + std::to_string(write_error);
  return ok;
}

} // namespace pv::rt

#endif // _WIN32
