// PsychoVault original source. Validated argv for the browser entry point.
// P1/S041 of docs/Psycho_MASTER-PLAN.md.
#pragma once
#include "../ipc/Psycho_bus.hpp"
#include <string>
#include <vector>

namespace pv::rt {

struct CommandLineOptions {
  std::vector<std::string> urls;
  std::string profile_directory;
  std::string language;
  std::string capture_path; // retained for the shell's conformance harness
  bool private_mode = false;
  bool diagnostics = false;
  bool smoke = false;
  bool expect_image = false;
};

// Parses argv without constructing a shell command or starting a process.
// URLs are HTTP(S), about:blank, or registered psychovault:// pages only.
// Repeated --url and positional URLs preserve input order.
bool parse_command_line(const std::vector<std::string> &arguments,
                        CommandLineOptions &options, Error &error);
std::vector<std::string> command_line_startup_urls(const CommandLineOptions &options);

bool valid_language_tag(const std::string &language);

} // namespace pv::rt
