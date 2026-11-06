// PsychoVault original source. The surface registry and compositor rules.
#include "Psycho_surface.hpp"
#include <algorithm>
#include <cstdint>
#include <limits>
#include <new>

namespace pv::rt {
namespace {

constexpr size_t kMaxSurfaceBytes = 128u * 1024u * 1024u;
constexpr size_t kMaxTotalBytes = 512u * 1024u * 1024u;
constexpr size_t kMaxSurfaceCount = 64;

bool surface_bytes(int width, int height, size_t &bytes) {
  if (width <= 0 || height <= 0 || width > 16384 || height > 16384)
    return false;
  const uint64_t pixels = uint64_t(width) * uint64_t(height);
  if (pixels > kMaxSurfaceBytes / 4) return false;
  bytes = size_t(pixels * 4);
  return true;
}

} // namespace

int SurfaceRegistry::create(int width, int height, const std::string &owner, Error &error) {
  size_t bytes = 0;
  if (!surface_bytes(width, height, bytes)) {
    error = {"surface.size", "refusing surface geometry or per-surface allocation " +
                                 std::to_string(width) + "x" + std::to_string(height)};
    return -1;
  }
  if (surfaces_.size() >= kMaxSurfaceCount || bytes > kMaxTotalBytes - allocated_bytes_) {
    error = {"surface.budget", "surface count or aggregate pixel-memory budget exhausted"};
    return -1;
  }
  if (next_id_ <= 0 || next_id_ == std::numeric_limits<int>::max()) {
    error = {"surface.ids", "surface id space exhausted"};
    return -1;
  }
  Surface surface;
  surface.id = next_id_;
  surface.width = width;
  surface.height = height;
  try {
    surface.pixels.assign(bytes, 0);
    surface.owner = owner;
    surfaces_.emplace(surface.id, std::move(surface));
  } catch (const std::bad_alloc &) {
    error = {"surface.memory", "surface allocation failed"};
    return -1;
  }
  const int id = next_id_++;
  allocated_bytes_ += bytes;
  error = Error{};
  return id;
}

const Surface *SurfaceRegistry::find(int id) const {
  auto it = surfaces_.find(id);
  return it == surfaces_.end() ? nullptr : &it->second;
}

bool SurfaceRegistry::present(int id, const Bytes &bgra, int width, int height, Error &error) {
  auto it = surfaces_.find(id);
  if (it == surfaces_.end()) {
    error = {"surface.missing", "no surface " + std::to_string(id)};
    return false;
  }
  if (width != it->second.width || height != it->second.height ||
      bgra.size() != size_t(width) * size_t(height) * 4) {
    error = {"surface.shape", "frame shape does not match the registered surface"};
    return false;
  }
  it->second.pixels = bgra;
  it->second.generation += 1;
  error = Error{};
  return true;
}

bool SurfaceRegistry::resize(int id, int width, int height, Error &error) {
  auto it = surfaces_.find(id);
  if (it == surfaces_.end()) {
    error = {"surface.missing", "no surface " + std::to_string(id)};
    return false;
  }
  size_t bytes = 0;
  if (!surface_bytes(width, height, bytes)) {
    error = {"surface.size", "refusing resize or per-surface allocation to " +
                                 std::to_string(width) + "x" + std::to_string(height)};
    return false;
  }
  const size_t previous = it->second.pixels.size();
  if (bytes > kMaxTotalBytes - (allocated_bytes_ - previous)) {
    error = {"surface.budget", "aggregate pixel-memory budget exhausted"};
    return false;
  }
  Bytes replacement;
  try {
    replacement.assign(bytes, 0); // allocate before mutating, and clear stale pixels
  } catch (const std::bad_alloc &) {
    error = {"surface.memory", "surface resize allocation failed"};
    return false;
  }
  it->second.width = width;
  it->second.height = height;
  it->second.pixels = std::move(replacement);
  allocated_bytes_ = allocated_bytes_ - previous + bytes;
  error = Error{};
  return true;
}

bool SurfaceRegistry::destroy(int id) {
  auto it = surfaces_.find(id);
  if (it == surfaces_.end()) return false;
  allocated_bytes_ -= it->second.pixels.size();
  surfaces_.erase(it);
  return true;
}

bool Compositor::composite(const SurfaceRegistry &registry, const std::vector<Placement> &order,
                           Surface &target, Error &error) {
  size_t target_bytes = 0;
  if (!surface_bytes(target.width, target.height, target_bytes) ||
      target.pixels.size() != target_bytes) {
    error = {"composite.target", "the destination framebuffer is malformed or oversized"};
    return false;
  }
  for (const Placement &placement : order) {
    const Surface *source = registry.find(placement.surface_id);
    if (!source) {
      error = {"composite.missing", "surface " + std::to_string(placement.surface_id) +
                                        " is not registered"};
      return false;
    }
    size_t source_bytes = 0;
    if (!surface_bytes(source->width, source->height, source_bytes) ||
        source->pixels.size() != source_bytes) {
      error = {"composite.source", "the registered source framebuffer is malformed"};
      return false;
    }
    // Do placement arithmetic in 64 bits: arbitrary signed positions must not
    // overflow before clipping against the target's rectangle.
    const int64_t left64 = std::max<int64_t>(0, placement.x);
    const int64_t top64 = std::max<int64_t>(0, placement.y);
    const int64_t right64 = std::min<int64_t>(target.width,
        int64_t(placement.x) + source->width);
    const int64_t bottom64 = std::min<int64_t>(target.height,
        int64_t(placement.y) + source->height);
    if (right64 <= left64 || bottom64 <= top64)
      continue; // fully off-screen: nothing to do, nothing to break
    const int left = int(left64), top = int(top64);
    const int right = int(right64), bottom = int(bottom64);
    for (int y = top; y < bottom; ++y) {
      int src_y = y - placement.y;
      for (int x = left; x < right; ++x) {
        int src_x = x - placement.x;
        size_t src = (size_t(src_y) * size_t(source->width) + size_t(src_x)) * 4;
        size_t dst = (size_t(y) * size_t(target.width) + size_t(x)) * 4;
        // Byte order is BGRA in both buffers: a straight copy per channel.
        for (int channel = 0; channel < 4; ++channel)
          target.pixels[dst + size_t(channel)] = source->pixels[src + size_t(channel)];
      }
    }
  }
  error = Error{};
  return true;
}

} // namespace pv::rt
