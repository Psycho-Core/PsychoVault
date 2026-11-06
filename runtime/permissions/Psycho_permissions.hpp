// PsychoVault original source. My permission service: site permissions by
// origin, prompts, per-profile persistence, and the deny-by-default list
// inherited verbatim from the legacy defaults. P1/S036 of
// docs/Psycho_MASTER-PLAN.md.
//
// The laws, stated so nobody forgets them:
//   1. THE LEGACY DEFAULTS ARE THE FLOOR. The legacy inventory's
//      defaultSettings.denyPerms - geolocation, media, clipboard-read, midi,
//      sync-xhr, wake-lock, notifications - are exactly my known kinds, and
//      each defaults to Deny. Nothing defaults to Allow anywhere. A kind I
//      never heard of is not "probably fine": it refuses "perm.kind".
//   2. ONE DECISION PER ORIGIN PER KIND, ON HONEST ORIGINS. The store key is
//      the origin: scheme://host[:port], lowercase, no path, no query, no
//      trailing slash. Unnormalized origins refuse "perm.origin" - the caller
//      normalizes, I verify. http and https are the only schemes with site
//      permissions; my internal pages hold none.
//   3. THE PROMPT IS THE ONLY PATH FROM DEFAULT TO ANSWER. One visible
//      prompt per (origin, kind): re-asking coalesces to the pending token
//      instead of stacking dialogs. Pending prompts are bounded at 16.
//      "Ask" is not an answer a user may give. An answer remembered with no
//      persistence seam refuses "perm.unavailable" and stays pending - the
//      user may then answer without remember.
//   4. PERSISTENCE IS EXACTLY WHAT THE PROFILE HEARD. Remembered decisions
//      cross the save seam; resets cross the drop seam; session answers
//      cross neither. Loading the profile happens once, merges last-wins,
//      and drops malformed rows rather than trusting them.
//   5. PRIVATE MODE IS A BUILD WITHOUT THE SEAMS, NOT A FLAG. A service
//      constructed with no profile seams keeps every law in memory, for the
//      session, and simply never persists. S037 builds exactly that.
#pragma once
#include "../ipc/Psycho_bus.hpp"
#include <functional>
#include <map>
#include <set>
#include <string>
#include <utility>
#include <vector>

namespace pv::rt {

enum class Decision { Deny, Ask, Allow };

// One profile row: origin + kind + the remembered answer.
struct PermissionGrant {
  std::string origin, kind;
  bool allow = false;
};

class PermissionService {
public:
  struct Seams {
    // The profile store: absent means session-only (law 5's private build).
    std::function<std::vector<PermissionGrant>()> load = {};
    std::function<void(const PermissionGrant &)> save = {};
    std::function<void(const std::string &origin, const std::string &kind)> drop = {};
    // The shell shows the prompt and hands the token back through answer().
    // Absent means prompts refuse "perm.unavailable".
    std::function<void(const std::string &origin, const std::string &kind)>
        show_prompt = {};
  };
  explicit PermissionService(Seams seams) : seams_(std::move(seams)) {}

  // Law 1's tables, public so diagnostics and the settings UI print the same
  // truth the enforcer uses.
  static const std::set<std::string> &known_kinds();
  static bool valid_origin(const std::string &origin); // law 2's parse

  // The effective decision: stored answer, else the kind's default. Refuses
  // "perm.kind" or "perm.origin" - a programmer's typo is not a quiet Deny.
  bool decide(const std::string &origin, const std::string &kind,
              Decision &out, Error &error) const;

  // The verbs. grant/block remember (persistent, save seam fires); reset
  // returns to the default (drop seam fires if a remembered row died);
  // resetting what was never set is a quiet no-op, not a refusal.
  bool grant(const std::string &origin, const std::string &kind, Error &error);
  bool block(const std::string &origin, const std::string &kind, Error &error);
  bool reset(const std::string &origin, const std::string &kind, Error &error);
  // Every kind the origin holds, back to defaults. Returns rows dropped.
  size_t reset_origin(const std::string &origin, Error &error);

  // The prompt (law 3). Returns the pending token, 0 with the refusal named.
  uint64_t ask(const std::string &origin, const std::string &kind,
               Error &error);
  // The user's answer. remember=true persists (law 4) and needs the seam.
  bool answer(uint64_t token, Decision user, bool remember, Error &error);
  // The tab died mid-prompt: drop the pending token, no record. False when
  // the token never pended.
  bool drop_prompt(uint64_t token);
  size_t pending() const { return pending_.size(); }

  // The profile, once (law 4). Second loads refuse "perm.state".
  bool load_profile(Error &error);
  bool persistence_enabled() const { return bool(seams_.save); }
  size_t stored() const { return store_.size(); }

  static constexpr size_t kPendingLimit = 16;

private:
  bool check(const std::string &origin, const std::string &kind,
             Error &error) const; // both laws 1 and 2, named
  bool store(const std::string &origin, const std::string &kind,
             Decision decision, bool remember, Error &error);

  struct Entry {
    Decision decision = Decision::Deny;
    bool persistent = false;
  };
  Seams seams_;
  std::map<std::pair<std::string, std::string>, Entry> store_;
  std::map<uint64_t, std::pair<std::string, std::string>> pending_; // token -> key
  uint64_t next_token_ = 1;
  bool loaded_ = false;
};

} // namespace pv::rt
