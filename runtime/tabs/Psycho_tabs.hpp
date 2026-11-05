// PsychoVault original source. My tab model - the broker-side rules behind
// every tab strip: ordered tab lists per window, groups, pinning, mute,
// discard, moves between windows, and the recently-closed stack.
// P1/S026 of docs/Psycho_MASTER-PLAN.md.
//
// Boundary discipline as before: this file is pure decision-making with no OS
// handles at all. Tabs reference windows by the integer ids the window model
// (S024) hands out and reference content by nothing yet - the WebContents
// equivalent arrives in S027 and will live one layer above this model.
#pragma once
#include "../ipc/Psycho_bus.hpp"
#include <deque>
#include <map>
#include <string>
#include <vector>

namespace pv::rt {

enum class TabState { Live, Discarded };

struct Tab {
  int id = -1;
  int window_id = -1;   // which attached window's ordered list holds me
  std::string label;    // working title until S027 navigation owns titles
  int group_id = -1;    // -1: ungrouped. A group never spans windows.
  bool pinned = false;
  bool muted = false;
  TabState state = TabState::Live;
  uint32_t generation = 0; // bumps every time a discarded tab revives
};

struct TabGroup {
  int id = -1;
  int window_id = -1; // groups live in exactly one window, always contiguous
  std::string name;
  std::string color;  // a decorative token; colour parsing is the engine's job
};

// What the recently-closed stack remembers about one tab: everything needed to
// restore it where it was. Restoration always mints a fresh tab id - ids are
// history and are never resurrected.
struct ClosedTab {
  int tab_id = -1;
  int window_id = -1;
  std::string label;
  bool pinned = false;
  bool muted = false;
  TabState state = TabState::Live;
  int index = 0;            // position in the window order at close time
  int group_id = -1;        // group at close time (may have evaporated since)
  std::string group_name;   // snapshot so a lone survivor can restore the group
  std::string group_color;
};

// The model. Windows are attached explicitly (the broker mirrors S024's
// create/destroy here), tabs live in exactly one attached window's ordered
// list, and a window that has tabs always has an active tab. Index arguments
// are insertion indices with -1 meaning "the end"; out-of-range values never
// refuse, they clamp into the legal region. The one ordering law that is
// structural and unbreakable: pinned tabs are always a prefix of the order,
// so no index can ever place an unpinned tab inside the pinned block.
class TabModel {
public:
  bool attach_window(int window_id, Error &error);   // dup: tabs.window-exists
  bool detach_window(int window_id, Error &error);   // busy: tabs.window-busy

  int open_tab(int window_id, const std::string &label, int index, Error &error);
  bool close_tab(int tab_id, Error &error);   // remembers me on the closed stack
  bool activate(int tab_id, Error &error);    // active tabs are never discarded
  const Tab *active_in(int window_id) const;

  // Moving one tab that belongs to a group is refused (tabs.grouped-move);
  // the group travels as a block through move_group_to_window instead, so the
  // contiguity law cannot be broken half-way.
  bool move_within_window(int window_id, int tab_id, int index, Error &error);
  bool move_to_window(int tab_id, int dest_window, int index, Error &error);
  bool move_group_to_window(int group_id, int dest_window, int index, Error &error);
  bool move_window_to_window(int src_window, int dest_window, int index, Error &error);

  // Pinning a grouped tab pins the whole group as one block - a group never
  // straddles the pinned/unpinned boundary.
  bool set_pinned(int tab_id, bool pinned, Error &error);
  bool set_muted(int tab_id, bool muted, Error &error);
  bool discard(int tab_id, Error &error);     // the active tab refuses

  int group_tabs(const std::vector<int> &tab_ids, const std::string &name,
                 const std::string &color, Error &error);
  bool ungroup(int group_id, Error &error);
  bool set_group(int group_id, const std::string &name, const std::string &color,
                 Error &error);

  // LIFO restoration. A refused reopen (its window is gone) leaves the stack
  // entry in place so nothing is ever silently dropped.
  int reopen_last(Error &error);
  const std::deque<ClosedTab> &closed() const { return closed_; }

  std::vector<int> order(int window_id) const; // tab ids, front to back
  const Tab *find(int tab_id) const;
  const TabGroup *find_group(int group_id) const;

private:
  struct WindowTabs {
    std::vector<int> order;
    int active = -1;
  };

  struct Found {
    Tab *tab = nullptr;
    WindowTabs *window = nullptr;
    size_t index = 0;
  };

  Found lookup(int tab_id);
  size_t pinned_count(const WindowTabs &window) const;
  // Where index may legally land for a tab of this pinnedness, after clamping.
  size_t clamp_index(const WindowTabs &window, int index, bool pinned) const;
  void assign_active(WindowTabs &window, int tab_id); // discarded tabs revive
  void replace_active(WindowTabs &window, size_t removed_index);
  void remember_closed(const Tab &tab);
  // The one placement primitive every move funnels through: insert ids as a
  // contiguous block in dest, land them in the right pin family, retarget
  // window_id on tabs and their groups, repair the source's active tab.
  bool transplant(const std::vector<int> &ids, WindowTabs &src, WindowTabs &dst,
                  int dest_window, int index);

  std::map<int, WindowTabs> windows_;
  std::map<int, Tab> tabs_;
  std::map<int, TabGroup> groups_;
  std::deque<ClosedTab> closed_;
  int next_tab_id_ = 1;
  int next_group_id_ = 1;
  static constexpr size_t kClosedLimit = 25;
};

} // namespace pv::rt
