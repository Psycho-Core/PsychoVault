#pragma once
#include "../engine/base.hpp"
namespace pv {
Resource fetch_url(const Url &url, int timeout_ms = 20000, size_t max_bytes = kMaxDocument);
} // namespace pv
