# TODO

Active backlog. Knowledge/diagnostic items live in the README's Conceptual
section, not here.

## Features

- [x] **#4 — PNG export as a true screenshot.** Replace the composite path
  (`export_combined` reconstructs from raw spectrum/waterfall `PixelBuffer`s, no
  overlays) with an `SDL_RenderReadPixels` grab of the rendered frame, so exports
  capture the left axes, bottom freq bar, markers, PEAK, and status WYSIWYG. Keep
  the `.json` sidecar metadata. (Decision recorded: screenshot, not CPU
  composite.)

- [ ] **#5 — Amplitude trigger (low priority).** Draggable horizontal threshold
  line on the AF spectrum; when a bin peak crosses it, freeze the display (and/or
  auto-save). Rationale: ~16 ms frame cadence vs ~150 ms human reaction makes
  manual capture of transient (radar/drone) signals impossible. RF scanning is
  consistent so lower urgency; transient capture is the real use.

  _Design — DEFERRED pending usage analysis with the primary (technical) user._
  Implementation plumbing is already mapped: `peak_db` (main.cpp, =
  `FftAnalyzer::get_max_db()`) is the bin peak to compare; freeze = skip the
  dequeue/update at the `got_samples` gate while still presenting; auto-save
  reuses the v3.4.0 framebuffer capture (`SdlRenderer::request_capture` ->
  `export_framebuffer`); the dB<->y mapping already exists in
  `draw_left_axes` / `SpectrumDisplay::sample_at_x`.

  Interaction constraints (agreed):
  - Plain mouse is taken: motion = hover readout, click = drop marker. A plain
    drag for the trigger collides with both.
  - **Use Shift as the modifier** — `Shift+drag` moves the threshold line,
    `Shift+click` arms at that level. Plain mouse keeps current behavior; no new
    keybinding (avoid shortcut bloat as features grow).

  Open questions for the usage analysis:
  1. Trigger action: freeze only / auto-save only / freeze + auto-save?
  2. Unfreeze + re-arm gesture?
  3. Auto-save cadence if not freezing (one PNG per crossing? debounce window?).

## Performance — SIMD pass (done)

FFT is already at numpy speed (PocketFFT) — left as is.

- [x] **IQ uint8->float conversion** (`rtl_sdr_device.cpp`): the program's
  highest-volume loop (~4M/s, producer thread). Extracted a restrict-qualified
  free helper so GCC emits clean full-width AVX2 with no alias versioning;
  object text -254 B. Done `cfe6015`.

Declined — not worth it (measured): the render-path candidates (spectrum
peak-pick `std::max`, waterfall decimation, `quantize_db`, bar-path `get_color`)
are all sub-10us/frame and the frame is **vsync-bound** (present ~10ms idle wait,
render_build ~5.6ms, ~10ms slack). Vectorizing them buys no observable fps/
latency and only grows the binary — against the small-size motto. Revisit only
if running vsync-off or on a 120/144 Hz panel (per-frame budget shrinks).

Already vectorized (do not redo): `SignalProcessor::apply_window` / `remove_dc`,
`FftAnalyzer` post-FFT power/dB + `fast_log10_avx2`, waterfall RGBA LUT gather,
waterfall downsample inner sum.

## Deferred (not scheduled)

- [ ] Clang/LLVM 21 as a second compiler (sanitizers, ThinLTO, vectorization
  remarks); main port cost is the PGO pipeline (`.gcda` → `.profraw` +
  `llvm-profdata`) and `-flto=thin`/`lld`. Keep GCC primary.
- [x] ~~Reserved left-gutter axes (zero occlusion)~~ — declined. The clean
  version insets both plots and shifts every freq<->x mapping (spectrum geometry,
  waterfall width, freq scale, markers, cursor, click-to-place) — 50+ lines across
  4 files, not worth it. Kept the translucent overlay strip; instead split the
  strip to skip the freq-bar band so it no longer collides at the bottom-left.
- [ ] Embedded Windows `.exe` icon (windres resource step).
- [ ] click-to-tune, persisted config, NBFM/WBFM/AM audio demod (largest, last).
