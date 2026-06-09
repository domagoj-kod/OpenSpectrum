# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## Build Commands

```bash
make              # debug build (O0, symbols, OPENSPECTRUM_DEBUG defined)
make release      # O3 + LTO + -march=haswell + footprint trim
make profile      # O2 + gprof instrumentation
make dist         # package a release bundle for the current platform → dist/
make clean        # remove build/ and binary

# PGO pipeline (binary becomes ~15-20 % faster on the trained workload)
make profile-gen      # build instrumented binary that writes .gcda files
./openspectrum        # exercise the hot path for 60-180 s, exit cleanly (Ctrl-C)
make clean
make profile-use      # rebuild consuming the profile
make profile-clean    # wipe pgo-data/ before regenerating
```

The binary is `openspectrum` (Linux) or `openspectrum.exe` (Windows/MSYS2).

Release / PGO targets enable: `-march=haswell` (AVX2 + FMA3), `-flto`, `-ffunction-sections -fdata-sections -fvisibility=hidden -fvisibility-inlines-hidden` + `-Wl,--gc-sections` for footprint trim, and `-falign-functions=32 -falign-loops=32` so hot loops fit in one DSB fetch line. Trimmed flags live in `TRIM_CFLAGS` / `TRIM_LDFLAGS` in the Makefile.

There is no Makefile test target. Tests in `test/` require standalone compilation:
```bash
g++ -std=c++20 test/render_test.cpp -o test/render_test.out -lSDL2
```

Linting (clang-tidy config in `.clang-tidy`):
```bash
clang-tidy src/**/*.cpp -- $(pkg-config --cflags sdl2) -Isrc -Iinclude -Ithird_party/pocketfft -std=c++20
```

**Header-dependency tracking**: the per-object compile rules pass `DEPFLAGS := -MMD -MP`, emitting a `.d` file beside each `.o` that lists the headers it pulled in; these are `-included` at the bottom of the Makefile, so editing a header rebuilds every `.cpp` that includes it. `DEPFLAGS` is kept **separate from `CXXFLAGS` on purpose** — `release` / `profile` / `profile-gen` / `profile-use` override `CXXFLAGS` via recursive make, which would strip `-MMD -MP` if it lived there. If you add a new compile rule, append `$(DEPFLAGS)` to it.

## Release & packaging

`make dist` builds a self-contained, distributable bundle for the current platform into `dist/` — Linux → AppImage, Windows/MSYS2 → zip with bundled DLLs. It dispatches on `$(OS)` to the platform script (`packaging/linux-appimage.sh` or `packaging/windows-bundle.sh`) and stamps a version: `VERSION` defaults to `git describe --tags --always`, override with `make dist VERSION=v2.5.0`.

`.github/workflows/release.yml` runs the **same scripts** so local and CI packaging stay identical. It triggers on `push` of a `v*` tag (and has a manual `workflow_dispatch` that builds artifacts but does *not* publish a Release). Two jobs — Linux AppImage on `ubuntu-latest`, Windows zip on `windows-latest` (MSYS2 MINGW64) — build, upload a workflow artifact unconditionally, and publish to a GitHub Release (body extracted from the annotated tag message). Cutting a release = `git tag -s vX -F notes.txt && git push origin vX`; see `RELEASING.md` for the full checklist.

The app icon (`packaging/openspectrum.png`, with `openspectrum.svg` source) is **committed**, not rasterized at build time — the packaging scripts consume the PNG directly, so there is no build-time SVG-to-PNG dependency.

## Architecture

The pipeline is a single-threaded render loop fed by an async RTL-SDR callback thread. Both threads call `enable_ftz_daz()` on first entry (per-thread MXCSR; the render thread sets it at the top of `main`, the callback thread via a `thread_local` guard on first sample). This eliminates the ~100-cycle microcode trap on denormal floats that show up near the dB noise floor.

