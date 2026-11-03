// PsychoVault original source. My own regular expression engine.
#include "Psycho_regex.hpp"
#include <algorithm>
#include <stdexcept>

namespace pv {
namespace {
constexpr size_t kInfinity = size_t(-1);
// I refuse to run a pattern forever. Catastrophic backtracking becomes a clear
// error instead of a frozen browser.
constexpr size_t kStepLimit = 2000000;
constexpr int kNestingLimit = 900;

bool is_digit(char32_t c) {
  return c >= U'0' && c <= U'9';
}
bool is_word(char32_t c) {
  return is_digit(c) || (c >= U'a' && c <= U'z') || (c >= U'A' && c <= U'Z') || c == U'_';
}
char32_t fold(char32_t c) {
  return (c >= U'A' && c <= U'Z') ? char32_t(c + 32) : c;
}
using Range = std::pair<char32_t, char32_t>;
std::vector<Range> digit_ranges() {
  return {{U'0', U'9'}};
}
std::vector<Range> word_ranges() {
  return {{U'0', U'9'}, {U'A', U'Z'}, {U'a', U'z'}, {U'_', U'_'}};
}
std::vector<Range> space_ranges() {
  return {{U'\t', U'\r'}, {U' ', U' '}, {0x00A0, 0x00A0}, {0xFEFF, 0xFEFF}};
}
} // namespace

struct Regex::Node {
  enum class Kind {
    Empty,
    Char,
    Any,
    Class,
    Sequence,
    Alternation,
    Repeat,
    Group,
    Anchor,
    Backref,
    Lookahead
  };
  Kind kind = Kind::Empty;
  char32_t ch = 0;
  bool negate = false;
  std::vector<Range> ranges;
  std::vector<NodePtr> children;
  size_t min = 1, max = 1;
  bool greedy = true;
  int group = -1;
  int anchor = 0;
  bool positive = true;
};

// I turn the pattern text into my syntax tree. Every failure sets one message.
struct RegexCompiler {
  std::vector<uint32_t> src;
  size_t i = 0;
  int captures = 0;
  bool failed = false;
  std::string error;

  explicit RegexCompiler(std::vector<uint32_t> text) : src(std::move(text)) {}
  bool more() const { return i < src.size(); }
  char32_t peek(size_t offset = 0) const {
    return i + offset < src.size() ? src[i + offset] : 0;
  }
  void fail(const std::string &message) {
    if (!failed) {
      failed = true;
      error = message;
    }
  }
  Regex::NodePtr node() const { return std::make_shared<Regex::Node>(); }
  Regex::NodePtr empty() const { return node(); }
  Regex::NodePtr make_char(char32_t c) const {
    auto n = node();
    n->kind = Regex::Node::Kind::Char;
    n->ch = c;
    return n;
  }
  Regex::NodePtr make_class(std::vector<Range> ranges, bool negate) const {
    auto n = node();
    n->kind = Regex::Node::Kind::Class;
    n->ranges = std::move(ranges);
    n->negate = negate;
    return n;
  }
  Regex::NodePtr make_anchor(int which) const {
    auto n = node();
    n->kind = Regex::Node::Kind::Anchor;
    n->anchor = which;
    return n;
  }

  Regex::NodePtr parse() {
    auto root = alternation();
    if (!failed && more())
      fail("unexpected character in pattern");
    return failed ? nullptr : root;
  }

  Regex::NodePtr alternation() {
    std::vector<Regex::NodePtr> branches;
    branches.push_back(sequence());
    while (!failed && more() && peek() == U'|') {
      ++i;
      branches.push_back(sequence());
    }
    if (failed)
      return nullptr;
    if (branches.size() == 1)
      return branches[0];
    auto n = node();
    n->kind = Regex::Node::Kind::Alternation;
    n->children = std::move(branches);
    return n;
  }

  Regex::NodePtr sequence() {
    auto n = node();
    n->kind = Regex::Node::Kind::Sequence;
    while (!failed && more() && peek() != U'|' && peek() != U')') {
      auto term = quantified();
      if (failed)
        return nullptr;
      if (!term)
        break;
      n->children.push_back(std::move(term));
    }
    if (failed)
      return nullptr;
    if (n->children.empty())
      return empty();
    if (n->children.size() == 1)
      return n->children[0];
    return n;
  }

