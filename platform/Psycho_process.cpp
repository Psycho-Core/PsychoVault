#include "Psycho_process.hpp"
#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <aclapi.h>
#include <sddl.h>
#include <userenv.h>
#ifndef DESKTOP_ALL_ACCESS
#define DESKTOP_ALL_ACCESS (STANDARD_RIGHTS_REQUIRED | 0x01FF)
#endif
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
namespace {
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
std::optional<Json> decode_frame(Bytes &buffer) {
  if (buffer.size() < 4)
    return std::nullopt;
  uint32_t n = get32(buffer.data());
  if (n > kMaxIpcBytes)
    throw std::runtime_error("IPC frame exceeds byte limit");
  if (buffer.size() < size_t(n) + 4)
    return std::nullopt;
  std::string_view text(reinterpret_cast<const char *>(buffer.data() + 4), n);
  auto result = Json::parse(text, kMaxIpcBytes);
  buffer.erase(buffer.begin(), buffer.begin() + 4 + n);
  if (!result)
    throw std::runtime_error("IPC frame is not valid JSON");
  if (result->type != Json::Type::Object)
    throw std::runtime_error("IPC frame is not an object");
  return result;
}
#ifdef _WIN32
struct WinHandle {
  HANDLE value = nullptr;
  WinHandle() = default;
  explicit WinHandle(HANDLE h) : value(h) {}
  ~WinHandle() {
    reset();
  }
  WinHandle(const WinHandle &) = delete;
  WinHandle &operator=(const WinHandle &) = delete;
  void reset(HANDLE h = nullptr) {
    if (value && value != INVALID_HANDLE_VALUE)
      CloseHandle(value);
    value = h;
  }
  HANDLE release() {
    HANDLE h = value;
    value = nullptr;
    return h;
  }
};
std::wstring wide(const std::string &s) {
  if (s.empty())
    return {};
  int n = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, s.data(), int(s.size()), nullptr, 0);
  if (n <= 0)
    throw std::runtime_error("invalid UTF-8 in platform string");
  std::wstring w(size_t(n), 0);
  MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, s.data(), int(s.size()), w.data(), n);
  return w;
}
std::string narrow(const std::wstring &w) {
  if (w.empty())
    return {};
  int n = WideCharToMultiByte(CP_UTF8, 0, w.data(), int(w.size()), nullptr, 0, nullptr, nullptr);
  std::string s(size_t(n), 0);
  WideCharToMultiByte(CP_UTF8, 0, w.data(), int(w.size()), s.data(), n, nullptr, nullptr);
  return s;
}
std::string os_error(const char *operation, DWORD code = GetLastError()) {
  return std::string(operation) + " (Windows error " + std::to_string(code) + ")";
}
bool grant_path(const std::wstring &path, PSID sid, ACCESS_MASK access) {
  PACL old = nullptr;
  PSECURITY_DESCRIPTOR sd = nullptr;
  if (GetNamedSecurityInfoW(path.c_str(), SE_FILE_OBJECT, DACL_SECURITY_INFORMATION, nullptr,
                            nullptr, &old, nullptr, &sd) != ERROR_SUCCESS)
    return false;
  EXPLICIT_ACCESSW ea{};
  ea.grfAccessPermissions = access;
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
bool grant_object(HANDLE object, PSID sid, ACCESS_MASK access, std::string &error) {
  PACL old = nullptr;
  PSECURITY_DESCRIPTOR sd = nullptr;
  DWORD info = GetSecurityInfo(object, SE_WINDOW_OBJECT, DACL_SECURITY_INFORMATION, nullptr,
                               nullptr, &old, nullptr, &sd);
  if (info != ERROR_SUCCESS) {
    error = os_error("GetSecurityInfo window object", info);
    return false;
  }
  EXPLICIT_ACCESSW ea{};
  ea.grfAccessPermissions = access;
  ea.grfAccessMode = GRANT_ACCESS;
  ea.grfInheritance = NO_INHERITANCE;
  BuildTrusteeWithSidW(&ea.Trustee, sid);
  PACL updated = nullptr;
  DWORD entries = SetEntriesInAclW(1, &ea, old, &updated);
  DWORD set = entries == ERROR_SUCCESS
                  ? SetSecurityInfo(object, SE_WINDOW_OBJECT, DACL_SECURITY_INFORMATION, nullptr,
                                    nullptr, updated, nullptr)
                  : entries;
  if (updated)
    LocalFree(updated);
  if (sd)
    LocalFree(sd);
  if (set != ERROR_SUCCESS) {
    error = os_error("SetSecurityInfo window object", set);
    return false;
  }
  return true;
}
bool set_private_object_low_label(HANDLE object, std::string &error) {
  PSECURITY_DESCRIPTOR sd = nullptr;
  ULONG size = 0;
  if (!ConvertStringSecurityDescriptorToSecurityDescriptorW(L"S:(ML;;NW;;;LW)", SDDL_REVISION_1,
                                                            &sd, &size)) {
    error = os_error("ConvertStringSecurityDescriptorToSecurityDescriptor");
    return false;
  }
  BOOL present = FALSE, defaulted = FALSE;
  PACL label = nullptr;
  BOOL got = GetSecurityDescriptorSacl(sd, &present, &label, &defaulted);
  DWORD rc = got && present ? SetSecurityInfo(object, SE_WINDOW_OBJECT, LABEL_SECURITY_INFORMATION,
                                              nullptr, nullptr, nullptr, label)
                            : ERROR_INVALID_SECURITY_DESCR;
  LocalFree(sd);
  if (rc != ERROR_SUCCESS) {
    error = os_error("SetSecurityInfo low mandatory label", rc);
    return false;
  }
  return true;
}
bool grant_private_object_principals(HANDLE object, PSID app_sid, ACCESS_MASK access,
                                     std::string &error) {
  if (!grant_object(object, app_sid, access, error))
    return false;
  HANDLE token = nullptr;
  std::vector<uint8_t> buffer;
  if (OpenProcessToken(GetCurrentProcess(), TOKEN_QUERY, &token)) {
    DWORD needed = 0;
    GetTokenInformation(token, TokenUser, nullptr, 0, &needed);
    buffer.resize(needed);
    if (needed && GetTokenInformation(token, TokenUser, buffer.data(), needed, &needed)) {
      PSID user = reinterpret_cast<PTOKEN_USER>(buffer.data())->User.Sid;
      if (!grant_object(object, user, access, error)) {
        CloseHandle(token);
        return false;
      }
    }
    needed = 0;
    GetTokenInformation(token, TokenLogonSid, nullptr, 0, &needed);
    buffer.resize(needed);
    if (needed && GetTokenInformation(token, TokenLogonSid, buffer.data(), needed, &needed)) {
      PSID logon = reinterpret_cast<PTOKEN_GROUPS>(buffer.data())->Groups[0].Sid;
      if (!grant_object(object, logon, access, error)) {
        CloseHandle(token);
        return false;
      }
    }
    CloseHandle(token);
  }
  SID_IDENTIFIER_AUTHORITY authority = {0, 0, 0, 0, 0, 15};
  PSID all_packages = nullptr;
  if (AllocateAndInitializeSid(&authority, 2, 2, 1, 0, 0, 0, 0, 0, 0, &all_packages)) {
    bool ok = grant_object(object, all_packages, access, error);
    FreeSid(all_packages);
    if (!ok)
      return false;
  }
  return true;
}
bool overlapped_io(HANDLE file, void *data, DWORD size, bool reading, HANDLE stop, int timeout,
                   size_t &transferred, std::string &error) {
  transferred = 0;
  DWORD done = 0;
  WinHandle event(CreateEventW(nullptr, TRUE, FALSE, nullptr));
  if (!event.value) {
    error = os_error("CreateEvent");
    return false;
  }
  OVERLAPPED ov{};
  ov.hEvent = event.value;
  BOOL ok = reading ? ReadFile(file, data, size, nullptr, &ov)
                    : WriteFile(file, data, size, nullptr, &ov);
  DWORD code = ok ? ERROR_SUCCESS : GetLastError();
  if (!ok && code != ERROR_IO_PENDING) {
    error = os_error(reading ? "ReadFile" : "WriteFile", code);
    return false;
  }
  if (!ok) {
    HANDLE waits[2] = {event.value, stop};
    DWORD wait =
        WaitForMultipleObjects(stop ? 2 : 1, waits, FALSE, timeout < 0 ? INFINITE : DWORD(timeout));
    if (wait != WAIT_OBJECT_0) {
      CancelIoEx(file, &ov);
      // The OVERLAPPED and its buffer must stay alive until cancellation ends.
      GetOverlappedResult(file, &ov, &done, TRUE);
      transferred = done;
      error = wait == WAIT_OBJECT_0 + 1 ? "I/O cancelled" : "I/O timeout";
      return false;
    }
  }
  if (!GetOverlappedResult(file, &ov, &done, FALSE)) {
    error = os_error("GetOverlappedResult");
    return false;
  }
  transferred = done;
  if (!transferred)
    error = "I/O end of stream";
  return transferred != 0;
}
#endif
} // namespace
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
Process::Process() {
#ifdef _WIN32
  stop_event_ = CreateEventW(nullptr, TRUE, FALSE, nullptr);
#endif
}
Process::~Process() {
  terminate();
  close_transport();
#ifdef _WIN32
  if (desktop_)
    CloseDesktop(HDESK(desktop_));
  if (station_)
    CloseWindowStation(HWINSTA(station_));
  if (job_)
    CloseHandle(job_);
  if (stop_event_)
    CloseHandle(stop_event_);
  if (sid_)
    FreeSid(sid_);
#endif
}
void Process::close_transport() {
#ifdef _WIN32
  if (read_)
    CloseHandle(read_);
  if (write_)
    CloseHandle(write_);
#else
  if (read_)
    ::close(int(intptr_t(read_)));
  if (write_)
    ::close(int(intptr_t(write_)));
#endif
  read_ = write_ = nullptr;
  input_buffer_.clear();
}
void Process::request_stop() {
  stop_requested_.store(true, std::memory_order_release);
#ifdef _WIN32
  if (stop_event_)
    SetEvent(stop_event_);
#endif
}
bool Process::read_more(int timeout_ms) {
  uint8_t chunk[65536];
  size_t got = 0;
#ifdef _WIN32
  if (!overlapped_io(read_, chunk, static_cast<DWORD>(sizeof(chunk)), true, stop_event_, timeout_ms,
                     got, io_error_))
    return false;
#else
  pollfd p{int(intptr_t(read_)), POLLIN, 0};
  int rc = poll(&p, 1, timeout_ms);
  if (stop_requested_.load()) {
    io_error_ = "I/O cancelled";
    return false;
  }
  if (rc == 0) {
    io_error_ = "I/O timeout";
    return true;
  }
  if (rc < 0) {
    io_error_ = "poll failed";
    return false;
  }
  ssize_t n = ::read(int(intptr_t(read_)), chunk, sizeof(chunk));
  if (n <= 0) {
    io_error_ = "I/O end of stream";
    return false;
  }
  got = size_t(n);
#endif
  if (input_buffer_.size() + got > kMaxIpcBytes + 4) {
    io_error_ = "IPC buffered bytes exceed limit";
    return false;
  }
  input_buffer_.insert(input_buffer_.end(), chunk, chunk + got);
  return true;
}
#ifdef _WIN32
bool Process::start(const std::string &exe, const std::string &args, bool sandbox,
                    std::string &error) {
  if (process_ || stopping()) {
    error = "process cannot be restarted";
    return false;
  }
  if (!stop_event_) {
    error = os_error("CreateEvent");
    return false;
  }
  auto fail = [&](const std::string &message) {
    error = message;
    terminate();
    close_transport();
    return false;
  };
  std::string unique;
  for (auto b : random_bytes(12)) {
    const char *hex = "0123456789abcdef";
    unique += hex[b >> 4];
    unique += hex[b & 15];
  }
  std::wstring pipe_name =
      L"\\\\.\\pipe\\PsychoVault." + std::to_wstring(GetCurrentProcessId()) + L"." + wide(unique);
  SECURITY_ATTRIBUTES sa{sizeof(sa), nullptr, TRUE};
  WinHandle server(CreateNamedPipeW(
      pipe_name.c_str(), PIPE_ACCESS_DUPLEX | FILE_FLAG_OVERLAPPED | FILE_FLAG_FIRST_PIPE_INSTANCE,
      PIPE_TYPE_BYTE | PIPE_READMODE_BYTE | PIPE_WAIT | PIPE_REJECT_REMOTE_CLIENTS, 1, 65536, 65536,
      0, &sa));
  if (server.value == INVALID_HANDLE_VALUE) {
    server.value = nullptr;
    return fail(os_error("CreateNamedPipe"));
  }
  SetHandleInformation(server.value, HANDLE_FLAG_INHERIT, 0);
  WinHandle child(CreateFileW(pipe_name.c_str(), GENERIC_READ | GENERIC_WRITE, 0, &sa,
                              OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr));
  if (child.value == INVALID_HANDLE_VALUE) {
    child.value = nullptr;
    return fail(os_error("Connect renderer pipe"));
  }
  WinHandle connect_event(CreateEventW(nullptr, TRUE, FALSE, nullptr));
  OVERLAPPED connect_ov{};
  connect_ov.hEvent = connect_event.value;
  if (!ConnectNamedPipe(server.value, &connect_ov)) {
    DWORD rc = GetLastError();
    if (rc == ERROR_IO_PENDING && WaitForSingleObject(connect_event.value, 5000) == WAIT_OBJECT_0) {
      DWORD ignored = 0;
      GetOverlappedResult(server.value, &connect_ov, &ignored, FALSE);
    } else if (rc != ERROR_PIPE_CONNECTED)
      return fail(os_error("ConnectNamedPipe", rc));
  }
  PSID app_sid = nullptr;
  std::wstring desktop_name;
  if (sandbox) {
    std::wstring container = L"PsychoVault.Renderer";
    HRESULT hr =
        CreateAppContainerProfile(container.c_str(), L"PsychoVault renderer",
                                  L"Isolated PsychoVault website renderer", nullptr, 0, &app_sid);
    if (hr == HRESULT_FROM_WIN32(ERROR_ALREADY_EXISTS))
      hr = DeriveAppContainerSidFromAppContainerName(container.c_str(), &app_sid);
    if (FAILED(hr) || !app_sid)
      return fail(os_error("CreateAppContainerProfile", DWORD(hr)));
    sid_ = app_sid;
    if (!grant_path(wide(exe), app_sid, FILE_GENERIC_READ | FILE_GENERIC_EXECUTE))
      return fail("cannot grant read/execute on renderer executable");
    // Only traverse ancestors. Do not grant directory listing or write access.
    for (size_t cut = exe.find_last_of("/\\"); cut != std::string::npos && cut > 2;
         cut = exe.find_last_of("/\\", cut - 1)) {
      if (!grant_path(wide(exe.substr(0, cut)), app_sid,
                      FILE_TRAVERSE | FILE_READ_ATTRIBUTES | SYNCHRONIZE))
        return fail("cannot grant renderer ancestor traversal");
    }
    std::wstring station_name = L"PsychoVault." + wide(unique);
    station_ = CreateWindowStationW(station_name.c_str(), 0,
                                    WINSTA_ALL_ACCESS | STANDARD_RIGHTS_REQUIRED, nullptr);
    if (!station_)
      return fail(os_error("CreateWindowStation"));
    desktop_ = CreateDesktopW(L"Renderer", nullptr, nullptr, 0, DESKTOP_ALL_ACCESS, nullptr);
    if (!desktop_)
      return fail(os_error("CreateDesktop"));
    // These grants are on private renderer objects only. I include the user,
    // logon and application-package principals because USER32 initialization
    // can check more than the package SID alone.
    ACCESS_MASK station_access = WINSTA_ALL_ACCESS | STANDARD_RIGHTS_REQUIRED;
    ACCESS_MASK desktop_access = DESKTOP_ALL_ACCESS;
    if (!grant_private_object_principals(station_, app_sid, station_access, error))
      return fail("cannot grant private window-station access: " + error);
    if (!grant_private_object_principals(desktop_, app_sid, desktop_access, error))
      return fail("cannot grant private desktop access: " + error);
    if (!set_private_object_low_label(station_, error))
      return fail("cannot label private window station: " + error);
    if (!set_private_object_low_label(desktop_, error))
      return fail("cannot label private desktop: " + error);
    desktop_name = station_name + L"\\Renderer";
    restricted = true;
  }
  STARTUPINFOEXW si{};
  si.StartupInfo.cb = DWORD(sizeof(si));
  si.StartupInfo.dwFlags = STARTF_USESTDHANDLES | STARTF_USESHOWWINDOW;
  si.StartupInfo.wShowWindow = SW_HIDE;
  si.StartupInfo.hStdInput = child.value;
  si.StartupInfo.hStdOutput = child.value;
  si.StartupInfo.hStdError = child.value;
  if (!desktop_name.empty())
    si.StartupInfo.lpDesktop = desktop_name.data();
  SIZE_T size = 0;
  DWORD attribute_count = sandbox ? 2 : 1;
  InitializeProcThreadAttributeList(nullptr, attribute_count, 0, &size);
  if (!size)
    return fail(os_error("InitializeProcThreadAttributeList size"));
  std::vector<uint8_t> buffer(size);
  auto attrs = reinterpret_cast<LPPROC_THREAD_ATTRIBUTE_LIST>(buffer.data());
  if (!InitializeProcThreadAttributeList(attrs, attribute_count, 0, &size))
    return fail(os_error("InitializeProcThreadAttributeList"));
  HANDLE inherit[1] = {child.value};
  SECURITY_CAPABILITIES caps{};
  caps.AppContainerSid = app_sid;
  bool attrs_ok = UpdateProcThreadAttribute(attrs, 0, PROC_THREAD_ATTRIBUTE_HANDLE_LIST, inherit,
                                            sizeof(inherit), nullptr, nullptr) != FALSE;
  if (sandbox)
    attrs_ok =
        attrs_ok && UpdateProcThreadAttribute(attrs, 0, PROC_THREAD_ATTRIBUTE_SECURITY_CAPABILITIES,
                                              &caps, sizeof(caps), nullptr, nullptr) != FALSE;
  if (!attrs_ok) {
    DeleteProcThreadAttributeList(attrs);
    return fail(os_error("UpdateProcThreadAttribute"));
  }
  si.lpAttributeList = attrs;
  std::wstring command = L"\"" + wide(exe) + L"\" " + wide(args);
  PROCESS_INFORMATION pi{};
  DWORD flags = EXTENDED_STARTUPINFO_PRESENT | CREATE_SUSPENDED;
  BOOL created = CreateProcessW(wide(exe).c_str(), command.data(), nullptr, nullptr, TRUE, flags,
                                nullptr, nullptr, &si.StartupInfo, &pi);
  DWORD create_error = created ? 0 : GetLastError();
  DeleteProcThreadAttributeList(attrs);
  child.reset();
  if (!created)
    return fail(os_error("CreateProcess", create_error));
  process_ = pi.hProcess;
  thread_ = pi.hThread;
  pid = int(pi.dwProcessId);
  read_ = write_ = server.release();
  job_ = CreateJobObjectW(nullptr, nullptr);
  if (!job_)
    return fail(os_error("CreateJobObject"));
  JOBOBJECT_EXTENDED_LIMIT_INFORMATION limits{};
  limits.BasicLimitInformation.LimitFlags =
      JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE | JOB_OBJECT_LIMIT_DIE_ON_UNHANDLED_EXCEPTION |
      JOB_OBJECT_LIMIT_ACTIVE_PROCESS | JOB_OBJECT_LIMIT_PROCESS_MEMORY;
  limits.BasicLimitInformation.ActiveProcessLimit = 1;
  limits.ProcessMemoryLimit = SIZE_T(512) * 1024 * 1024;
  if (!SetInformationJobObject(job_, JobObjectExtendedLimitInformation, &limits, sizeof(limits)))
    return fail(os_error("SetInformationJobObject"));
  JOBOBJECT_BASIC_UI_RESTRICTIONS ui{};
  ui.UIRestrictionsClass = JOB_OBJECT_UILIMIT_HANDLES | JOB_OBJECT_UILIMIT_SYSTEMPARAMETERS |
                           JOB_OBJECT_UILIMIT_DISPLAYSETTINGS | JOB_OBJECT_UILIMIT_EXITWINDOWS;
  if (!SetInformationJobObject(job_, JobObjectBasicUIRestrictions, &ui, sizeof(ui)))
    return fail(os_error("SetInformationJobObject UI restrictions"));
  if (!AssignProcessToJobObject(job_, process_))
    return fail(os_error("AssignProcessToJobObject"));
  if (ResumeThread(thread_) == DWORD(-1))
    return fail(os_error("ResumeThread"));
  return true;
}
bool Process::send(const Json &j, std::string &error) {
  std::lock_guard lock(write_mutex_);
  if (stopping() || io_broken_ || !write_) {
    error = "renderer I/O is stopped";
    return false;
  }
  auto text = j.dump();
  if (text.size() > kMaxIpcBytes) {
    error = "IPC message too large";
    return false;
  }
  Bytes framed(4 + text.size());
  put32(framed.data(), uint32_t(text.size()));
  std::copy(text.begin(), text.end(), framed.begin() + 4);
  size_t done = 0;
  if (!overlapped_io(write_, framed.data(), DWORD(framed.size()), false, stop_event_, 5000, done,
                     error) ||
      done != framed.size()) {
    io_broken_ = true;
    io_error_ = error;
    return false;
  }
  return true;
}
std::optional<Json> Process::receive(int timeout) {
  if (io_broken_ || stopping() || !read_)
    return std::nullopt;
  try {
    auto result = decode_frame(input_buffer_);
    if (result)
      return result;
    if (!read_more(timeout)) {
      if (stopping() || io_error_ == "I/O timeout")
        return std::nullopt;
      io_broken_ = true;
      return std::nullopt;
    }
    return decode_frame(input_buffer_);
  } catch (const std::exception &e) {
    io_broken_ = true;
    io_error_ = e.what();
    return std::nullopt;
  }
}
void Process::terminate() {
  request_stop();
  if (process_)
    TerminateProcess(process_, 0);
  if (process_) {
    WaitForSingleObject(process_, 2000);
    CloseHandle(process_);
    process_ = nullptr;
  }
  if (thread_) {
    CloseHandle(thread_);
    thread_ = nullptr;
  }
}
bool Process::alive() const {
  return process_ && WaitForSingleObject(process_, 0) == WAIT_TIMEOUT;
}
std::optional<uint32_t> Process::exit_code() const {
  if (!process_)
    return std::nullopt;
  DWORD code = 0;
  if (!GetExitCodeProcess(process_, &code) || code == STILL_ACTIVE)
    return std::nullopt;
  return uint32_t(code);
}
std::string executable_path() {
  std::vector<wchar_t> buf(32768);
  DWORD n = GetModuleFileNameW(nullptr, buf.data(), DWORD(buf.size()));
  if (!n || n >= buf.size())
    return {};
  return narrow(std::wstring(buf.data(), n));
}
#else
bool Process::start(const std::string &exe, const std::string &args, bool sandbox,
                    std::string &error) {
  if (sandbox) {
    error = "Windows AppContainer sandbox is not available on this platform";
    return false;
  }
  if (pid || stopping()) {
    error = "process cannot be restarted";
    return false;
  }
  int in_pipe[2], out_pipe[2];
  if (pipe(in_pipe)) {
    error = "pipe failed";
    return false;
  }
  if (pipe(out_pipe)) {
    ::close(in_pipe[0]);
    ::close(in_pipe[1]);
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
    for (int fd : {in_pipe[0], in_pipe[1], out_pipe[0], out_pipe[1]})
      ::close(fd);
    error = "fork failed";
    return false;
  }
  if (child_pid == 0) {
    dup2(in_pipe[0], 0);
    dup2(out_pipe[1], 1);
    for (int fd : {in_pipe[0], in_pipe[1], out_pipe[0], out_pipe[1]})
      ::close(fd);
    execv(exe.c_str(), argv.data());
    _exit(127);
  }
  ::close(in_pipe[0]);
  ::close(out_pipe[1]);
  write_ = reinterpret_cast<void *>(intptr_t(in_pipe[1]));
  read_ = reinterpret_cast<void *>(intptr_t(out_pipe[0]));
  pid = child_pid;
  return true;
}
bool Process::send(const Json &j, std::string &error) {
  std::lock_guard lock(write_mutex_);
  if (stopping() || io_broken_) {
    error = "renderer I/O is stopped";
    return false;
  }
  return write_frame(write_, j, error);
}
std::optional<Json> Process::receive(int timeout) {
  if (io_broken_ || stopping() || !read_)
    return std::nullopt;
  try {
    auto result = decode_frame(input_buffer_);
    if (result)
      return result;
    if (!read_more(timeout)) {
      io_broken_ = true;
      return std::nullopt;
    }
    return decode_frame(input_buffer_);
  } catch (const std::exception &e) {
    io_broken_ = true;
    io_error_ = e.what();
    return std::nullopt;
  }
}
void Process::terminate() {
  request_stop();
  if (pid > 0) {
    kill(pid, SIGKILL);
    waitpid(pid, nullptr, 0);
    pid = 0;
  }
}
bool Process::alive() const {
  return pid > 0 && kill(pid, 0) == 0;
}
std::optional<uint32_t> Process::exit_code() const {
  if (pid <= 0)
    return std::nullopt;
  int status = 0;
  if (waitpid(pid, &status, WNOHANG) != pid)
    return std::nullopt;
  return WIFEXITED(status) ? uint32_t(WEXITSTATUS(status)) : uint32_t(128 + WTERMSIG(status));
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
