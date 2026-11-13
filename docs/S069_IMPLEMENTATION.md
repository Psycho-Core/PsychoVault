# S069 — UI fonts and text measurements

## Implemented

- I bundle the unmodified Inter variable upright font, copyright 2020 The Inter Project Authors, licensed under the SIL Open Font License 1.1. Source, hashes and the full license are in `assets/fonts/`. The Windows MSVC build embeds the font and license as executable resources, including in the renderer child and headless/test executables. The preview ZIP also carries `licenses/Inter-OFL.txt`; package verification checks it matches the source license.
- On Windows, DirectWrite 5 loads the embedded font into an in-memory private collection without installing it or writing a font file. The app page requests `PsychoVault UI`, an alias for the bundled Inter family, followed by system fallbacks. If the modern DirectWrite API or resource is unavailable, the alias is skipped and the remaining CSS family stack is used. `Inter` also selects the bundled collection when available.
- I retain the full declared CSS family list instead of discarding all but its first member. DirectWrite selects the first installed or bundled family, including generic sans-serif, serif and monospace mappings; DirectWrite's system glyph fallback handles characters missing from that selected font. The family list and numeric weight now travel from layout measurement through the frame, shell, and rasterization, including editor caret and selection measurement. Legacy bool-only paint commands retain their 400/700 behavior.
- DirectWrite receives numeric weights 1–1000, with regular/bold aliases. Format cache keys include collection, numeric weight, and the same 1/64 px quantized size used to create the format. Glyphs use Direct2D grayscale antialiasing; DirectWrite layout and the render target use CSS-pixel/96-DPI units.

## Evidence

- Linux: `python3 tools/Psycho_build.py --compiler gcc --target tests --outdir /tmp/pv-s069 && /tmp/pv-s069/pv-tests` passed **1,059 engine checks / 2,906 total checks**. These tests verify family/weight frame serialization and stable CSS layout width at DPR 1, 1.5 and 2; Linux still uses approximate text advances and no text raster backend.
- Windows: [push-triggered Actions run 43493151145](https://github.com/Psycho-Core/PsychoVault/actions/runs/43493151145) passed on commit `15e8b1028edbc463051e66071a11aae3d0137eb7`. MSVC built all executables; engine tests checked the private font collection, CSS-family fallback, stable repeated measurement, grayscale glyphs, and distinct CJK fallback glyphs. Headless/native smoke captures, isolated renderer and ZIP/license verification also passed. The temporary branch-only trigger was subsequently removed; this is not evidence of manual-dispatch permission.

## Limits

- CSS family selection is first-available plus DirectWrite system glyph fallback, not full browser CSS per-character fallback across every declared family. `@font-face`, download fonts, font stretch, italic faces and complete CSS relative-weight behavior remain open. Canvas 2D font-string family/weight parsing remains limited.
- The bundled font covers only the supplied Inter upright variable face; DirectWrite/system fonts supply other scripts and styles. Systems without DirectWrite 5 use the existing system-family fallback. The private collection is initialized per rendering thread and duplicates the embedded font bytes in memory.
- DPR regressions compare CSS-layout measurements for a fixed viewport, not real multi-monitor DPI moves or 125%/150% screenshot parity. Raster is drawn at 96 DPI before the shell's scaling path; monitor-specific optical quality and actual DPI transitions still need interactive and golden-image validation. Font weights, hinting, fallback metrics and display smoothing can vary by OS/version. Non-Windows builds do not render text.
