// PsychoVault original source. The Windows half of child launch: real process
// envelopes with job objects and restricted tokens. P1/S022.
//
// This file compiles only into the Windows build. It is written once carefully
// and proven on the first authorized manual Windows build; my Linux tree keeps
// the honest "unsupported" answers in Psycho_process.cpp instead.
#include "Psycho_process.hpp"

#ifdef _WIN32

#include <windows.h>

namespace pv::rt {
namespace {

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

  // Two anonymous pipes: mine to the child, the child's back to me. The bus's
  // framed wire runs over these byte for byte - S021's format does not change
  // when the transport becomes a real pipe.
  HANDLE child_stdin_read = nullptr, my_write = nullptr;
  HANDLE my_read = nullptr, child_stdout_write = nullptr;
  if (!CreatePipe(&child_stdin_read, &my_write, &inherit, 1 << 20) ||
      !CreatePipe(&my_read, &child_stdout_write, &inherit, 1 << 20)) {
    error = last_error_text("CreatePipe");
    if (child_stdin_read) CloseHandle(child_stdin_read);
    if (my_write) CloseHandle(my_write);
    if (my_read) CloseHandle(my_read);
    if (child_stdout_write) CloseHandle(child_stdout_write);
    return false;
  }
  // The parent ends must not leak into the child; only the child's ends inherit.
  SetHandleInformation(my_write, HANDLE_FLAG_INHERIT, 0);
  SetHandleInformation(my_read, HANDLE_FLAG_INHERIT, 0);

  HANDLE job = CreateJobObjectW(nullptr, nullptr);
  if (!job) {
    error = last_error_text("CreateJobObjectW");
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
      return false;
    }
  }

  // A restricted token takes privileges away instead of granting: maximum
  // privileges off, so "renderer cannot read my profile" is an OS fact, not a
  // channel rule. (A later stage narrows this into a real sandbox profile.)
  HANDLE restricted = nullptr;
  if (!CreateRestrictedToken(GetCurrentProcessToken(),
                             DISABLE_MAX_PRIVILEGE | LUA_TOKEN, 0, nullptr, 0, nullptr, 0,
                             nullptr, &restricted)) {
    error = last_error_text("CreateRestrictedToken");
    CloseHandle(job);
    return false;
  }

  STARTUPINFOW startup{};
  startup.cb = sizeof(startup);
  startup.dwFlags = STARTF_USESTDHANDLES;
  startup.hStdInput = child_stdin_read;
  startup.hStdOutput = child_stdout_write;
  startup.hStdError = child_stdout_write;

  // Windows needs a writable wide command line.
  int wide_len = MultiByteToWideChar(CP_UTF8, 0, command_line.c_str(), -1, nullptr, 0);
  std::wstring wide(size_t(wide_len - 1), L'\0');
  MultiByteToWideChar(CP_UTF8, 0, command_line.c_str(), -1, wide.data(), wide_len);

  PROCESS_INFORMATION pi{};
  BOOL ok = CreateProcessAsUserW(restricted, nullptr, wide.data(), nullptr, nullptr, TRUE,
                                 CREATE_SUSPENDED | CREATE_UNICODE_ENVIRONMENT, nullptr, nullptr,
                                 &startup, &pi);
  CloseHandle(restricted);
  // The parent's copies of the child's pipe ends are only the child's handles.
  CloseHandle(child_stdin_read);
  CloseHandle(child_stdout_write);
  if (!ok) {
    error = last_error_text("CreateProcessAsUserW");
    CloseHandle(my_write);
    CloseHandle(my_read);
    CloseHandle(job);
    return false;
  }
  if (!AssignProcessToJobObject(job, pi.hProcess)) {
    error = last_error_text("AssignProcessToJobObject");
    TerminateProcess(pi.hProcess, 1);
    CloseHandle(pi.hProcess);
    CloseHandle(pi.hThread);
    CloseHandle(my_write);
    CloseHandle(my_read);
    CloseHandle(job);
    return false;
  }
  ResumeThread(pi.hThread);
  CloseHandle(pi.hThread);

  out.process = reinterpret_cast<intptr_t>(pi.hProcess);
  out.job = reinterpret_cast<intptr_t>(job);
  out.to_child = reinterpret_cast<intptr_t>(my_write);
  out.from_child = reinterpret_cast<intptr_t>(my_read);
  out.pid = pi.dwProcessId;
  (void)label;
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

} // namespace pv::rt

#endif // _WIN32
