#include "process.hpp"
#ifdef _WIN32
#define NOMINMAX
#include <windows.h>
#include <aclapi.h>
#include <userenv.h>
#else
#include <cerrno>
#include <cstring>
#include <fcntl.h>
#include <poll.h>
#include <signal.h>
#include <sys/wait.h>
#include <unistd.h>
#endif
namespace pv {
static void put32(uint8_t *p, uint32_t n) {
  p[0] = n;
  p[1] = n >> 8;
  p[2] = n >> 16;
  p[3] = n >> 24;
}
static uint32_t get32(const uint8_t *p) {
  return uint32_t(p[0]) | uint32_t(p[1]) << 8 | uint32_t(p[2]) << 16 | uint32_t(p[3]) << 24;
}
static bool write_all(void *h, const uint8_t *data, size_t n) {
#ifdef _WIN32
  HANDLE handle = h;
  while (n) {
    DWORD done = 0;
    if (!WriteFile(handle, data, DWORD(std::min<size_t>(n, 1u << 20)), &done, nullptr) || !done)
      return false;
    data += done;
    n -= done;
  }
  return true;
#else
  int fd = int(intptr_t(h));
  while (n) {
    ssize_t done = ::write(fd, data, n);
    if (done <= 0) {
      if (errno == EINTR)
        continue;
      return false;
    }
    data += done;
    n -= size_t(done);
  }
  return true;
#endif
}
static bool read_all(void *h, uint8_t *data, size_t n) {
#ifdef _WIN32
  HANDLE handle = h;
  while (n) {
    DWORD done = 0;
    if (!ReadFile(handle, data, DWORD(std::min<size_t>(n, 1u << 20)), &done, nullptr) || !done)
      return false;
    data += done;
    n -= done;
  }
  return true;
#else
  int fd = int(intptr_t(h));
  while (n) {
    ssize_t done = ::read(fd, data, n);
    if (done <= 0) {
      if (done < 0 && errno == EINTR)
        continue;
      return false;
    }
    data += done;
    n -= size_t(done);
  }
  return true;
#endif
}
bool write_frame(void *h, const Json &message, std::string &error) {
  auto text = message.dump();
  if (text.size() > kMaxResourceBytes) {
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
#ifdef _WIN32
  HANDLE handle = h;
  DWORD available = 0;
  int waited = 0;
  while (true) {
    if (!PeekNamedPipe(handle, nullptr, 0, nullptr, &available, nullptr))
      return std::nullopt;
    if (available >= 4)
      break;
    if (timeout_ms >= 0 && waited >= timeout_ms)
      return std::nullopt;
    Sleep(5);
    waited += 5;
  }
#else
  int fd = int(intptr_t(h));
  pollfd p{fd, POLLIN, 0};
  int rc = poll(&p, 1, timeout_ms);
  if (rc <= 0)
    return std::nullopt;
#endif
  uint8_t header[4];
  if (!read_all(h, header, 4))
    return std::nullopt;
  uint32_t n = get32(header);
  if (n > kMaxResourceBytes)
    return std::nullopt;
  std::string text(n, '\0');
  if (n && !read_all(h, reinterpret_cast<uint8_t *>(text.data()), n))
    return std::nullopt;
  return Json::parse(text, kMaxResourceBytes);
}
#ifdef _WIN32
static std::wstring wide(const std::string &s) {
  if (s.empty())
    return {};
  int n = MultiByteToWideChar(CP_UTF8, 0, s.c_str(), int(s.size()), nullptr, 0);
  std::wstring w(n, 0);
  MultiByteToWideChar(CP_UTF8, 0, s.c_str(), int(s.size()), w.data(), n);
  return w;
}
static std::string narrow(const std::wstring &w) {
  if (w.empty())
    return {};
  int n = WideCharToMultiByte(CP_UTF8, 0, w.c_str(), int(w.size()), nullptr, 0, nullptr, nullptr);
  std::string s(n, 0);
  WideCharToMultiByte(CP_UTF8, 0, w.c_str(), int(w.size()), s.data(), n, nullptr, nullptr);
  return s;
}
static bool grant_execute(const std::wstring &path, PSID sid) {
  PACL old = nullptr;
  PSECURITY_DESCRIPTOR sd = nullptr;
  if (GetNamedSecurityInfoW(path.c_str(), SE_FILE_OBJECT, DACL_SECURITY_INFORMATION, nullptr,
                            nullptr, &old, nullptr, &sd) != ERROR_SUCCESS)
    return false;
  EXPLICIT_ACCESSW ea{};
  ea.grfAccessPermissions = FILE_GENERIC_READ | FILE_GENERIC_EXECUTE;
  ea.grfAccessMode = GRANT_ACCESS;
  ea.grfInheritance = NO_INHERITANCE;
  BuildTrusteeWithSidW(&ea.Trustee, sid);
  PACL updated = nullptr;
  bool ok = SetEntriesInAclW(1, &ea, old, &updated) == ERROR_SUCCESS &&
            SetNamedSecurityInfoW(const_cast<wchar_t *>(path.c_str()), SE_FILE_OBJECT,
                                  DACL_SECURITY_INFORMATION, nullptr, nullptr, updated,
                                  nullptr) == ERROR_SUCCESS;
  if (updated)
    LocalFree(updated);
  if (sd)
    LocalFree(sd);
  return ok;
}
Process::~Process() {
  terminate();
  if (read)
    CloseHandle(read);
  if (write)
    CloseHandle(write);
  if (job)
    CloseHandle(job);
  if (thread)
    CloseHandle(thread);
  if (process)
    CloseHandle(process);
  if (sid)
    FreeSid(sid);
}
bool Process::start(const std::string &exe, const std::string &args, bool sandbox,
                    std::string &error) {
  SECURITY_ATTRIBUTES sa{sizeof(sa), nullptr, TRUE};
  HANDLE in_read = nullptr, in_write = nullptr, out_read = nullptr, out_write = nullptr;
  if (!CreatePipe(&in_read, &in_write, &sa, 0) || !CreatePipe(&out_read, &out_write, &sa, 0)) {
    error = "cannot create renderer pipes";
    return false;
  }
  SetHandleInformation(in_write, HANDLE_FLAG_INHERIT, 0);
  SetHandleInformation(out_read, HANDLE_FLAG_INHERIT, 0);
  PSID app_sid = nullptr;
  bool made_profile = false;
  std::wstring container = L"PsychoVault.Renderer";
  if (sandbox) {
    HRESULT hr =
        CreateAppContainerProfile(container.c_str(), L"PsychoVault renderer",
                                  L"Isolated PsychoVault website renderer", nullptr, 0, &app_sid);
    if (FAILED(hr)) {
      if (hr == HRESULT_FROM_WIN32(ERROR_ALREADY_EXISTS))
        hr = DeriveAppContainerSidFromAppContainerName(container.c_str(), &app_sid);
    } else
      made_profile = true;
    if (FAILED(hr) || !app_sid) {
      error = "cannot create AppContainer identity";
      CloseHandle(in_read);
      CloseHandle(in_write);
      CloseHandle(out_read);
      CloseHandle(out_write);
      return false;
    }
    sid = app_sid;
    restricted = true;
    if (!grant_execute(wide(exe), app_sid)) {
      error = "cannot grant renderer execute access";
      return false;
    }
    auto slash = exe.find_last_of("/\\");
    if (slash != std::string::npos && !grant_execute(wide(exe.substr(0, slash)), app_sid)) {
      error = "cannot grant renderer directory traversal";
      return false;
    }
  }
  STARTUPINFOEXW si{};
  si.StartupInfo.cb = sizeof(si);
  si.StartupInfo.dwFlags = STARTF_USESTDHANDLES;
  si.StartupInfo.hStdInput = in_read;
  si.StartupInfo.hStdOutput = out_write;
  si.StartupInfo.hStdError = GetStdHandle(STD_ERROR_HANDLE);
  SIZE_T size = 0;
  InitializeProcThreadAttributeList(nullptr, 1, 0, &size);
  std::vector<uint8_t> buffer(size);
  auto attrs = reinterpret_cast<LPPROC_THREAD_ATTRIBUTE_LIST>(buffer.data());
  InitializeProcThreadAttributeList(attrs, 1, 0, &size);
  HANDLE inherit[2] = {in_read, out_write};
  UpdateProcThreadAttribute(attrs, 0, PROC_THREAD_ATTRIBUTE_HANDLE_LIST, inherit, sizeof(inherit),
                            nullptr, nullptr);
  SECURITY_CAPABILITIES caps{};
  caps.AppContainerSid = app_sid;
  caps.CapabilityCount = 0;
  caps.Capabilities = nullptr;
  if (sandbox)
    UpdateProcThreadAttribute(attrs, 0, PROC_THREAD_ATTRIBUTE_SECURITY_CAPABILITIES, &caps,
                              sizeof(caps), nullptr, nullptr);
  si.lpAttributeList = attrs;
  std::wstring command = L"\"" + wide(exe) + L"\" " + wide(args);
  std::vector<wchar_t> cmd(command.begin(), command.end());
  cmd.push_back(0);
  PROCESS_INFORMATION pi{};
  DWORD flags = EXTENDED_STARTUPINFO_PRESENT | CREATE_SUSPENDED;
  BOOL created = CreateProcessW(wide(exe).c_str(), cmd.data(), nullptr, nullptr, TRUE, flags,
                                nullptr, nullptr, &si.StartupInfo, &pi);
  DeleteProcThreadAttributeList(attrs);
  CloseHandle(in_read);
  CloseHandle(out_write);
  if (!created) {
    error = "CreateProcess failed: " + std::to_string(GetLastError());
    CloseHandle(in_write);
    CloseHandle(out_read);
    return false;
  }
  process = pi.hProcess;
  thread = pi.hThread;
  pid = int(pi.dwProcessId);
  read = out_read;
  write = in_write;
  job = CreateJobObjectW(nullptr, nullptr);
  JOBOBJECT_EXTENDED_LIMIT_INFORMATION limits{};
  limits.BasicLimitInformation.LimitFlags =
      JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE | JOB_OBJECT_LIMIT_DIE_ON_UNHANDLED_EXCEPTION |
      JOB_OBJECT_LIMIT_ACTIVE_PROCESS | JOB_OBJECT_LIMIT_PROCESS_MEMORY;
  limits.BasicLimitInformation.ActiveProcessLimit = 1;
  limits.ProcessMemoryLimit = 256ull * 1024 * 1024;
  SetInformationJobObject(job, JobObjectExtendedLimitInformation, &limits, sizeof(limits));
  if (!AssignProcessToJobObject(job, process)) {
    error = "cannot assign renderer job";
    TerminateProcess(process, 1);
    return false;
  }
  ResumeThread(thread);
  (void)made_profile;
  return true;
}
bool Process::send(const Json &j, std::string &error) {
  std::lock_guard lock(write_mutex);
  return write_frame(write, j, error);
}
std::optional<Json> Process::receive(int timeout) {
  return read_frame(read, timeout);
}
void Process::terminate() {
  if (process)
    TerminateProcess(process, 0);
  if (process) {
    WaitForSingleObject(process, 2000);
    CloseHandle(process);
    process = nullptr;
  }
  if (thread) {
    CloseHandle(thread);
    thread = nullptr;
  }
}
bool Process::alive() const {
  return process && WaitForSingleObject(process, 0) == WAIT_TIMEOUT;
}
std::string executable_path() {
  wchar_t buf[32768];
  DWORD n = GetModuleFileNameW(nullptr, buf, 32768);
  return narrow(std::wstring(buf, n));
}
#else
Process::~Process() {
  terminate();
  if (read)
    close(int(intptr_t(read)));
  if (write)
    close(int(intptr_t(write)));
}
bool Process::start(const std::string &exe, const std::string &args, bool sandbox,
                    std::string &error) {
  if (sandbox) {
    error = "Windows AppContainer sandbox is not available on this platform";
    return false;
  }
  int in_pipe[2], out_pipe[2];
  if (pipe(in_pipe) || pipe(out_pipe)) {
    error = "pipe failed";
    return false;
  }
  std::vector<std::string> parts = split(args, ' ');
  std::vector<char *> argv;
  argv.push_back(const_cast<char *>(exe.c_str()));
  for (auto &p : parts)
    if (!p.empty())
      argv.push_back(const_cast<char *>(p.c_str()));
  argv.push_back(nullptr);
  pid_t child_pid = fork();
  if (child_pid < 0) {
    error = "fork failed";
    return false;
  }
  if (child_pid == 0) {
    dup2(in_pipe[0], 0);
    dup2(out_pipe[1], 1);
    close(in_pipe[1]);
    close(out_pipe[0]);
    execv(exe.c_str(), argv.data());
    _exit(127);
  }
  close(in_pipe[0]);
  close(out_pipe[1]);
  write = reinterpret_cast<void *>(intptr_t(in_pipe[1]));
  read = reinterpret_cast<void *>(intptr_t(out_pipe[0]));
  pid = child_pid;
  restricted = false;
  return true;
}
bool Process::send(const Json &j, std::string &error) {
  std::lock_guard lock(write_mutex);
  return write_frame(write, j, error);
}
std::optional<Json> Process::receive(int timeout) {
  return read_frame(read, timeout);
}
void Process::terminate() {
  if (pid > 0) {
    kill(pid, SIGKILL);
    waitpid(pid, nullptr, 0);
    pid = 0;
  }
}
bool Process::alive() const {
  return pid > 0 && kill(pid, 0) == 0;
}
std::string executable_path() {
  char buf[4096];
  ssize_t n = readlink("/proc/self/exe", buf, sizeof(buf) - 1);
  if (n <= 0)
    return {};
  buf[n] = 0;
  return buf;
}
#endif
std::string executable_dir() {
  auto p = executable_path();
  auto slash = p.find_last_of("/\\");
  return slash == std::string::npos ? "." : p.substr(0, slash);
}
} // namespace pv
