# S066 — incremental layout and damage-based repaint

**Status (2026-11-01):** Engine behavior and the Win32 partial-paint path are implemented as a first pass. The engine tests pass on Linux. The Win32 code is not compiled or visually exercised in this Linux environment. This work does not yet provide subtree-level incremental layout.

## Implemented

- `Page` retains its most recent `LayoutResult` and reuses it when document structure/text/attributes, computed style, local scroll offsets, loaded-resource generation, viewport/layout width and document scroll are unchanged. A diagnostic `layout_rebuild_count()` enables a regression to prove reuse. When any of those layout inputs change, the existing full document layout runs and refreshes the cache; this is cache reuse, not dirty-subtree reflow.
- Each rendered frame compares the ordered paint-command snapshots to the previous frame. Changed commands contribute their old and new clipped bounds; nearby regions are coalesced. Initial frames, viewport/scroll changes, resource/canvas updates, paint-list length changes, and excessive region counts fall back to full viewport damage.
- The Win32 shell consumes frame damage for current-tab frames, maps document/fixed coordinates through scroll and zoom, clips to the page content viewport, and invalidates those rectangles. Navigation/loading/title changes and cases without valid damage metadata retain full-window invalidation. Empty damage produces no invalidation.
- The Win32 paint path keeps a persistent software canvas, rasterizes only within `PAINTSTRUCT.rcPaint`, and transfers only the dirty rectangle to the window DC. This avoids rebuilding a full canvas and presenting the full DIB for ordinary partial paints.

## Regression and validation

`S066LayoutReuseAndDamageRegions` checks that a second unchanged render and pointer movement over a non-hover-sensitive element do not rebuild layout or emit damage, and that a hover color change rebuilds changed computed layout inputs and reports bounded local damage.

Linux validation: `python3 tools/Psycho_build.py --compiler gcc --target tests --outdir /tmp/pv-s066-verified && /tmp/pv-s066-verified/pv-tests` passed **996 engine checks / 2,843 total checks**. `git diff --check` also passed at the time of validation.

## Remaining work / limits

- Layout still recomputes as a whole on any layout-signature change; per-node dependency tracking, dirty subtree propagation, and partial layout are not implemented.
- Comparing serialized command snapshots is linear in the paint list and depends on stable ordering. Reordering/insertions that change list length intentionally trigger a full-viewport fallback; same-length reorderings can conservatively report broad local regions rather than semantic invalidation.
- The engine reports repaint damage but does not maintain a per-region retained command index. The shell still walks/draws the complete command stream under a raster clip, even though raster writes and DIB transfer are bounded to the invalid region.
- Only renderer-frame invalidation was narrowed. Many independent shell interaction paths still invalidate the full window. Care is needed for moving/removed content, text caret/blink, canvas/video updates, fixed layers, overlays, opacity/shadow/filter extents, and partial invalidations after surface recreation.
- The Win32 shell modifications need Windows compilation and interactive validation, especially GDI top-down DIB partial transfer, resize/expose behavior, DPI/zoom mapping, and scroll/fixed-position damage clipping. Linux tests do not establish those platform behaviors.
