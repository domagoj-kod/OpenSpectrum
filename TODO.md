# TODO

Active backlog only. Completed work lives in git log; knowledge/diagnostics
live in `docs/TECHNICAL.md`, not here. Measured performance claims
live in `docs/MEASUREMENTS.md`.

## Open

- [ ] **Maybe: label the raw peak by the trigger line.** The trigger compares
  the raw `get_max_db()` (PEAK), a few dB above the drawn trace (2026-10-15:
  TRIG -68 dB, PEAK -63.1 dB, drawn peak about -72 dB); the README now says
  to set it with max-hold on.
- [ ] **Rare small losses on the B210 below the host ceiling.** Six of 28
  captures of 20 s lost one burst each (one UHD `O`, 8 to 132 thousand
  samples), at 4, 8 and 16 MS/s and at both FFT sizes, so neither explains it
  (Windows sweep, 2026-10-14, in `docs/MEASUREMENTS.md`). Not the capture
  writer: without `--iq-log` five runs at 16 MS/s / 65536 still showed 0 to 2
  `O` each, and the write has run on the logger's own thread since `6fce657`.
  Next: log when each gap happens, and the same sweep on another host.
- [ ] **Run the RTL-SDR on the source interface.** `SampleSource` and 64-bit
  tuning were merged without a live RTL run (the dongle was away): a 30 s
  `--iq-log` capture, then `dropped_samples`, size and measured rate in the
  sidecar. Also Ctrl+click tuning, the frozen hold and the saved settings,
  so far run on the B210 only: the RTL's freq and gain (`rtl.`, and `rtl-hf.`
  with `--direct-sampling`) are restored and kept apart from `uhd.`.
- [ ] **Rest of the USRP hardware tests.** Done: native Windows sweep up to
  the host ceiling (20 to 24 MS/s), `benchmark_rate`, the Windows UHD build.
  Left: a CW tone by cable (at least 30 dB of attenuation) whose phase stays
  linear over the capture; a one-hour soak; USB pulled mid-capture
  (`--iq-duration` is checked only while frames arrive); native Linux;
  `--antenna`, other USRP families, the second channel.
- [ ] **Clang/LLVM 21 as a second compiler** (ThinLTO, vectorization remarks).
  Port cost is `-flto=thin`/`lld`; keep GCC primary. Not a sanitizer play — GCC
  already has `-fsanitize=address,undefined`; add a `sanitize:` target instead.
## Declined / out of scope — don't re-tread

- **Re-render at native resolution on maximize** — **declined; fixed 720p
  upscale is the chosen design.** The renderer is pinned to a fixed 1280×720
  logical canvas (`SDL_SetRenderLogicalPresentation`, no CLI knob) and the GPU
  point-samples it to any window/display. Native re-render would, at 4K, take
  the pane ~1.4 kHz/px → ~530 Hz/px — the one condition under which 32768/65536
  show what they compute — but it costs 2–3× the raster/GPU/RAM on the weak
  target for resolution the mean-trace column decimation discards anyway, and it
  reintroduces four problems the fixed canvas gives us for free: waterfall
  history depth and panel layout become window-size-dependent again (the
  inconsistency this replaced), `m_hist_counts` must track a live-resized
  history, `PixelBuffer` + both displays + scroll textures + the freq-scale
  cache all realloc on resize, and the three bugs logical presentation solves
  come back. If ever revisited, **do not just delete that call** — read the
  comment at `sdl_renderer.cpp:143` first.

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
30×/s costs more power than it saves on a 15 W part). **fast-math on PocketFFT**
(`-funsafe-math-optimizations -fno-math-errno` on `fft_analyzer.o`) — a dev-box
microbench says −28%; the target says **+2.4% instructions and no cycle change**.
Measured, rejected, don't re-try on the microbench number: `docs/MEASUREMENTS.md` →
*fast-math on PocketFFT: rejected*.

**Nothing left in the DSP.** At 65536 the transform is 71% of real work;
`get_max_db` is the only unvectorized loop and is worth 0.05 ms of a 33 ms frame.
Every size 1024–65536 holds 30 fps on a 15 W i5-7300U. Read the `cpu` caveat in
`docs/MEASUREMENTS.md` before acting on any performance number — `cpu` is wall-clock
divided by whatever clock the governor picked, not a measure of work.
