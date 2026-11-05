// PsychoVault original source. My broker services: dialogs, clipboard,
// notifications, tray, the application menu and context-menu plumbing.
// P1/S031 of docs/Psycho_MASTER-PLAN.md.
//
// Boundary discipline as before: every OS touchpoint (the real file picker,
// the real clipboard, the real toast, the real tray icon) sits behind a seam
// and proves itself on the first authorized Windows build. What lives here is
// the law: validation, origin gating, single-modal-per-window, bounded
// memory, canonical accelerators, radio exclusivity, offered-set enforcement.
// Unwired seams are not silent stubs: they refuse "…unavailable". The broker
// drives these services as typed calls; the IPC envelope adapters arrive when
// the UI starts speaking to them over the bus.
#pragma once
#include "../ipc/Psycho_bus.hpp"
#include <deque>
#include <functional>
#include <map>
#include <set>
#include <string>
#include <vector>

namespace pv::rt {

// ---------- dialogs ----------

struct DialogRequest {
  enum class Kind { Open, Save, Message } kind = Kind::Message;
  int window_id = -1;
  std::string title;                 // picker caption
  std::string message;               // Message kind: the text (required)
  std::vector<std::string> filters;  // extension tokens, e.g. "png"
  std::string default_name;          // Save kind: required
  bool from_shell = false;           // my UI asked, not a page
};

struct DialogResult {
  bool cancelled = true;
  std::string path; // Open/Save answer
  int button = -1;  // Message answer: 0 = affirmative
};

class DialogService {
public:
  struct Seams {
    // The real platform picker. Absent means this build cannot show dialogs,
    // and my law says that is a refusal, not a silent nothing.
    std::function<DialogResult(const DialogRequest &)> show = {};
  };
  explicit DialogService(Seams seams) : seams_(std::move(seams)) {}
  bool run(const DialogRequest &request, DialogResult &result, Error &error);
  bool busy(int window_id) const { return modals_.count(window_id) != 0; }

private:
  Seams seams_;
  std::set<int> modals_; // one modal per window, ever
  static constexpr size_t kFilterLimit = 8;
};

// ---------- clipboard ----------

class ClipboardService {
public:
  struct Seams {
    std::function<void(const std::string &)> apply = {}; // OS clipboard, later
  };
  explicit ClipboardService(Seams seams) : seams_(std::move(seams)) {}
  bool write_text(const std::string &origin, const std::string &text, Error &error);
  // Reading is the privileged direction: who wrote stays on record, and an
  // untrusted origin is refused.
  bool read_text(const std::string &origin, bool trusted, std::string &out,
                 Error &error);
  uint64_t generation() const { return generation_; }
  const std::string &last_writer() const { return writer_; }

private:
  Seams seams_;
  std::string text_;
  std::string writer_;
  uint64_t generation_ = 0;
  static constexpr size_t kTextLimit = 1 << 20; // 1 MiB of UTF-8 and no more
};

// ---------- notifications ----------

struct Notice {
  int id = -1;
  std::string origin, title, body;
};

class NotificationService {
public:
  struct Seams {
    std::function<void(const Notice &)> post = {}; // real toasts, later
  };
  explicit NotificationService(Seams seams) : seams_(std::move(seams)) {}
  // has_permission is decided by the caller's permission store; mine is the
  // refusal law: no permission, muted origin or empty title all refuse first.
  bool notify(const std::string &origin, const std::string &title,
              const std::string &body, bool has_permission, Error &error);
  void mute(const std::string &origin, bool muted);
  const std::deque<Notice> &recent() const { return recent_; }

private:
  Seams seams_;
  std::deque<Notice> recent_; // bounded: the newest 50
  std::set<std::string> muted_;
  int next_id_ = 1;
  static constexpr size_t kRecentLimit = 50;
};

// ---------- tray ----------

class TrayService {
public:
  struct Seams {
    // Clicks and menu events land here; the Windows tray icon wires in later.
    std::function<void(const std::string &event)> on_event = {};
  };
  explicit TrayService(Seams seams) : seams_(std::move(seams)) {}
  bool create(const std::string &tooltip, Error &error); // at most one tray
  bool destroy(Error &error);
  bool set_tooltip(const std::string &tooltip, Error &error);
  void event(const std::string &name); // platform -> seam, when alive
  bool alive() const { return alive_; }
  const std::string &tooltip() const { return tooltip_; }

private:
  Seams seams_;
  bool alive_ = false;
  std::string tooltip_;
  static constexpr size_t kTooltipLimit = 128;
};

// ---------- menus ----------

struct MenuItem {
  std::string id;          // command id; separators carry no id
  std::string label;
  std::string accelerator; // canonicalised at install: "shift+f5" -> "Shift+F5"
  enum class Type { Normal, Checkbox, Radio, Separator } type = Type::Normal;
  bool enabled = true;
  bool checked = false;
  std::vector<MenuItem> submenu = {};
};

class MenuService {
public:
  struct Seams {
    std::function<void(const std::string &command_id)> on_command = {};
  };
  explicit MenuService(Seams seams) : seams_(std::move(seams)) {}

  // A menu is installed whole: ids unique menu-wide, accelerator clashes
  // refused among siblings, separators carry no id, 256 items is the ceiling.
  bool install(std::vector<MenuItem> root, Error &error);
  bool dispatch(const std::string &id, Error &error); // checks/radios update first
  bool set_checked(const std::string &id, bool checked, Error &error);
  bool set_enabled(const std::string &id, bool enabled, Error &error);
  const MenuItem *find(const std::string &id) const;

  static std::string canonical_accelerator(const std::string &raw);

private:
  Seams seams_;
  std::vector<MenuItem> root_;
  std::map<std::string, MenuItem *> nodes_;
  std::map<std::string, MenuItem *> parents_; // for radio sibling law
};

// ---------- context-menu plumbing ----------

struct ContextInfo {
  std::string kind; // "link", "selection", "image", "editable", ...
  int contents_id = -1;
  int window_id = -1;
  std::string link_url;
  std::string selection;
};

class ContextMenus {
public:
  struct Seams {
    std::function<void(const std::string &command_id, const ContextInfo &)>
        on_command = {};
  };
  explicit ContextMenus(Seams seams) : seams_(std::move(seams)) {}
  // One provider per context kind; re-registering is refused.
  bool register_provider(
      std::string kind,
      std::function<std::vector<MenuItem>(const ContextInfo &)> provider,
      Error &error);
  // Build the menu, remember the offered set, hand back a token. Bounded: at
  // most 16 context menus outstanding.
  uint64_t open(const ContextInfo &info, Error &error);
  // The offered set is the law: choosing anything else is refused.
  bool choose(uint64_t token, const std::string &command_id, Error &error);
  bool cancel(uint64_t token);
  size_t outstanding() const { return open_.size(); }

private:
  struct Session {
    ContextInfo info;
    std::set<std::string> offered;
  };
  Seams seams_;
  std::map<std::string, std::function<std::vector<MenuItem>(const ContextInfo &)>>
      providers_;
  std::map<uint64_t, Session> open_;
  uint64_t next_token_ = 1;
  static constexpr size_t kOutstandingLimit = 16;
};

} // namespace pv::rt
