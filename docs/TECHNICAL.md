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
| 2.048 MS/s | 65536 | 31.25 Hz/bin | 32 ms |
| 1.024 MS/s | 16384 | 62.5 Hz/bin | 16 ms |

Bigger `N` → finer frequency, coarser time, more latency. That trade is fixed.
Past the point where `Δt` exceeds the signal's coherence time, extra bins just
show the same noise at tighter spacing — no new information.

**There is a second, harder ceiling: the display.** At 2.048 MS/s across a
~1400 px spectrum pane, one pixel already covers ~1.4 kHz — so 16384's 125 Hz
bins are already ~11× finer than anything that can be drawn, and 65536's 31 Hz
bins are ~45× finer. Those extra bins are not wasted: the display averages every
bin landing in a pixel column, and more bins per column means a smoother noise
floor. But they buy **smoothness, not visible detail**. This is worth knowing
before comparing against other analyzers — a program running a far larger FFT
over the same span is usually buying averaging, not resolution you can see, and
the same smoothness is available far more cheaply from the averaging the display
already does (see *Amplitude scale and autoscaling*). Resolution below
~1 kHz/pixel only becomes visible with a zoom/span control, which this program
does not have.

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

### Amplitude scale and autoscaling

Both panes autoscale, and both anchor their bottom on the **median** bin, not the
minimum. A periodogram bin of Gaussian noise is exponentially distributed, so the
*minimum* over N bins is an extreme-value statistic with no physical meaning: at
16384 bins the deepest null lands ~42 dB below the real noise floor and re-rolls
every frame. Anchoring there stretched the pane across ~90 dB (half of it empty),
pinned the floor at mid-screen, and made the axis labels jitter. The median sits a
fixed 1.6 dB below the floor's mean power and is stable to ~0.05 dB. The top
tracks the peak (`max + 5 dB`) — `max` is signal-dominated and well behaved.

The y-axis is **signed dBFS**: `-83dB` means 83 dB below full scale, the same
reference as the PEAK readout above.

The drawn trace is the **mean** of the bins falling in each pixel column, not the
peak. Averaging is what makes the noise floor read as a line rather than a band of
grass — a peak detector over ~12 bins/column reads ~4.9 dB high and still spreads
~1.8 dB. Max-hold (`m`) is the peak detector when you want one. Video averaging
(`a`, a per-bin EMA) is **on by default**; the panel's TRACE field shows `AVG`.
Together they put the noise floor at ~0.5 dB of spread.

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

The pipeline is **render-backend-bound, not DSP-bound — through FFT 16384.**
Measured:

- **CPU** (DC removal + window + FFT + display update) is sub-2 ms even at FFT
  16384 on a 15 W i5-7300U. Further FFT/SIMD work is not warranted at the
  default sizes.

> **`cpu` is not a measure of how much work the code does.** Read it as
> work ÷ whatever clock the governor picked. `--max-fps 30` leaves the CPU idle
> ~97% of the time, so it never leaves its lowest P-state: the T470 `perf stat`
> runs below report **0.7–0.8 GHz** on a part with a 2.6 GHz base and 3.5 GHz
> turbo. Same binary, same input, i7-12700H at FFT 65536: **0.95 ms in a tight
> loop vs 2.37 ms at a 30 fps duty cycle** — 2.5× for identical instructions,
> from clock ramp plus caches going cold across the 32 ms idle. Frame-to-frame
> jitter is ±1.6 ms (p10 1.25 / p90 2.89) and is the governor, not the workload.
>
> Consequences, in order of how often they get this wrong:
> 1. **Fine for comparing configurations** — both sides inflate alike, so
>    "65536 costs more than 32768" holds. That is what the tables below are for.
> 2. **Useless as a work budget.** The real per-frame DSP at 65536 is ~1.14 ms,
>    not 3.25. Optimizing against the reported number is chasing the governor:
>    the fixed clock/cold-cache penalty is ~60% of it and no code change touches it.
> 3. **The headroom is far larger than "25% of budget" suggests** — that figure
>    is measured while idling at a quarter of base clock. There is ~4× of clock
>    in reserve the workload never asks for.
>
> Measure real work in a tight loop against the project sources (that is where
> the 1.14 ms breakdown below comes from); measure *pacing* with `FRAME-TIMING`.
> Do not mix the two — dividing a tight-loop number by a `FRAME-TIMING` number
> is how this section previously concluded the FFT was 23% of `cpu` when it is
> 71% of the work.
- **The opt-in 32768/65536 sizes invert that.** On an i7-12700H / Direct3D 11
  at 2.048 MS/s, CPU is **0.80 / 1.55 / 3.25 ms** at 16384 / 32768 / 65536
  while `render_build` holds ~1.5 ms — so **65536 is the one configuration
  where this pipeline is DSP-bound**. Roughly 4× the CPU of 16384 (2.4% → 9.8%
  of one core). Both sides are governor-inflated (see above), so the *ratio*
  holds even though the absolute values do not.

