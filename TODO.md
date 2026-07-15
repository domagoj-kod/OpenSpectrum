# TODO

Active backlog only. Completed work lives in git log; knowledge/diagnostics
live in the README's Conceptual section, not here.

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
  `quantize_db`, bar `get_color`) — each sub-10 µs/frame and the frame is
  vsync-bound, so vectorizing buys no fps and only grows the binary. Revisit
  only if running vsync-off or on a 120/144 Hz panel.
- **Glyph atlas for the HUD** (bake the font once at init + one
  `SDL_RenderGeometry` call, replacing `blit_text`'s per-string texture cache
  and its mark-sweep). Analysed in full, deliberately not done:
  - **LOC: net ~−50, not −400.** The text subsystem is only ~290 lines and the
    98-line `BITMAP_FONT` table *stays* — it is what bakes the atlas.
  - **Perf: it enables nothing.** Maybe `render_build` ~2.4→~1.9 ms on D3D11.
    The frame spends ~5.5 ms of a 33 ms budget even at FFT 65536; there is no
    deadline to relieve.
  - **Bigger FFT sizes do not change this** (asked and answered 2026-07-14).
    The atlas saves a *fixed* text cost regardless of FFT size; 32768/65536
    grow `cpu` (DSP), a different stage — so as a fraction of a 65536 frame the
    atlas's value *falls*. And 65536 is opt-in; the 4096 default spends ~0.15 ms
    on DSP.
  - **Real value is concept reduction**, not lines or speed: it deletes a whole
    mental model (per-frame texture lifecycle + mark-sweep + cache-key-by-
    colour+string + invalidation) in favour of "bake once, emit quads". Decide
    it on comprehension grounds or when text load grows — never on LOC/perf.
  - **It pessimizes the software renderer** (asked and answered 2026-07-15).
    The atlas's win is collapsing N blits into one batched `SDL_RenderGeometry`
    — it saves *draw-call overhead*, a GPU-backend concept. The software
    renderer has no draw calls to batch: `SDL_RenderCopy` takes an optimized
    blit path (row copies for axis-aligned, unscaled, same-format rects),
    `SDL_RenderGeometry` goes through the general triangle rasteriser with
    per-pixel interpolation. So it trades a row-copy for a rasteriser to save
    overhead that isn't there — and does it on the battery/thermal worst case.
  - **Its win is below the measurement noise floor.** The ~0.5 ms it claims is
    3× smaller than the governor's own frame-to-frame jitter (±1.6 ms; see the
    `cpu` caveat in `docs/TECHNICAL.md`). It cannot be shown to work, which for
    a codebase heading to junior maintainers is the worst kind of change: it
    can't be defended, so it can't be safely modified later either.
  - **Risk if attempted:** the `BLENDMODE_NONE` opaque backdrop behind
    status/PEAK needs explicit filled rects (glyph quads only draw lit pixels),
    and it touches every overlay call site — re-verify the whole HUD.

## Do not redo

Already vectorized: `SignalProcessor::apply_window`/`remove_dc`, `FftAnalyzer`
post-FFT power/dB + `fast_log10_avx2`, waterfall RGBA LUT gather, waterfall
downsample inner sum. FFT is PocketFFT (numpy-speed). The pipeline is
render-bound, not DSP-bound **through FFT 16384** — the opt-in 32768/65536
sizes invert that (65536 is the one DSP-bound config), but they are not the
default and the frame still finishes in ~5.5 ms of a 33 ms budget. See
CLAUDE.md's performance profile before acting on either claim.
