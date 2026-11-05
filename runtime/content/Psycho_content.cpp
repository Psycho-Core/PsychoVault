// PsychoVault original source. See the header for intent and boundaries - this
// file is the navigation controller's rulebook worked out in full.
#include "Psycho_content.hpp"
#include <algorithm>
#include <cctype>

namespace pv::rt {

namespace {

Error refuse(const char *code, const std::string &message) {
  Error error;
  error.code = code;
  error.message = message;
  return error;
}

std::string lower(std::string text) {
  for (char &c : text) c = (char)std::tolower((unsigned char)c);
  return text;
}

} // namespace

std::string nav_kind_name(NavKind kind) {
  switch (kind) {
  case NavKind::New: return "new";
  case NavKind::Back: return "back";
  case NavKind::Forward: return "forward";
  case NavKind::Reload: return "reload";
  }
  return "unknown";
}

bool admit_http_url(const std::string &raw, std::string &canon, Error &error) {
  error = Error{};
  std::string url = raw;
  // ASCII whitespace at the edges is typing noise, never part of an address.
  while (!url.empty() && std::isspace((unsigned char)url.front())) url.erase(0, 1);
  while (!url.empty() && std::isspace((unsigned char)url.back())) url.pop_back();
  if (url.empty()) {
    error = refuse("nav.empty", "an empty string is not an address");
    return false;
  }
  for (char c : url) {
    if ((unsigned char)c < 0x20 || c == 0x7f) {
      error = refuse("nav.control", "control bytes do not belong in addresses");
      return false;
    }
  }
  const size_t scheme_end = url.find("://");
  if (scheme_end == std::string::npos || scheme_end == 0) {
    error = refuse("nav.scheme", "addresses must name a scheme, like https://");
    return false;
  }
  const std::string scheme = lower(url.substr(0, scheme_end));
  if (scheme != "http" && scheme != "https") {
    error = refuse("nav.scheme", "scheme '" + scheme + "' is not admitted by policy");
    return false;
  }
  const size_t host_begin = scheme_end + 3;
  const size_t host_end = url.find_first_of("/?#", host_begin);
  const std::string host =
      lower(url.substr(host_begin, host_end == std::string::npos
                                       ? std::string::npos : host_end - host_begin));
  if (host.empty()) {
    error = refuse("nav.host", "a host is required after " + scheme + "://");
    return false;
  }
  canon = scheme + "://" + host +
          (host_end == std::string::npos ? "" : url.substr(host_end));
  return true;
}

WebContents::WebContents(int id, int tab_id, Seams seams)
    : id_(id), tab_id_(tab_id), seams_(std::move(seams)) {
  if (!seams_.notify) seams_.notify = [](const Envelope &) {};
  if (!seams_.admit) seams_.admit = admit_http_url;
}

void WebContents::tell(const char *method, Json payload) const {
  Envelope envelope;
  envelope.channel = Channel::Tab;
  envelope.method = method;
  envelope.request_id = 0; // notify: I announce, I never demand an answer
  envelope.payload = std::move(payload);
  seams_.notify(envelope);
}

void WebContents::abort_pending() {
  if (pending_ == -1) return;
  log_[pending_].status = NavStatus::Aborted;
  pending_ = -1;
}

int WebContents::start(NavKind kind, std::string url, int target_index) {
  abort_pending(); // one navigation in flight at a time, by construction
  Navigation nav;
  nav.id = next_nav_id_++;
  nav.kind = kind;
  nav.status = NavStatus::Pending;
  nav.url = std::move(url);
  nav.target_index = target_index;
  log_.push_back(std::move(nav));
  pending_ = (int)log_.size() - 1;
  // Bounded audit memory: corpses drop from the front, never the pending one.
  while (log_.size() > kLogLimit && pending_ > 0) {
    log_.erase(log_.begin());
    --pending_;
  }
  state_ = ContentState::Loading;
  error_code_.clear();
  Json loading = Json::make_object();
  loading.set("contents", Json(id_));
  loading.set("url", Json(log_[pending_].url));
  loading.set("kind", Json(nav_kind_name(kind)));
  tell("tab:loading", std::move(loading));
  Json state = Json::make_object();
  state.set("contents", Json(id_));
  state.set("state", Json(std::string("loading")));
  tell("tab:state", std::move(state));
  return (int)log_[pending_].id;
}

int WebContents::go(const std::string &url, Error &error) {
  error = Error{};
  std::string canon;
  if (!seams_.admit(url, canon, error)) return -1; // refusal changes nothing
  return start(NavKind::New, std::move(canon), -1);
}

int WebContents::go_back(Error &error) {
  error = Error{};
  if (!can_go_back()) {
    error = refuse("nav.bounds", "already at the oldest entry in this session");
    return -1;
  }
  const int target = index_ - 1;
  return start(NavKind::Back, history_[target].url, target);
}

int WebContents::go_forward(Error &error) {
  error = Error{};
  if (!can_go_forward()) {
    error = refuse("nav.bounds", "already at the newest entry in this session");
    return -1;
  }
  const int target = index_ + 1;
  return start(NavKind::Forward, history_[target].url, target);
}

int WebContents::reload(Error &error) {
  error = Error{};
  if (index_ == -1) {
    error = refuse("nav.no-entry", "nothing committed yet - nothing to reload");
    return -1;
  }
  return start(NavKind::Reload, history_[index_].url, index_);
}

void WebContents::apply_metadata(HistoryEntry &entry, const std::string &title,
                                 const std::string &favicon) {
  // A revisit refreshes metadata when supplied, but never grows history.
  if (!title.empty()) entry.title = title;
  if (!favicon.empty()) entry.favicon = favicon;
}

bool WebContents::commit(const std::string &title, const std::string &favicon,
                         Error &error) {
  error = Error{};
  if (pending_ == -1) {
    error = refuse("nav.no-pending", "no navigation is waiting to commit");
    return false;
  }
  Navigation &nav = log_[pending_];
  if (nav.kind == NavKind::New) {
    // A committed NEW navigation kills the forward tail, here and only here -
    // a failed or aborted navigation never rewrites the past.
    history_.resize((size_t)index_ + 1);
    HistoryEntry entry;
    entry.id = next_entry_id_++;
    entry.url = nav.url;
    entry.title = title;
    entry.favicon = favicon;
    entry.navigation = nav.id;
    history_.push_back(std::move(entry));
    while (history_.size() > kHistoryLimit) history_.erase(history_.begin());
    index_ = (int)history_.size() - 1;
  } else {
    index_ = nav.target_index;
    HistoryEntry &entry = history_[(size_t)index_];
    apply_metadata(entry, title, favicon);
  }
  const HistoryEntry &landed = history_[(size_t)index_];
  nav.status = NavStatus::Committed;
  nav.target_index = index_;
  pending_ = -1;
  state_ = ContentState::Idle;

  Json url = Json::make_object();
  url.set("contents", Json(id_));
  url.set("url", Json(landed.url));
  url.set("entry", Json(landed.id));
  tell("tab:url", std::move(url));
  if (!landed.title.empty()) {
    Json notice = Json::make_object();
    notice.set("contents", Json(id_));
    notice.set("title", Json(landed.title));
    tell("tab:title", std::move(notice));
  }
  if (!landed.favicon.empty()) {
    Json notice = Json::make_object();
    notice.set("contents", Json(id_));
    notice.set("favicon", Json(landed.favicon));
    tell("tab:favicon", std::move(notice));
  }
  Json state = Json::make_object();
  state.set("contents", Json(id_));
  state.set("state", Json(std::string("idle")));
  tell("tab:state", std::move(state));
  return true;
}

bool WebContents::fail(const std::string &code, const std::string &message,
                       Error &error) {
  error = Error{};
  if (pending_ == -1) {
    error = refuse("nav.no-pending", "no navigation is waiting to fail");
    return false;
  }
  Navigation &nav = log_[pending_];
  nav.status = NavStatus::Failed;
  nav.error_code = code;
  error_code_ = code;
  state_ = ContentState::Failed;
  const std::string url = nav.url;
  pending_ = -1;

  Json notice = Json::make_object();
  notice.set("contents", Json(id_));
  notice.set("code", Json(code));
  notice.set("message", Json(message));
  notice.set("url", Json(url));
  tell("tab:error", std::move(notice));
  Json state = Json::make_object();
  state.set("contents", Json(id_));
  state.set("state", Json(std::string("failed")));
  tell("tab:state", std::move(state));
  return true;
}

bool WebContents::stop(Error &error) {
  error = Error{};
  if (pending_ == -1) return true; // user stop is idempotent
  log_[pending_].status = NavStatus::Aborted;
  pending_ = -1;
  state_ = ContentState::Idle;
  Json state = Json::make_object();
  state.set("contents", Json(id_));
  state.set("state", Json(std::string("idle")));
  tell("tab:state", std::move(state));
  return true;
}

bool WebContents::set_title(const std::string &title, Error &error) {
  error = Error{};
  if (index_ == -1) {
    error = refuse("nav.no-entry", "nothing committed yet - nothing to retitle");
    return false;
  }
  history_[(size_t)index_].title = title;
  Json notice = Json::make_object();
  notice.set("contents", Json(id_));
  notice.set("title", Json(title));
  tell("tab:title", std::move(notice));
  return true;
}

bool WebContents::set_favicon(const std::string &favicon, Error &error) {
  error = Error{};
  if (index_ == -1) {
    error = refuse("nav.no-entry", "nothing committed yet - nothing to icon");
    return false;
  }
  history_[(size_t)index_].favicon = favicon;
  Json notice = Json::make_object();
  notice.set("contents", Json(id_));
  notice.set("favicon", Json(favicon));
  tell("tab:favicon", std::move(notice));
  return true;
}

const HistoryEntry *WebContents::current() const {
  if (index_ == -1) return nullptr;
  return &history_[(size_t)index_];
}

const Navigation *WebContents::pending() const {
  if (pending_ == -1) return nullptr;
  return &log_[(size_t)pending_];
}

} // namespace pv::rt
