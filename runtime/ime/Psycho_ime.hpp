// PsychoVault original source. My IME and dead-key law: composition, candidate
// window placement, commit and cancel, end to end for every editor surface I
// own. P1/S033 of docs/Psycho_MASTER-PLAN.md.
//
// The laws, stated so nobody forgets them:
//   1. ONE COMPOSITION PER SURFACE. A dead key's pending accent and a platform
//      IME's preedit are the SAME state - a surface composes or it does not.
//      Beginning twice refuses; finishing twice refuses. Two surfaces never
//      share one keystroke's worth of state.
//   2. PLATFORM TEXT IS SUSPECT. Preedit must be valid UTF-8, bounded in
//      size, and the caret must sit on a character boundary inside it. A
//      platform that hands me garbage is refused, not sanitized behind.
//   3. COMMIT IS THE ONLY WAY TEXT ARRIVES. Cancel leaves the field exactly
//      as it was before the composition began; an empty commit delivers
//      nothing. Every committed string crosses the seam exactly once.
//   4. THE CANDIDATE WINDOW FOLLOWS THE CARET AND STAYS IN THE WINDOW. While
//      composing, its spot is the caret's baseline, clamped inside the window
//      bounds, flipping above the caret line when the bottom would clip. No
//      composition, no window: asking while idle refuses.
//   5. A DEAD KEY IS A PENDING ACCENT, NOT TEXT. Accent then space yields the
//      accent; accent then a combining letter yields the precomposed letter;
//      accent then anything else yields accent plus letter; the same accent
//      twice yields the accent. Cancelling drops the accent honestly. Unknown
//      accent tokens refuse by name.
//
// Boundary discipline as before: the real platform pieces (the TSF sink on
// Windows, the painted candidate list) sit behind seams and refuse
// "…unavailable" until the first authorized Windows build wires them. What
// lives here is the law those pieces will obey. Page text fields join at
// S062; the editor surfaces I own today are the shell UI's fields.
#pragma once
#include "../ipc/Psycho_bus.hpp"
#include <functional>
#include <map>
#include <string>

namespace pv::rt {

// A rectangle in window-space pixels.
struct ImeRect {
  int x = 0, y = 0, w = 0, h = 0;
  int right() const { return x + w; }
  int bottom() const { return y + h; }
};

// The visible composition: preedit text plus the caret's byte offset within
// it. `dead` marks the pending-accent form of the same state (law 1): the
// preedit then shows the accent itself, underlined.
struct Composition {
  std::string preedit;
  size_t caret = 0;
  bool dead = false;
};

class ImeService {
public:
  struct Seams {
    // Where is the caret right now, in window-space pixels? The candidate
    // window law (law 4) cannot be kept without an honest answer.
    std::function<ImeRect(int surface_id)> caret_rect = {};
    // The bounds the candidate window must stay inside.
    std::function<ImeRect(int surface_id)> window_rect = {};
    // Preedit changed (or cleared, after commit/cancel): repaint the field.
    std::function<void(int surface_id, const Composition &)> on_preedit = {};
    // Committed text crosses exactly once (law 3).
    std::function<void(int surface_id, const std::string &)> on_commit = {};
  };
  explicit ImeService(Seams seams) : seams_(std::move(seams)) {}

  // Lifecycle. begin refuses while this surface already composes ("ime.busy");
  // update/commit/cancel refuse when it does not ("ime.idle"). A missing seam
  // the call cannot keep its law without refuses "ime.unavailable".
  bool begin(int surface_id, Error &error);
  bool update(int surface_id, const std::string &preedit, size_t caret,
              Error &error);
  // Empty text is legal: it ends the composition delivering nothing, the way
  // platforms say "never mind" at the last moment.
  bool commit(int surface_id, const std::string &text, Error &error);
  bool cancel(int surface_id, Error &error);
  // A surface dying mid-composition is not an error; its state dies with it.
  void surface_closed(int surface_id);

  bool composing(int surface_id) const;
  // Null when idle. The pointer dies with the next call that mutates state.
  const Composition *composition(int surface_id) const;

  // Law 4. Refuses "ime.idle" without a composition.
  bool candidate_position(int surface_id, int &x, int &y, Error &error) const;

  // Dead keys. Both feed the one composition state; both refuse "ime.busy"
  // against a platform composition and "ime.unavailable" without their seams.
  bool dead_key(int surface_id, const std::string &accent, Error &error);
  // A printable character arrived while an accent pends: resolve by law 5.
  bool type_char(int surface_id, char c, Error &error);
  bool pending_accent(int surface_id) const;

private:
  // Ends the composition: notifies the cleared preedit, erases state.
  void finish(int surface_id);

  Seams seams_;
  std::map<int, Composition> states_;

  static constexpr size_t kPreeditLimit = 4096; // bytes of UTF-8, per composition
  // The candidate window's own size is a policy constant until the painted
  // list exists; the clamping law is already honest about it.
  static constexpr int kCandidateW = 260;
  static constexpr int kCandidateH = 180;
};

} // namespace pv::rt
