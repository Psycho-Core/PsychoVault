# S067 — compositor and presentation slices

**Status (2026-11-01):** Tile damage metadata, shell tile invalidation, compositor-layer counters, DWM-flushed presentation, and approximate missed-refresh instrumentation are implemented. Linux engine tests pass. Windows behavior has not been built or interactively validated in this environment.

## Implemented

- Frame damage is quantized into **128 × 128 CSS-pixel tiles**, tagged as fixed-position or document-scrolled. The shell maps tile coordinates through page scroll and zoom, clips them to the content viewport, and invalidates only those screen rectangles. Full damage or an oversized tile set uses a viewport fallback; empty damage yields no tile invalidation.
- Frame diagnostics now include tile size/count, full-damage state, changed paint-command count, fixed/scrolled paint-command counts, and effect-layer count. Fixed-position commands and existing `PushLayer`/`PopLayer` effect groups remain explicit in paint data, so the shell/raster can distinguish the viewport-fixed stream from scrolled content.
- The Win32 paint path flushes GDI work and calls `DwmFlush()` before completing the paint cycle. It records painted frames and estimates missed refresh slots when a paint/raster/presentation cycle exceeds the current display refresh budget; periodic counters are emitted through `OutputDebugStringA` for debugger/ETW-style inspection.
- `S067CompositorTilesLayersAndFrameMetrics` verifies 128px tile metadata, fixed/effect layer classification, initial full damage, and empty tile damage on an unchanged frame.

## Validation

`python3 tools/Psycho_build.py --compiler gcc --target tests --outdir /tmp/pv-s067-tests && /tmp/pv-s067-tests/pv-tests` passed **1,040 engine checks / 2,887 total checks**.

## Remaining work / limits

- This is not a persistent GPU compositor. Layer classification builds on existing fixed flags and effect `PushLayer`/`PopLayer` groups; cached independent raster surfaces for scrolling, transformed, and fixed layers are not implemented. The current raster still traverses the paint stream under clips, and effect groups still allocate software scratch surfaces per paint.
- Tile invalidation reduces dirty rectangles but the retained window surface is a single software canvas, not independently cached tile bitmaps. Tiles use CSS-pixel sizing before shell zoom scaling.
- `DwmFlush()` pacing and the missed-refresh estimate require Windows verification. The estimate attributes long paint cycles to missed refresh slots; it is not a hardware-present fence or a complete dropped-frame telemetry pipeline. Variable refresh, occlusion, monitor transitions, and multi-window scheduling need validation.
- S067's full acceptance remains open until retained layer caching, real Windows compositor validation, and scroll/transform/fixed-content performance profiling are complete.
