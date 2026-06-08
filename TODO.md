# OpenSpectrum TODO

## Performance / DSP

- [x] Raise selectable FFT-size range to `{1024, 2048, 4096, 8192, 16384}` to
  exploit PocketFFT (shipped in v2.5.0). All sizes verified under the 16 ms /
  60 FPS budget on Haswell+ (1024 ~10 ms, up to ~15 ms at 16384).

### Deferred (salvaged from IMPROVEMENTS.md — likely obsolete now that perf targets are met)

- [ ] **Zero-copy FFT** (accept `FrameHandle` directly, skip one ~32 KB memcpy).
  Deferred: the signal chain needs an in-place intermediate buffer (DC removal +
  windowing), so true zero-copy is awkward for marginal gain.
- [ ] **Batch sample processing** (accumulate 2–4 FFTs, average, render once).
  Deferred: adds (batch−1)×FFT latency, and the render loop already drops backlog
  to the newest frame at 60 FPS, so it buys little.
- [ ] **Fixed-point color math.** Effectively obsolete: superseded by the 256-entry
  packed RGBA LUT + AVX2 gather in the waterfall render path.

## CI / tooling

- [ ] PR build-check workflow (Linux compile on PRs to main) — catch build breaks
  pre-merge. (Lower priority while solo / committing direct to main.)
- [x] Makefile header-dependency tracking (`-MMD -MP`): editing a header currently
  doesn't rebuild dependent .cpp → risk of stale objects.

## Cosmetic

- [ ] Embedded application icon for the Windows `.exe` (windres resource step).
  Not needed for the release to function; the AppImage already has its icon.
