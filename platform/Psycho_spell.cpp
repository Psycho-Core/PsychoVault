#include "Psycho_spell.hpp"
#include "Psycho_embedded_words.hpp" // written by tools/Psycho_build.py from Psycho_words.txt

#include <algorithm>
#include <cctype>
#include <unordered_set>

namespace pv {

namespace {

std::string lower_copy(const std::string &word) {
  std::string out = word;
  for (char &c : out)
    c = char(std::tolower(static_cast<unsigned char>(c)));
  return out;
}

bool has_digit(const std::string &word) {
  for (char c : word)
    if (std::isdigit(static_cast<unsigned char>(c)))
      return true;
  return false;
}

// The common contractions my vocabulary cannot cover by mining prose, kept
// explicitly so the behaviour does not drift with my documents.
const char *kContractions[] = {
    "aren't",  "can't",    "couldn't", "didn't", "doesn't", "don't",
    "hadn't",  "hasn't",   "haven't",  "he'd",   "he'll",  "he's",
    "here's",  "how's",    "i'd",      "i'll",   "i'm",    "i've",
    "isn't",   "it'll",    "it's",     "let's",  "mightn't", "mustn't",
    "needn't", "o'clock",  "shan't",   "she'd",  "she'll", "she's",
    "shouldn't", "that's", "there's",  "they'd", "they'll", "they're",
    "they've", "wasn't",   "we'd",     "we'll",  "we're",  "we've",
    "weren't", "what's",   "who's",    "won't",  "wouldn't", "you'd",
    "you'll",  "you're",   "you've",
};

const char *kTails[] = {"s", "t", "re", "ll", "ve", "d", "m"};

bool is_contraction_tail(const std::string &tail) {
  for (const char *t : kTails)
    if (tail == t)
      return true;
  return false;
}

bool is_contraction(const std::string &lower) {
  for (const char *c : kContractions)
    if (lower == c)
      return true;
  return false;
}

// Every string one edit away: deletion, transposition, substitution and
// insertion. Emitted in that rank order, which is the order closeness shows.
void edits_once(const std::string &word, std::vector<std::string> &out) {
  const size_t n = word.size();
  for (size_t i = 0; i < n; ++i) {
    std::string c = word;
    c.erase(c.begin() + ptrdiff_t(i));
    out.push_back(std::move(c));
  }
  for (size_t i = 0; i + 1 < n; ++i) {
    std::string c = word;
    std::swap(c[i], c[i + 1]);
    out.push_back(std::move(c));
  }
  for (size_t i = 0; i < n; ++i)
    for (char ch = 'a'; ch <= 'z'; ++ch) {
      if (word[i] == ch)
        continue;
      std::string c = word;
      c[i] = ch;
      out.push_back(std::move(c));
    }
  for (size_t i = 0; i <= n; ++i)
    for (char ch = 'a'; ch <= 'z'; ++ch) {
      std::string c = word;
      c.insert(c.begin() + ptrdiff_t(i), ch);
      out.push_back(std::move(c));
    }
}

} // namespace

void Spell::load_builtin_embedded() {
  builtin_.clear();
  const char *at = kPVEmbeddedWords;
  std::string word;
  for (const char *p = at; *p; ++p) {
    if (*p == '\n') {
      if (!word.empty() && word[0] != '#')
        builtin_.insert(word);
      word.clear();
    } else if (*p != '\r') {
      word.push_back(*p);
    }
  }
  if (!word.empty() && word[0] != '#')
    builtin_.insert(word);
}

void Spell::replace_extra(const std::string &text) {
  extra_.clear();
  std::string word;
  for (char raw : text) {
    unsigned char c = static_cast<unsigned char>(raw);
    if (std::isalpha(c)) {
      word.push_back(char(std::tolower(c)));
    } else {
      if (word.size() >= 2)
        extra_.insert(word);
      word.clear();
    }
  }
  if (word.size() >= 2)
    extra_.insert(word);
}

void Spell::set_users(const std::vector<std::string> &words) {
  users_.clear();
  for (const auto &word : words)
    add_user(word);
}

void Spell::add_user(const std::string &word) {
  std::string lower = lower_copy(word);
  if (lower.size() >= 2 && !has_digit(lower))
    users_.insert(lower);
}

bool Spell::known(const std::string &lower) const {
  bool hit = extra_.count(lower) || users_.count(lower);
  hit = hit || (builtin_enabled && builtin_.count(lower));
  return hit != 0;
}

bool Spell::correct(const std::string &word) const {
  if (word.size() < 2 || has_digit(word))
    return true;
  // Never loaded a dictionary means I cannot judge, so I stay quiet.
  if (builtin_.empty() && extra_.empty() && users_.empty())
    return true;
  std::string lower = lower_copy(word);
  // An ALL-CAPS shout or a Capitalised start matches the plain form.
  if (known(lower))
    return true;
  if (is_contraction(lower))
    return true;
  // name's: a possessive tail on a known word.
  size_t apos = lower.find_last_of('\'');
  if (apos != std::string::npos && apos > 0) {
    std::string head = lower.substr(0, apos), tail = lower.substr(apos + 1);
    if (is_contraction_tail(tail) && (known(head) || head == "i" || head == "you" ||
                                      head == "he" || head == "she" || head == "we" ||
                                      head == "they" || head == "it" || head == "who" ||
                                      head == "there" || head == "that" || head == "what" ||
                                      head == "here" || head == "how" || head == "let"))
      return true;
    // word's after a known word, and trailing ' on a known plural-style word.
    if (tail.empty() && known(head))
      return true;
  }
  return false;
}

std::vector<std::string> Spell::suggest(const std::string &word, size_t limit) const {
  std::vector<std::string> out;
  std::string lower = lower_copy(word);
  if (correct(word))
    return out;
  std::unordered_set<std::string> seen;
  std::vector<std::string> round;
  edits_once(lower, round);
  auto collect = [&](const std::vector<std::string> &candidates) {
    for (const std::string &c : candidates) {
      if (seen.count(c))
        continue;
      seen.insert(c);
      if (known(c))
        out.push_back(c);
    }
  };
  collect(round);
  if (out.empty() && lower.size() >= 6) {
    // Longer words get one more edit of reach, but I cap the fan-out so a
    // giant unknown token cannot stall a keystroke.
    std::vector<std::string> deeper;
    size_t spent = 0;
    for (const std::string &first : round) {
      if (spent > 400000)
        break;
      edits_once(first, deeper);
      spent += deeper.size();
      collect(deeper);
      std::sort(deeper.begin(), deeper.end());
      deeper.erase(std::unique(deeper.begin(), deeper.end()), deeper.end());
      if (out.size() >= limit)
        break;
    }
  }
  // Within the classic ranks, shorter alphabetical order keeps output stable.
  std::sort(out.begin(), out.end());
  out.erase(std::unique(out.begin(), out.end()), out.end());
  if (out.size() > limit)
    out.resize(limit);
  return out;
}

size_t Spell::active_count() const {
  return (builtin_enabled ? builtin_.size() : 0) + extra_.size() + users_.size();
}

bool spell_looks_like_url(const std::string &text) {
  std::string lower = lower_copy(text);
  if (lower.find("://") != std::string::npos)
    return true;
  if (lower.rfind("www.", 0) == 0)
    return true;
  if (text.find(' ') != std::string::npos)
    return false;
  size_t dot = lower.rfind('.');
  if (dot == std::string::npos || dot + 1 >= lower.size())
    return false;
  std::string tail = lower.substr(dot + 1);
  if (tail.size() < 2 || tail.size() > 8)
    return false;
  for (char c : tail)
    if (!std::isalpha(static_cast<unsigned char>(c)))
      return false;
  std::string host = lower.substr(0, dot);
  return !host.empty();
}

std::vector<SpellMark> spell_check_text(const Spell &spell, const std::string &text,
                                        size_t suggest_limit) {
  std::vector<SpellMark> marks;
  if (text.empty() || spell_looks_like_url(text))
    return marks;
  size_t i = 0;
  const size_t n = text.size();
  while (i < n) {
    unsigned char c = static_cast<unsigned char>(text[i]);
    if (!std::isalpha(c)) {
      ++i;
      continue;
    }
    size_t start = i;
    while (i < n) {
      unsigned char k = static_cast<unsigned char>(text[i]);
      if (std::isalpha(k) || (text[i] == '\'' && i + 1 < n &&
                              std::isalpha(static_cast<unsigned char>(text[i + 1])))) {
        ++i;
      } else {
        break;
      }
    }
    std::string word = text.substr(start, i - start);
    while (!word.empty() && word.back() == '\'')
      word.pop_back();
    if (word.size() < 3 || has_digit(word))
      continue;
    if (!spell.correct(word)) {
      SpellMark mark;
      mark.start = start;
      mark.length = word.size();
      mark.suggestions = spell.suggest(word, suggest_limit);
      marks.push_back(std::move(mark));
    }
  }
  return marks;
}

} // namespace pv
