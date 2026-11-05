// PsychoVault original source. See the header for the two laws - this file is
// how I keep them.
#include "Psycho_apphost.hpp"

namespace pv::rt {

namespace {

// The curated minimum per origin kind. This table IS the privilege model:
// what is not listed does not exist for the page. Note what shell pages do
// NOT get: vault, proton, extensions - extension-less means extension-less.
const ExposedEntry kShellProfile[] = {
    {Channel::Settings, "settings:get"}, {Channel::Settings, "settings:set"},
    {Channel::History, "history:list"},  {Channel::Tab, "tab:new"},
    {Channel::Tab, "tab:activate"},      {Channel::Internal, "internal:list"},
    {Channel::Lifecycle, "lifecycle:ping"},
};

const ExposedEntry kDiagnosticsProfile[] = {
    {Channel::Internal, "internal:list"},
    {Channel::Diagnostics, "diagnostics:stage"},
    {Channel::Lifecycle, "lifecycle:ping"},
};

// Web pages get no facade. There are deliberately no entries here.
const ExposedEntry kWebProfile[] = {};

} // namespace

AppHost::AppHost(int tab_id, const InternalPage *page, Seams seams)
    : tab_id_(tab_id), seams_(std::move(seams)),
      bridge_(PreloadBridge::Seams{seams_.invoke}) {
  const ExposedEntry *profile = kWebProfile;
  size_t count = 0;
  if (page) {
    page_name_ = page->name;
    if (page->category == "shell") {
      kind_ = PageKind::Shell;
      profile = kShellProfile;
      count = sizeof(kShellProfile) / sizeof(kShellProfile[0]);
    } else {
      kind_ = PageKind::Diagnostics;
      profile = kDiagnosticsProfile;
      count = sizeof(kDiagnosticsProfile) / sizeof(kDiagnosticsProfile[0]);
    }
  }
  for (size_t i = 0; i < count; ++i) {
    Error error;
    // A curated name that fails to expose is a build-time bug; I log it and
    // keep the rest of the facade rather than pretend the page booted whole.
    if (!bridge_.expose(profile[i].channel, profile[i].method, error) && seams_.log)
      seams_.log({std::string("apphost.profile"),
                  page_name_ + ": could not expose " + profile[i].method + " (" +
                      error.code + ")"});
  }
  if (kind_ != PageKind::Web) {
    Error error;
    if (!bridge_.install(page_, error) && seams_.log)
      seams_.log({std::string("apphost.install"),
                  page_name_ + ": facade failed to install (" + error.code + ")"});
  }
}

bool AppHost::run_ui(std::string_view source, const std::string &label) {
  try {
    page_.run(source);
    return true;
  } catch (js::JsError &thrown) {
    if (seams_.log)
      seams_.log({std::string("apphost.script"),
                  page_name_ + " " + label + ": " + thrown.value.to_string()});
  } catch (std::exception &bound) {
    // Fuel exhaustion lands here: a runaway script cost the page, not me.
    if (seams_.log)
      seams_.log({std::string("apphost.bound"),
                  page_name_ + " " + label + ": " + bound.what()});
  }
  return false;
}

} // namespace pv::rt
