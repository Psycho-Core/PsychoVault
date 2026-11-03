// PsychoVault original source. My own regular expression engine.
// I parse a pattern into a small syntax tree and match it by backtracking over
// UTF-8 decoded code points, so a pattern can never split a character.
#pragma once
#include "Psycho_base.hpp"
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <vector>

namespace pv {
struct RegexGroup {
  bool matched = false;
  size_t begin = 0, end = 0;
};
struct RegexResult {
  bool matched = false;
  // I index by capture number: 0 is the whole match.
  std::vector<RegexGroup> groups;
};
class Regex {
public:
  Regex() = default;
  // I return nothing and set error when the pattern is malformed.
  static std::optional<Regex> compile(std::string_view pattern, std::string_view flags,
                                      std::string &error);
  // I search from start and report the leftmost match.
  bool search(const std::vector<uint32_t> &text, size_t start, RegexResult &result) const;
  bool valid() const { return root_ != nullptr; }
  const std::string &source() const { return source_; }
  const std::string &flags() const { return flags_; }
  bool global() const { return global_; }
  bool sticky() const { return sticky_; }
  bool ignore_case() const { return ignore_case_; }
  bool multiline() const { return multiline_; }
  int captures() const { return captures_; }

private:
  struct Node;
  using NodePtr = std::shared_ptr<Node>;
  // I pass the rest of the match in as a continuation, which is what lets me
  // backtrack without a bytecode machine.
  using Cont = std::function<bool(size_t)>;
  friend struct RegexCompiler;
  bool match_node(const Node &node, size_t pos, const Cont &k) const;
  bool match_sequence(const Node &node, size_t index, size_t pos, const Cont &k) const;
  bool match_repeat(const Node &node, size_t count, size_t pos, const Cont &k) const;
  bool match_simple(const Node &node, size_t pos) const;
  bool in_class(const Node &node, char32_t c) const;
  bool equal_char(char32_t a, char32_t b) const;
  NodePtr root_;
  std::string source_, flags_;
  int captures_ = 0;
  bool global_ = false, ignore_case_ = false, multiline_ = false, dot_all_ = false, sticky_ = false;
  // I keep the subject and the capture slots here so a match reads as one call.
  mutable const std::vector<uint32_t> *text_ = nullptr;
  mutable std::vector<RegexGroup> slots_;
  mutable size_t steps_ = 0;
  mutable int depth_ = 0;
};
} // namespace pv
