// PsychoVault original source. See Psycho_diagnostics_page.hpp.
#include "Psycho_diagnostics_page.hpp"

namespace pv::rt {

namespace {

const char *kHead =
    "<!doctype html><html lang=\"en\"><head><meta charset=\"utf-8\">"
    "<title>%TITLE%</title><style>"
    "html,body{margin:0;background:#080d16;color:#e6edf3;"
    "font-family:\"PsychoVault UI\",\"Segoe UI\",Arial,sans-serif}"
    ".page{width:760px;margin:48px auto 0}"
    "h1{margin:0 0 6px;font-size:28px;font-weight:600}"
    ".note{margin:0 0 24px;color:#9aa9bb;font-size:13px}"
    ".row{padding:12px 16px;margin-bottom:8px;border:1px solid #253449;"
    "border-radius:8px;background:#101927}"
    ".name{font-size:14px;font-weight:700}"
    ".pass{color:#4ade80}.fail{color:#f87171}.na{color:#fbbf24}"
    ".detail{margin-top:4px;color:#9aa9bb;font-size:12px}"
    "</style></head><body><div class=\"page\">";
const char *kTail = "</div></body></html>";

std::string head(const std::string &title) {
  std::string out = kHead;
  const std::string marker = "%TITLE%";
  out.replace(out.find(marker), marker.size(), diagnostics_html_escape(title));
  return out;
}

void row(std::string &out, const std::string &name, const char *cls,
         const std::string &verdict, const std::string &detail) {
  out += "<div class=\"row\"><div class=\"name\">" + diagnostics_html_escape(name) +
         " - <span class=\"" + cls + "\">" + diagnostics_html_escape(verdict) +
         "</span></div>";
  if (!detail.empty())
    out += "<div class=\"detail\">" + diagnostics_html_escape(detail) + "</div>";
  out += "</div>";
}

} // namespace

std::string diagnostics_html_escape(const std::string &text) {
  std::string out;
  out.reserve(text.size());
  for (char ch : text) {
    switch (ch) {
    case '&': out += "&amp;"; break;
    case '<': out += "&lt;"; break;
    case '>': out += "&gt;"; break;
    case '"': out += "&quot;"; break;
    case '\'': out += "&#39;"; break;
    default: out += ch; break;
    }
  }
  return out;
}

std::string render_nettest_page(const std::vector<DiagnosticStageResult> &stages) {
  std::string out = head("PsychoVault network test");
  out += "<h1>Network test</h1><p class=\"note\">Each stage reports what it "
         "measured. A stage with no probe wired says so; it is never counted as "
         "a pass.</p>";
  for (const auto &stage : stages) {
    if (!stage.error.empty()) {
      row(out, stage.name, "na", "unavailable (" + stage.error + ")", stage.detail);
    } else {
      row(out, stage.name, stage.ok ? "pass" : "fail", stage.ok ? "passed" : "failed",
          stage.detail);
    }
  }
  out += kTail;
  return out;
}

std::string render_version_page(bool available, const VersionInfo &info,
                                const Error &error) {
  std::string out = head("PsychoVault version");
  out += "<h1>Version</h1>";
  if (!available) {
    out += "<p class=\"note\">No version facts were provided.</p>";
    row(out, "version", "na", "unavailable (" + error.code + ")", error.message);
    out += kTail;
    return out;
  }
  out += "<p class=\"note\">Facts supplied by the broker, shown as given.</p>";
  row(out, "runtime", "pass", info.runtime_version, "");
  row(out, "engine", "pass", info.engine_version, "");
  row(out, "profile", "pass", info.profile, "");
  std::string flags;
  for (const auto &flag : info.flags) flags += (flags.empty() ? "" : " ") + flag;
  row(out, "flags", "pass", flags.empty() ? "none" : flags, "");
  if (info.hashes.empty()) {
    row(out, "hashes", "na", "none provided", "");
  } else {
    for (const auto &[name, value] : info.hashes) row(out, "hash " + name, "pass", value, "");
  }
  out += kTail;
  return out;
}

} // namespace pv::rt