**Where the work actually is.** Per-frame stages at FFT 65536, tight loop,
i7-12700H, measured against the project sources — the breakdown `cpu` cannot
give you. This is the list to check before proposing any DSP optimization:

| Stage | ms | share |
|-------|---:|------:|
| pocketfft transform | 0.812 | **71%** |
| `update_spectrum` (`nth_element`) | 0.142 | 12% |
| sign-trick fill + dB/log10 loop | 0.067 | 6% |
| `get_max_db` | 0.058 | 5% |
| `apply_window` | 0.029 | 3% |
| `add_spectrum_line` | 0.017 | 1% |
| `remove_dc` | 0.014 | 1% |
| **total real work** | **1.14** | |

The transform dominates; everything around it is already AVX2 and already
negligible. `get_max_db` is the one unvectorized loop left (a scalar max
reduction — GCC will not auto-vectorize float max without `-ffast-math`), and
it is worth 0.05 ms of a 33 ms budget. There is no DSP optimization left that
pays for its own code.

### The plan cache: less work, worse `cpu` — the worked example

PocketFFT defaults `POCKETFFT_CACHE_SIZE` to 0, rebuilding the plan (factorize +
twiddles + ~512 KB alloc/free at 65536) on every call, 30×/s.
`src/fft/pocketfft_wrapper.h` sets it to 1. Measured on the **T470 (i5-7300U),
FFT 65536, `--max-fps 30`, 40 s**, against the v3.9.0 baseline in `logs/`:

| | v3.9.0 | v3.9.1 | |
|---|---:|---:|---|
| page-faults | 271,568 | 9,000 | −97% |
| instructions | 15,596M | 12,763M | −18% |
| cpu-cycles | 12,419M | 9,955M | −20% |
| sys time | 2.34 s | 1.27 s | −46% |
| task-clock | 14,628 ms | 12,611 ms | −14% |
| **`cpu` (wall)** | **6.67 ms** | **7.73 ms** | **+16%** |
| fps | 30.0 | 30.0 | — |

**Strictly less work by every hardware counter, and 16% more wall-clock.** The
effective clock through the DSP block falls **849 → 789 MHz**: removing sustained
work gives the governor less reason to ramp, so what remains runs slower. `cpu` is
wall-clock, so it reports the slowdown and conceals the −20% cycles. The +16% is
not noise — the interquartile ranges do not overlap, and 32768 shifts the same way
(4.05 → 4.57, 705 → 680 MHz).

This is what the caveat above means in practice, and it is why the change is kept:
nothing waits on `cpu` (fps is capped and identical, the frame uses ~9.6 ms of 33,
and at 65536 the sample window is itself 32 ms — so +1 ms of DSP is ~1.5% of the
real latency chain), while −20% cycles at a lower clock is less energy. **It is a
power win that the headline performance metric reports as a regression.**

