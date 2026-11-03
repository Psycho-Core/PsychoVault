#pragma once
// I ship as one executable. My renderer is embedded inside me as a Windows
// resource, and I unpack it into my own per-user folder and verify it before I
// run it. I still keep the renderer in a separate sandboxed process, because
// that boundary is what protects me from a hostile page.
#include "../engine/Psycho_base.hpp"

namespace pv {
// I return a path to a renderer I trust, or an empty string and a real reason.
std::string renderer_executable(std::string &error);
// I expose this so my About page can say whether I am running my embedded copy.
bool renderer_is_embedded();
} // namespace pv
