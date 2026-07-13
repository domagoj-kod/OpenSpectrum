# OpenSpectrum — Technical Reference

Deeper material for advanced users and maintainers: the RF/DSP concepts behind
the display, the module architecture, performance characteristics, how to add a
new SDR backend, and the build's security hardening. For install and usage see
the [README](../README.md); for day-to-day maintainer guidance and hot-path
invariants see [CLAUDE.md](../CLAUDE.md).

## Signal, bandwidth, and resolution

> RTL-SDR outputs complex IQ samples, so the observed bandwidth is roughly equal
> to the sample rate.

For:

```
fc = 98.8 MHz    # center frequency
fs = 2.048 MS/s  # sample rate
```

the observed span is:

```
97.776 ---------------- 98.8 ---------------- 99.824 MHz
          <- 1.024 MHz -> <- 1.024 MHz ->
```

all simultaneously in the IQ stream.

### FFT size, resolution, and latency

The span is set only by `fs`; the FFT size never adds bandwidth, it only slices
that fixed span more finely:

```
bin resolution   Δf = fs / N     # N = FFT size
frame time       Δt = N / fs     # how long one FFT covers
```

| fs | N | Δf (resolution) | Δt (latency) |
|----|---|-----------------|--------------|
| 2.048 MS/s | 2048 | 1000 Hz/bin | 1 ms |
| 2.048 MS/s | 16384 | 125 Hz/bin | 8 ms |
| 1.024 MS/s | 16384 | 62.5 Hz/bin | 16 ms |

Bigger `N` → finer frequency, coarser time, more latency. That trade is fixed.
Past the point where `Δt` exceeds the signal's coherence time, extra bins just
show the same noise at tighter spacing — no new information.

### Where the data stops being real

- **`fs` above ~2.4 MS/s** — USB 2.0 can't keep up, buffers drop, and the
  dropouts show up as broadband glitches. This is the practical ceiling.
- **Band edges** — the R820T2 tuner's analog IF filter (auto-set, roughly
  tracks `fs`) rolls off the outer ~10–20 % of the span. The edges are filter
  shape, not flat response.
- **Center bin** — DC offset and LO leakage put a spike at `fc` regardless of
  tuning.
- **Valid `fs` ranges** — the RTL only accepts **225–300 kS/s** and
  **900 kS/s–3.2 MS/s**; the gap in between is rejected.

Clean working window ≈ **0.9–2.4 MHz**: the middle ~80 % of the span is real
device data, the edges are tuner roll-off, and the center is the DC artifact.

### PEAK readout

The top-right `PEAK` value is **dBFS** — decibels relative to full scale, not
watts or dBm. The IQ samples are normalized to `[-1, 1]`, so `0 dB` is the ADC's
full scale and everything real reads below it (the noise floor typically lands
around `-70…-100 dBFS` depending on FFT size, window, and gain). It is
**uncalibrated and relative** — no antenna gain, impedance, or cable-loss term —
so it cannot be converted to absolute power (dBm) without a per-gain calibration
offset.

## Window functions

`RECTANGLE`, `HANN`, `HAMMING`, `BLACKMAN`, `BLACKMAN_HARRIS` (default),
`FLAT_TOP`. Select at launch with `-W NAME`, or cycle at runtime with
`UP`/`DOWN`.

## Architecture

*Divide et impera* — SDR processing is decomposed into isolated, interchangeable
modules communicating over narrow interfaces. Following community standards
(librtlsdr, pocketfft) keeps compatibility and adoption easy.

```
OpenSpectrum/
├── src/
│   ├── hardware/       # SDR device abstraction (RTL2832U, future devices)
│   ├── signal/         # DC removal, windowing
│   ├── fft/            # FFT & spectral analysis (PocketFFT)
│   ├── visualization/  # Spectrum & waterfall rendering logic
│   ├── gui/            # SDL3 window and event management
│   └── utils/          # Logging, config, utilities
├── include/
└── third_party/        # stb_image_write (PNG export), pocketfft
```

