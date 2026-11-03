#pragma once
// My extension engine. Extension code is the one third-party exception, so I keep
// it in a separate interpreter realm with default-deny permissions. Every
// privileged effect is validated here in the broker, never trusted from script.
#include "../engine/Psycho_js.hpp"
#include "Psycho_proxy.hpp"

namespace pv {

struct ExtensionManifest {
  int manifest_version = 3;
  std::string id, name, version, description;
  std::string service_worker;      // relative path to the background script
  std::string action_title, action_popup;
  std::vector<std::string> permissions;
  std::vector<std::string> host_permissions;
  std::vector<std::string> content_scripts;
  bool valid = false;
  std::string error;

  bool has_permission(const std::string &permission) const;
  bool matches_host(const Url &url) const;
  static ExtensionManifest parse(const std::string &text);
};

struct ExtensionRecord {
  ExtensionManifest manifest;
  std::string directory;      // empty for a bundled extension
  bool enabled = true;
  bool bundled = false;
  std::string install_error;
  Json storage = Json::make_object();
};

// I hand the worker only the capabilities its manifest declares.
struct ExtensionEffects {
  // Returns false and sets `error` when the caller lacks the permission.
  std::function<bool(const std::string &extension_id, const ProxyConfig &config,
                     std::string &error)> set_proxy;
  std::function<ProxyConfig(const std::string &extension_id)> get_proxy;
  std::function<bool(const std::string &extension_id, std::string &error)> clear_proxy;
  std::function<void(const std::string &extension_id, const std::string &level,
                     const std::string &message)> log;
  std::function<bool(const std::string &extension_id, const std::string &policy,
                     std::string &error)> set_webrtc_policy;
};

class ExtensionHost {
public:
  ExtensionHost();
  ~ExtensionHost();

  void set_effects(ExtensionEffects effects) { effects_ = std::move(effects); }
  // I load an unpacked extension from a directory I control.
  bool install_directory(const std::string &directory, std::string &error);
  // A bundled extension ships with me, so I give it a stable id and refuse removal.
  bool install_bundled(const std::string &directory, const std::string &id, std::string &error);
  bool remove(const std::string &id, std::string &error);
  bool set_enabled(const std::string &id, bool enabled, std::string &error);

  std::vector<ExtensionRecord> list() const;
  const ExtensionRecord *find(const std::string &id) const;

  // I start (or restart) the background realm for an enabled extension.
  bool start_worker(const std::string &id, std::string &error);
  void stop_worker(const std::string &id);
  // I run pending timers so alarms and keepalives make progress.
  void tick();
  // I deliver a browser event to every enabled worker that registered for it.
  void broadcast(const std::string &event, const Json &payload);

  bool load_storage(const std::string &profile_directory, std::string &error);
  bool save_storage(const std::string &profile_directory, std::string &error) const;

  // Diagnostics I can show in the UI without pretending an extension works.
  std::vector<std::string> diagnostics() const;
  // I surface the last worker failure so my UI can explain a broken extension.
  std::string worker_error(const std::string &id) const;

private:
  struct Worker;
  std::map<std::string, ExtensionRecord> extensions_;
  std::map<std::string, std::unique_ptr<Worker>> workers_;
  ExtensionEffects effects_;
  std::vector<std::string> diagnostics_;
  std::string storage_path_;

  bool finish_install(const ExtensionManifest &manifest, const std::string &root, bool bundled,
                      std::string &error);
  void install_api(Worker &worker, const ExtensionRecord &record);
  bool grant(const ExtensionRecord &record, const std::string &permission,
             std::string &error) const;
};
} // namespace pv
