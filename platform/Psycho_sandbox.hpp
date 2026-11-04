// PsychoVault original source. My confinement layer. I would rather describe what
// this actually does than call it a sandbox: it is a job object that caps the memory
// and CPU of my own process, so a page that runs away takes the tab down instead of
// the machine. It is not an isolation boundary, and the status text says so.
#pragma once
#include "../engine/Psycho_base.hpp"

namespace pv {

// What confinement I managed to apply, in words a person can check against the
// settings panel rather than a green tick.
struct ConfinementStatus {
  bool limited = false;      // a job object with a memory cap is in force
  bool isolated = false;     // page code runs in a different security context
  std::string mode;          // short label: "job object", "none"
  std::string reason;        // why, when it is not in force
  uint64_t memory_bytes = 0; // the cap, when there is one
  uint32_t pid = 0;
};

// Caps the memory and CPU this process may use. A memory cap of zero means no cap.
// Returns false and fills `error` when the platform refuses, which is a state the
// panel shows rather than hides.
bool confine_process(uint64_t memory_bytes, uint32_t cpu_percent, std::string &error);
void release_confinement();
ConfinementStatus confinement_status();

// The honest sentence the panel prints. Kept here so the window code and my
// diagnostics cannot drift apart.
std::string confinement_summary(const ConfinementStatus &status);

} // namespace pv
