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

At 2.048 MS/s with the default `--max-fps 30`, the pipeline holds **30.0 fps at
every selectable FFT size, 1024 through 65536**, on a 15 W i5-7300U. CPU is not
the limit at any size. The rest of this section is how that was measured, what
the numbers mean, and — more often — what they do not.

### Reading `cpu`

`cpu` in the `FRAME-TIMING` line is wall-clock around DC removal + window + FFT
+ display update. It is **work ÷ whatever clock the governor chose**, not a
measure of how much work the code does.

`--max-fps 30` leaves the CPU idle ~97% of the time, so it never leaves its
lowest P-state. Effective clock (cycles ÷ task-clock) across the sweep below, on
a part with a **2.6 GHz base / 3.5 GHz turbo**:

| FFT | 1024 | 2048 | 4096 | 8192 | 16384 | 32768 | 65536 |
|-----|-----:|-----:|-----:|-----:|------:|------:|------:|
| clock | 350 MHz | 420 MHz | 413 MHz | 454 MHz | 484 MHz | 587 MHz | 749 MHz |

**13–29% of base, and it rises with the workload.** Three consequences:

1. **`cpu` is not comparable across FFT sizes.** A 65536 frame runs on a core
   clocked 2.1× faster than a 1024 frame. Any "cpu scales as N log N" reading is
   measuring the governor's response as much as the transform.
2. **`cpu` is not a work budget.** Most of it is clock state. No code change
   touches that, so optimising against the number is chasing the governor.
3. **A same-size, same-session A/B is the only clean use** — and even then the
   run-to-run spread reaches 13–18% at 32768/65536 (see below), which is larger
   than most changes worth making.

Corollary: never divide a tight-loop measurement by a `FRAME-TIMING` one. They
are different regimes; the same work measures ~2.5× apart between them.

And the converse, which is easier to get wrong: **a number measured on another
machine is not a wrong number.** `cpu` at FFT 65536 is ~3.3 ms on an i7-12700H
and ~7 ms on an i5-7300U; both are correct, and neither refutes the other. Before
retracting a figure, check the machine it was labelled with — every performance
claim in this file names its box for that reason. Retract on evidence from the
same box, not from a different one.

### Live testing: FFT sweep, v3.9.0 vs v3.9.1

**Method.** `./bench.sh <binary> <tag>`, both binaries in one session on one
box. 7 sizes × 2 reps × 40 s, `--play` of a 21 s capture at 2.048 MS/s,
`--max-fps 30`, SIGINT shutdown. Counters are exact (the event set fits the PMU;
nothing multiplexed). Conditions recorded per run in `logs/<tag>-conditions.txt`.

**Box.** ThinkPad T470, i5-7300U (2.6 GHz base), Intel HD 620, OpenGL,
**SMT disabled**, `powersave` / `intel_pstate`, Ubuntu. Taken at the then-default
**1050×576** canvas; the shipped canvas is now a fixed 1280×720 (~1.5× the
pixels). `cpu` here is DSP-side (DC + window + FFT + display update) and barely
moves with canvas size — the raster cost lives in `render_build`/`present`, which
rise sub-linearly and still hold 30.0 fps.

`cpu` per run median, ms — both runs shown, because the spread is the point:

| FFT | v3.9.0 | v3.9.1 | verdict |
|-----|--------|--------|---------|
| 1024 | 0.13, 0.12 | 0.11, 0.11 | v3.9.1 faster |
| 2048 | 0.21, 0.22 | 0.19, 0.19 | v3.9.1 faster |
| **4096** (default) | 0.53, 0.55 | **0.47, 0.47** | **v3.9.1 faster** |
| 8192 | 1.42, 1.34 | 0.95, 0.94 | v3.9.1 faster |
| 16384 | 2.64, 2.60 | 2.53, 1.87 | v3.9.1 faster |
| 32768 | 4.42, 4.83 | 3.77, 4.44 | unresolved — runs overlap |
| 65536 | 7.06, 6.85 | 6.73, 7.63 | unresolved — runs overlap |

`fps` is **30.0 at every size in both builds**. `render_build` 0.98–1.61 ms and
`present` 0.93–1.62 ms, both flat across FFT size in both builds — render cost is
independent of bin count (the scroll path uploads one line, not the pane).

"Unresolved" means the two runs of each build interleave: at 32768/65536 the
run-to-run spread (13–18%) exceeds any effect. It does not mean *equal*. Two reps
is thin; raise `REPS` before claiming anything there.

### Live testing: Windows / Direct3D 11, live device

