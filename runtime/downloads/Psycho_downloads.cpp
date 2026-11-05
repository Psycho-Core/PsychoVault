// PsychoVault original source. See the header for the five laws - this file
// is how I keep them.
#include "Psycho_downloads.hpp"

namespace pv::rt {

namespace {

Error refuse(const char *code, const std::string &message) {
  Error error;
  error.code = code;
  error.message = message;
  return error;
}

// My wire: downloads ride http/https like every page load. Anything else has
// no business on a socket I name here.
bool honest_url(const std::string &url) {
  return url.rfind("https://", 0) == 0 || url.rfind("http://", 0) == 0;
}

} // namespace

const char *download_state_name(DownloadState state) {
  switch (state) {
  case DownloadState::Running: return "running";
  case DownloadState::Paused: return "paused";
  case DownloadState::Complete: return "complete";
  case DownloadState::Cancelled: return "cancelled";
  case DownloadState::Failed: return "failed";
  }
  return "?";
}

std::string DownloadService::sanitize_filename(const std::string &url) {
  // The path ends at the first ? or #; the name is what follows the last /.
  std::string path = url;
  const size_t cut = path.find_first_of("?#");
  if (cut != std::string::npos) path.resize(cut);
  const size_t slash = path.find_last_of('/');
  std::string name = slash == std::string::npos ? path : path.substr(slash + 1);
  std::string out;
  for (char c : name) {
    const bool safe = (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
                      (c >= '0' && c <= '9') || c == '.' || c == '_' ||
                      c == '-' || c == ' ';
    out += safe ? c : '_';
  }
  // Neither nothing nor "dot-only" is a name the wire gets to choose.
  bool only_dots = true;
  for (char c : out)
    if (c != '.') {
      only_dots = false;
      break;
    }
  if (out.empty() || only_dots) out = "download";
  if (out.size() > 64) out.resize(64);
  return out;
}

bool DownloadService::terminal(const DownloadRecord &rec) {
  return rec.state == DownloadState::Complete ||
         rec.state == DownloadState::Cancelled ||
         rec.state == DownloadState::Failed;
}

std::deque<DownloadRecord>::iterator DownloadService::find(int id) {
  for (auto it = history_.begin(); it != history_.end(); ++it)
    if (it->id == id) return it;
  return history_.end();
}

std::deque<DownloadRecord>::const_iterator DownloadService::find(int id) const {
  for (auto it = history_.begin(); it != history_.end(); ++it)
    if (it->id == id) return it;
  return history_.end();
}

void DownloadService::changed(const DownloadRecord &rec) {
  if (seams_.on_change) seams_.on_change(rec);
}

size_t DownloadService::active_count() const {
  size_t n = 0;
  for (const DownloadRecord &rec : history_)
    if (!terminal(rec)) ++n;
  return n;
}

int DownloadService::start(const std::string &url, const std::string &target,
                           Error &error) {
  error = Error{};
  if (!seams_.begin_download) {
    error = refuse("download.unavailable", "this build has no transport");
    return 0;
  }
  if (url.empty() || !honest_url(url)) {
    error = refuse("download.url", "downloads ride http/https: '" + url + "'");
    return 0;
  }
  if (active_count() >= kActiveLimit) {
    error = refuse("download.too-many", "32 downloads are already in flight");
    return 0;
  }
  DownloadRecord rec;
  rec.id = next_id_++;
  rec.url = url;
  rec.target = target;
  rec.suggested_name = sanitize_filename(url);
  history_.push_back(rec);
  while (history_.size() > kHistoryLimit) {
    // Law 1: with at most 32 in flight, the oldest of 200 is always terminal.
    history_.pop_front();
  }
  const int handle = seams_.begin_download(history_.back());
  DownloadRecord &live = history_.back();
  if (handle <= 0) {
    // The transport refused after the attempt was recorded (law 1); the
    // record stays and tells the truth, the caller hears the refusal.
    live.state = DownloadState::Failed;
    live.error = "download.transport";
    changed(live);
    error = refuse("download.transport", "the transport refused this download");
    return 0;
  }
  live.transport = handle;
  changed(live);
  return rec.id;
}

bool DownloadService::progress(int id, uint64_t received, uint64_t total,
                               bool resumable, Error &error) {
  error = Error{};
  auto it = find(id);
  if (it == history_.end()) {
    error = refuse("download.unknown", "progress for a download I never started");
    return false;
  }
  if (terminal(*it)) {
    error = refuse("download.finished", "terminal records do not move");
    return false;
  }
  if (received < it->received) {
    error = refuse("download.regress", "bytes never un-arrive");
    return false;
  }
  if (it->total != 0 && total != 0 && total < it->total) {
    error = refuse("download.regress", "a known total never shrinks");
    return false;
  }
  if (total != 0 && received > total) {
    error = refuse("download.overrun", "more bytes arrived than exist");
    return false;
  }
  it->received = received;
  if (total != 0) it->total = total;
  it->resumable = resumable; // the wire's latest word (law 3)
  changed(*it);
  return true;
}

bool DownloadService::pause(int id, Error &error) {
  error = Error{};
  auto it = find(id);
  if (it == history_.end()) {
    error = refuse("download.unknown", "pausing a download I never started");
    return false;
  }
  if (terminal(*it)) {
    error = refuse("download.finished", "terminal records do not move");
    return false;
  }
  if (it->state != DownloadState::Running) {
    error = refuse("download.state", "only a running download can pause");
    return false;
  }
  if (!it->resumable) {
    error = refuse("download.cannot-pause", "the wire never said ranges");
    return false;
  }
  if (seams_.pause_download) seams_.pause_download(it->transport);
  it->state = DownloadState::Paused;
  changed(*it);
  return true;
}

bool DownloadService::resume(int id, Error &error) {
  error = Error{};
  auto it = find(id);
  if (it == history_.end()) {
    error = refuse("download.unknown", "resuming a download I never started");
    return false;
  }
  if (it->state != DownloadState::Paused) {
    error = refuse(terminal(*it) ? "download.finished" : "download.state",
                   "only a paused download can resume");
    return false;
  }
  // Resume from what we honestly hold: the transport re-asks with a Range.
  if (seams_.resume_download) {
    seams_.resume_download(it->transport, it->received);
  }
  it->state = DownloadState::Running;
  changed(*it);
  return true;
}

bool DownloadService::cancel(int id, Error &error) {
  error = Error{};
  auto it = find(id);
  if (it == history_.end()) {
    error = refuse("download.unknown", "cancelling a download I never started");
    return false;
  }
  if (terminal(*it)) {
    error = refuse("download.finished", "terminal records do not move");
    return false;
  }
  if (seams_.cancel_download) seams_.cancel_download(it->transport);
  it->state = DownloadState::Cancelled;
  changed(*it);
  return true;
}

bool DownloadService::complete(int id, Error &error) {
  error = Error{};
  auto it = find(id);
  if (it == history_.end()) {
    error = refuse("download.unknown", "completing a download I never started");
    return false;
  }
  if (terminal(*it)) {
    error = refuse("download.finished", "terminal records do not move");
    return false;
  }
  if (it->state != DownloadState::Running) {
    error = refuse("download.state", "only a running download can complete");
    return false;
  }
  if (it->total != 0 && it->received < it->total) {
    // Law 4: the transport said done, the bytes disagree - that is a failed
    // download, whatever the transport hoped.
    it->state = DownloadState::Failed;
    it->error = "download.truncated";
    changed(*it);
    return true;
  }
  it->state = DownloadState::Complete;
  changed(*it);
  return true;
}

bool DownloadService::fail(int id, const std::string &why, Error &error) {
  error = Error{};
  auto it = find(id);
  if (it == history_.end()) {
    error = refuse("download.unknown", "failing a download I never started");
    return false;
  }
  if (terminal(*it)) {
    error = refuse("download.finished", "terminal records do not move");
    return false;
  }
  it->state = DownloadState::Failed;
  it->error = why.empty() ? "download.failed" : why;
  changed(*it);
  return true;
}

bool DownloadService::set_target(int id, const std::string &path,
                                 Error &error) {
  error = Error{};
  auto it = find(id);
  if (it == history_.end()) {
    error = refuse("download.unknown", "targeting a download I never started");
    return false;
  }
  if (terminal(*it)) {
    error = refuse("download.finished",
                   "save-as while bytes can move; after that the file is where it is");
    return false;
  }
  if (path.empty()) {
    error = refuse("download.target", "a target needs a name");
    return false;
  }
  it->target = path;
  changed(*it);
  return true;
}

bool DownloadService::open(int id, Error &error) {
  error = Error{};
  auto it = find(id);
  if (it == history_.end()) {
    error = refuse("download.unknown", "opening a download I never started");
    return false;
  }
  if (it->state != DownloadState::Complete) {
    error = refuse("download.not-complete", "only a finished download can open");
    return false;
  }
  if (it->target.empty()) {
    error = refuse("download.target", "no save-as target was ever chosen");
    return false;
  }
  if (seams_.file_exists && !seams_.file_exists(it->target)) {
    error = refuse("download.missing", "the disk no longer holds this file");
    return false;
  }
  if (!seams_.open_file) {
    error = refuse("download.unavailable", "this build cannot open files");
    return false;
  }
  seams_.open_file(it->target);
  return true;
}

bool DownloadService::reveal(int id, Error &error) {
  error = Error{};
  auto it = find(id);
  if (it == history_.end()) {
    error = refuse("download.unknown", "revealing a download I never started");
    return false;
  }
  if (it->state != DownloadState::Complete) {
    error = refuse("download.not-complete", "only a finished download can reveal");
    return false;
  }
  if (it->target.empty()) {
    error = refuse("download.target", "no save-as target was ever chosen");
    return false;
  }
  if (seams_.file_exists && !seams_.file_exists(it->target)) {
    error = refuse("download.missing", "the disk no longer holds this file");
    return false;
  }
  if (!seams_.reveal_file) {
    error = refuse("download.unavailable", "this build cannot reveal files");
    return false;
  }
  seams_.reveal_file(it->target);
  return true;
}

const DownloadRecord *DownloadService::record(int id) const {
  auto it = find(id);
  return it == history_.end() ? nullptr : &*it;
}

bool DownloadService::remove_record(int id, Error &error) {
  error = Error{};
  auto it = find(id);
  if (it == history_.end()) {
    error = refuse("download.unknown", "removing a download I never started");
    return false;
  }
  if (!terminal(*it)) {
    error = refuse("download.state", "in-flight downloads are cancelled, not erased");
    return false;
  }
  history_.erase(it);
  return true;
}

void DownloadService::clear_history() {
  for (auto it = history_.begin(); it != history_.end();) {
    if (terminal(*it)) it = history_.erase(it);
    else ++it;
  }
}

} // namespace pv::rt
