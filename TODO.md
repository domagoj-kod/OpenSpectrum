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

- [x] **#5 — Amplitude trigger.** Horizontal threshold line on the AF spectrum;
  a bin peak crossing it freezes the display until resumed. Rationale: ~16 ms
  frame cadence vs ~150 ms human reaction makes manual capture of transient
  (radar/drone) signals impossible. _DONE — HW-validated; docs (README + CLAUDE.md)
  written. Post-test fixes: TRIG tag moved off the PEAK corner, frozen waterfall
  time-axis held static (`8b04f39`)._

  As built (decisions from the primary user — freeze-only, Space to resume):
  - **Shift+left-drag / Shift+click** in the spectrum pane sets the dB threshold
    at the pointer and arms (no new keybinding; plain mouse keeps hover/marker).
    Captured in `poll_events` (motion button-mask, no button-up tracking) as a
    `take_trigger_set()` request main() maps y->dB.
  - **Drag onto the pane bottom edge disarms** (hides the line).
  - Fire = `trig_armed && peak_db >= threshold` (main.cpp 4.2). Freeze halts the
    dequeue at a gate before §2 while still presenting, so the triggering frame
    is the one held; status bar shows `TRIGGERED @ x dB`, plus a centered FROZEN
    banner (`SdlRenderer::draw_trigger`).
  - **Space** = `ControlState::request_unfreeze()` -> resume + re-arm (threshold
    and armed state persist). Threshold line y recomputed each frame from the
    autoscaled dB range so it tracks the view.

  Validate on HW, then: README Mouse-Controls + key table, CLAUDE.md
  ControlState-flow note, refreshed screenshot. Possible follow-ups if wanted:
  optional auto-save on trigger (reuse v3.4.0 capture), edge-vs-level option.

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

- [ ] Clang/LLVM 21 as a second compiler (ThinLTO, vectorization remarks);
  port cost is `-flto=thin`/`lld`. Keep GCC primary. Note: sanitizers are not a
  reason — GCC already has `-fsanitize=address,undefined`; add a `sanitize:`
  target instead of porting compilers.
- [x] ~~Reserved left-gutter axes (zero occlusion)~~ — declined. The clean
  version insets both plots and shifts every freq<->x mapping (spectrum geometry,
  waterfall width, freq scale, markers, cursor, click-to-place) — 50+ lines across
  4 files, not worth it. Kept the translucent overlay strip; instead split the
  strip to skip the freq-bar band so it no longer collides at the bottom-left.
- [x] Embedded Windows `.exe` icon (windres resource step). _Done: redesigned
  abstract icon (terminal tile + carrier/sidebands), committed `.svg`/`.png`/
  multi-size `.ico`; Makefile `windres` step gated on `OS=Windows_NT` links the
  icon resource into the `.exe`. Wiring verified via `make -n OS=Windows_NT`;
  execution validates in CI (MSYS2)._
- [ ] click-to-tune, persisted config (largest, last). Audio demod (NBFM/WBFM/AM)
  dropped — out of scope for a spectrum analyzer.
