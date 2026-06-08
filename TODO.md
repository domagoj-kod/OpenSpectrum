# OpenSpectrum TODO

## Performance / DSP

- [ ] **Raise the selectable FFT-size range to exploit PocketFFT.** The engine was
  migrated KissFFT → PocketFFT (much stronger at ≥4K and cache-friendly), but the
  UI still offers the legacy KissFFT-era set `{512, 1024, 2048, 4096}`
  (`src/control_state.cpp:23`). Proposed new set: `{2048, 4096, 8192, 16384}`
  (note: 16384 = 2^14, not 16884). The engine only requires power-of-two
  (`main.cpp:234`), so no engine change is needed — just the constraints list,
  plus validation that memory/perf hold at 16384.
  - Measure cpu_ms via the `T` timing HUD at 8192 and 16384 before committing.
  - Consider keeping 1024 for fast time-resolution use (transient/hopping signals).

## CI / tooling

- [ ] PR build-check workflow (Linux compile on PRs to main) — catch build breaks
  pre-merge. (Lower priority while solo / committing direct to main.)
- [ ] Makefile header-dependency tracking (`-MMD -MP`): editing a header currently
  doesn't rebuild dependent .cpp → risk of stale objects.

## Cosmetic

- [ ] Embedded application icon for the Windows `.exe` (windres resource step).
  Not needed for the release to function; the AppImage already has its icon.
