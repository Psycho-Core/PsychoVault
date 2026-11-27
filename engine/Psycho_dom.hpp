#pragma once
#include "Psycho_base.hpp"
namespace pv {
struct Node {
  enum class Type { Document, Element, Text, Comment };
  Type type;
  int id = -1, parent = -1;
  std::string name, text;
  std::vector<std::pair<std::string, std::string>> attributes;
  std::vector<int> children;
  bool get_attribute(std::string_view key, std::string &value) const;
  std::string attribute(std::string_view key, std::string fallback = {}) const;
  void set_attribute(std::string key, std::string value);
  bool has_class(std::string_view name) const;
};
struct Document {
  std::vector<Node> nodes;
  Url url;
  std::string content_type = "text/html", charset = "utf-8";
  std::vector<Diagnostic> diagnostics;
  int root = -1, head = -1, body = -1;
  Document();
  int create(Node::Type type, std::string name = {}, std::string text = {});
  void append(int parent, int child);
  // Insert child under parent before ref (a child of parent), or at the end when ref is -1.
  void insert_before(int parent, int child, int ref);
  // A copy of a node, with its subtree when deep; the copy has no parent.
  int clone_node(int id, bool deep);
  void detach(int child);
  void remove(int child);
  std::string text_content(int id) const;
  void set_text(int id, const std::string &text);
  int element(std::string_view name, int parent = -1) const;
  int by_id(std::string_view id) const;
  std::vector<int> query(std::string_view selector, int scope = -1) const;
  int query_one(std::string_view selector, int scope = -1) const;
  std::string title() const;
  void parse(std::string_view html);
  void parse_fragment(int parent, std::string_view html);
};
bool is_void(std::string_view tag);
// Render password text as one bullet per Unicode scalar; retain the underlying
// value for editing and DOM APIs. This is not full grapheme-cluster handling.
std::string control_display_value(const Node &node, std::string_view value);
struct SelectorState {
  int hovered = -1, focused = -1, active = -1;
  bool focus_visible = true;
  int scope = -1;
};
// Reports whether the engine's bounded selector parser accepts the syntax;
// this is not a complete CSS selector validator.
bool selector_parseable(std::string_view selector);
bool matches_selector(const Document &doc, int node, std::string_view selector,
                      const SelectorState *state = nullptr);
bool form_control_valid(const Document &doc, int node);
} // namespace pv
