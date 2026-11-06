// PsychoVault original source. The Windows half of child launch: real process
// envelopes with job objects and restricted tokens. P1/S022.
//
// This file compiles only into the Windows build. It is written once carefully
// and proven on the first authorized manual Windows build; my Linux tree keeps
// the honest "unsupported" answers in Psycho_process.cpp instead.
#include "Psycho_process.hpp"
#include "Psycho_process_transport.hpp"

#ifdef _WIN32

#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <userenv.h>
#include <algorithm>
#include <limits>
#include <utility>
#include <vector>

namespace pv::rt {
namespace {

struct SidGuard {
  PSID value = nullptr;
  ~SidGuard() { if (value) FreeSid(value); }
};

std::string last_error_text(const char *what) {
  DWORD code = GetLastError();
  wchar_t *buffer = nullptr;
  FormatMessageW(FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_ALLOCATE_BUFFER, nullptr, code, 0,
                 reinterpret_cast<wchar_t *>(&buffer), 0, nullptr);
  std::string text = what;
  text += " failed with error " + std::to_string(code);
  if (buffer) {
    size_t needed = WideCharToMultiByte(CP_UTF8, 0, buffer, -1, nullptr, 0, nullptr, nullptr);
    if (needed > 1) {
      std::string utf8(needed - 1, '\0');
      WideCharToMultiByte(CP_UTF8, 0, buffer, -1, utf8.data(), int(needed), nullptr, nullptr);
      text += ": " + utf8;
    }
    LocalFree(buffer);
  }
  return text;
}

} // namespace

bool spawn_child_platform(const std::string &label, const std::string &command_line,
                          WinChildHandle &out, std::string &error) {
  SECURITY_ATTRIBUTES inherit{};
  inherit.nLength = sizeof(inherit);
  inherit.bInheritHandle = TRUE;

  // Two anonymous pipes: the renderer gets only its stdin/stdout handles. The
  // explicit process attribute allowlist below prevents unrelated inheritable
  // handles in the privileged broker from leaking into the child.
  HANDLE child_stdin_read = nullptr, my_write = nullptr;
  HANDLE my_read = nullptr, child_stdout_write = nullptr;
  auto close_pipe_handles = [&]() {
    if (child_stdin_read) { CloseHandle(child_stdin_read); child_stdin_read = nullptr; }
    if (my_write) { CloseHandle(my_write); my_write = nullptr; }
    if (my_read) { CloseHandle(my_read); my_read = nullptr; }
    if (child_stdout_write) { CloseHandle(child_stdout_write); child_stdout_write = nullptr; }
  };
  if (!CreatePipe(&child_stdin_read, &my_write, &inherit, 1 << 20) ||
      !CreatePipe(&my_read, &child_stdout_write, &inherit, 1 << 20)) {
    error = last_error_text("CreatePipe");
    close_pipe_handles();
    return false;
  }
  if (!SetHandleInformation(my_write, HANDLE_FLAG_INHERIT, 0) ||
      !SetHandleInformation(my_read, HANDLE_FLAG_INHERIT, 0)) {
    error = last_error_text("SetHandleInformation");
    close_pipe_handles();
    return false;
  }

  HANDLE job = CreateJobObjectW(nullptr, nullptr);
  if (!job) {
    error = last_error_text("CreateJobObjectW");
    close_pipe_handles();
    return false;
  }
  {
    JOBOBJECT_EXTENDED_LIMIT_INFORMATION limits{};
    limits.BasicLimitInformation.LimitFlags = JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE |
                                              JOB_OBJECT_LIMIT_DIE_ON_UNHANDLED_EXCEPTION;
    if (!SetInformationJobObject(job, JobObjectExtendedLimitInformation, &limits,
                                 sizeof(limits))) {
      error = last_error_text("SetInformationJobObject");
      CloseHandle(job);
      close_pipe_handles();
      return false;
    }
  }

  // A restricted token removes privileges; an AppContainer identity provides
  // the actual OS boundary. No network capability is granted, so renderers
  // must request network work through the broker service. The profile SID is
  // stable across launches and only the SID allocation is process-local.
  HANDLE current_token = nullptr;
  const DWORD token_access = TOKEN_DUPLICATE | TOKEN_ASSIGN_PRIMARY | TOKEN_QUERY |
                             TOKEN_ADJUST_DEFAULT | TOKEN_ADJUST_SESSIONID;
  if (!OpenProcessToken(GetCurrentProcess(), token_access, &current_token)) {
    error = last_error_text("OpenProcessToken");
    CloseHandle(job);
    close_pipe_handles();
    return false;
  }
  HANDLE restricted = nullptr;
  if (!CreateRestrictedToken(current_token,
                             DISABLE_MAX_PRIVILEGE | LUA_TOKEN, 0, nullptr, 0, nullptr, 0,
                             nullptr, &restricted)) {
    error = last_error_text("CreateRestrictedToken");
    CloseHandle(current_token);
    CloseHandle(job);
    close_pipe_handles();
    return false;
  }
  CloseHandle(current_token);
  constexpr wchar_t kRendererAppContainer[] = L"PsychoVault.Renderer";
  PSID app_container_sid = nullptr;
  HRESULT app_container_result = CreateAppContainerProfile(
      kRendererAppContainer, L"PsychoVault Renderer",
      L"Isolated PsychoVault renderer with broker-only network access",
      nullptr, 0, &app_container_sid);
  if (app_container_result == HRESULT_FROM_WIN32(ERROR_ALREADY_EXISTS)) {
    app_container_result = DeriveAppContainerSidFromAppContainerName(
        kRendererAppContainer, &app_container_sid);
  }
  if (FAILED(app_container_result) || !app_container_sid) {
    error = "AppContainer profile creation failed: " +
            std::to_string(static_cast<unsigned long>(app_container_result));
    if (app_container_sid) FreeSid(app_container_sid);
    CloseHandle(restricted);
    CloseHandle(job);
    close_pipe_handles();
    return false;
  }
  SidGuard app_container_sid_guard{app_container_sid};

  SIZE_T attribute_bytes = 0;
  InitializeProcThreadAttributeList(nullptr, 2, 0, &attribute_bytes);
  std::vector<unsigned char> attribute_storage(attribute_bytes);
  auto *attributes = reinterpret_cast<LPPROC_THREAD_ATTRIBUTE_LIST>(attribute_storage.data());
  if (attribute_bytes == 0 ||
      !InitializeProcThreadAttributeList(attributes, 2, 0, &attribute_bytes)) {
    error = last_error_text("InitializeProcThreadAttributeList");
    CloseHandle(restricted);
    CloseHandle(job);
    close_pipe_handles();
    return false;
  }
  HANDLE inherited_handles[] = {child_stdin_read, child_stdout_write};
  if (!UpdateProcThreadAttribute(attributes, 0, PROC_THREAD_ATTRIBUTE_HANDLE_LIST,
                                 inherited_handles, sizeof(inherited_handles), nullptr, nullptr)) {
    error = last_error_text("UpdateProcThreadAttribute(handle list)");
    DeleteProcThreadAttributeList(attributes);
    CloseHandle(restricted);
    CloseHandle(job);
    close_pipe_handles();
    return false;
  }
  SECURITY_CAPABILITIES sandbox{};
  sandbox.AppContainerSid = app_container_sid;
  if (!UpdateProcThreadAttribute(attributes, 0,
                                 PROC_THREAD_ATTRIBUTE_SECURITY_CAPABILITIES,
                                 &sandbox, sizeof(sandbox), nullptr, nullptr)) {
    error = last_error_text("UpdateProcThreadAttribute(AppContainer)");
    DeleteProcThreadAttributeList(attributes);
    CloseHandle(restricted);
    CloseHandle(job);
    close_pipe_handles();
    return false;
  }

  STARTUPINFOEXW startup{};
  startup.StartupInfo.cb = sizeof(startup);
  startup.StartupInfo.dwFlags = STARTF_USESTDHANDLES;
  startup.StartupInfo.hStdInput = child_stdin_read;
  startup.StartupInfo.hStdOutput = child_stdout_write;
  startup.StartupInfo.hStdError = child_stdout_write;
  startup.lpAttributeList = attributes;

  // Windows requires a writable wide command line, including its NUL slot.
  const int wide_len = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS,
                                           command_line.c_str(), -1, nullptr, 0);
  if (wide_len <= 0) {
    error = "command line is not valid UTF-8";
    DeleteProcThreadAttributeList(attributes);
    CloseHandle(restricted);
    CloseHandle(job);
    close_pipe_handles();
    return false;
  }
  std::wstring wide(size_t(wide_len), L'\0');
  if (MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, command_line.c_str(), -1,
                          wide.data(), wide_len) != wide_len) {
    error = last_error_text("MultiByteToWideChar(command line)");
    DeleteProcThreadAttributeList(attributes);
    CloseHandle(restricted);
    CloseHandle(job);
    close_pipe_handles();
    return false;
  }
  wide.resize(size_t(wide_len - 1));

  PROCESS_INFORMATION pi{};
  const DWORD flags = CREATE_SUSPENDED | CREATE_UNICODE_ENVIRONMENT |
                      EXTENDED_STARTUPINFO_PRESENT;
  const BOOL created = CreateProcessAsUserW(
      restricted, nullptr, wide.data(), nullptr, nullptr, TRUE, flags, nullptr, nullptr,
      &startup.StartupInfo, &pi);
  const DWORD create_error = created ? 0u : GetLastError();
  DeleteProcThreadAttributeList(attributes);
  CloseHandle(restricted);
  // The parent's copies of the child's pipe ends must not remain open.
  if (child_stdin_read) { CloseHandle(child_stdin_read); child_stdin_read = nullptr; }
  if (child_stdout_write) { CloseHandle(child_stdout_write); child_stdout_write = nullptr; }
  if (!created) {
    error = "CreateProcessAsUserW failed with error " + std::to_string(create_error);
    CloseHandle(job);
    close_pipe_handles();
    return false;
  }
  if (!AssignProcessToJobObject(job, pi.hProcess)) {
    error = last_error_text("AssignProcessToJobObject");
    TerminateProcess(pi.hProcess, 1);
    WaitForSingleObject(pi.hProcess, 5000);
    CloseHandle(pi.hProcess);
    CloseHandle(pi.hThread);
    CloseHandle(job);
    close_pipe_handles();
    return false;
  }
  if (ResumeThread(pi.hThread) == DWORD(-1)) {
    error = last_error_text("ResumeThread");
    TerminateProcess(pi.hProcess, 1);
    WaitForSingleObject(pi.hProcess, 5000);
    CloseHandle(pi.hThread);
    CloseHandle(pi.hProcess);
    CloseHandle(job);
    close_pipe_handles();
    return false;
  }
  CloseHandle(pi.hThread);

  out.process = reinterpret_cast<intptr_t>(pi.hProcess);
  out.job = reinterpret_cast<intptr_t>(job);
  out.to_child = reinterpret_cast<intptr_t>(my_write);
  out.from_child = reinterpret_cast<intptr_t>(my_read);
  my_write = nullptr;
  my_read = nullptr;
  out.pid = pi.dwProcessId;
  (void)label;
  return true;
}