  bool digits(size_t &value) {
    size_t start = i, result = 0;
    while (more() && is_digit(peek())) {
      if (result > 1000000)
        result = 1000000;
      result = result * 10 + size_t(peek() - U'0');
      ++i;
    }
    value = result;
    return i > start;
  }

  Regex::NodePtr quantified() {
    auto atom = element();
    if (failed || !atom)
      return atom;
    while (more()) {
      size_t min = 0, max = 0;
      bool repeat = true;
      char32_t c = peek();
      if (c == U'*') {
        ++i;
        min = 0;
        max = kInfinity;
      } else if (c == U'+') {
        ++i;
        min = 1;
        max = kInfinity;
      } else if (c == U'?') {
        ++i;
        min = 0;
        max = 1;
      } else if (c == U'{') {
        size_t save = i;
        ++i;
        if (!digits(min)) {
          i = save;
          repeat = false;
        } else if (more() && peek() == U',') {
          ++i;
          if (!digits(max))
            max = kInfinity;
        } else {
          max = min;
        }
        if (repeat) {
          if (!more() || peek() != U'}') {
            fail("unterminated counted quantifier");
            return nullptr;
          }
          ++i;
          if (max < min) {
            fail("counted quantifier range is out of order");
            return nullptr;
          }
        }
      } else {
        repeat = false;
      }
      if (!repeat)
        break;
      bool greedy = true;
      if (more() && peek() == U'?') {
        ++i;
        greedy = false;
      }
      auto n = node();
      n->kind = Regex::Node::Kind::Repeat;
      n->children.push_back(std::move(atom));
      n->min = min;
      n->max = max;
      n->greedy = greedy;
      atom = n;
    }
    return atom;
  }

  Regex::NodePtr element() {
    if (!more())
      return empty();
    char32_t c = peek();
    if (c == U'(')
      return group();
    if (c == U'[')
      return character_class();
    if (c == U'.') {
      ++i;
      auto n = node();
      n->kind = Regex::Node::Kind::Any;
      return n;
    }
    if (c == U'^') {
      ++i;
      return make_anchor(0);
    }
    if (c == U'$') {
      ++i;
      return make_anchor(1);
    }
    if (c == U'\\')
      return escape();
    ++i;
    return make_char(c);
  }

  Regex::NodePtr group() {
    ++i; // (
    bool capture = true, lookahead = false, positive = true;
    if (more() && peek() == U'?') {
      ++i;
      if (more() && peek() == U':') {
        ++i;
        capture = false;
      } else if (more() && peek() == U'=') {
        ++i;
        lookahead = true;
      } else if (more() && peek() == U'!') {
        ++i;
        lookahead = true;
        positive = false;
      } else if (more() && peek() == U'<') {
        // I accept a named group and match it as an ordinary capture.
        ++i;
        size_t start = i;
        while (more() && peek() != U'>')
          ++i;
        if (!more() || i == start) {
          fail("unterminated group name");
          return nullptr;
        }
        ++i;
      } else {
        fail("unsupported group syntax");
        return nullptr;
      }
    }
    int index = capture ? ++captures : -1;
    auto inner = alternation();
    if (failed)
      return nullptr;
    if (!more() || peek() != U')') {
      fail("missing closing parenthesis");
      return nullptr;
    }
    ++i;
    auto n = node();
    n->kind = lookahead ? Regex::Node::Kind::Lookahead : Regex::Node::Kind::Group;
    n->group = index;
    n->positive = positive;
    n->children.push_back(std::move(inner));
    return n;
  }

  bool hex(char32_t &value, int digits_wanted) {
    char32_t result = 0;
    for (int k = 0; k < digits_wanted; ++k) {
      if (!more())
        return false;
      char32_t c = peek();
      int digit = -1;
      if (c >= U'0' && c <= U'9')
        digit = int(c - U'0');
      else if (c >= U'a' && c <= U'f')
        digit = int(c - U'a') + 10;
      else if (c >= U'A' && c <= U'F')
        digit = int(c - U'A') + 10;
      else
        return false;
      result = char32_t(result * 16 + digit);
      ++i;
    }
    value = result;
    return true;
  }