Two prediction failures worth inheriting, both from this one change:
1. A **tight-loop micro-benchmark** said 0.830 → 0.715 ms — meaningless here.
2. A **WSL2 / i7-12700H A/B of the real app** said `cpu` −26%. The T470 said
   **+16%**. A different governor and clock range flipped the **sign**, not just
   the magnitude. Deltas do not port across machines — measure on the target.
   ("WSL2 predicts within 5%" is about *absolute* values on the *same* CPU.)
- **Latency** is frame-paced: ~16.7 ms/frame at 60 fps vsync. Per-frame compute
  is a small fraction of that, so the frame rate holds at the cap from 4096
  through **65536**. It does not hold beyond: 131072 needs four device
  callbacks per frame, capping the line rate at ~15.6/s (measured 15.4–16.0
  fps), which is why it is not a selectable size.
- **65536's fps headroom is ~4%, and it is arithmetic, not performance.** The
  `--play`/device frame supply is `rate / fft_size`: at 2.048 MS/s that is
  **31.25 frames/s at 65536**, against the default 30 fps cap. So the locked
  30.0 fps measured there is the cap sitting just under the supply — **raising
  `--max-fps` above 31 does nothing at that size**, and any drop in supply shows
  up immediately as dropped frames. This is the same wall that excludes 131072
  (15.6/s); 65536 is simply the last size that clears the default cap. It is not
  a CPU limit — the frame uses ~9.6 ms of 33 ms on a T470.
- **Memory** ~51 MB resident at FFT 16384 (native Windows / Direct3D 11,
  measured at `99257e3`; the older "<49 MB" figure was v3.7.0). It is bounded
  but **scales mildly with FFT size** — the frame pool is 12 frames ×
  `fft_size` × 8 B, measured **+8 MB going 4096 → 65536**. The waterfall stores
  one byte per cell (4:1 vs float); the ~1 MB IQ write buffer exists only while
  a capture is running.
- **Renderer** — keep the default backend: Direct3D 11 (Windows), OpenGL
  (Linux), both hold a clean 60 fps at ~1 ms CPU. **Don't force Vulkan on old
  Intel iGPUs** (HD 6xx / Gen9): roughly half the frame rate for no benefit. The
  software renderer works and warns loudly but rasterizes on a single CPU core —
  use `--max-fps 30` to cut its power and thermal cost.

> A Linux/WSL2 memory profile reads much higher — most of it is the Mesa
> software-GL backing store from running without GPU passthrough, an artifact of
> the environment, not heap growth. The Direct3D 11 build keeps those surfaces
> in video memory, so the ~51 MB Direct3D 11 figure is the representative one.

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
is noise. So `--max-fps` is the real power control, and **through 16384** FFT
size is primarily a resolution/latency choice.

That stops being true at the opt-in sizes. At 65536 the per-frame CPU is ~3.25
ms on an i7-12700H (and roughly double that on a T470-class part) — no longer
noise against a 33 ms budget, and ~4× the DSP load of 16384 for a resolution
gain the pane cannot render (see below). **At 32768/65536, FFT size is a real
power lever**; pick them for narrow-signal work, not as a default.

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

These flags guard the program's actual input surface — `.iq` captures, the
`.meta.json` sidecar, and the RTL-SDR USB stream — where a malformed file or a
glitching device could otherwise turn a parser slip into a memory-safety issue.
There is no network listener, no elevated privilege, and no setuid surface, so
nothing beyond this near-zero-cost baseline is warranted (don't add to it, don't
strip it). The flags are *defense-in-depth*; prevention lives in the parsers:
the `.meta.json` scrape is size-bounded and rejects a zero sample rate
(`iq_playback.cpp`), CLI numbers are type- and range-checked — FFT size against
an explicit allow-list, so a pathological `2^30` cannot reach the frame pool and
the CLI cannot select a size the UI and docs do not offer (`arg_parser.cpp`,
`main.cpp`), and `.iq` samples are screened for NaN/Inf before entering the DSP
pipeline.
