// PsychoVault original source. The surface registry and compositor rules.
#include "Psycho_surface.hpp"

namespace pv::rt {
namespace {

bool sane_size(int width, int height) { return width > 0 && height > 0 && width <= 16384 && height <= 16384; }

} // namespace

int SurfaceRegistry::create(int width, int height, const std::string &owner, Error &error) {
  if (!sane_size(width, height)) {
    error = {"surface.size", "refusing surface geometry " + std::to_string(width) + "x" +
                                 std::to_string(height)};
    return -1;
  }
  Surface surface;
  surface.id = next_id_++;
  surface.width = width;
  surface.height = height;
  surface.pixels.assign(size_t(width) * size_t(height) * 4, 0);
  surface.owner = owner;
  int id = surface.id;
  surfaces_[id] = std::move(surface);
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
  if (!sane_size(width, height)) {
    error = {"surface.size", "refusing resize to " + std::to_string(width) + "x" +
                                 std::to_string(height)};
    return false;
  }
  it->second.width = width;
  it->second.height = height;
  it->second.pixels.assign(size_t(width) * size_t(height) * 4, 0); // cleared, never stale
  error = Error{};
  return true;
}

bool SurfaceRegistry::destroy(int id) { return surfaces_.erase(id) != 0; }

bool Compositor::composite(const SurfaceRegistry &registry, const std::vector<Placement> &order,
                           Surface &target, Error &error) {
  if (target.pixels.size() != size_t(target.width) * size_t(target.height) * 4) {
    error = {"composite.target", "the destination framebuffer is malformed"};
    return false;
  }
  for (const Placement &placement : order) {
    const Surface *source = registry.find(placement.surface_id);
    if (!source) {
      error = {"composite.missing", "surface " + std::to_string(placement.surface_id) +
                                        " is not registered"};
      return false;
    }
    // Intersection first: nothing outside the target's rectangle is written,
    // and nothing off-screen in the source is read.
    int left = placement.x < 0 ? 0 : placement.x;
    int top = placement.y < 0 ? 0 : placement.y;
    int right = placement.x + source->width;
    int bottom = placement.y + source->height;
    if (right > target.width)
      right = target.width;
    if (bottom > target.height)
      bottom = target.height;
    if (right <= left || bottom <= top)
      continue; // fully off-screen: nothing to do, nothing to break
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