```
RTL-SDR callback thread
  └─ async_sample_callback()
       ├─ FTZ/DAZ set once per thread
       ├─ accumulates IQ samples into g_sample_accumulator_frame
       └─ pushes complete FFT-sized FrameHandles → g_sample_queue

Main thread (main.cpp)
  └─ SDL2 events → ControlState
  └─ dequeues FrameHandle from g_sample_queue (8 ms timeout)
  └─ drains queue to newest — drops any backlog (frame-rate throttle)
  └─ SignalProcessor: remove_dc() + apply_window()
  └─ FftAnalyzer: execute() → db_spectrum[]
  └─ SpectrumDisplay::update_spectrum()
  └─ WaterfallDisplay::add_spectrum_line()
  └─ SdlRenderer: render_displays() or render_displays_scroll()
```

The throttle (`while (!g_sample_queue.empty()) move + pop`) runs inside the dequeue lock. Each move-assignment releases the prior `FrameHandle` to the pool; only the newest survives. Prevents render-loop latency accumulation when the callback outpaces the renderer.

**Shutdown is not an error path**: `stop_streaming()` cancels the async transfer, which makes `rtlsdr_read_async` return a negative code (e.g. -5) on the callback thread. This is *expected*, not a failure — it is gated on `m_thread_running`, so do not log or re-flag it as an ERROR. Only an unexpected negative return while still running is a real fault.

### Key data structures

**FramePool / FrameHandle** (`include/openspectrum/frame_pool.h`): Cache-line-aligned (64-byte) pool of `complex<float>` buffers. `FrameHandle` is RAII — destruction returns the frame to its pool. Never copy; always move.

**RingBuffer<T>** (`src/utils/ring_buffer.h`): Fixed-capacity circular buffer. `operator[](0)` = oldest, `back()` = newest. `full()` is the gate for the waterfall scroll path.

**PixelBuffer** (`src/visualization/spectrum_display.h`): Non-copyable raw `uint8_t*` buffer for RGBA pixel data. Used by both displays.

### FFT backend (PocketFFT)

`third_party/pocketfft/pocketfft_hdronly.h` is the backend; KissFFT was removed. The shim `src/fft/pocketfft_wrapper.h` defines `pocketfft_cpx = std::complex<float>` and exposes `pocketfft_forward` / `pocketfft_inverse`. Because `pocketfft_cpx` is layout-compatible with the caller's `std::complex<float>`, the non-DC-center input fill is a plain `memcpy` and the optional output copy is a single `memcpy`.

The Windows MinGW build needs the patched `aligned_alloc` shim near line 152 of `pocketfft_hdronly.h`: on `_WIN32` it routes to `_aligned_malloc` / `_aligned_free` from `<malloc.h>`. Restoring the upstream `::aligned_alloc` will break MSYS2 compilation; do not revert that block.

### FftAnalyzer hot loop

`FftAnalyzer::execute()` post-FFT loop is manually vectorized and *manually unswitched* on `m_extra_spectra_enabled` because GCC's loop-unswitching pass refused to hoist it through the AVX2 intrinsics:

- **Fast path (default)** computes power → fma → vectorized `fast_log10_avx2` → store `m_db_spectrum`. Uses the identity `20·log10(sqrt(p)·c) ≡ 10·log10(p·c²)`, so the sqrt is eliminated. Epsilon is squared (`1e-24`) to match the power=0 behavior of the original formula.
- **Extras path** (when `set_extra_spectra_enabled(true)`) additionally stores `m_power_spectrum`, `m_magnitude_spectrum` (via `_mm256_sqrt_ps`), and runs a scalar `atan2` pass to fill `m_phase_spectrum`.

The default is *off*. Nothing in the current pipeline consumes magnitude / power / phase, so the secondary spectra are computed only if a future consumer opts in via the setter.

`fast_log10_avx2` (file-local helper) computes log10 via `2·atanh((m-1)/(m+1))` with a Horner polynomial through y⁶. Max relative error ~1.7e-5 → ~0.00015 dB, well below display resolution.

The DC-center mode does a one-shot halves-swap on `m_output_buffer` after the FFT (`std::swap` over the first N/2 elements with the second N/2). The downstream power loop then reads contiguously without `(i + N/2) % N` arithmetic. The bulk loop assumes `scale=2`; DC and Nyquist bins (`scale=1`) are patched scalar after.