**Corroborating only — the T470 sweep above is the primary dataset.** Spot check
of the shipped Windows build (v3.9.1, `a772ae7`) against a live RTL-SDR, not
`--play`: i7-12700H / Direct3D 11 / `--max-fps 30`, **SMT enabled** (locked-down
machine, BIOS not accessible), one session, two sizes, no reps, and **no `perf`**
— so no counters, and wall-clock is all there is. Weigh it accordingly: it is
enough to confirm a figure previously measured on the same box under the same
conditions, and not enough to build on. Per-second `FRAME-TIMING` medians:

| FFT | cpu | render_build | present | windows |
|-----|----:|-------------:|--------:|--------:|
| 4096 (default) | **0.13** (0.25 incl. waterfall fill) | 1.15 | 0.46 | 13 |
| 65536 | **3.32** (p25–p75 3.23–3.38) | 1.26 | 0.51 | 73 |

Two things worth taking from it:

- **The crossover is real and lands between 16384 and 32768, on both machines.**
  At the 4096 default the render stage dominates the DSP by ~12× on Windows
  (1.61 vs 0.13) and ~6× on the T470 (3.08 vs 0.47). At 65536 it inverts: the
  DSP is **1.9× the render stage** on Windows (3.32 vs 1.77) and ~3.8× on the
  T470 (7.24 vs 1.91). So *render-bound through 16384, DSP-bound at 32768/65536*
  is an accurate description of this pipeline — read "bound" as *which stage
  dominates*, not as *what fails*: the frame rate is 30.0 either way.
- **Measurement noise is machine-dependent.** The 12700H holds a 4.6%
  interquartile spread at 65536 where the i5-7300U spreads 13–18%. A faster part
  that ramps quickly is also a quieter instrument — do not assume a spread
  measured on one box transfers to another.

### Finding: PocketFFT plan cache (v3.9.1)

PocketFFT defaults `POCKETFFT_CACHE_SIZE` to 0, which reconstructs the plan —
factorize, recompute the twiddle tables, allocate and free them — on **every**
`c2c` call, 30×/s, for a plan that never changes. `src/fft/pocketfft_wrapper.h`
sets it to 1 (one FFT size is live at a time; a 10-entry cache measured no faster
for 7× the twiddle memory).

**Result: it helps the small sizes, including the 4096 default, and does nothing
at the large ones.** The default drops 0.55 → 0.47 ms (−14%); 32768 and 65536 are
unresolved.

That is what theory predicts, and it is the reason to expect it: **the rebuild is
O(N) twiddle computation against an O(N log N) transform**, so it is
proportionally largest where the transform is cheapest, and drowns as N grows.

What it does **not** do, all measured, all previously claimed in this file and
wrong:

- **It is not a 65536 optimisation.** At 65536 the effect is unresolvable.
- **It does not cut cycles.** Instructions fall monotonically (−1.5% at 1024 to
  −12.7% at 65536) while cycles move −3.4%…+0.8%. The removed work was cheap,
  high-IPC ALU work absorbed in spare issue slots. Fewer instructions ≠ less time.
- **It does not fix a page-fault storm.** At 65536: 164,468 → 161,898 (−1.6%).
  The recurring faults are PocketFFT's per-call *scratch* buffer crossing glibc's
  mmap threshold, which the plan cache does not touch. Fault counts are also
  bimodal across runs — glibc's dynamic mmap threshold adapts to allocation
  history — so treat them as an unstable metric.
- **It is not a power win.** Cycles are flat; there is no energy saving to claim.

Kept because it measurably improves the default size, costs one `#define`, and
no size shows a reproducible regression.

### Where the per-frame work is

Per-stage cost at FFT 65536, **tight loop, i7-12700H**, measured against the
project sources. These are *relative shares of real work* — not per-frame cost on
any machine, and not comparable to the `cpu` figures above, which are duty-cycled
and governor-limited. Check this list before proposing any DSP optimisation:

| Stage | ms | share |
|-------|---:|------:|
| pocketfft transform | 0.812 | **71%** |
| `update_spectrum` (`nth_element`) | 0.142 | 12% |
| sign-trick fill + dB/log10 loop | 0.067 | 6% |
| `get_max_db` | 0.058 | 5% |
| `apply_window` | 0.029 | 3% |
| `add_spectrum_line` | 0.017 | 1% |
| `remove_dc` | 0.014 | 1% |
| **total** | **1.14** | |

The transform dominates; everything around it is already AVX2 and already
negligible. `get_max_db` is the only unvectorized loop left (a scalar max
reduction — GCC will not auto-vectorize float max without `-ffast-math`) and is
worth 0.05 ms of a 33 ms frame. There is no DSP optimisation left that pays for
its own code.

### Latency and the frame-supply ceiling