  Regex::NodePtr escape() {
    ++i; // backslash
    if (!more()) {
      fail("pattern ends with a backslash");
      return nullptr;
    }
    char32_t c = src[i++];
    switch (c) {
    case U'd':
      return make_class(digit_ranges(), false);
    case U'D':
      return make_class(digit_ranges(), true);
    case U'w':
      return make_class(word_ranges(), false);
    case U'W':
      return make_class(word_ranges(), true);
    case U's':
      return make_class(space_ranges(), false);
    case U'S':
      return make_class(space_ranges(), true);
    case U'b':
      return make_anchor(2);
    case U'B':
      return make_anchor(3);
    case U'n':
      return make_char(U'\n');
    case U'r':
      return make_char(U'\r');
    case U't':
      return make_char(U'\t');
    case U'f':
      return make_char(U'\f');
    case U'v':
      return make_char(U'\v');
    case U'0':
      return make_char(0);
    case U'x': {
      char32_t value = 0;
      if (!hex(value, 2)) {
        fail("bad \\x escape");
        return nullptr;
      }
      return make_char(value);
    }
    case U'u': {
      char32_t value = 0;
      if (!hex(value, 4)) {
        fail("bad \\u escape");
        return nullptr;
      }
      return make_char(value);
    }
    default:
      break;
    }
    if (is_digit(c) && c != U'0') {
      int index = int(c - U'0');
      while (more() && is_digit(peek()) && index * 10 + int(peek() - U'0') <= captures)
        index = index * 10 + int(src[i++] - U'0');
      auto n = node();
      n->kind = Regex::Node::Kind::Backref;
      n->group = index;
      return n;
    }
    return make_char(c);
  }

  void class_piece(std::vector<Range> &ranges, bool &consumed_char, char32_t &value) {
    consumed_char = true;
    if (peek() == U'\\') {
      ++i;
      if (!more()) {
        fail("pattern ends with a backslash");
        return;
      }
      char32_t c = src[i++];
      if (c == U'd') {
        for (auto r : digit_ranges())
          ranges.push_back(r);
        consumed_char = false;
        return;
      }
      if (c == U'w') {
        for (auto r : word_ranges())
          ranges.push_back(r);
        consumed_char = false;
        return;
      }
      if (c == U's') {
        for (auto r : space_ranges())
          ranges.push_back(r);
        consumed_char = false;
        return;
      }
      if (c == U'n') {
        value = U'\n';
        return;
      }
      if (c == U'r') {
        value = U'\r';
        return;
      }
      if (c == U't') {
        value = U'\t';
        return;
      }
      if (c == U'f') {
        value = U'\f';
        return;
      }
      if (c == U'v') {
        value = U'\v';
        return;
      }
      if (c == U'x') {
        char32_t parsed = 0;
        if (!hex(parsed, 2)) {
          fail("bad \\x escape in a character class");
          return;
        }
        value = parsed;
        return;
      }
      if (c == U'u') {
        char32_t parsed = 0;
        if (!hex(parsed, 4)) {
          fail("bad \\u escape in a character class");
          return;
        }
        value = parsed;
        return;
      }
      value = c;
      return;
    }
    value = src[i++];
  }

  Regex::NodePtr character_class() {
    ++i; // [
    bool negate = false;
    if (more() && peek() == U'^') {
      ++i;
      negate = true;
    }
    std::vector<Range> ranges;
    while (more()) {
      if (peek() == U']')
        break;
      bool single = false;
      char32_t lo = 0;
      class_piece(ranges, single, lo);
      if (failed)
        return nullptr;
      if (!single)
        continue;
      if (more() && peek() == U'-' && i + 1 < src.size() && src[i + 1] != U']') {
        ++i;
        bool single_hi = false;
        char32_t hi = 0;
        class_piece(ranges, single_hi, hi);
        if (failed)
          return nullptr;
        if (!single_hi) {
          fail("character class range ends with a set");
          return nullptr;
        }
        if (hi < lo)
          std::swap(lo, hi);
        ranges.push_back({lo, hi});
      } else {
        ranges.push_back({lo, lo});
      }
    }
    if (!more()) {
      fail("unterminated character class");
      return nullptr;
    }
    ++i; // ]
    return make_class(std::move(ranges), negate);
  }
};
} // namespace pv

