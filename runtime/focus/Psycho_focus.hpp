// PsychoVault original source. My focus and activation law: window focus, tab
// focus, page focus, dialog modality, and the background throttling policy
// that follows from them. P1/S034 of docs/Psycho_MASTER-PLAN.md.
//
// The laws, stated so nobody forgets them:
//   1. ONE ANSWER AT EVERY LEVEL. At any instant there is at most one active
//      window, at most one focused tab per window, and at most one focused
//      element per tab. Queries never guess: a dead node's answer is
//      "nothing" (for visibility) or a refusal (for operations).
//   2. BLUR BEFORE FOCUS, NEVER TWICE. Every change emits its blur event
//      before its focus event, and a node never receives two focus (or two
//      blur) events in a row - re-asking for what already holds focus is a
//      no-op, not a notification.
//   3. A MODAL DIALOG CAPTURES ITS OWN WINDOW. While a window is modal, tab
//      switches and element focus inside it refuse; other windows live free.
//      Death ends modality with the window.
//   4. THE DEAD CANNOT HOLD FOCUS. Closing a focused tab moves focus to the
//      declared fallback - the next tab in the window's order, else the one
//      before; closing the active window activates the most recently opened
//      remaining window, else nothing (an app with no focused window is
//      legal). Death itself is not an event: a dead page hears no blur.
//   5. VISIBILITY FOLLOWS FOCUS, AND THROTTLING FOLLOWS VISIBILITY. A tab is
//      ACTIVE when it is focused in the active, unoccluded window; VISIBLE
//      when it is the focused tab of a window the user could see; HIDDEN
//      otherwise - background tabs, occluded windows, unknown ids. Timer
//      floors are the policy that falls out: 0 / 250 / 1000 ms. Numbers are
//      policy constants, tuned when real hardware measures them; the tiers
//      are law.
//
// The modal state here is the same fact S031's dialog service enforces as
// one-modal-per-window: the broker wires the two together at the M1 slice.
// All notifications travel one seam; a build with no listener still keeps
// every state law.
#pragma once
#include "../ipc/Psycho_bus.hpp"
#include <functional>
#include <map>
#include <string>
#include <vector>

namespace pv::rt {

enum class PageVisibility { Active, Visible, Hidden };

// One focus transition. Level names the ring of the focus chain: window, tab
// within window, element within tab.
struct FocusEvent {
  enum class Op { Blur, Focus } op = Op::Focus;
  enum class Level { Window, Tab, Element } level = Level::Window;
  int id = -1;         // window or tab id
  std::string element; // Element level only
};

class FocusService {
public:
  struct Seams {
    // Every transition announced (law 2), in order. Optional: the state law
    // never depends on somebody listening.
    std::function<void(const FocusEvent &)> on_event = {};
  };
  explicit FocusService(Seams seams) : seams_(std::move(seams)) {}

  // Lifecycle. Newly opened windows activate (with the events law 2
  // demands). Tabs may open focused or in the background; asking to focus a
  // tab while its window is modal refuses "focus.modal". Anything never
  // registered refuses "focus.unknown"; re-registering refuses
  // "focus.duplicate".
  bool window_opened(int window_id, Error &error);
  bool window_closed(int window_id, Error &error);
  bool tab_opened(int window_id, int tab_id, bool activate, Error &error);
  bool tab_closed(int window_id, int tab_id, Error &error);

  // Focus moves. Re-focusing the current holder is a no-op that keeps law 2's
  // silence. Element focus only lands while the tab is its window's focused
  // tab in a non-modal window ("focus.inactive" / "focus.modal"); an empty
  // element id refuses "focus.arg".
  bool focus_window(int window_id, Error &error);
  bool focus_tab(int window_id, int tab_id, Error &error);
  bool focus_element(int tab_id, const std::string &element_id, Error &error);
  // Back to the page body: clears the tab's element with its blur event.
  bool blur_element(int tab_id, Error &error);

  // Dialog modality (law 3). Opening twice or closing what never opened
  // refuses "focus.modal".
  bool modal_open(int window_id, Error &error);
  bool modal_close(int window_id, Error &error);
  // Minimized or fully covered: the window's tabs turn HIDDEN (law 5).
  bool set_occluded(int window_id, bool occluded, Error &error);

  // Queries (law 1): dead nodes answer "nothing", wrongly, never.
  int active_window() const { return active_window_; }
  int focused_tab(int window_id) const;            // -1 when unknown/none
  std::string focused_element(int tab_id) const;   // "" when none or dead
  bool is_modal(int window_id) const;
  // Page focus in the document sense: window active, tab focused, and an
  // element actually holding it.
  bool has_page_focus(int tab_id) const;
  PageVisibility visibility_of(int tab_id) const;  // unknown -> Hidden
  bool timer_floor(int tab_id, int &floor_ms, Error &error) const;

  // Law 5's policy constants, exposed so diagnostics can print them.
  static constexpr int kActiveFloorMs = 0;
  static constexpr int kVisibleFloorMs = 250;
  static constexpr int kHiddenFloorMs = 1000;

private:
  struct WindowState {
    bool modal = false;
    bool occluded = false;
    int focused_tab = -1;
  };
  void emit(FocusEvent::Op op, FocusEvent::Level level, int id,
            const std::string &element = "");

  Seams seams_;
  std::map<int, WindowState> windows_;
  std::vector<int> window_order_;            // registration order, for fallbacks
  std::map<int, std::vector<int>> tabs_;     // window -> tabs in open order
  std::map<int, int> tab_window_;            // tab -> owning window
  std::map<int, std::string> element_focus_; // tab -> focused element
  int active_window_ = -1;                   // -1: no focused window (legal)
};

} // namespace pv::rt
