// My length-prefixed JSON framing. This is deliberately free of any process,
// window or UI dependency so my renderer can link it inside an AppContainer
// without importing user32 or gdi32.
#pragma once
#include "../engine/Psycho_base.hpp"
#include <optional>

namespace pv {
namespace ipc {
void put32(uint8_t *p, uint32_t n);
uint32_t get32(const uint8_t *p);
bool write_all(void *handle, const uint8_t *data, size_t n);
bool read_all(void *handle, uint8_t *data, size_t n);
} // namespace ipc

bool write_frame(void *handle, const Json &message, std::string &error);
std::optional<Json> read_frame(void *handle, int timeout_ms);
} // namespace pv
