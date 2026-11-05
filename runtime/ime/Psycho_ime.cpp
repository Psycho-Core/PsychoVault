// PsychoVault original source. See the header for the five laws - this file
// is how I keep them.
#include "Psycho_ime.hpp"
#include <utility>

namespace pv::rt {

namespace {

Error refuse(const char *code, const std::string &message) {
  Error error;
  error.code = code;
  error.message = message;
  return error;
}

// Strict UTF-8: overlong forms, surrogates and anything past U+10FFFF are
// refuse-worthy, because a platform feeding me those is already lying.
bool valid_utf8(const std::string &s) {
  size_t i = 0;
  while (i < s.size()) {
    const unsigned char c = static_cast<unsigned char>(s[i]);
    if (c < 0x80) {
      ++i;
      continue;
    }
    size_t extra = 0;
    uint32_t cp = 0;
    if ((c & 0xE0) == 0xC0) {
      extra = 1;
      cp = c & 0x1F;
    } else if ((c & 0xF0) == 0xE0) {
      extra = 2;
      cp = c & 0x0F;
    } else if ((c & 0xF8) == 0xF0) {
      extra = 3;
      cp = c & 0x07;
    } else {
      return false; // bare continuation byte or an impossible lead
    }
    if (i + extra >= s.size()) return false; // truncated sequence
    for (size_t k = 1; k <= extra; ++k) {
      const unsigned char cc = static_cast<unsigned char>(s[i + k]);
      if ((cc & 0xC0) != 0x80) return false;
      cp = (cp << 6) | (cc & 0x3F);
    }
    const uint32_t floor = extra == 1 ? 0x80 : extra == 2 ? 0x800 : 0x10000;
    if (cp < floor) return false;                       // overlong encoding
    if (cp > 0x10FFFF) return false;
    if (cp >= 0xD800 && cp <= 0xDFFF) return false;     // surrogates
    i += extra + 1;
  }
  return true;
}

// A caret byte offset is honest at the string's end or on a lead byte.
bool utf8_boundary(const std::string &s, size_t at) {
  if (at > s.size()) return false;
  return at == s.size() ||
         (static_cast<unsigned char>(s[at]) & 0xC0) != 0x80;
}

// The dead-key table, written out in full so a reader can audit every pair:
// accent token -> base letter -> (lowercase, uppercase) precomposed results.
// UTF-8 is spelled as byte escapes (engine precedent: Psycho_base.cpp), so
// every compiler reads this file identically. Tokens are the ASCII spellings
// my key layer produces.
struct DeadRow {
  char base;
  const char *lower;
  const char *upper;
};
const DeadRow *dead_rows(const std::string &accent, size_t &count) {
  static const DeadRow acute[] = { // á Á é É í Í ó Ó ú Ú ý Ý ć Ć ń Ń
      {'a', "\xc3\xa1", "\xc3\x81"}, {'e', "\xc3\xa9", "\xc3\x89"},
      {'i', "\xc3\xad", "\xc3\x8d"}, {'o', "\xc3\xb3", "\xc3\x93"},
      {'u', "\xc3\xba", "\xc3\x9a"}, {'y', "\xc3\xbd", "\xc3\x9d"},
      {'c', "\xc4\x87", "\xc4\x86"}, {'n', "\xc5\x84", "\xc5\x83"}};
  static const DeadRow grave[] = { // à À è È ì Ì ò Ò ù Ù
      {'a', "\xc3\xa0", "\xc3\x80"}, {'e', "\xc3\xa8", "\xc3\x88"},
      {'i', "\xc3\xac", "\xc3\x8c"}, {'o', "\xc3\xb2", "\xc3\x92"},
      {'u', "\xc3\xb9", "\xc3\x99"}};
  static const DeadRow circumflex[] = { // â Â ê Ê î Î ô Ô û Û ĉ Ĉ ĝ Ĝ ĥ Ĥ ĵ Ĵ ŝ Ŝ ŵ Ŵ ŷ Ŷ
      {'a', "\xc3\xa2", "\xc3\x82"}, {'e', "\xc3\xaa", "\xc3\x8a"},
      {'i', "\xc3\xae", "\xc3\x8e"}, {'o', "\xc3\xb4", "\xc3\x94"},
      {'u', "\xc3\xbb", "\xc3\x9b"}, {'c', "\xc4\x89", "\xc4\x88"},
      {'g', "\xc4\x9d", "\xc4\x9c"}, {'h', "\xc4\xa5", "\xc4\xa4"},
      {'j', "\xc4\xb5", "\xc4\xb4"}, {'s', "\xc5\x9d", "\xc5\x9c"},
      {'w', "\xc5\xb5", "\xc5\xb4"}, {'y', "\xc5\xb7", "\xc5\xb6"}};
  static const DeadRow tilde[] = { // ã Ã ñ Ñ õ Õ
      {'a', "\xc3\xa3", "\xc3\x83"}, {'n', "\xc3\xb1", "\xc3\x91"},
      {'o', "\xc3\xb5", "\xc3\x95"}};
  static const DeadRow diaeresis[] = { // ä Ä ë Ë ï Ï ö Ö ü Ü ÿ Ÿ
      {'a', "\xc3\xa4", "\xc3\x84"}, {'e', "\xc3\xab", "\xc3\x8b"},
      {'i', "\xc3\xaf", "\xc3\x8f"}, {'o', "\xc3\xb6", "\xc3\x96"},
      {'u', "\xc3\xbc", "\xc3\x9c"}, {'y', "\xc3\xbf", "\xc5\xb8"}};
  if (accent == "'") {
    count = sizeof(acute) / sizeof(acute[0]);
    return acute;
  }
  if (accent == "`") {
    count = sizeof(grave) / sizeof(grave[0]);
    return grave;
  }
  if (accent == "^") {
    count = sizeof(circumflex) / sizeof(circumflex[0]);
    return circumflex;
  }
  if (accent == "~") {
    count = sizeof(tilde) / sizeof(tilde[0]);
    return tilde;
  }
  if (accent == "\"") {
    count = sizeof(diaeresis) / sizeof(diaeresis[0]);
    return diaeresis;
  }
  count = 0;
  return nullptr;
}

bool known_accent(const std::string &accent) {
  size_t count = 0;
  return dead_rows(accent, count) != nullptr;
}

// Law 5's resolution: the committed text for accent + character.
std::string dead_resolve(const std::string &accent, char c) {
  if (c == ' ') return accent; // space asks for the accent itself
  size_t count = 0;
  const DeadRow *rows = dead_rows(accent, count);
  const bool upper = c >= 'A' && c <= 'Z';
  const char want = upper ? static_cast<char>(c - 'A' + 'a') : c;
  for (size_t i = 0; i < count; ++i) {
    if (rows[i].base == want) return upper ? rows[i].upper : rows[i].lower;
  }
  // No such combination: the accent and the character both arrive, honestly.
  return accent + std::string(1, c);
}

} // namespace

bool ImeService::begin(int surface_id, Error &error) {
  error = Error{};
  if (!seams_.on_preedit) {
    error = refuse("ime.unavailable", "this build has no preedit sink for IME");
    return false;
  }
  if (states_.count(surface_id)) {
    error = refuse("ime.busy", "surface already composes; one at a time");
    return false;
  }
  Composition comp;
  states_[surface_id] = comp;
  seams_.on_preedit(surface_id, states_[surface_id]);
  return true;
}

bool ImeService::update(int surface_id, const std::string &preedit,
                        size_t caret, Error &error) {
  error = Error{};
  if (!seams_.on_preedit) {
    error = refuse("ime.unavailable", "this build has no preedit sink for IME");
    return false;
  }
  auto it = states_.find(surface_id);
  if (it == states_.end()) {
    error = refuse("ime.idle", "update with no composition in flight");
    return false;
  }
  if (preedit.size() > kPreeditLimit) {
    error = refuse("ime.text", "preedit beyond the 4096-byte bound");
    return false;
  }
  if (!valid_utf8(preedit)) {
    error = refuse("ime.text", "malformed UTF-8 in preedit");
    return false;
  }
  if (!utf8_boundary(preedit, caret)) {
    error = refuse("ime.caret", "caret off the end or mid-character");
    return false;
  }
  it->second.preedit = preedit;
  it->second.caret = caret;
  it->second.dead = false; // a platform update owns the preedit now
  seams_.on_preedit(surface_id, it->second);
  return true;
}

bool ImeService::commit(int surface_id, const std::string &text,
                        Error &error) {
  error = Error{};
  if (!seams_.on_commit || !seams_.on_preedit) {
    error = refuse("ime.unavailable", "this build cannot deliver IME text");
    return false;
  }
  if (states_.find(surface_id) == states_.end()) {
    error = refuse("ime.idle", "commit with no composition in flight");
    return false;
  }
  if (!text.empty()) {
    if (text.size() > kPreeditLimit || !valid_utf8(text)) {
      error = refuse("ime.text", "committed text is malformed or unbounded");
      return false;
    }
    // Law 3: the committed string crosses exactly once, before state dies.
    seams_.on_commit(surface_id, text);
  }
  finish(surface_id);
  return true;
}

bool ImeService::cancel(int surface_id, Error &error) {
  error = Error{};
  if (!seams_.on_preedit) {
    error = refuse("ime.unavailable", "this build has no preedit sink for IME");
    return false;
  }
  if (states_.find(surface_id) == states_.end()) {
    error = refuse("ime.idle", "cancel with no composition in flight");
    return false;
  }
  finish(surface_id); // nothing crosses: the field is as it was
  return true;
}

void ImeService::surface_closed(int surface_id) {
  // Nobody to notify: the surface that would hear it is gone.
  states_.erase(surface_id);
}

bool ImeService::composing(int surface_id) const {
  return states_.count(surface_id) != 0;
}

const Composition *ImeService::composition(int surface_id) const {
  auto it = states_.find(surface_id);
  return it == states_.end() ? nullptr : &it->second;
}

bool ImeService::pending_accent(int surface_id) const {
  auto it = states_.find(surface_id);
  return it != states_.end() && it->second.dead;
}

bool ImeService::candidate_position(int surface_id, int &x, int &y,
                                    Error &error) const {
  error = Error{};
  if (states_.find(surface_id) == states_.end()) {
    error = refuse("ime.idle", "no composition, so no candidate window");
    return false;
  }
  if (!seams_.caret_rect || !seams_.window_rect) {
    error = refuse("ime.unavailable",
                   "this build cannot answer where the caret is");
    return false;
  }
  const ImeRect caret = seams_.caret_rect(surface_id);
  const ImeRect win = seams_.window_rect(surface_id);
  // Law 4: hang the window at the caret's baseline, then pull it back inside.
  x = caret.x;
  const int max_x = win.right() - kCandidateW;
  if (x > max_x) x = max_x;
  if (x < win.x) x = win.x;
  y = caret.bottom();
  if (y + kCandidateH > win.bottom() && caret.y - kCandidateH >= win.y) {
    y = caret.y - kCandidateH; // the bottom would clip: flip above the line
  }
  if (y + kCandidateH > win.bottom()) y = win.bottom() - kCandidateH;
  if (y < win.y) y = win.y;
  return true;
}

bool ImeService::dead_key(int surface_id, const std::string &accent,
                          Error &error) {
  error = Error{};
  if (!seams_.on_preedit || !seams_.on_commit) {
    error = refuse("ime.unavailable", "this build cannot show or deliver IME");
    return false;
  }
  if (!known_accent(accent)) {
    error = refuse("ime.accent", "unknown dead-key token '" + accent + "'");
    return false;
  }
  auto it = states_.find(surface_id);
  if (it != states_.end()) {
    if (!it->second.dead) {
      error = refuse("ime.busy", "a platform composition owns this surface");
      return false;
    }
    if (it->second.preedit == accent) {
      // The same accent twice means "I really want the accent" (law 5).
      const std::string text = accent;
      seams_.on_commit(surface_id, text);
      finish(surface_id);
      return true;
    }
    // Two different accents: the first becomes text, the second pends.
    const std::string first = it->second.preedit;
    seams_.on_commit(surface_id, first);
    Composition next;
    next.preedit = accent;
    next.caret = accent.size();
    next.dead = true;
    it->second = next;
    seams_.on_preedit(surface_id, it->second);
    return true;
  }
  Composition comp;
  comp.preedit = accent;
  comp.caret = accent.size();
  comp.dead = true;
  states_[surface_id] = comp;
  seams_.on_preedit(surface_id, states_[surface_id]);
  return true;
}

bool ImeService::type_char(int surface_id, char c, Error &error) {
  error = Error{};
  if (!seams_.on_commit || !seams_.on_preedit) {
    error = refuse("ime.unavailable", "this build cannot deliver IME text");
    return false;
  }
  auto it = states_.find(surface_id);
  if (it == states_.end() || !it->second.dead) {
    error = refuse("ime.idle", "no accent pends on this surface");
    return false;
  }
  const std::string text = dead_resolve(it->second.preedit, c);
  seams_.on_commit(surface_id, text);
  finish(surface_id);
  return true;
}

void ImeService::finish(int surface_id) {
  states_.erase(surface_id);
  if (seams_.on_preedit) {
    const Composition cleared;
    seams_.on_preedit(surface_id, cleared); // the underline comes off
  }
}

} // namespace pv::rt
