#pragma once
// My spelling checker, written from scratch: a word set plus classic Levenshtein
// edit-distance suggestions. The vocabulary is my own starter English (mined
// from my documents into platform/Psycho_words.txt and embedded into the exe),
// words the owner taught me, and words streamed in from a dictionary source
// file or URL I control.
#include <cstddef>
#include <string>
#include <unordered_set>
#include <vector>

namespace pv {

// One misspelled word: where it sits in the text and what I would type
// instead, best first.
struct SpellMark {
  size_t start = 0, length = 0;
  std::vector<std::string> suggestions;
};

class Spell {
public:
  // OFF means even my built-in vocabulary stays silent; mining the rest still
  // counts. The toggle is the spellBuiltin setting.
  bool builtin_enabled = true;

  void load_builtin_embedded();
  void replace_extra(const std::string &text);
  void set_users(const std::vector<std::string> &words);
  void add_user(const std::string &word);

  bool correct(const std::string &word) const;
  std::vector<std::string> suggest(const std::string &word, size_t limit = 3) const;

  size_t builtin_count() const { return builtin_.size(); }
  size_t extra_count() const { return extra_.size(); }
  size_t user_count() const { return users_.size(); }
  size_t active_count() const;

private:
  std::unordered_set<std::string> builtin_, extra_, users_;
  bool known(const std::string &lower) const;
};

// Text I should leave alone: URLs, addresses and machine tokens are not prose.
bool spell_looks_like_url(const std::string &text);
// Split text into words and say which ones fail my dictionary. Words under
// three letters, numbers and anything carrying digits are skipped.
std::vector<SpellMark> spell_check_text(const Spell &spell, const std::string &text,
                                        size_t suggest_limit = 3);

} // namespace pv
