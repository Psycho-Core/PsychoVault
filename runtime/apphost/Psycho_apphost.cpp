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
    {Channel::Diagnostics, "diagnostics:nettest"},
    {Channel::Diagnostics, "diagnostics:version"},
    {Channel::Lifecycle, "lifecycle:ping"},
};

// A private renderer has a distinct shell policy: no profile settings or
// history surface, even read-only. Its shell can manage private tabs and
// resolve its own internal pages. Extensions remain absent in both policies.
const ExposedEntry kPrivateShellProfile[] = {
    {Channel::Tab, "tab:new"},
    {Channel::Tab, "tab:activate"},
    {Channel::Internal, "internal:list"},
    {Channel::Lifecycle, "lifecycle:ping"},
};

// The new-tab app surface gets only the calls its address and tab controls need.
// It has no settings, history, diagnostics, extension, vault or network facade.
const ExposedEntry kNewTabProfile[] = {
    {Channel::Tab, "tab:new"},
    {Channel::Tab, "tab:activate"},
    {Channel::Lifecycle, "lifecycle:ping"},
};

const ExposedEntry kPrivateDiagnosticsProfile[] = {
    {Channel::Internal, "internal:list"},
    {Channel::Diagnostics, "diagnostics:stage"},
    {Channel::Diagnostics, "diagnostics:nettest"},
    {Channel::Diagnostics, "diagnostics:version"},
    {Channel::Lifecycle, "lifecycle:ping"},
};

// The crash page shows only broker-owned failure state; it gets neither the
// general diagnostics controls nor profile/settings surfaces.
const ExposedEntry kCrashProfile[] = {
    {Channel::Internal, "internal:list"},
    {Channel::Lifecycle, "lifecycle:ping"},
};

// Web pages get no facade. The unused sentinel keeps MSVC from rejecting an
// empty C array; the constructor deliberately exposes zero entries for web.
const ExposedEntry kWebProfile[] = {{Channel::Internal, ""}};

} // namespace

AppHost::AppHost(int tab_id, const InternalPage *page, Seams seams,
                 RendererPolicy policy)
    : tab_id_(tab_id), policy_(policy), seams_(std::move(seams)),
      bridge_(PreloadBridge::Seams{seams_.invoke}) {
  const ExposedEntry *profile = kWebProfile;
  size_t count = 0;
  if (page) {
    page_name_ = page->name;
    if (page->category == "crash") {
      kind_ = PageKind::Crash;
      profile = kCrashProfile;
      count = sizeof(kCrashProfile) / sizeof(kCrashProfile[0]);
    } else if (page->category == "shell") {
      kind_ = PageKind::Shell;
      if (page->name == "newtab") {
        profile = kNewTabProfile;
        count = sizeof(kNewTabProfile) / sizeof(kNewTabProfile[0]);
      } else if (policy_ == RendererPolicy::Private) {
        profile = kPrivateShellProfile;
        count = sizeof(kPrivateShellProfile) / sizeof(kPrivateShellProfile[0]);
      } else {
        profile = kShellProfile;
        count = sizeof(kShellProfile) / sizeof(kShellProfile[0]);
      }
    } else {
      kind_ = PageKind::Diagnostics;
      if (policy_ == RendererPolicy::Private) {
        profile = kPrivateDiagnosticsProfile;
        count = sizeof(kPrivateDiagnosticsProfile) / sizeof(kPrivateDiagnosticsProfile[0]);
      } else {
        profile = kDiagnosticsProfile;
        count = sizeof(kDiagnosticsProfile) / sizeof(kDiagnosticsProfile[0]);
      }
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
