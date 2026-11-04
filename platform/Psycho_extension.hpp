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
  // A manifest key pins the extension id the Chrome way (SHA-256 of the DER
  // key, nibbles as a..p). Proton's account site only messages that one id, so
  // I honour it instead of my path hash.
  std::string key, key_id;
  std::vector<std::string> externally_connectable;
  bool valid = false;
  std::string error;

  bool has_permission(const std::string &permission) const;
  bool matches_host(const Url &url) const;
  // A page my renderer serves may message this extension only when its URL
  // matches a declared externally_connectable pattern.
  bool connectable_from(const Url &url) const;
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
  // A worker's fetch never leaves my own network stack. The callback returns
  // false with an error when the URL cannot be served; chrome-extension:// URLs
  // are resolved inside the extension's own folder before this runs, so this
  // hook only ever sees network addresses.
  struct FetchRequest {
    std::string url, method, body, content_type;
    // One line per header, "Name: value".
    std::vector<std::string> headers;
  };
  struct FetchAnswer {
    int status = 0;
    std::string mime, text, error;
  };
  std::function<FetchAnswer(const std::string &extension_id, const FetchRequest &)> fetch;
  // A proxy answered 407: ask the extension that owns the proxy for the
  // credentials its webRequest.onAuthRequired provider supplies. Returns false
  // when no auth provider supplied anything.
  std::function<bool(std::string &user, std::string &pass, int attempts)>
      proxy_credentials;
  // Real tab effects the host asks the shell for.
  std::function<int(const std::string &url, bool active)> open_tab;
  std::function<bool(int tab_id, std::string &url_out)> resolve_tab;
  std::function<bool(int tab_id)> close_tab;
  std::function<bool(int tab_id, const std::string &code, std::string &result_out)>
      run_in_tab;
  // The notification surface: a short line the user can see.
  std::function<void(const std::string &extension_id, const std::string &id,
                     const std::string &title, const std::string &message)> notify;
  // How long since the user last touched the machine, in seconds.
  std::function<double()> idle_seconds;
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
  // My network stack asks: does any running extension provide proxy
  // credentials for this challenge? The answer comes straight from the
  // extension's own code, so a real Proton 407 gets a real Proton credential.
  bool request_proxy_credentials(std::string &user, std::string &pass, int attempts);
  // My shell drives messaging: popup pages and whitelisted sites talk to a
  // worker through runtime.onMessage / runtime.onMessageExternal and get the
  // answer (or the extension error) back as JSON.
  bool send_message(const std::string &id, const Json &message, const std::string &sender_url,
                    std::string &response_json);
  // A page under chrome-extension:// opens in a tab; my shell routes the file
  // read through me so the payload never comes from the network by accident.
  bool read_page_resource(const std::string &extension_id, const std::string &path,
                          std::string &contents, std::string &mime) const;
  const ExtensionManifest *manifest_of(const std::string &id) const;

  bool load_storage(const std::string &profile_directory, std::string &error);
  bool save_storage(const std::string &profile_directory, std::string &error) const;

  // Diagnostics I can show in the UI without pretending an extension works.
  std::vector<std::string> diagnostics() const;
  // I surface the last worker failure so my UI can explain a broken extension.
  std::string worker_error(const std::string &id) const;

public:
  // The worker realm's guts. Public only so my own helper functions in the cpp
  // can name the type; it is not a surface another platform file should use.
  struct Worker;
private:
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
