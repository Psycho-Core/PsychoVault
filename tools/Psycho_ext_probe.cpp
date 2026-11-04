// tools/Psycho_ext_probe.cpp - I boot a vendored extension inside my own
// ExtensionHost, with the effect surfaces stubbed honestly, and print what the
// worker actually did: registered listeners, console lines, proxy writes, tab
// calls. This is how I know the Proton VPN service worker really starts in my
// engine instead of guessing from a browser icon.
#include "platform/Psycho_extension.hpp"
#include "engine/Psycho_base.hpp"
#include <iostream>
#include <map>

using namespace pv;

int main(int argc, char **argv) {
  if (argc < 2) {
    std::cerr << "usage: pv-ext-probe <extension-directory> [ticks]\n";
    return 2;
  }
  const std::string dir = argv[1];
  const int ticks = argc > 2 ? std::atoi(argv[2]) : 8;

  ExtensionHost host;
  ExtensionEffects effects;
  effects.log = [](const std::string &id, const std::string &level, const std::string &message) {
    std::cout << "[" << id << "] console." << level << ": " << message << "\n";
  };
  effects.set_proxy = [](const std::string &id, const ProxyConfig &config, std::string &) {
    std::cout << "[" << id << "] proxy.settings.set mode=" << config.mode_name()
              << " host=" << config.single.authority() << "\n";
    return true;
  };
  effects.get_proxy = [](const std::string &) { return ProxyConfig::direct(); };
  effects.clear_proxy = [](const std::string &id, std::string &) {
    std::cout << "[" << id << "] proxy.settings.clear\n";
    return true;
  };
  effects.set_webrtc_policy = [](const std::string &id, const std::string &value, std::string &) {
    std::cout << "[" << id << "] privacy.webrtc=" << value << "\n";
    return true;
  };
  effects.fetch = [](const std::string &id, const ExtensionEffects::FetchRequest &request) {
    ExtensionEffects::FetchAnswer answer;
    std::cout << "[" << id << "] fetch " << request.method << " " << request.url;
    if (!request.body.empty()) std::cout << " (" << request.body.size() << " bytes body)";
    std::cout << "\n";
    // I answer locally without any network: the probe is about my worker
    // plumbing, not Proton's servers.
    if (request.url.find("config.json") != std::string::npos) { answer.status = 404; return answer; }
    answer.status = 200;
    answer.mime = "application/json";
    answer.text = "{}";
    return answer;
  };
  effects.open_tab = [](const std::string &url, bool active) {
    std::cout << "[tabs] create url=" << url << " active=" << active << "\n";
    return 7;
  };
  effects.resolve_tab = [](int id, std::string &url) {
    if (id == 7) { url = "https://example.com/"; return true; }
    return false;
  };
  effects.close_tab = [](int id) { std::cout << "[tabs] remove " << id << "\n"; return true; };
  effects.run_in_tab = [](int, const std::string &code, std::string &result) {
    std::cout << "[tabs] executeScript " << code.substr(0, 60) << "\n";
    result = "ok";
    return true;
  };
  effects.notify = [](const std::string &id, const std::string &note, const std::string &title,
                      const std::string &message) {
    std::cout << "[" << id << "] notification #" << note << " \"" << title << "\" \""
              << message << "\"\n";
  };
  effects.idle_seconds = [] { return 0.0; };
  host.set_effects(std::move(effects));

  std::string error;
  if (!host.install_directory(dir, error)) {
    std::cerr << "install failed: " << error << "\n";
    return 1;
  }
  auto all = host.list();
  if (all.empty()) {
    std::cerr << "nothing installed\n";
    return 1;
  }
  const std::string id = all.front().manifest.id;
  std::cout << "installed id=" << id << " name=" << all.front().manifest.name << "\n";

  // A readable failure: run-time errors in the boot script are objects made of
  // name/message; dump them plainly instead of "[object Object]".
  std::string boot_error;
  bool booted = host.start_worker(id, boot_error);
  if (!booted) {
    std::cerr << "worker start failed: " << boot_error << "\n";
    auto werr = host.worker_error(id);
    if (!werr.empty()) std::cerr << "worker error: " << werr << "\n";
    for (auto &d : host.diagnostics()) std::cout << "diag: " << d << "\n";
    return 1;
  }
  std::cout << "worker started\n";
  for (int i = 0; i < ticks; ++i) host.tick();

  // Boot must have registered the listener set the extension lives by.
  auto err = host.worker_error(id);
  if (!err.empty()) std::cout << "worker error: " << err << "\n";
  for (auto &d : host.diagnostics()) std::cout << "diag: " << d << "\n";
  std::cout << "probe done\n";
  return err.empty() ? 0 : 1;
}
