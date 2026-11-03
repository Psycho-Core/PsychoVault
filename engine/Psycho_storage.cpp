// PsychoVault original source. My web storage keeps each origin's items in the
// order they were added and refuses a write that would pass the quota.
#include "Psycho_storage.hpp"
#include <algorithm>

namespace pv {
namespace {
auto find_entry(std::vector<std::pair<std::string, std::string>> &items, const std::string &name) {
  return std::find_if(items.begin(), items.end(),
                      [&](const std::pair<std::string, std::string> &item) {
                        return item.first == name;
                      });
}
} // namespace

bool Storage::set(const std::string &origin, const std::string &name, const std::string &value) {
  auto &items = data_[origin];
  auto existing = find_entry(items, name);
  size_t used = 0;
  for (const auto &item : items)
    used += cost(item.first, item.second);
  // A write that replaces an existing item only pays for the difference.
  if (existing != items.end())
    used -= cost(existing->first, existing->second);
  if (used + cost(name, value) > kQuota)
    return false;
  if (existing != items.end())
    existing->second = value;
  else
    items.emplace_back(name, value);
  return true;
}

bool Storage::remove(const std::string &origin, const std::string &name) {
  auto found = data_.find(origin);
  if (found == data_.end())
    return false;
  auto &items = found->second;
  auto existing = find_entry(items, name);
  if (existing == items.end())
    return false;
  items.erase(existing);
  return true;
}

std::string Storage::get(const std::string &origin, const std::string &name, bool *found) const {
  if (found)
    *found = false;
  auto it = data_.find(origin);
  if (it == data_.end())
    return {};
  for (const auto &item : it->second) {
    if (item.first == name) {
      if (found)
        *found = true;
      return item.second;
    }
  }
  return {};
}

void Storage::clear(const std::string &origin) {
  data_.erase(origin);
}

size_t Storage::length(const std::string &origin) const {
  auto it = data_.find(origin);
  return it == data_.end() ? 0 : it->second.size();
}

std::string Storage::key(const std::string &origin, size_t index) const {
  auto it = data_.find(origin);
  if (it == data_.end() || index >= it->second.size())
    return {};
  return it->second[index].first;
}

size_t Storage::bytes(const std::string &origin) const {
  auto it = data_.find(origin);
  if (it == data_.end())
    return 0;
  size_t used = 0;
  for (const auto &item : it->second)
    used += cost(item.first, item.second);
  return used;
}

Json Storage::to_json() const {
  Json root = Json::make_object();
  for (const auto &[origin, items] : data_) {
    Json object = Json::make_object();
    for (const auto &[name, value] : items)
      object.set(name, value);
    root.set(origin, std::move(object));
  }
  return root;
}

void Storage::from_json(const Json &json) {
  data_.clear();
  if (json.type != Json::Type::Object)
    return;
  for (const auto &[origin, object] : json.object) {
    if (object.type != Json::Type::Object)
      continue;
    auto &items = data_[origin];
    for (const auto &[name, value] : object.object)
      if (value.type == Json::Type::String)
        items.emplace_back(name, value.string);
  }
}
} // namespace pv
