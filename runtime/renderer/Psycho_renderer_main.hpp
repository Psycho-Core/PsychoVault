// PsychoVault original source. Internal entry point for the broker's sandboxed
// renderer process; the shell executable dispatches here for its private mode.
#pragma once

namespace pv::rt {
int run_renderer_child();
}