### SignalProcessor SIMD invariants

`m_window_coeffs_doubled` is a 2N-float copy of the window with each coefficient duplicated: `[w0, w0, w1, w1, ...]`. This lets the AVX2 `apply_window` issue a single `_mm256_mul_ps` against interleaved IQ data without on-the-fly shuffles. `precompute_window()` regenerates both arrays whenever size or window type changes; if you add a new window function, write into both `m_window_coeffs` and `m_window_coeffs_doubled`.

`remove_dc` uses 4 independent `__m256` accumulators (breaks the FMA latency chain on Haswell — port-0 throughput 1, latency 4–5) processing 16 complex floats per iter, then a horizontal reduce. Pass 2 broadcasts the mean and unrolled-4 subtracts.

Both `apply_window` and `remove_dc` keep a scalar fallback under `#ifdef __AVX2__`. Debug builds (no `-march=haswell`) exercise the scalar path.

### Render path (two modes)

After `WaterfallDisplay::add_spectrum_line()` is called, main.cpp checks `waterfall_display.needs_full_render()`:

- **Full render** (`needs_full_render() == true`): called while `m_history` is filling or after `reset()` / `rebuild_rgba_lut()`. Uploads entire `m_pixels` (spectrum + waterfall) to the SDL texture via `render_displays()`.

- **Scroll render** (`needs_full_render() == false`, only when `m_history.full()`): waterfall renders only the newest line into `m_new_line` (~5 KB). `render_displays_scroll()` GPU-shifts `m_wf_scroll_tex` up by `line_height` pixels using a src/dst rect blit and uploads only the new bottom strip via the narrow `m_wf_line_tex` streaming texture. Replaces ~1.5 MB/frame CPU→GPU upload with ~5 KB.

`needs_full_render()` returns `m_needs_full_render || !m_history.full()` — the flag alone is not sufficient because it is cleared inside `render()`, before main.cpp reads it.

**Transition continuity**: the scroll textures (`m_wf_scroll_tex` / `m_wf_scroll_aux`) are not created during the fill phase — they're allocated lazily inside `render_displays_scroll()` via `ensure_wf_scroll_textures()`. On the *first* scroll call, `m_wf_scroll_valid` is false; instead of clearing to black, the seed branch in `render_displays_scroll()` copies the current waterfall content from `m_texture` (which was filled by the prior `render_displays()` passes) into the scroll target, shifted up by `line_height`. This makes the fill→scroll transition seamless.

### WaterfallDisplay internals

History is stored as `RingBuffer<vector<uint8_t>>` — dB values are quantized to `uint8_t` over a fixed -120..0 dB range (4:1 vs float). The RGBA LUT (`m_rgba_lut[256]`, packed `uint32_t`) maps quantized values to colors; the render inner loop is a `memcpy` per pixel (scalar) or `_mm256_i32gather_epi32` × 8 pixels (AVX2, guarded by `#ifdef __AVX2__`).

### Platform differences

| Concern | Windows (`_WIN32`) | Linux |
|---------|-------------------|-------|
| `STREAM_BUFF` | 64 buffers | 32 buffers (usbfs DMA limit) |
| SDL render driver | `SDL_HINT_RENDER_DRIVER = "direct3d11"` set before `SDL_CreateRenderer` | default |
| Security linker flags | none (static libs) | `-Wl,-z,now -Wl,-z,relro -Wl,-z,noexecstack` |
| pocketfft `aligned_alloc` | `_aligned_malloc` / `_aligned_free` from `<malloc.h>` | `::aligned_alloc` (C11) |

### ControlState flow

Keyboard input (`SdlControlInput`) writes into `ControlState` (frequency, gain, FFT size, window function). Main loop polls `control_state.fft_size_changed()`, `window_changed()`, etc. each iteration. `control_state.apply_to_device(dev)` is the single place hardware registers are updated. On frequency change, the sample queue is drained immediately so the spectrum snaps in sync with the freq-scale overlay.

