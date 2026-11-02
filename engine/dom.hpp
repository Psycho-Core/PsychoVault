#pragma once
#include "base.hpp"
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
bool matches_selector(const Document &doc, int node, std::string_view selector);
} // namespace pv
