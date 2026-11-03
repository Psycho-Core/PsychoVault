// I test the real renderer boundary, not an unsandboxed helper process.
#include "../platform/process.hpp"
#include <filesystem>
#include <iostream>
int main() {
  using namespace pv;
  std::filesystem::path sentinel;
  try {
    auto suffix = base64_encode(random_bytes(12));
    for (char &c : suffix)
      if (c == '+' || c == '/')
        c = 'x';
    sentinel = std::filesystem::temp_directory_path() /
               ("psychovault-sandbox-sentinel-" + suffix + ".tmp");
    std::string error;
    if (!write_file_atomic(sentinel.string(), "broker-only sandbox sentinel\n", error)) {
      std::cerr << "sandbox fixture failed: " << error << "\n";
      return 1;
    }
    Process plain;
    if (plain.start(executable_dir() + "\\pv-sandbox-minimal.exe",
                    "--sandbox-probe \"" + sentinel.string() + "\"", false, error)) {
      auto report = plain.receive(5000);
      if (report)
        std::cout << "Unsandboxed minimal probe reached main: AppContainer="
                  << report->flag("appContainer") << "\n";
      else
        std::cout << "Unsandboxed minimal probe produced no report; I/O=" << plain.io_error()
                  << "\n";
      plain.terminate();
    } else
      std::cout << "Unsandboxed minimal start failed: " << error << "\n";
    Process minimal;
    if (!minimal.start(executable_dir() + "\\pv-sandbox-minimal.exe",
                       "--sandbox-probe \"" + sentinel.string() + "\"", true, error)) {
      std::cerr << "minimal sandbox start failed: " << error << "\n";
    } else {
      auto report = minimal.receive(5000);
      auto exit = minimal.exit_code();
      if (report) {
        std::cout << "Minimal probe: AppContainer=" << report->flag("appContainer")
                  << " fileDenied=" << report->flag("fileDenied")
                  << " networkDenied=" << report->flag("networkDenied") << "\n";
      } else {
        std::cout << "Minimal probe failed. Exit code=";
        if (exit)
          std::cout << *exit;
        else
          std::cout << "running";
        std::cout << "; I/O=" << minimal.io_error() << "\n";
      }
      minimal.terminate();
    }
    Process renderer;
    if (!renderer.start(executable_dir() + "\\pv-renderer.exe",
                        "--sandbox-probe \"" + sentinel.string() + "\"", true, error)) {
      std::cerr << "sandbox start failed: " << error << "\n";
      std::error_code ignored;
      std::filesystem::remove(sentinel, ignored);
      return 1;
    }
    auto report = renderer.receive(5000);
    auto exit = renderer.exit_code();
    bool ok = renderer.sandboxed() && report && report->str("tag") == "sandbox" &&
              report->flag("appContainer") && report->flag("fileDenied") &&
              report->flag("networkTested") && report->flag("networkDenied");
    if (report) {
      std::cout << "AppContainer=" << report->flag("appContainer")
                << " fileDenied=" << report->flag("fileDenied")
                << " networkTested=" << report->flag("networkTested")
                << " TCP+UDP denied=" << report->flag("networkDenied") << "\n";
      for (const char *key : {"tokenError", "fileError", "startupError", "tcpError", "udpError"})
        std::cout << key << "=" << report->num(key) << "\n";
    } else {
      std::cout << "No sandbox report. Exit code=";
      if (exit)
        std::cout << *exit;
      else
        std::cout << "running";
      std::cout << "; I/O=" << renderer.io_error() << "\n";
    }
    renderer.terminate();
    std::error_code ignored;
    std::filesystem::remove(sentinel, ignored);
    std::cout << (ok ? "SANDBOX PASS: token, broker-file denial and direct TCP/UDP denial\n"
                     : "SANDBOX FAIL: all three boundary checks are required\n");
    return ok ? 0 : 1;
  } catch (const std::exception &e) {
    std::cerr << "sandbox test failed: " << e.what() << "\n";
    if (!sentinel.empty()) {
      std::error_code ignored;
      std::filesystem::remove(sentinel, ignored);
    }
    return 1;
  }
}
