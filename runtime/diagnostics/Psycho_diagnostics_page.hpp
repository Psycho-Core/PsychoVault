// PsychoVault original source. Plain HTML for psychovault://nettest and
// psychovault://version. The pages carry no script and show exactly what the
// diagnostics service reported: an unwired stage is shown as unavailable, never
// as a pass, and a missing version fact is shown as missing, never invented.
// P1/S038 of docs/Psycho_MASTER-PLAN.md.
#pragma once
#include "Psycho_diagnostics.hpp"
#include <string>
#include <vector>

namespace pv::rt {

// Escapes text for HTML element content and quoted attribute values.
std::string diagnostics_html_escape(const std::string &text);

// One row per stage, in the order given (run_nettest() returns plan order).
std::string render_nettest_page(const std::vector<DiagnosticStageResult> &stages);

// When `available` is false the page names the refusal in `error` and shows no
// facts at all.
std::string render_version_page(bool available, const VersionInfo &info,
                                const Error &error);

} // namespace pv::rt
