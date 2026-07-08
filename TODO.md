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
- **Render-path SIMD** (spectrum peak-pick, waterfall decimation, `quantize_db`,
  bar `get_color`) — each sub-10 µs/frame and the frame is vsync-bound, so
  vectorizing buys no fps and only grows the binary. Revisit only if running
  vsync-off or on a 120/144 Hz panel.

## Do not redo

Already vectorized: `SignalProcessor::apply_window`/`remove_dc`, `FftAnalyzer`
post-FFT power/dB + `fast_log10_avx2`, waterfall RGBA LUT gather, waterfall
downsample inner sum. FFT is PocketFFT (numpy-speed). The pipeline is
render-bound, not DSP-bound — see CLAUDE.md's performance profile.