A single-threaded render loop is fed by a producer thread — the RTL-SDR async
callback, or an `IqPlaybackSource` under `--play` that replays a capture in real
time. Both push FFT-sized frames into a bounded queue; the renderer drains to
the newest each paint (a frame-rate throttle that prevents latency buildup). The
signal chain downstream of the callback is source-agnostic. See CLAUDE.md for
the full pipeline diagram and the SIMD hot-path invariants.

## Performance & footprint

The pipeline is **render-backend-bound, not DSP-bound.** Measured:

- **CPU** (DC removal + window + FFT + display update) is sub-2 ms even at FFT
  16384 on a 15 W i5-7300U. Further FFT/SIMD work is not warranted.
- **Latency** is frame-paced: ~16.7 ms/frame at 60 fps vsync. Per-frame compute
  is a small fraction of that and stays roughly flat across FFT sizes, so the
  frame rate holds at the cap from 4096 through 16384.
- **Memory** under 49 MB resident at FFT 16384 (native Windows / Direct3D 11,
  v3.7.0), lower at smaller sizes. Frame pools are bounded; the waterfall stores
  one byte per cell (4:1 vs float); the ~1 MB IQ write buffer exists only while a
  capture is running.
- **Renderer** — keep the default backend: Direct3D 11 (Windows), OpenGL
  (Linux), both hold a clean 60 fps at ~1 ms CPU. **Don't force Vulkan on old
  Intel iGPUs** (HD 6xx / Gen9): roughly half the frame rate for no benefit. The
  software renderer works and warns loudly but rasterizes on a single CPU core —
  use `--max-fps 30` to cut its power and thermal cost.

> A Linux/WSL2 memory profile reads much higher — most of it is the Mesa
> software-GL backing store from running without GPU passthrough, an artifact of
> the environment, not heap growth. The Direct3D 11 build keeps those surfaces
> in video memory, so the <49 MB Direct3D 11 figure is the representative one.

### Benchmark: FFT size vs. renderer

Measured on a **ThinkPad T470 (i5-7300U, Intel HD 620, 16 GB)** under Ubuntu,
commit `148b0a4` (representative of v3.7.0), replaying a 21 s capture with
`--play`, **vsync off / `--max-fps 0`**, stepping FFT size `1`→`5` live. Values
are steady-state averages of the per-second `FRAME-TIMING` log line; the first
~1 s after each size change is dropped as warm-up. SMT was left
enabled, so expect minor run-to-run variance — not order-of-magnitude.

Representative raw lines (timestamp/thread prefix stripped):

```
# OpenGL — FFT 1024
FRAME-TIMING fps=530.5 frames=531 | cpu avg=0.06 | render_build avg=1.42 | present avg=0.23 (ms)
# OpenGL — FFT 16384
FRAME-TIMING fps=125.1 frames=126 | cpu avg=1.56 | render_build avg=3.46 | present avg=0.38 (ms)
# Software — FFT 1024
FRAME-TIMING fps=280.2 frames=281 | cpu avg=0.04 | render_build avg=2.92 | present avg=0.52 (ms)
# Software — FFT 16384
FRAME-TIMING fps=121.8 frames=122 | cpu avg=0.59 | render_build avg=3.99 | present avg=0.63 (ms)
```

| FFT | Data rate¹ | OpenGL fps | Software fps | cpu² (ms) | Limited by |
|----:|-----------:|-----------:|-------------:|----------:|------------|
| 1024  | 2000 | ~530 | ~277 | 0.05 | renderer |
| 2048  | 1000 | ~529 | ~262 | 0.10 | renderer |
| 4096  |  500 | ~498 | ~257 | 0.15 | renderer (OpenGL at data rate) |
| 8192  |  250 | ~241 | ~248 | 0.35 | data rate |
| 16384 |  125 | ~124 | ~120 | 1.6  | data rate |

