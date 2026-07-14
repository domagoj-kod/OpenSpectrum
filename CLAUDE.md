# CLAUDE.md

Guidance for Claude Code (claude.ai/code) working in this repository.

## Purpose & audience

Real-time SDR spectrum analyzer (RTL2832U today; the modular device layer is ready for a company's proprietary SDR). Design priorities, in order: **low latency, low power** (extended battery lab measurements), then **minimal footprint**.

Who runs it: mostly **non-software people** — radio/electronics engineers testing devices — on **native Windows**, with a Linux/WSL2 developer path alongside. The `.iq` captures + PNG spectrogram exports feed a downstream **Python/AI** pipeline. Private repo, going public as it matures.

Two consequences that keep driving decisions: **(1) no scary UX** — a console window, an AV prompt, or a driver hoop reads as malware to this audience (see *Windows distribution* below), so native Windows ships GUI-only; **(2) grooming for handoff to junior maintainers** — favour clarity and deletion over cleverness.

## Build

```bash
make              # debug (O0, symbols, OPENSPECTRUM_DEBUG defined)
make release      # O3 + LTO + -march=haswell + footprint trim + strip
make profile      # O2 + gprof instrumentation
make dist         # package a release bundle for the current platform → dist/
make clean
```

Binary: `openspectrum` (Linux) / `openspectrum.exe` (Windows/MSYS2). No test suite.

Release flags: `-march=haswell` (AVX2+FMA3), `-flto`, `-ffunction-sections -fdata-sections -fvisibility=hidden -fvisibility-inlines-hidden` + `-Wl,--gc-sections`, `-falign-functions=32 -falign-loops=32` (hot loops in one DSB fetch line). See `TRIM_CFLAGS`/`TRIM_LDFLAGS` in the Makefile. `-s` (strip at link, release-only) drops the ~40K symbol table — there's no DWARF in the binary, so it costs nothing at runtime; debug/profile keep symbols for gdb/gprof.

**Linting is gradual** — format/tidy only lines changed vs a base ref, never the whole tree (a full pass would reflow the hand-tuned AVX2 blocks + the 8x16 font table). Config in `.clang-tidy` / `.clang-format`. Wrapper:
```bash
./lint.sh            # format changed lines vs main + clang-tidy the diff
./lint.sh --check    # non-mutating; non-zero exit if anything is flagged
./lint.sh HEAD~3     # arbitrary base ref
```
Its `-I` flags mirror the Makefile's `INCLUDES`; the **per-module dirs are required** (e.g. `control_state.h` includes `signal_processor.h` from `src/signal/`) — a bare `-Isrc -Iinclude` yields `file not found`.

**Header-dep tracking**: compile rules pass `DEPFLAGS := -MMD -MP` → a `.d` beside each `.o`, `-included` at the Makefile bottom, so editing a header rebuilds dependents. `DEPFLAGS` is kept **separate from `CXXFLAGS`** because `release`/`profile*` override `CXXFLAGS` via recursive make and would strip it. A new compile rule must append `$(DEPFLAGS)`.

## Release & packaging

`make dist` → self-contained bundle into `dist/` (Linux AppImage, Windows/MSYS2 zip with bundled DLLs). Dispatches on `$(OS)` to `packaging/{linux-appimage,windows-bundle}.sh`. Version defaults to `git describe --tags --always`; override `make dist VERSION=vX`.

`.github/workflows/release.yml` runs the **same scripts**; triggers on `v*` tag push (manual `workflow_dispatch` builds artifacts but does not publish). Two jobs (Linux `ubuntu-latest`, Windows MSYS2 MINGW64) build, upload an artifact, and publish a Release (body = annotated tag message). Cut a release: `git tag -s vX -F notes.txt && git push origin vX`; see `RELEASING.md`.

App icon: source `packaging/openspectrum.svg`; the rasterized `openspectrum.png` (Linux/AppImage) and multi-size `openspectrum.ico` (16–256 px, Windows) are **committed** and consumed directly — no build-time SVG rasterization. Regenerate after editing the SVG: `rsvg-convert -w256 -h256 openspectrum.svg -o openspectrum.png`, and render 16/24/32/48/64/128/256 PNGs + `icotool -c` for the `.ico`. On Windows the Makefile compiles `openspectrum.rc` → COFF via `windres` (`RES_OBJ`, gated on `OS=Windows_NT`, empty elsewhere) and links it into the `.exe` so the shell shows the icon.

**Windows distribution — unsigned = AV false positives.** A stripped, statically-linked `.exe` with no code-signing certificate and no download reputation reliably trips Windows Defender's ML heuristic (`Trojan:Win32/Wacatac.C!ml`; ~1/61 on VirusTotal — a false positive). The durable fix is **Authenticode signing** (EV cert → instant SmartScreen trust) + a Microsoft false-positive submission (`microsoft.com/en-us/wdsi/filesubmission`). Until then, do **not** add AV-suspicious Win32 APIs to the shipped build — `AttachConsole` console-attach was added for terminal output and reverted precisely because it worsened the FP. This is why native Windows is **GUI-only, no stdout/stderr**.

## Architecture

Single-threaded render loop fed by a producer thread: the async RTL-SDR callback, or with `--play file.iq` an `IqPlaybackSource` (`src/hardware/iq_playback.*`) replaying an IqLogger capture in real time, looping at EOF. Both feed `async_sample_callback`, so downstream is source-agnostic; playback mode holds no device (`dev` is null → tuner writes and the retune queue-drain are skipped). Producer + render threads call `enable_ftz_daz()` once per thread (kills the ~100-cycle denormal trap near the dB floor).

```
Producer thread (RTL-SDR callback  OR  IqPlaybackSource reader)
  └─ async_sample_callback()
       ├─ FTZ/DAZ set once per thread
       ├─ accumulates IQ samples into g_sample_accumulator_frame
       └─ pushes complete FFT-sized FrameHandles → g_sample_queue

Main thread (main.cpp)
  └─ SDL3 events → ControlState
  └─ dequeues FrameHandle from g_sample_queue (8 ms timeout)
  └─ drains queue to newest — drops any backlog (frame-rate throttle)
  └─ SignalProcessor: remove_dc() + apply_window()
  └─ FftAnalyzer: execute() → db_spectrum[]
  └─ SpectrumDisplay::update_spectrum()
  └─ WaterfallDisplay::add_spectrum_line()
  └─ SdlRenderer: render_displays() or render_displays_scroll()
```

The throttle (`while(!queue.empty()) move+pop`) runs inside the dequeue lock; each move releases the prior `FrameHandle` to the pool, only the newest survives — prevents latency accumulation when the callback outpaces the renderer.

**Shutdown is not an error path**: `stop_streaming()` cancels the transfer → `rtlsdr_read_async` returns negative (e.g. -5) on the callback thread. Expected, gated on `m_thread_running`; do not log as ERROR. Only a negative return while still running is a real fault.

### Key data structures
- **FramePool / FrameHandle** (`include/openspectrum/frame_pool.h`): 64-byte-aligned pool of `complex<float>` buffers. `FrameHandle` is RAII (destruction returns to its pool). Never copy; always move.
- **RingBuffer<T>** (`src/utils/ring_buffer.h`): fixed-capacity circular buffer. `[0]`=oldest, `back()`=newest. `full()` gates the waterfall scroll path.
- **PixelBuffer** (`src/visualization/spectrum_display.h`): non-copyable raw `uint8_t*` RGBA buffer, used by both displays.

### FFT backend (PocketFFT)
`third_party/pocketfft/pocketfft_hdronly.h`; shim `src/fft/pocketfft_wrapper.h` defines `pocketfft_cpx = std::complex<float>` and `pocketfft_forward` (forward-only — the analyzer never inverse-transforms). Layout-compatible, so input fill / output copy are `memcpy`.

**Windows MinGW**: the patched `aligned_alloc` shim (~line 152 of the header) routes to `_aligned_malloc`/`_free` from `<malloc.h>` on `_WIN32`. Restoring upstream `::aligned_alloc` breaks MSYS2 — do not revert.

### FftAnalyzer hot loop
`execute()` post-FFT loop is manually vectorized: power → fma → `fast_log10_avx2` → `m_db_spectrum`. Uses `20·log10(sqrt(p)·c) ≡ 10·log10(p·c²)` (sqrt eliminated); epsilon squared (`1e-24`). The dB spectrum is the only output — there is no power/magnitude/phase path (an earlier opt-in "extras" variant with a manual branch unswitch was removed once nothing consumed it).

`fast_log10_avx2` (file-local): log10 via `2·atanh((m-1)/(m+1))`, Horner through y⁶. Max rel err ~1.7e-5 (~0.00015 dB).

**Two-sided spectrum** (complex IQ): all N bins are computed and map 1:1 onto the axis `[center-rate/2, center+rate/2]`; every bin is scale=1 — there is no real-rfft negative-mirror fold, so no +6 dB doubling. DC centering is the **pre-FFT sign trick** (input × `(-1)^n` shifts the spectrum by N/2 → DC lands at bin N/2 = screen center). Do NOT also swap output halves — a second fftshift cancels the first and misaligns the spectrum with the axis by half the span.

### SignalProcessor SIMD invariants
`m_window_coeffs_doubled` is a 2N-float window with each coeff duplicated (`[w0,w0,w1,w1,...]`) so AVX2 `apply_window` is one `_mm256_mul_ps` against interleaved IQ, no shuffles. `precompute_window()` regenerates both arrays on size/type change — a new window must write both `m_window_coeffs` and `m_window_coeffs_doubled`.

`remove_dc`: 4 independent `__m256` accumulators (breaks the FMA latency chain on Haswell), 16 complex floats/iter, horizontal reduce, then broadcast-mean unrolled-4 subtract.

Both keep a scalar fallback under `#ifdef __AVX2__`; debug builds (no `-march=haswell`) exercise it.

### Render path (two modes)
After `add_spectrum_line()`, main.cpp checks `needs_full_render()`:
- **Full** (`true`, while `m_history` is filling or after `reset()`/`rebuild_rgba_lut()`): upload entire `m_pixels` via `render_displays()`.
- **Scroll** (`false`, only when `m_history.full()`): render only the newest line; `render_displays_scroll()` GPU-shifts `m_wf_scroll_tex` up by `line_height` and uploads the new bottom strip via `m_wf_line_tex` (~5 KB vs ~1.5 MB/frame).

`needs_full_render()` returns `m_needs_full_render || !m_history.full()` — the flag alone is insufficient (cleared inside `render()` before main.cpp reads it).

**Transition continuity**: scroll textures are allocated lazily in `render_displays_scroll()` (`ensure_wf_scroll_textures()`); on the first scroll call (`m_wf_scroll_valid` false) the seed branch copies the current waterfall from `m_texture` shifted up by `line_height` instead of clearing to black → seamless fill→scroll.

**Frame pacing**: vsync is on (hardcoded in `main.cpp`'s `SdlRenderer` ctor), so the loop is paced by the display refresh, not a busy-spin. `--max-fps N` (default **30**, `0` = uncapped) adds a top-of-loop `std::this_thread::sleep_until` that throttles *below* refresh to cut GPU/battery draw for long unattended runs; it paces every path (frozen / no-sample / render) uniformly and keeps vsync on, so frames stay tear-free. Windows' ~15 ms timer granularity makes the effective rate undershoot the cap slightly (harmless — undershoots power too). The right-panel PERF block (`fps` / `cpu` / `render_build` / `present` ms) is always shown; the old `t` toggle was removed.

### Autoscale: anchor on the median, never the min
Both panes autoscale, and both anchor their **bottom on the median, never the minimum**. This is the easiest thing in the codebase to "fix" back and get wrong — `min` looks like the obviously correct floor and is not.

A periodogram bin of Gaussian noise is exponentially distributed, so `min` over N bins is an *extreme-value statistic with no physical meaning*: at FFT 16384 the deepest null lands **~42 dB below the actual noise floor** and re-rolls every frame. Anchoring there stretched the spectrum pane over ~90 dB (half of it empty, floor pinned mid-screen, labels jittering) and stretched the waterfall LUT over ~80 dB the content never occupies (floor at mid-palette → the whole pane washed out to uniform light blue). The median sits a fixed **1.6 dB** below the floor's mean power and is stable to ~0.05 dB. `max` *is* fine as the top: it is signal-dominated, and even for pure noise only lands ~10 dB above the floor.

- `SpectrumDisplay::update_spectrum` → `nth_element` over a **stride-4 subsample** into `m_scratch` (noise bins are iid, so a quarter-sample gives the same median to ~0.1 dB; sorting all 16K cost ~0.2 ms/frame — a third of the DSP budget — for a number that barely moves). Bottom = median − `kFloorMargin` (10 dB), top = max + 5.
- `WaterfallDisplay::update_global_range` → cumulative walk to the median bucket. This is what the histogram is *for*: a percentile is O(256) and L1-resident, cheaper than the min/max scan it replaced.

Related invariants, same root cause — **the drawn trace must not be a peak detector**: the per-pixel-column reduction in `build_vertices` is a **mean** (`column_mean`), not a max. Max-of-12-bins reads ~4.9 dB high (E[max of n exponentials] = Hₙ) and still spreads ~1.8 dB — that is the "grass" on the noise floor. Nothing is lost by averaging: **max-hold is the peak detector** and keeps `std::max`; it is the trace that promises those semantics. `sample_at_x` must mirror whatever `build_vertices` does, or the cursor dot floats off the bar.

Sanity check when touching any of this: peak + 5 must equal the top axis label exactly.

### WaterfallDisplay
History = `RingBuffer<vector<uint8_t>>`; dB quantized to `uint8_t` over a fixed -120..0 dB range (4:1 vs float). RGBA LUT (`m_rgba_lut[256]`, `uint32_t`) maps quantized → color; render inner loop is `memcpy`/pixel (scalar) or `_mm256_i32gather_epi32`×8 (AVX2).

**Autoscale is incremental, not a rescan.** `m_hist_counts[256]` tracks how many history pixels sit at each quantized level; `update_global_range()` walks it (O(256), L1-resident) for the median + last-populated bucket instead of scanning the whole history each frame. The invariant: the histogram must stay in lockstep with `m_history` — `add_spectrum_line` decrements the evicted row's counts (`m_history[0]`, the slot `push()` is about to overwrite) *before* push and increments the new row's; `reset()` zeroes it. A debug-only (`#ifndef NDEBUG`) cross-check brute-force recounts the buckets and asserts they match `m_hist_counts` element-wise — it pins the invariant itself rather than one statistic derived from it, so any bookkeeping drift fails loudly under run-it validation and the oracle survives a change of readout.

### Platform differences
| Concern | Windows (`_WIN32`) | Linux |
|---------|-------------------|-------|
| SDL render driver | `SDL_HINT_RENDER_DRIVER = "direct3d11"` pre-`SDL_CreateRenderer` | default |
| Security linker flags | none (static libs) | `-Wl,-z,now -Wl,-z,relro -Wl,-z,noexecstack` |
| pocketfft `aligned_alloc` | `_aligned_malloc`/`_free` (`<malloc.h>`) | `::aligned_alloc` (C11) |

### Performance profile (measured — don't re-derive)
The pipeline is **render-backend-bound, never DSP-bound.** `cpu` (remove_dc + window + FFT + display update) is sub-2 ms even at FFT 16384 on a 15 W i5-7300U — further FFT/SIMD work is **not** warranted; that line is done.
- **`render_build`/`present` are not comparable across backends.** The vsync idle-wait lands in `present` on Direct3D 11 (Windows) and the software renderer, but in `render_build` on OpenGL. Only `cpu` is a clean cross-backend number — a large `present` or `render_build` is mostly idle vblank wait, **not** work.
- **Keep the default renderer.** OpenGL (Linux default) and D3D11 (Windows) lock a clean 60 fps with ~1 ms real CPU.
- **Vulkan is a trap on old Intel iGPUs (HD 620 / Gen9):** ~30 fps, ~24 ms real `render_build` — ~7× slower than OpenGL on the same box. Never suggest `SDL_RENDER_DRIVER=vulkan` there.
- **Software renderer** = battery/thermal worst case (raster + blit ~16 ms, all on one CPU core); correct, warns loudly, fine only as a fallback. `--max-fps 30` helps most here.
- **Memory is bounded, not FFT-scaling:** under 49 MB resident at FFT 16384 (largest transform) on the v3.7.0 Direct3D 11 Windows build, lower at smaller sizes. A Linux/WSL2 profile reads much higher — that's the Mesa software-GL backing store (no GPU passthrough), not heap. Full breakdown in `docs/TECHNICAL.md`.

### ControlState flow
`SdlControlInput` writes `ControlState` (freq, gain, FFT size, window). Main loop polls `*_changed()` each iter; `apply_to_device(dev)` is the single hardware-register write site. A freq change drains the sample queue so the spectrum snaps in sync with the freq scale.
- **Palette** (`c` / `Shift+C`): `cycle_palette(±1)` over `PALETTE_COUNT`; main looks up `kPaletteMap[...]`, pushes to both displays via `set_color_map()`. On the waterfall → `rebuild_rgba_lut()` → `m_needs_full_render=true`, recoloring the whole waterfall from quantized history (LUT is the only change).
- **Trace modes** (`m` max-hold, `a` averaging, `x` reset): flags in `ControlState`, pushed to `SpectrumDisplay` each frame (display resets only on transitions). Averaging = per-bin EMA (`kAvgAlpha=0.25`), **default on** (with the mean detector it takes the noise floor to ~0.5 dB of spread — that is what makes it a line rather than a band; the panel's TRACE field surfaces it so the default is not silent). Autoscale + max-hold track the raw spectrum. Max-hold = white segments in the same `SDL_RenderGeometry` buffer. Re-seed on bin-count change; the waterfall always shows raw data.
- **Amplitude trigger** (`Shift`+left-drag, `Space`): `main()` owns `trig_threshold_db`/`trig_armed`/`frozen` (like the marker list, not `ControlState`). `SdlRenderer::poll_events` turns `Shift`+left motion (button-mask, no button-up tracking) / button-down into a `take_trigger_set()` request (render-space y); plain left still drops a marker. main maps y→dB against the spectrum's autoscaled range (drag onto the pane bottom disarms), and fires when `peak_db >= threshold` (`get_max_db()`). Freeze = a gate **before §2** that skips the dequeue/processing but keeps `present_frame()` + input, so the triggering frame is the one held; `add_spectrum_line` sits after the gate so the waterfall history is genuinely static (the "seconds-ago" axis is held via `frozen_wf_top_seconds` so its labels don't drift against the live clock). `Space` → `ControlState::request_unfreeze()` → resume + re-arm. `SdlRenderer::draw_trigger` draws the threshold line + left-anchored `TRIG` tag (clear of the top-right PEAK) and the centered FROZEN banner.

### Frequency scale overlay
`render_frequency_scale()` bakes ticks+labels onto one target texture (`m_freq_scale_texture`) only when `center_hz`/`sample_rate_hz` change; `render_overlays()` issues one `SDL_RenderCopy`/frame. Avoids the per-frame blend-mode save/restore spike.

### Text renderer
`TextRenderer` (`src/gui/text_renderer.*`): bitmap font, no glyph cache — each `render_text()` returns a fresh caller-owned `SDL_Texture` (call sites cache the string-texture). Font = public-domain IBM VGA 8x16 (`BITMAP_FONT[96][16]`, 16 scanlines/glyph). **MSB = leftmost pixel** (`1 << (7 - src_x)`). Integer-only scaling (`scale = font_size / GLYPH_SRC_H`).

**Colour byte-order**: pack with `SDL_MapSurfaceRGBA(surface,r,g,b,a)`, never hand-rolled shifts — `RGBA32` is `ABGR8888` on LE x86, so a hand-rolled shift maps red onto alpha (any `r==0` colour → transparent). Use the `SDL_PIXELFORMAT_RGBA32` alias, never a hardcoded `8888`.

**SDL3 texture defaults**: SDL3 defaults alpha textures to `BLENDMODE_BLEND` + `SCALEMODE_LINEAR` (SDL2 was NONE/nearest). Transparent-filled text under `BLENDMODE_NONE` renders **opaque black** (the backdrop behind status/PEAK/IQ text). `render_text()` sets `BLENDMODE_NONE` + `SCALEMODE_NEAREST` on every texture it returns; call sites wanting transparency (freq-scale labels, timing overlay) opt into `BLEND`. All textures set `SCALEMODE_NEAREST` so resize scaling stays pixel-crisp.

## Code-layout discipline
`include/openspectrum/attributes.h` defines `OS_HOT`/`OS_COLD` (gated on GCC/Clang, else no-op):
- **`OS_HOT`** on per-frame functions: `FftAnalyzer::execute`, `SignalProcessor::apply_window`/`remove_dc`, `WaterfallDisplay::add_spectrum_line`, `SpectrumDisplay::update_spectrum`, `SdlRenderer::render_displays`/`render_displays_scroll`/`present_frame`.
- **`OS_COLD`** on init/one-shot: constructors, `precompute_window` + `compute_*` builders, `parse_arguments`, `print_usage`.

These survive `--gc-sections` and cluster hot text on contiguous I-cache lines.