Latency is frame-paced: ~16.7 ms/frame at 60 fps vsync, ~33 ms at `--max-fps 30`.
Per-frame compute is a fraction of that at every size, so the rate holds at the
cap from 1024 through 65536.

**65536's headroom is ~4%, and it is arithmetic, not performance.** Frame supply
is `rate / fft_size` = **31.25/s** at 2.048 MS/s, against the 30 fps default cap.
The locked 30.0 fps there is the cap sitting just under the supply, so raising
`--max-fps` above 31 does nothing at that size and any dip in supply drops frames
immediately. Same wall excludes 131072: four device callbacks per frame cap the
line rate at ~15.6/s (measured 15.4–16.0). 65536 is simply the last size that
clears the default cap. Neither is a CPU limit.

### Memory

~51 MB resident at FFT 16384 (native Windows / Direct3D 11, measured at
`99257e3`; the older "<49 MB" figure was v3.7.0). Bounded, but **scales mildly
with FFT size** — the frame pool is 12 frames × `fft_size` × 8 B, measured
**+8 MB going 4096 → 65536**. The waterfall stores one byte per cell (4:1 vs
float); the ~1 MB IQ write buffer exists only while a capture is running.

> A Linux/WSL2 memory profile reads much higher — mostly the Mesa software-GL
> backing store from running without GPU passthrough, an artifact of the
> environment, not heap growth. Direct3D 11 keeps those surfaces in video memory,
> so the ~51 MB figure is the representative one.

### Renderer

Keep the default backend: Direct3D 11 (Windows), OpenGL (Linux). Both hold a
clean 60 fps at ~1 ms CPU.

- **Don't force Vulkan on old Intel iGPUs** (HD 6xx / Gen9): roughly half the
  frame rate for no benefit.
- **Software renderer** is the battery/thermal worst case but ~2.5× OpenGL, not a
  disaster: `render_build` + `present` ≈ **5.9 ms** (3.17 + 2.77; vsync off there,
  so `present` is real blit work) against ~2.3 ms on OpenGL — T470 / Linux /
  1050×576 / `--max-fps 30` / FFT 32768. The gap is a flat **+3.6 ms/frame and
  does not grow with FFT size**. Still single-core, still warns loudly, still a
  fallback. An earlier "~16 ms" figure does not reproduce; its conditions are
  unknown. The v3.8.1 case-study figure below (9.7 → 6.7 ms) is a different
  machine and is not reconcilable with this one — re-measure per box.

### Benchmark: FFT size vs. renderer

Measured on a **ThinkPad T470 (i5-7300U, Intel HD 620, 16 GB)** under Ubuntu,
commit `148b0a4` (representative of v3.7.0), replaying a 21 s capture with
`--play`, **vsync off / `--max-fps 0`**, stepping FFT size `1`→`5` live. Values
are steady-state averages of the per-second `FRAME-TIMING` log line; the first
~1 s after each size change is dropped as warm-up. SMT was left
enabled, so expect minor run-to-run variance — not order-of-magnitude.

> **These `cpu` values are not comparable to the capped sweep above, and the gap
> is a useful control.** Uncapped, the CPU is saturated, so the governor ramps.
> Same box, same FFT 16384: **1.6 ms here vs 2.60 ms at `--max-fps 30`** — the
> same work, 1.6× apart, purely from clock state. Read this table for the
> *renderer-vs-data-rate crossover* it was built to show; do not read its `cpu`
> column as the cost of the DSP.

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
30 fps. **`--max-fps` is the real power control**; FFT size is a second-order one.

Sizing that against cycles rather than wall-clock (the sweep above, T470, whole
process over 40 s, so it includes the producer thread and GL):

| FFT | 4096 | 16384 | 32768 | 65536 |
|-----|-----:|------:|------:|------:|
| cycles | 3,163 M | 4,106 M | 5,733 M | 9,125 M |
| vs 4096 | — | +30% | +81% | **+189%** |

So through 16384 FFT size is primarily a resolution/latency choice costing ~30%
more cycles than the default. **At 32768/65536 it becomes a real power lever** —
65536 is ~2.9× the default's cycles, and it also drags the core to a higher clock
(413 → 749 MHz), which costs energy again on top. Pick the opt-in sizes for
narrow-signal work, not as a default, and see
[FFT size, resolution, and latency](#fft-size-resolution-and-latency) for why the
resolution they buy is not visible on the pane.

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

> **This is the measurement to copy.** It compares *counters* over a fixed
> workload with SMT disabled, not wall-clock — which is why its conclusion still
> stands while every wall-clock claim in this file has had to be redone. A ±60%
> swing in instructions is real regardless of what clock the governor picked; a
> ±16% swing in `cpu` usually is not.

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