bool child_process_running_platform(const WinChildHandle &handle) {
  HANDLE process = reinterpret_cast<HANDLE>(handle.process);
  return process && WaitForSingleObject(process, 0) == WAIT_TIMEOUT;
}

bool process_pipe_read(intptr_t raw_pipe, std::string &bytes, Error &error) {
  HANDLE pipe = reinterpret_cast<HANDLE>(raw_pipe);
  if (!pipe) {
    bytes.clear();
    error = {"process.pipe.missing", "process read pipe is absent"};
    return false;
  }
  char buffer[16384];
  DWORD received = 0;
  if (!ReadFile(pipe, buffer, DWORD(sizeof(buffer)), &received, nullptr)) {
    bytes.clear();
    error = {"process.pipe.read", last_error_text("ReadFile(process pipe)")};
    return false;
  }
  if (received == 0) {
    bytes.clear();
    error = {"process.pipe.closed", "child closed its IPC output"};
    return false;
  }
  bytes.assign(buffer, size_t(received));
  error = Error{};
  return true;
}

bool process_pipe_read_timeout(intptr_t raw_pipe, std::string &bytes, Error &error,
                               uint32_t timeout_ms) {
  HANDLE pipe = reinterpret_cast<HANDLE>(raw_pipe);
  if (!pipe) {
    bytes.clear();
    error = {"process.pipe.missing", "process read pipe is absent"};
    return false;
  }
  const ULONGLONG deadline = GetTickCount64() + timeout_ms;
  for (;;) {
    DWORD available = 0;
    if (!PeekNamedPipe(pipe, nullptr, 0, nullptr, &available, nullptr)) {
      bytes.clear();
      error = {"process.pipe.read", last_error_text("PeekNamedPipe(process pipe)")};
      return false;
    }
    if (available != 0) return process_pipe_read(raw_pipe, bytes, error);
    if (GetTickCount64() >= deadline) {
      bytes.clear();
      error = {"process.pipe.timeout", "renderer did not answer before the IPC deadline"};
      return false;
    }
    Sleep(5);
  }
}