**Palette cycling** (`c` cycles forward, `Shift+C` backward): `SdlControlInput` calls `ControlState::cycle_palette(±1)` over `PALETTE_COUNT` maps. Main loop polls `control_state.palette_changed()`, looks up `kPaletteMap[get_palette_index()]`, and pushes it to both displays via `set_color_map()`. On the waterfall, `set_color_map()` → `rebuild_rgba_lut()` → sets `m_needs_full_render = true`, so the *entire* waterfall is recolored from the quantized `uint8_t` history on the next frame (no re-acquisition needed — the LUT is the only thing that changed).

### Frequency scale overlay

`SdlRenderer::render_frequency_scale()` bakes ticks + labels onto a single `SDL_TEXTUREACCESS_TARGET` texture (`m_freq_scale_texture`) only when `center_hz` or `sample_rate_hz` changes. `render_overlays()` issues one `SDL_RenderCopy` per frame. This avoids the Serializing Operations spike from per-frame blend-mode save/restore calls.

### Text renderer

`TextRenderer` (`src/gui/text_renderer.{h,cpp}`) is a bitmap-font renderer with no glyph cache — each `render_text()` call returns a fresh caller-owned `SDL_Texture` (call sites cache the rendered string-texture themselves). The font is the public-domain **IBM VGA 8x16** ROM typeface (ASCII 32–127, `BITMAP_FONT[96][16]` — 16 bytes = 16 scanlines per glyph). **MSB of each byte is the leftmost pixel**, matching the `1 << (7 - src_x)` test in the blit loop. Scaling is **integer-only**: `scale = font_size / GLYPH_SRC_H`, so glyphs render at native 8x16, or 16x32, etc. — never a fractional size.

**Colour byte-order gotcha (recurred once)**: build the packed pixel with `SDL_MapRGBA(surface->format, r, g, b, a)`, *not* a hand-rolled `(r<<24)|(g<<16)|(b<<8)|a`. SDL's `RGBA32` is `ABGR8888` on little-endian x86, so the hand-rolled shift maps input red onto the alpha byte — any colour with `r == 0` then renders fully transparent. `SDL_MapRGBA` is correct on either endianness.

## Code-layout discipline

`include/openspectrum/attributes.h` defines `OS_HOT` and `OS_COLD` macros (gated on `__GNUC__ / __clang__`, no-op otherwise). Apply them to:

- **`OS_HOT`** on every function called per render frame: `FftAnalyzer::execute`, `SignalProcessor::apply_window` / `remove_dc`, `WaterfallDisplay::add_spectrum_line`, `SpectrumDisplay::update_spectrum`, `SdlRenderer::render_displays` / `render_displays_scroll` / `present_frame`.
- **`OS_COLD`** on init / argparse / window-precompute / one-shot paths: constructors, `SignalProcessor::precompute_window` and `compute_*` window builders, `parse_arguments`, `print_usage`.

These hints survive `--gc-sections` and tell the linker to cluster hot text on contiguous I-cache lines. Combined with PGO, the I-cache miss rate drops further than either alone.

## PGO workflow

The `.gcda` profile data is platform-, toolchain-, and build-path-specific. **Never share `pgo-data/` between Windows and Linux builds** — GCC embeds the absolute build path into the filename mangling. Always `make profile-clean` before switching platforms.

Common pitfall: `find pgo-data -name '*.gcda' | wc -l` (not `ls pgo-data | wc -l`) is the right count check. The mangled paths are real nested directories under `pgo-data/`; `ls` shows only the top-level prefix.

When exercising the binary during `profile-gen`:
- Run for at least 60 seconds with live RTL-SDR data.
- Change frequency a few times.
- **Don't** trigger `--help`, error paths, or features you don't use routinely — PGO will think they're hot.
- Exit with Ctrl-C; never `kill -9` (loses the `.gcda` writes).

Verify `profile-use` matched files cleanly: `make profile-use 2>&1 | grep -i "not found"` should print nothing. Warnings here mean partial PGO; the build silently falls back to default heuristics for unmatched units.
