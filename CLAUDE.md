# CLAUDE.md

For understanding the code and for the working rules of this repository. In
the spirit of late demoscene software: small, dense, nothing decorative. A
line earns its place; what the code or the compiler already says is not
repeated here.

C++20. Priorities, in order: low latency, low power (long battery runs), small
footprint. Users are radio engineers, mostly on native Windows; `.iq` captures
and PNG exports feed a Python pipeline. Keep it simple enough for a junior
maintainer: clarity and deletion over cleverness.

## Build and check

```bash
make                 # debug: -O0, asserts on
make release         # -O3, LTO, -march=haswell, stripped
make profile         # -O2 -pg
make check           # build and run tests/
make dist            # bundle for this platform -> dist/
make release UHD=1   # any target; adds the USRP source
./lint.sh [--check]  # clang-format + clang-tidy over src/ and tests/ (UHD=1 adds uhd_device.cpp)
./bench.sh <bin> <tag>
```

- Objects go to `build/$(CONFIG)[-uhd]/` and `all` copies the binary to
  `./openspectrum`: make tracks timestamps, not flags. A new config passes its
  own `CONFIG=`.
- Includes are paths from `src/` (`"utils/logger.h"`); only `-Isrc` is set.
- `DEPFLAGS` stays out of `CXXFLAGS` (release/profile replace `CXXFLAGS`); a new
  compile rule appends it.
- The tree is lint-clean; keep it so.
- `tests/*_test.cpp` are programs of asserts; a new test is a new file.
  Invariants that must hold at runtime stay as debug asserts in the code.

## Architecture

```
producer thread: SampleSource (RtlSdrDevice | UhdDevice | IqPlaybackSource)
  -> async_sample_callback: IQ capture tap (full rate), SampleQueue::push
render loop (main.cpp):
  SampleQueue::pop_newest  -> remove_dc, apply_window -> FftAnalyzer
  -> SpectrumDisplay, WaterfallDisplay -> SdlRenderer
```

- A new receiver is one `SampleSource` subclass (`docs/TECHNICAL.md`, "Adding
  a new SDR device"; capture format under "Capture files"). Frequencies are
  `uint64_t`.
- The render loop takes only the newest frame and drops the backlog: no
  latency builds up.
- `IqLogger` writes on its own thread; the producer only copies into its ring.
- `ControlState` holds what the keys change; `apply_to_device()` is the one
  place that writes the receiver.
- Markers, cursor readout and amplitude trigger live in
  `src/gui/plot_overlays.h`.
- After `stop()`, `rtlsdr_read_async` returning negative is the expected
  shutdown, not an error.

## Easy to break

- **Spectrum centring**: two-sided, DC centred by the pre-FFT `(-1)^n` sign
  trick. Do not also swap output halves.
- **Autoscale anchors on the median, never the min**: the min of a noise
  periodogram is an extreme value (~42 dB under the floor at FFT 16384).
- **The trace is a mean per pixel column, not a max**: max-hold is the peak
  detector. `sample_at_x` must mirror `build_vertices`.
- **The trigger compares the raw `get_max_db()` (panel PEAK)**, not the drawn
  trace, which sits a few dB lower.
- **Waterfall**: `m_hist_counts` stays in lockstep with `m_history` (a debug
  assert recounts it); only `mark_rendered()` clears `m_needs_full_render`.
- **Window functions** write both `m_window_coeffs` and
  `m_window_coeffs_doubled` (the AVX2 path uses the second).
- **PocketFFT**: the patched `aligned_alloc` for `_WIN32` is required on MSYS2.
- **SDL3**: pack colours with `SDL_MapSurfaceRGBA` (`RGBA32` is ABGR on
  little-endian). Textures default to blend + linear; `render_text` sets none +
  nearest.
- **FFT 4096 default is deliberate**: sized for the oldest machine expected,
  not the fastest.
- `OS_HOT`/`OS_COLD` (`src/utils/attributes.h`) mark per-frame and one-shot
  functions.

## Performance

There is no performance problem: 30 fps at every FFT size on a 15 W i5-7300U.
The DSP is AVX2 throughout; do not touch the SIMD, and fast-math was measured
and rejected. Read `docs/MEASUREMENTS.md` before optimising anything.

Measuring: same machine, same session, A and B alternated. `cpu` is wall clock
at whatever clock the governor chose, so compare it only at the same FFT size.
`render_build`/`present` hold the vsync wait on different sides per backend:
not comparable across backends. Keep the default renderer (OpenGL / D3D11);
Vulkan on old Intel iGPUs is several times slower.

## Changes and commits

- A request with two readings: ask, or name the one you took. Say so when a
  simpler way exists.
- Touch only what the task needs; remove what your change leaves unused. An
  unrelated problem is reported, not fixed in the same commit.
- Done means checked: `make release`, `make check`, `./lint.sh` (it lints the
  staged lines). A bug fix starts from a check that fails: a test in `tests/`
  or a stated reproduction. UI and device changes need a run on Windows (and
  the device); say what was and was not run.
- Nothing is committed before the maintainer has reviewed the diff.
- One verified change per commit; commits are not squashed.
- Conventional Commits: `type(scope): summary`, types `feat fix perf refactor
  docs test build ci chore style`. Subject at most 72 characters, imperative,
  lowercase, no full stop. Body wrapped at 76: what changed, why, how it was
  checked.
- Nothing is pushed, tagged or force-pushed without the maintainer's word, and
  nothing reaches the remote before it has run on the target.

## Windows and releases

- The unsigned `.exe` trips Defender's ML heuristic (a false positive); the fix
  is Authenticode signing. Do not add AV-suspicious Win32 APIs (`AttachConsole`
  was reverted for this). The `.exe` is a console program: the Makefile drops
  SDL's `-mwindows`.
- Release: `git tag -s vX -F notes.txt && git push origin vX`; CI runs the same
  `packaging/` scripts as `make dist` (`RELEASING.md`). `UHD=1` bundles also
  carry `uhd-sweep.ps1` and `play-bench.ps1`.
- Icon: `packaging/openspectrum.svg`; the `.png` and `.ico` are committed, so
  regenerate them after editing it.