bool process_pipe_write(intptr_t raw_pipe, const std::string &bytes, Error &error) {
  HANDLE pipe = reinterpret_cast<HANDLE>(raw_pipe);
  if (!pipe) {
    error = {"process.pipe.missing", "process write pipe is absent"};
    return false;
  }
  size_t offset = 0;
  while (offset < bytes.size()) {
    const DWORD request = DWORD(std::min<size_t>(bytes.size() - offset,
                                                  std::numeric_limits<DWORD>::max()));
    DWORD written = 0;
    if (!WriteFile(pipe, bytes.data() + offset, request, &written, nullptr) || written == 0) {
      error = {"process.pipe.write", last_error_text("WriteFile(process pipe)")};
      return false;
    }
    offset += size_t(written);
  }
  error = Error{};
  return true;
}

bool kill_child_platform(const WinChildHandle &handle, const std::string &reason,
                         std::string &error) {
  (void)reason;
  HANDLE process = reinterpret_cast<HANDLE>(handle.process);
  HANDLE job = reinterpret_cast<HANDLE>(handle.job);
  bool ok = true;
  std::string why;
  if (process && !TerminateProcess(process, 2)) {
    why = last_error_text("TerminateProcess");
    ok = false;
  }
  // Job ends the whole tree no matter what the direct terminate did; closing
  // the job with KILL_ON_JOB_CLOSE kills anything still standing.
  if (job)
    CloseHandle(job);
  if (process)
    CloseHandle(process);
  if (handle.to_child)
    CloseHandle(reinterpret_cast<HANDLE>(handle.to_child));
  if (handle.from_child)
    CloseHandle(reinterpret_cast<HANDLE>(handle.from_child));
  if (!ok)
    error = why;
  return ok;
}

std::unique_ptr<ProcessTransport> windows_child_transport(
    const WinChildHandle &child, Capability local_capability, const Schemas &schemas,
    bool accept_responses, Error &error, Limits limits, uint32_t read_timeout_ms) {
  if (!child.to_child || !child.from_child) {
    error = {"process.transport.pipe-missing", "child IPC pipe pair is incomplete"};
    return nullptr;
  }
  const intptr_t read_pipe = child.from_child;
  const intptr_t write_pipe = child.to_child;
  auto read = [read_pipe, read_timeout_ms](std::string &bytes, Error &read_error) {
    return read_timeout_ms
        ? process_pipe_read_timeout(read_pipe, bytes, read_error, read_timeout_ms)
        : process_pipe_read(read_pipe, bytes, read_error);
  };
  auto write = [write_pipe](const std::string &bytes, Error &write_error) {
    return process_pipe_write(write_pipe, bytes, write_error);
  };
  error = Error{};
  return std::make_unique<ProcessTransport>(local_capability, schemas,
                                             std::move(read), std::move(write),
                                             accept_responses, limits);
}

} // namespace pv::rt

#endif // _WIN32