namespace pv {
std::optional<Regex> Regex::compile(std::string_view pattern, std::string_view flags,
                                    std::string &error) {
  Regex regex;
  regex.source_ = std::string(pattern);
  regex.flags_ = std::string(flags);
  for (char flag : flags) {
    switch (flag) {
    case 'g':
      regex.global_ = true;
      break;
    case 'i':
      regex.ignore_case_ = true;
      break;
    case 'm':
      regex.multiline_ = true;
      break;
    case 's':
      regex.dot_all_ = true;
      break;
    case 'y':
      regex.sticky_ = true;
      break;
    case 'u':
    case 'd':
    case 'v':
      // I already match code points and I do not add named capture groups to
      // the match array, so these flags change nothing for me yet.
      break;
    default:
      error = std::string("invalid regular expression flag '") + flag + "'";
      return std::nullopt;
    }
  }
  RegexCompiler compiler(utf8_decode(regex.source_));
  auto root = compiler.parse();
  if (compiler.failed || !root) {
    error = compiler.error.empty() ? "invalid regular expression" : compiler.error;
    return std::nullopt;
  }
  regex.root_ = std::move(root);
  regex.captures_ = compiler.captures;
  return regex;
}

bool Regex::equal_char(char32_t a, char32_t b) const {
  if (a == b)
    return true;
  return ignore_case_ && fold(a) == fold(b);
}

bool Regex::in_class(const Node &node, char32_t c) const {
  char32_t value = ignore_case_ ? fold(c) : c;
  bool found = false;
  for (const auto &range : node.ranges) {
    char32_t lo = ignore_case_ ? fold(range.first) : range.first;
    char32_t hi = ignore_case_ ? fold(range.second) : range.second;
    if (lo > hi)
      std::swap(lo, hi);
    if (value >= lo && value <= hi) {
      found = true;
      break;
    }
  }
  return node.negate ? !found : found;
}

bool Regex::match_simple(const Node &node, size_t pos) const {
  const std::vector<uint32_t> &text = *text_;
  if (node.kind == Node::Kind::Any) {
    return pos < text.size() && (dot_all_ || text[pos] != U'\n');
  }
  if (pos >= text.size())
    return false;
  if (node.kind == Node::Kind::Char)
    return equal_char(text[pos], node.ch);
  return in_class(node, text[pos]);
}

bool Regex::match_sequence(const Node &node, size_t index, size_t pos, const Cont &k) const {
  if (index == node.children.size())
    return k(pos);
  return match_node(*node.children[index], pos, [this, &node, index, &k](size_t next) {
    return match_sequence(node, index + 1, next, k);
  });
}

bool Regex::match_repeat(const Node &node, size_t count, size_t pos, const Cont &k) const {
  const Node &child = *node.children[0];
  if (++depth_ > kNestingLimit) {
    --depth_;
    throw std::runtime_error("regular expression nesting limit");
  }
  struct Guard {
    int &depth;
    ~Guard() { --depth; }
  } guard{depth_};
  bool simple = child.kind == Node::Kind::Char || child.kind == Node::Kind::Any ||
                child.kind == Node::Kind::Class;
  if (simple) {
    // I count a run of single characters in a loop, so `.*` cannot exhaust my
    // stack on a long line.
    size_t taken = 0;
    size_t scan = pos;
    while (count + taken < node.max && match_simple(child, scan)) {
      ++scan;
      ++taken;
      if (++steps_ > kStepLimit)
        throw std::runtime_error("regular expression step limit");
    }
    if (count + taken < node.min)
      return false;
    if (node.greedy) {
      for (size_t take = taken + 1; take-- > 0;) {
        if (count + take >= node.min && k(pos + take))
          return true;
        if (take == 0)
          break;
      }
      return false;
    }
    for (size_t take = 0; take <= taken; ++take) {
      if (count + take < node.min)
        continue;
      if (k(pos + take))
        return true;
    }
    return false;
  }
  auto again = [this, &node, count, pos, &k](size_t next) {
    // An iteration that consumes nothing would repeat forever.
    if (next == pos)
      return false;
    return match_repeat(node, count + 1, next, k);
  };
  if (node.greedy) {
    if (count < node.max && match_node(child, pos, again))
      return true;
    return count >= node.min ? k(pos) : false;
  }
  if (count >= node.min && k(pos))
    return true;
  if (count >= node.max)
    return false;
  return match_node(child, pos, again);
}

bool Regex::match_node(const Node &node, size_t pos, const Cont &k) const {
  if (++steps_ > kStepLimit)
    throw std::runtime_error("regular expression step limit");
  const std::vector<uint32_t> &text = *text_;
  switch (node.kind) {
  case Node::Kind::Empty:
    return k(pos);
  case Node::Kind::Char:
    if (pos < text.size() && equal_char(text[pos], node.ch))
      return k(pos + 1);
    return false;
  case Node::Kind::Any:
    if (pos < text.size() && (dot_all_ || text[pos] != U'\n'))
      return k(pos + 1);
    return false;
  case Node::Kind::Class:
    if (pos < text.size() && in_class(node, text[pos]))
      return k(pos + 1);
    return false;
  case Node::Kind::Sequence:
    return match_sequence(node, 0, pos, k);
  case Node::Kind::Alternation: {
    for (const auto &child : node.children)
      if (match_node(*child, pos, k))
        return true;
    return false;
  }
  case Node::Kind::Repeat:
    return match_repeat(node, 0, pos, k);
  case Node::Kind::Group: {
    size_t index = size_t(std::max(0, node.group));
    bool tracked = node.group >= 0 && index < slots_.size();
    if (!tracked)
      return match_node(*node.children[0], pos, k);
    RegexGroup saved = slots_[index];
    bool ok = match_node(*node.children[0], pos, [this, index, pos, &k](size_t end) {
      RegexGroup previous = slots_[index];
      slots_[index] = RegexGroup{true, pos, end};
      if (k(end))
        return true;
      slots_[index] = previous;
      return false;
    });
    if (!ok)
      slots_[index] = saved;
    return ok;
  }
  case Node::Kind::Lookahead: {
    std::vector<RegexGroup> snapshot = slots_;
    bool matched = match_node(*node.children[0], pos, [](size_t) { return true; });
    if (matched == node.positive && k(pos))
      return true;
    slots_ = std::move(snapshot);
    return false;
  }
  case Node::Kind::Backref: {
    if (node.group < 0 || size_t(node.group) >= slots_.size())
      return false;
    const RegexGroup &group = slots_[size_t(node.group)];
    if (!group.matched)
      return k(pos);
    size_t length = group.end - group.begin;
    if (pos + length > text.size())
      return false;
    for (size_t offset = 0; offset < length; ++offset)
      if (!equal_char(text[pos + offset], text[group.begin + offset]))
        return false;
    return k(pos + length);
  }
  case Node::Kind::Anchor: {
    switch (node.anchor) {
    case 0:
      return pos == 0 || (multiline_ && text[pos - 1] == U'\n') ? k(pos) : false;
    case 1:
      return pos == text.size() || (multiline_ && text[pos] == U'\n') ? k(pos) : false;
    case 2: {
      bool before = pos > 0 && is_word(text[pos - 1]);
      bool after = pos < text.size() && is_word(text[pos]);
      return before != after ? k(pos) : false;
    }
    default: {
      bool before = pos > 0 && is_word(text[pos - 1]);
      bool after = pos < text.size() && is_word(text[pos]);
      return before == after ? k(pos) : false;
    }
    }
  }
  }
  return false;
}

bool Regex::search(const std::vector<uint32_t> &text, size_t start, RegexResult &result) const {
  result = RegexResult{};
  if (!root_)
    return false;
  text_ = &text;
  steps_ = 0;
  depth_ = 0;
  size_t begin = std::min(start, text.size());
  for (size_t pos = begin; pos <= text.size(); ++pos) {
    slots_.assign(size_t(captures_) + 1, RegexGroup{});
    bool matched =
        match_node(*root_, pos, [this, pos](size_t end) {
          slots_[0] = RegexGroup{true, pos, end};
          return true;
        });
    if (matched) {
      result.matched = true;
      result.groups = slots_;
      text_ = nullptr;
      return true;
    }
    if (sticky_)
      break;
  }
  text_ = nullptr;
  return false;
}
} // namespace pv
