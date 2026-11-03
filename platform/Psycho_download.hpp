#pragma once
// My download manager. Transfers run on their own threads and publish state
// under a mutex so the UI never blocks on a socket.
#include "Psycho_network.hpp"
#include <atomic>
#include <mutex>
#include <thread>

namespace pv {

struct DownloadItem {
  int id = 0;
  std::string url, final_url, path, filename, mime;
  std::string state = "queued"; // queued | running | complete | cancelled | failed
  std::string error;
  uint64_t received = 0, total = 0;
  int status = 0;
  double started = 0, finished = 0;
  Json to_json() const;
};

class DownloadManager {
public:
  DownloadManager();
  ~DownloadManager();
  DownloadManager(const DownloadManager &) = delete;
  DownloadManager &operator=(const DownloadManager &) = delete;

  void set_directory(const std::string &directory);
  std::string directory() const;
  // I return the new download id, or 0 when I refused the request.
  int start(const Url &url, const NetworkPolicy &policy, std::string &error);
  bool cancel(int id);
  bool remove(int id);
  void clear_finished();
  std::vector<DownloadItem> snapshot() const;
  size_t active_count() const;

  bool load(const std::string &profile_directory);
  bool save() const;

private:
  struct Job {
    std::shared_ptr<std::atomic<bool>> cancelled;
    std::thread worker;
  };
  mutable std::mutex mutex_;
  std::vector<DownloadItem> items_;
  std::map<int, std::shared_ptr<Job>> jobs_;
  std::string directory_, profile_directory_;
  int next_id_ = 1;

  void update(int id, const std::function<void(DownloadItem &)> &change);
  void reap();
};
} // namespace pv
