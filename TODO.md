# TODO

Active backlog only. Completed work lives in git log; knowledge/diagnostics
live in the README's Conceptual section, not here. Measured performance claims
live in `docs/TECHNICAL.md`.

## Open

- [ ] **click-to-tune + persisted config** (largest, most-wanted). Click the
  spectrum to retune; persist last freq/gain/FFT/window/palette across runs.
- [ ] **Clang/LLVM 21 as a second compiler** (ThinLTO, vectorization remarks).
  Port cost is `-flto=thin`/`lld`; keep GCC primary. Not a sanitizer play — GCC
  already has `-fsanitize=address,undefined`; add a `sanitize:` target instead.

## Declined / out of scope — don't re-tread

- **Audio demod (NBFM/WBFM/AM)** — out of scope for a spectrum analyzer.
- **Reserved left-gutter axes** — insets both plots and shifts every freq↔x
  mapping (spectrum geometry, waterfall width, freq scale, markers, cursor);
  50+ lines across 4 files. Kept the translucent overlay strip instead.
- **Render-path SIMD** (spectrum column reduce, waterfall decimation,
  `quantize_db`, bar `get_color`) — **retired, not deferred.** `build_vertices`
  measures 11.5 µs at FFT 4096 and 21.5 µs at 65536: **0.065% of a 33 ms frame.**
  No frame rate makes it pay — a 144 Hz budget is ~6900 µs, uncapped at FFT 1024
  is ~1900 µs. Don't re-measure it.
- **Glyph atlas for the HUD** (bake the font at init + one `SDL_RenderGeometry`,
  replacing `blit_text`'s per-string texture cache and mark-sweep) — **declined.**
  Net ~−50 LOC, not −400: `BITMAP_FONT` stays, it is what bakes the atlas. Its
  ~0.5 ms claim is 3× below the governor's own frame-to-frame jitter, so it
  cannot be shown to work. It pessimizes the software renderer — batching saves
  *draw-call overhead*, which a software rasteriser doesn't have, so it would
  trade an optimized blit for a triangle rasteriser on the worst-case backend.
  And it touches every overlay call site, including the `BLENDMODE_NONE` backdrop
  behind status/PEAK (glyph quads only draw lit pixels, so that needs explicit
  filled rects). Its one real value is concept reduction — revisit on
  comprehension grounds if text load grows, never on LOC or speed.

## Do not redo

**Already vectorized:** `SignalProcessor::apply_window`/`remove_dc`, `FftAnalyzer`
post-FFT power/dB + `fast_log10_avx2`, the sign-trick input fill, waterfall RGBA
LUT gather, waterfall downsample inner sum. FFT is PocketFFT with the plan cached
(`src/fft/pocketfft_wrapper.h`).

**Tried, worked:** plan cache (`POCKETFFT_CACHE_SIZE 1`) — 0.55 → 0.47 ms at the
4096 default, nothing resolvable at 32768/65536. Panel-text string cache +
`STATIC` textures (v3.8.1) — −44% instructions, −54% cycles vs v3.8.0.

**Tried, didn't work / not worth it:** render-path SIMD and the glyph atlas
(above). OpenMP across FFT butterflies — PocketFFT returns 1 thread for a single
1D transform by construction, so it means forking vendored code; and the win is
unusable anyway (fps is capped, latency is frame-paced, and waking a second core
30×/s costs more power than it saves on a 15 W part).

**Nothing left in the DSP.** At 65536 the transform is 71% of real work;
`get_max_db` is the only unvectorized loop and is worth 0.05 ms of a 33 ms frame.
Every size 1024–65536 holds 30 fps on a 15 W i5-7300U. Read the `cpu` caveat in
`docs/TECHNICAL.md` before acting on any performance number — `cpu` is wall-clock
divided by whatever clock the governor picked, not a measure of work.
