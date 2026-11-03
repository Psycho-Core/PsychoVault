// PsychoVault original source. I keep web storage myself: one ordered list per
// origin, with the quota a browser is expected to enforce.
#pragma once
#include "Psycho_base.hpp"
#include <map>
#include <string>
#include <utility>
#include <vector>

namespace pv {

class Storage {
public:
  // Browsers give a page about five megabytes per origin. I count the bytes of
  // name plus value, which is the number a page can actually see.
  static constexpr size_t kQuota = 5u * 1024u * 1024u;
  // I return false when the write would go over quota, so the page can raise
  // QuotaExceededError instead of silently losing data.
  bool set(const std::string &origin, const std::string &name, const std::string &value);
  bool remove(const std::string &origin, const std::string &name);
  std::string get(const std::string &origin, const std::string &name, bool *found = nullptr) const;
  void clear(const std::string &origin);
  size_t length(const std::string &origin) const;
  // I return keys in the order they were first written, which is what a page
  // walking key(0), key(1)... expects.
  std::string key(const std::string &origin, size_t index) const;
  size_t bytes(const std::string &origin) const;
  size_t origins() const {
    return data_.size();
  }
  Json to_json() const;
  void from_json(const Json &json);

private:
  using Entry = std::pair<std::string, std::string>;
  std::map<std::string, std::vector<Entry>> data_;
  static size_t cost(const std::string &name, const std::string &value) {
    return name.size() + value.size();
  }
};
} // namespace pv
