#include "Psycho_download.hpp"
#include <filesystem>

namespace pv {

Json DownloadItem::to_json() const {
  Json j = Json::make_object();
  j.set("id", id);
  j.set("url", url);
  j.set("finalUrl", final_url);
  j.set("path", path);
  j.set("filename", filename);
  j.set("mime", mime);
  j.set("state", state);
  j.set("error", error);
  j.set("received", double(received));
  j.set("total", double(total));
  j.set("status", status);
  j.set("started", started);
  j.set("finished", finished);
  return j;
}

DownloadManager::DownloadManager() { directory_ = default_download_directory(); }

DownloadManager::~DownloadManager() {
  {
    std::lock_guard lock(mutex_);
    for (auto &entry : jobs_)
      if (entry.second && entry.second->cancelled) entry.second->cancelled->store(true);
  }
  // I join every worker before my state dies so no thread touches a dead mutex.
  std::map<int, std::shared_ptr<Job>> remaining;
  {
    std::lock_guard lock(mutex_);
    remaining.swap(jobs_);
  }
  for (auto &entry : remaining)
    if (entry.second && entry.second->worker.joinable()) entry.second->worker.join();
}

void DownloadManager::set_directory(const std::string &directory) {
  std::lock_guard lock(mutex_);
  if (!directory.empty()) directory_ = directory;
}

std::string DownloadManager::directory() const {
  std::lock_guard lock(mutex_);
  return directory_;
}

void DownloadManager::update(int id, const std::function<void(DownloadItem &)> &change) {
  std::lock_guard lock(mutex_);
  for (auto &item : items_)
    if (item.id == id) {
      change(item);
      return;
    }
}

void DownloadManager::reap() {
  std::vector<std::shared_ptr<Job>> finished;
  {
    std::lock_guard lock(mutex_);
    for (auto it = jobs_.begin(); it != jobs_.end();) {
      bool terminal = false;
      for (const auto &item : items_)
        if (item.id == it->first)
          terminal = item.state == "complete" || item.state == "failed" ||
                     item.state == "cancelled";
      if (terminal && it->second) {
        finished.push_back(it->second);
        it = jobs_.erase(it);
      } else {
        ++it;
      }
    }
  }
  // I join outside the lock because the worker takes the lock to publish state.
  for (auto &job : finished)
    if (job && job->worker.joinable()) job->worker.join();
}

int DownloadManager::start(const Url &url, const NetworkPolicy &policy, std::string &error) {
  if (!url.network()) {
    error = "I only download http and https URLs";
    return 0;
  }
  reap();
  std::string blocked;
  if (policy_blocks(policy, url, blocked)) {
    error = blocked;
    return 0;
  }
  namespace fs = std::filesystem;
  int id = 0;
  std::string destination;
  {
    std::lock_guard lock(mutex_);
    if (jobs_.size() >= 8) {
      error = "I already have 8 downloads running";
      return 0;
    }
    std::error_code ec;
    fs::create_directories(fs::path(directory_), ec);
    if (ec) {
      error = "I cannot create my download folder";
      return 0;
    }
    auto name = safe_download_name(url, "");
    destination = unique_download_path(directory_, name);
    id = next_id_++;
    DownloadItem item;
    item.id = id;
    item.url = url.href();
    item.final_url = url.href();
    item.path = destination;
    item.filename = fs::path(destination).filename().string();
    item.state = "running";
    item.started = double(now_ms());
    items_.insert(items_.begin(), item);
    if (items_.size() > 500) items_.resize(500);
    auto job = std::make_shared<Job>();
    job->cancelled = std::make_shared<std::atomic<bool>>(false);
    jobs_[id] = job;
    auto cancelled = job->cancelled;
    job->worker = std::thread([this, id, url, destination, policy, cancelled] {
      auto outcome = download_to_file(
          url, destination, policy, [&] { return cancelled->load(); },
          [&](const DownloadProgress &progress) {
            update(id, [&](DownloadItem &item) {
              item.received = progress.received;
              item.total = progress.total;
            });
          });
      update(id, [&](DownloadItem &item) {
        item.received = outcome.received;
        if (outcome.total) item.total = outcome.total;
        item.status = outcome.status;
        item.final_url = outcome.final_url;
        item.mime = outcome.mime;
        item.error = outcome.error;
        item.finished = double(now_ms());
        item.state = outcome.ok ? "complete" : outcome.cancelled ? "cancelled" : "failed";
      });
      save();
    });
  }
  save();
  return id;
}

bool DownloadManager::cancel(int id) {
  std::shared_ptr<Job> job;
  {
    std::lock_guard lock(mutex_);
    auto found = jobs_.find(id);
    if (found == jobs_.end()) return false;
    job = found->second;
  }
  if (job && job->cancelled) job->cancelled->store(true);
  return true;
}

bool DownloadManager::remove(int id) {
  cancel(id);
  reap();
  {
    std::lock_guard lock(mutex_);
    for (auto it = items_.begin(); it != items_.end(); ++it)
      if (it->id == id) {
        if (it->state == "running") return false;
        items_.erase(it);
        break;
      }
  }
  save();
  return true;
}

void DownloadManager::clear_finished() {
  reap();
  {
    std::lock_guard lock(mutex_);
    items_.erase(std::remove_if(items_.begin(), items_.end(),
                                [](const DownloadItem &item) {
                                  return item.state == "complete" || item.state == "failed" ||
                                         item.state == "cancelled";
                                }),
                 items_.end());
  }
  save();
}

std::vector<DownloadItem> DownloadManager::snapshot() const {
  std::lock_guard lock(mutex_);
  return items_;
}

size_t DownloadManager::active_count() const {
  std::lock_guard lock(mutex_);
  return jobs_.size();
}

bool DownloadManager::load(const std::string &profile_directory) {
  namespace fs = std::filesystem;
  {
    std::lock_guard lock(mutex_);
    profile_directory_ = profile_directory;
  }
  auto path = fs::path(profile_directory) / "owned-downloads.json";
  if (!fs::exists(path)) return true;
  try {
    auto json = Json::parse(read_text_file(path.string()));
    if (!json || json->type != Json::Type::Array) return false;
    std::lock_guard lock(mutex_);
    for (const auto &entry : json->array) {
      if (entry.type != Json::Type::Object || items_.size() >= 500) continue;
      DownloadItem item;
      item.id = int(entry.num("id", 0));
      item.url = entry.str("url");
      item.final_url = entry.str("finalUrl", item.url);
      item.path = entry.str("path");
      item.filename = entry.str("filename");
      item.mime = entry.str("mime");
      item.state = entry.str("state", "failed");
      item.error = entry.str("error");
      item.received = uint64_t(std::max(0.0, entry.num("received", 0)));
      item.total = uint64_t(std::max(0.0, entry.num("total", 0)));
      item.status = int(entry.num("status", 0));
      item.started = entry.num("started", 0);
      item.finished = entry.num("finished", 0);
      // A transfer cannot survive a restart, so I never claim one is still live.
      if (item.state == "running" || item.state == "queued") {
        item.state = "failed";
        item.error = "interrupted when I closed the browser";
      }
      if (item.id >= next_id_) next_id_ = item.id + 1;
      items_.push_back(item);
    }
  } catch (const std::exception &) {
    return false;
  }
  return true;
}

bool DownloadManager::save() const {
  std::string target;
  Json all = Json::make_array();
  {
    std::lock_guard lock(mutex_);
    if (profile_directory_.empty()) return true;
    target = (std::filesystem::path(profile_directory_) / "owned-downloads.json").string();
    for (const auto &item : items_) all.array.push_back(item.to_json());
  }
  std::string error;
  return write_file_atomic(target, all.dump() + "\n", error);
}
} // namespace pv
