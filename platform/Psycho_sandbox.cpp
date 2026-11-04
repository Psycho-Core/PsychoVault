// PsychoVault original source. A job object with a memory cap, a CPU rate and a rule
// that kills the process rather than letting an unhandled exception sit there. The
// page still runs in this process, so this is a resource boundary and I do not call
// it isolation.
#include "Psycho_sandbox.hpp"

#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#endif

namespace pv {
namespace {
#ifdef _WIN32
HANDLE g_job = nullptr;

std::string last_error_text(const char *what) {
  return std::string(what) + " (error " + std::to_string(GetLastError()) + ")";
}
#endif
} // namespace

bool confine_process(uint64_t memory_bytes, uint32_t cpu_percent, std::string &error) {
#ifdef _WIN32
  if (memory_bytes == 0 && cpu_percent == 0) {
    error = "I was asked for no limit, so I applied none";
    return false;
  }
  if (!g_job) {
    g_job = CreateJobObjectW(nullptr, nullptr);
    if (!g_job) {
      error = last_error_text("I could not create a job object");
      return false;
    }
  }
  JOBOBJECT_EXTENDED_LIMIT_INFORMATION limits{};
  limits.BasicLimitInformation.LimitFlags = JOB_OBJECT_LIMIT_DIE_ON_UNHANDLED_EXCEPTION |
                                            JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE;
  if (memory_bytes > 0) {
    limits.BasicLimitInformation.LimitFlags |= JOB_OBJECT_LIMIT_PROCESS_MEMORY;
    limits.ProcessMemoryLimit = SIZE_T(memory_bytes);
  }
  if (cpu_percent > 0 && cpu_percent <= 100) {
    // A single-process CPU rate needs a completion port to report overruns, which I
    // do not set up, so I cap the wall-clock per process instead: the job is killed
    // if it uses more than the allowance for one minute of unbroken work.
    limits.BasicLimitInformation.LimitFlags |= JOB_OBJECT_LIMIT_PROCESS_TIME;
    limits.PerProcessUserTimeLimit.QuadPart = LONGLONG(cpu_percent) * 6000000LL;
  }
  if (!SetInformationJobObject(g_job, JobObjectExtendedLimitInformation, &limits,
                               sizeof(limits))) {
    error = last_error_text("I could not set my job limits");
    return false;
  }
  if (!AssignProcessToJobObject(g_job, GetCurrentProcess())) {
    DWORD code = GetLastError();
    // A process that already belongs to a job can refuse a second one on older
    // builds. That is a real refusal and I report it rather than pretend.
    error = "I could not put my process in the job" + std::string(" (error ") +
            std::to_string(code) + ")";
    return false;
  }
  error.clear();
  return true;
#else
  (void)memory_bytes;
  (void)cpu_percent;
  error = "This build has no job objects, so I applied no limit";
  return false;
#endif
}

void release_confinement() {
#ifdef _WIN32
  if (g_job) {
    CloseHandle(g_job);
    g_job = nullptr;
  }
#endif
}

ConfinementStatus confinement_status() {
  ConfinementStatus status;
#ifdef _WIN32
  status.pid = GetCurrentProcessId();
  if (!g_job) {
    status.mode = "none";
    status.reason = "I did not apply a memory cap for this session";
    return status;
  }
  JOBOBJECT_EXTENDED_LIMIT_INFORMATION limits{};
  if (!QueryInformationJobObject(g_job, JobObjectExtendedLimitInformation, &limits,
                                 sizeof(limits), nullptr)) {
    status.mode = "none";
    status.reason = "I could not read my job limits back (error " +
                    std::to_string(GetLastError()) + ")";
    return status;
  }
  ULONG_PTR flags = limits.BasicLimitInformation.LimitFlags;
  status.limited = (flags & JOB_OBJECT_LIMIT_PROCESS_MEMORY) != 0 &&
                   limits.ProcessMemoryLimit > 0;
  status.memory_bytes = uint64_t(limits.ProcessMemoryLimit);
  status.mode = status.limited ? "job object" : "job object, no memory cap";
  // The page runs in this process. Saying otherwise would be the exact kind of claim
  // I keep telling myself not to make.
  status.isolated = false;
  status.reason = "page code runs in my process; a job object limits it, it does not isolate it";
#else
  status.mode = "none";
  status.reason = "this build has no job objects, so page code runs with no limit";
#endif
  return status;
}

std::string confinement_summary(const ConfinementStatus &status) {
  std::string text = status.limited ? "Memory cap in force: " : "No memory cap: ";
  if (status.limited)
    text += std::to_string(status.memory_bytes / (1024 * 1024)) + " MiB";
  else
    text += status.reason;
  text += ". Page code is not isolated from my process.";
  return text;
}

} // namespace pv