¹ `rate / FFT` = the real-time frame supply from `--play` at 2.048 MS/s — a hard
ceiling, since the renderer can't draw a frame that doesn't exist yet.
² CPU = DC removal + window + FFT + display update; backend-independent.

Two regimes, and the crossover is the whole story:

- **Renderer-bound (small FFT).** The faucet supplies more frames than the GPU
  can draw, so the backend sets the ceiling: OpenGL ~530 fps, software ~260 fps —
  **OpenGL ~2× faster**. The cost is `render_build` (~1.4 ms OpenGL vs ~3.0 ms
  software); CPU/DSP is negligible (<0.2 ms). FFT size barely moves the ceiling.
- **Data-bound (large FFT).** `rate / FFT` drops below both renderers' ceilings,
  so both converge to the data rate — at 16384, ~120–125 fps **regardless of
  backend**. The renderer choice stops mattering; you're waiting on the real-time
  faucet (see [FFT size, resolution, and latency](#fft-size-resolution-and-latency)).
- **Crossover** is where `rate / FFT` dips under the renderer ceiling: ~4096 for
  OpenGL, ~8192 for software.

**Power note.** "Smaller FFT = higher fps" holds only in the renderer-bound
regime — and higher fps means *more* GPU/battery draw, not less. For long
unattended runs the lever points the other way: `--max-fps 30` pins every size to
30 fps, at which point the per-frame CPU gap (0.05 ms at 1024 vs 1.6 ms at 16384)
is noise. So `--max-fps` is the real power control; FFT size is primarily a
resolution/latency choice.

### Case study: the panel-text render regression (v3.8.0 → v3.8.1)

v3.8.0 consolidated all status / PEAK / timing text into the right-hand
instrument panel. Every label and value was drawn through `blit_text`, which
**created, rasterized, uploaded, and destroyed an `SDL_Texture` per string, per
frame** — ~15–40 times a frame. v3.7.0's overlays were cached textures. The cost
was invisible in fps (vsync pins the frame rate regardless), but it was real
render work and therefore real power — design priority #2.

Measured across v3.7.0 (`6c8e173`), v3.8.0 (`7785c84`), and the v3.8.1 fix,
**hyperthreading disabled**, replaying an FM capture with `--play`. The shipped
builds hardcode **vsync on**, so fps is pinned and the signal lives in
`render_build` and CPU counters, not frame rate. Direct3D 11 figures are from an
**i7-12700H** — the churn is worst on an accelerated backend, where the baseline
render is tiny; the Linux `perf stat` is from the **T470 (i5-7300U)**.

On Direct3D 11 and the software renderer the vsync idle-wait lands in `present`,
so `render_build` is real work — the metric that exposes the regression. (On
OpenGL the idle lands in `render_build` and masks it; read total CPU counters
there instead.)

| `render_build` (ms) — Direct3D 11 / i7-12700H | v3.7.0 | v3.8.0 | v3.8.1 |
|---|---:|---:|---:|
| `--max-fps 30` | 2.95 | 5.80 | **2.74** |
| `--max-fps 60` | 2.30 | 4.09 | **2.16** |

v3.8.0 nearly doubled `render_build`; v3.8.1 returned it *below* v3.7.0. The
backend-independent picture is the same — `perf stat` over an identical ~34.5 s
workload (T470, OpenGL, FFT 16384, `--max-fps 60`):

| Counter (same ~34.5 s workload) | v3.7.0 | v3.8.0 | v3.8.1 |
|---|---:|---:|---:|
| instructions | 10.93 B | 17.57 B | **9.78 B** |
| cycles | 10.39 B | 17.60 B | **8.12 B** |
| task-clock | 15.56 s | 20.59 s | **12.71 s** |

v3.8.0 spent **+61% instructions / +69% cycles** for pixel-identical output;
v3.8.1 cut instructions 44% and cycles 54% below v3.8.0, landing under v3.7.0 —
the panel now renders cheaper than the pre-panel UI did. On the same Windows box,
Task Manager tracked it in power terms at FFT 16384: CPU / GPU roughly
1% / 4.5% (v3.7.0) → 2% / 9% (v3.8.0) → 0.8% / 3.5% (v3.8.1).

The fix is two changes:

- **`SdlRenderer::blit_text` caches the rendered string-texture**, keyed by
  colour + string; a per-frame mark-and-sweep evicts strings not drawn last
  frame, bounding the cache to what's on screen. Unchanged labels/values are
  rasterized once, not every frame — and it covers every overlay caller, not
  just the panel.
- **`TextRenderer::render_text` allocates the glyph texture as
  `SDL_TEXTUREACCESS_STATIC`, not `TARGET`.** It is only ever sampled, so the
  render-target view `TARGET` reserves is dead weight — heaviest on Direct3D 11.

On the software renderer the win is smaller (`render_build` 9.7 → 6.7 ms at
`--max-fps 30`, part of it shifting into `present`): text rasterization was never
the bottleneck there, the whole-scene raster dominates.

> **Maintainer takeaway.** Never create/destroy an `SDL_Texture` per frame for
> content that rarely changes — cache it and upload only on change, the pattern
> `render_frequency_scale` already uses for the frequency scale. Under vsync a
> render/power regression like this is invisible in fps; watch `render_build`
> (Direct3D 11 / software) or a `perf stat` instruction count, not the frame rate.

## Adding a new SDR device

The device sits behind a narrow surface, so adding hardware doesn't touch the
rest of the pipeline. There is one concrete backend today, `RtlSdrDevice`
(`src/hardware/rtl_sdr_device.{h,cpp}`), and no abstract base class yet —
extracting an `SdrDeviceBase` is the natural refactor once a second device
lands. Until then a new backend simply mirrors `RtlSdrDevice`'s public interface.

### 1. Mirror the interface

Feed the pipeline **asynchronously**: register a callback, and the device's
producer thread pushes pooled `FrameHandle`s of IQ samples as they arrive.

```cpp
// src/hardware/new_sdr_device.h
#pragma once
#include "openspectrum/frame_pool.h"
#include <functional>

class NewSdrDevice {
public:
    bool open();
    void close();
    bool is_open() const;

    void set_frequency(uint32_t freq_hz);
    void set_sample_rate(uint32_t rate_hz);
    void set_gain(float gain_db);

    // Register a sink, then start. The device owns its producer thread and
    // hands the pipeline cache-aligned FrameHandles.
    using FrameCallback = std::function<void(openspectrum::FrameHandle)>;
    void set_frame_callback(FrameCallback cb);
    void start_streaming(size_t buffer_count = 8);
    void stop_streaming();
};
```

### 2. Wire it in

`main.cpp` constructs the device, points its frame callback at
`async_sample_callback`, and starts streaming. Swapping in your backend is the
only change — everything downstream consumes `FrameHandle`s and is
source-agnostic. (The `--play` path proves it: it drives the same callback with
no device at all.) The processor, FFT, and visualization stay untouched.

## Testing & hardware checks

There is no automated test suite — OpenSpectrum is a real-time GUI/SDR app whose
behavior is verified by running it against live hardware or a recorded capture
(`--play`). Confirm the device is seen first:

```bash
rtl_test -t       # list available RTL-SDR devices
rtl_eeprom        # device info
```

## Security hardening

Builds compile with these flags by default:

| Flag | Purpose |
|------|---------|
| `-D_FORTIFY_SOURCE=2` | Buffer overflow protection |
| `-fstack-protector-strong` | Stack canary protection |
| `-Wl,-z,now` | Disable lazy binding |
| `-Wl,-z,relro` | Relocation read-only |
| `-Wl,-z,noexecstack` | Non-executable stack |
| `-Wall -Wextra -Wpedantic` | Comprehensive warnings |

Linker hardening is Linux-side; the Windows build links static libs instead —
see CLAUDE.md's platform table.
