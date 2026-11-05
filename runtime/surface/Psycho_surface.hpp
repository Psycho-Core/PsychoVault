// PsychoVault original source. My surface path: my engine paints a shared
// surface, my broker composites it onto the window's framebuffer, and no page
// code ever sees the window handle. P1/S025 of docs/Psycho_MASTER-PLAN.md.
//
// Ownership rule, stated so nobody forgets it: the SurfaceRegistry lives on the
// broker and holds the ONLY writable copy of every surface's pixels. Renderers
// hand bytes over through present(); the compositor reads; the window applies.
#pragma once
#include "../../engine/Psycho_base.hpp"
#include "../../engine/Psycho_raster.hpp"
#include "../../runtime/ipc/Psycho_bus.hpp"
#include <map>
#include <string>
#include <vector>

namespace pv::rt {

struct Surface {
  int id = -1;
  int width = 0, height = 0;
  Bytes pixels;            // BGRA, exactly width * height * 4 bytes
  uint32_t generation = 0; // every accepted present bumps me
  std::string owner;       // who produced me - audit field, never trusted
};

// One shared surface record per presentation site. Every mutation returns a
// structured answer; a malformed frame is refused instead of smearing memory.
class SurfaceRegistry {
public:
  int create(int width, int height, const std::string &owner, Error &error);
  const Surface *find(int id) const;
  // The renderer's frame lands here. Wrong byte counts are refusals, because a
  // size lie is how a compositor reads out of bounds.
  bool present(int id, const Bytes &bgra, int width, int height, Error &error);
  bool resize(int id, int width, int height, Error &error);
  bool destroy(int id);

private:
  std::map<int, Surface> surfaces_;
  int next_id_ = 1;
};

// The compositor: my engine's surfaces become one framebuffer. List order is
// paint order - later entries draw over earlier ones. Writing outside the
// target's rectangle is structurally impossible: regions are intersected first.
class Compositor {
public:
  struct Placement {
    int surface_id;
    int x = 0, y = 0; // destination origin inside the target
  };
  bool composite(const SurfaceRegistry &registry, const std::vector<Placement> &order,
                 Surface &target, Error &error);
};

// The Windows half: present one composite target to one window (declared for
// every platform, defined only in the Windows build).
bool windows_present(intptr_t hwnd, const Surface &target, std::string &error);

} // namespace pv::rt
