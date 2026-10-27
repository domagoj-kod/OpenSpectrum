# OpenSpectrum — Measurements

The evidence behind the numbers that [TECHNICAL.md](TECHNICAL.md), CLAUDE.md
and the code comments rely on. Every entry names its date, machine, build and
conditions; a figure without them does not belong here. Newest first.

**Summary.** At 2.048 MS/s and the default `--max-fps 30`, every FFT size from
1024 to 65536 holds 30.0 fps on a 15 W i5-7300U. CPU is not the limit at any
size, and FFT size barely moves power: the app adds ~0.7 W of package power
at every size, 65536 only ~0.07 W more than 4096.

## Machines

| Name | CPU | Renderer | OS | Notes |
|---|---|---|---|---|
| T470 | i5-7300U, 15 W, 2.6 GHz base / 3.5 GHz turbo | Intel HD 620, OpenGL | Ubuntu | `powersave` / `intel_pstate`; SMT off unless noted |
| T470 (Arch) | the same box | Intel HD 620, OpenGL, native Wayland (KWin) | Arch, kernel 7.2.11, KDE Plasma | `powersave` / `intel_pstate`, EPP `balance_power`, on battery; SMT `notsupported` |
| 12700H | i7-12700H | Direct3D 11 (Windows 11); OpenGL through WSLg (WSL2) | Windows 11, WSL2 | SMT on (BIOS locked); no `perf` under WSL2 |

## How to measure

- Same machine, same session, A and B alternated; at least three runs before
  a number becomes a finding. A delta measured on one machine does not port to
  another (two cases below: the plan cache and fast-math).
- Linux: `./bench.sh <binary> <tag>` sweeps the FFT sizes over a `--play`
  capture and logs `FRAME-TIMING` plus `perf` counters and the run conditions.
  Where Intel RAPL exists it also logs package energy, with an idle stretch per
  rep to subtract. Run it as yourself: it calls `sudo` for the counter, and
  `sudo ./bench.sh` drops `WAYLAND_DISPLAY`, so SDL silently falls back to X11.
  Windows: `packaging/play-bench.ps1`. USRP losses: `packaging/uhd-sweep.ps1`.
- Prefer counters (instructions, cycles) over a fixed workload to wall clock.
  The panel-text entry below is the one to copy.
- `render_build` and `present` hold the vsync wait on different sides per
  backend (OpenGL in `render_build`, Direct3D 11 and software in `present`):
  compare them only on one backend.

### Reading `cpu`

`cpu` in the `FRAME-TIMING` line is wall clock around DC removal, window, FFT
and display update: **work divided by whatever clock the governor chose**. At
`--max-fps 30` the CPU idles ~97% of the time and stays in its lowest P-states,
so the clock rises with the workload (T470, 2026-07-15):

| FFT | 1024 | 2048 | 4096 | 8192 | 16384 | 32768 | 65536 |
|-----|-----:|-----:|-----:|-----:|------:|------:|------:|
| clock | 350 MHz | 420 MHz | 413 MHz | 454 MHz | 484 MHz | 587 MHz | 749 MHz |

So:

1. `cpu` is not comparable across FFT sizes: a 65536 frame runs on a core
   clocked 2.1× faster than a 1024 frame.
2. `cpu` is not a work budget; most of it is clock state.
3. The only clean use is a same-size, same-session A/B, and even then the
   T470's run-to-run spread reaches 13–18% at 32768/65536.
4. Never divide a tight-loop measurement by a `FRAME-TIMING` one: the same work
   measures ~2.5× apart between them. Uncapped, the governor ramps: FFT 16384 on
   the T470 is 1.6 ms uncapped and 2.60 ms at `--max-fps 30`.
5. A number from another machine is not a wrong number: `cpu` at 65536 is
   ~3.3 ms on the 12700H and ~7 ms on the T470. Retract only on evidence from
   the same box.

## 2026-10-27 · FFT sweep with package energy · v3.12.0 · T470 (Arch)

Release `UHD=1` AppImage, `bench.sh` over a new 30 s capture (92.6 MHz,
2.048 MS/s), `--max-fps 30`, vsync, 3 × 40 s per size, `perf` exact (nothing
multiplexed), on battery, `SDL_VIDEO_DRIVER=wayland`. `cpu` pools the
3 × 36 windows bench.sh keeps. Package power is RAPL `intel-rapl:0` (cores,
iGPU, uncore; not DRAM, panel or the rest of the board) over each whole
40 s run. Idle is the same counter over a 40 s `sleep` at the start of each
rep: 0.67 / 0.62 / 0.63 W. Logs in `data/logs/` (not in git).

| FFT | fps (min) | `cpu` ms | `cpu` median per run | `render_build` ms | `present` ms | package W per run | app W over idle |
|---|---|---|---|---|---|---|---|
| 1024 | 30.0 (30.0) | 0.10 | 0.10 / 0.10 / 0.10 | 1.26 | 1.39 | 1.41 / 1.40 / 1.38 | 0.76 |
| 2048 | 30.0 (30.0) | 0.19 | 0.18 / 0.19 / 0.19 | 1.23 | 1.38 | 1.26 / 1.34 / 1.28 | 0.65 |
| **4096** | 30.0 (30.0) | **0.46** | 0.46 / 0.48 / 0.41 | 1.49 | 1.46 | 1.37 / 1.31 / 1.29 | **0.68** |
| 8192 | 30.0 (29.9) | 0.98 | 0.97 / 0.96 / 1.13 | 1.46 | 1.43 | 1.29 / 1.30 / 1.31 | 0.66 |
| 16384 | 30.0 (29.6) | 2.20 | 2.24 / 2.15 / 2.00 | 1.47 | 1.21 | 1.27 / 1.30 / 1.25 | 0.63 |
| 32768 | 30.0 (30.0) | 3.77 | 3.71 / 3.88 / 3.90 | 1.05 | 0.91 | 1.31 / 1.34 / 1.35 | 0.69 |
| 65536 | 30.0 (29.9) | 7.50 | 7.74 / 7.55 / 6.86 | 0.97 | 0.96 | 1.38 / 1.42 / 1.37 | 0.75 |

- **Power is flat in FFT size.** The app roughly doubles package power
  (+0.63–0.76 W) at every size. Only the ends rise, by ~0.1 W over 16384, with
  runs that do not overlap it. At 1024 the cost is wakeups: 2000 frames/s
  through the queue, 84k context switches per run against 24k at 4096. At 65536
  it is cycles: 11.6 G against 5.1 G at 4096 (+125%), yet only ~0.07 W more.
  The cycle count overstated FFT size as a power cost; the likely reason, not
  separated here, is that cycles at low clocks are cheap next to the iGPU and
  compositing that every size pays alike.
- **`cpu` matches v3.9.1 on Ubuntu** (2026-07-15): within 0.03 ms of it up to
  8192 and inside its two runs above. Whole-process cycles are higher than
  July's: 4096 3.16 → 5.15 G, 65536 9.13 → 11.57 G. The DSP time is unchanged, so the extra cycles lie outside
  it; the canvas (1050×576 then), KWin and EPP were not separated.
- **The first ~12 s of every run cost more**: `cpu` climbs ~0.2 ms per
  second, then drops to the steady value. Until the waterfall history is
  full, `WaterfallDisplay::render()` repaints every line each frame. That adds
  up to ~2.5 ms at any size, far under the 33 ms frame, but it is in every
  figure above. v3.9.1 has the same code path; its energy share was not
  separated.

## 2026-10-14 · Baseline matrix · `b389cba` · 12700H

Release sizes (Linux, GCC 15.2.0): 375 376 B, 405 408 B with `UHD=1`; Windows
`UHD=1` bundle `openspectrum.exe` 803 840 B.

**WSL2, OpenGL (WSLg).** `bench.sh` over `data/phase1/p1rel4_*.iq` (92.6 MHz,
4 MS/s, cf32, 30 s), vsync, `--max-fps 30`, 3 × 40 s per size, no `perf`.
Median over 3 × 36 windows; logs in `data/phase1/bench/` (not in git).

| FFT | fps (min) | `cpu` ms | `cpu` per-run range | `render_build` ms | `present` ms |
|---|---|---|---|---|---|
| 1024 | 30.0 (29.9) | 0.04 | 0.04–0.04 | 2.46 | 4.29 |
| 2048 | 30.0 (29.9) | 0.07 | 0.06–0.07 | 2.56 | 4.32 |
| 4096 | 30.0 (29.9) | 0.12 | 0.11–0.12 | 2.64 | 4.38 |
| 8192 | 30.0 (29.9) | 0.24 | 0.22–0.25 | 2.89 | 4.65 |
| 16384 | 30.0 (29.9) | 0.48 | 0.46–0.58 | 2.84 | 4.59 |
| 32768 | 30.0 (29.9) | 0.99 | 0.91–1.02 | 2.95 | 4.69 |
| 65536 | 30.0 (29.9) | 1.69 | 1.59–1.79 | 2.66 | 4.59 |

**Windows 11, Direct3D 11.** Same capture, `UHD=1` bundle, vsync,
`--max-fps 30`, 3 × 45 s per size.

| FFT | fps (min) | `cpu` ms | `cpu` median per run | `render_build` ms | `present` ms |
|---|---|---|---|---|---|
| 4096 | 30.0 (29.6) | 0.23 | 0.23 / 0.23 / 0.23 | 1.58 | 0.67 |
| 65536 | 30.0 (29.5) | 3.19 | 3.05 / 3.22 / 3.34 | 1.18 | 0.48 |

## 2026-10-22 · B210 losses, direct port vs hub · second laptop

Reported by the RF team, not reproduced here. A different laptop from the
12700H (CPU, OS, UHD version and tool not stated), the same USB A to B cable
as every B210 run in this file, larger buffers, 10 s per test, two channels
unless noted. "Through hub" is two Realtek SuperSpeed hubs (probably a dock)
on a different Intel USB controller than the direct port.

| Rate | Bus load, `sc16` | Direct to laptop port | Through hub |
|---|---|---|---|
| 2 × 10 MS/s | 80 MB/s | 1.2 % dropped | 0.2 % |
| 2 × 15 MS/s | 120 MB/s | ~45 % | 0.5 % |
| 2 × 20 MS/s | 160 MB/s | ~93 % | 1.0 % |
| 1 × 20 MS/s | 80 MB/s | 6 % | 0.1 % |
| 2 × 30.72 MS/s | 246 MB/s | — | 88 % |
| 1 × 61.44 MS/s (`benchmark_rate`) | 246 MB/s | 95 % | 90 % |

Bus load assumes `sc16` on the wire (4 bytes per sample per channel); the
report does not name the wire format. A hub adds no bandwidth, so the gain
is the controller's: the port the device hangs off decides the ceiling as
much as the machine does. The two 246 MB/s rows fail alike whatever the
channel count, so the limit is bytes per second, between 160 and 246 MB/s
on the hub's controller. No row is loss-free, and 10 s is too short to show
the rare single bursts seen on the 12700H below. Not yet tried: the 12700H's
other USB controllers, and `sc8` on the wire (half the bytes, 8-bit range).

## 2026-10-13/14 · B210 capture losses · 12700H

`UhdDevice`, `cs16` captures at 2.44 GHz, USB 3.

**WSL2 through usbipd**, release build, 30 s, 2026-10-13:

| Run | Samples in file | `dropped_samples` | `measured_sample_rate_hz` |
|---|---|---|---|
| 2.048 MS/s | 60 903 424 | 0 | 2 048 033 (+16 ppm) |
| 4 MS/s | 118 976 512 | 0 | 4 000 029 (+7 ppm) |
| 2.048 MS/s, process stopped for 1 s (`kill -STOP`) | 58 875 523 | 2 006 180 | 1 980 578 (-3.3 %) |

Each file is exactly `sample_count` × 4 bytes. The third row is the control
that the counter is real: file plus dropped is 60 881 703, the same 29.7 s as
the clean run, and the measured rate falls by the missing share. usbipd is the
limit here: UHD's `benchmark_rate` held 2.048 MS/s cleanly, had one overrun in
20 s at 5 MS/s, and from 10 MS/s up delivered about 5.6 MS/s whatever was
asked.

**Native Windows 11**, CI build (UCRT64, UHD 4.8), `uhd-sweep.ps1`, 20 s,
2026-10-13:

| Rate | FFT 1024: dropped | FFT 65536: dropped |
|---|---|---|
| 2 MS/s | 0 | 0 |
| 4 MS/s | 0 | 0 |
| 8 MS/s | 0 | 54 875 (0.03 %) |
| 16 MS/s | 0 | 74 436 (0.02 %) |
| 32 MS/s | 431 805 414 (68 %) | 441 763 553 (70 %) |
| 56 MS/s | 965 619 240 (87 %) | capture did not end |

Every file was `sample_count` × 4 bytes, the clean runs were within 43 ppm of
the asked rate, and the program used under half a core. File plus dropped
comes to 19.8–19.9 s of the asked rate in both collapsed runs, so the loss
count holds there too. (The 56 MS/s / 65536 run that did not end was fixed in
`3567187`; rerun 2026-10-14, it ends on time and 32 and 56 MS/s draw at 30 fps
while losing.)

The collapse above 16 MS/s is the host. UHD's `benchmark_rate` (Ettus's
Windows UHD 4.8, no disk, 20 s) on the same machine: clean at 16 and 20 MS/s,
57 % lost at 24, 75 % at 28, 86 % at 32, 95 % at 56 MS/s. This host's ceiling
is between 20 and 24 MS/s. Every failing run reports about 1260 overruns in
20 s whatever the rate, one per 15.9 ms, close to Windows' 15.6 ms timer tick:
a reading of the number, not a finding.

Below the ceiling, 2026-10-14: 28 runs of 20 s (8 and 16 MS/s at both sizes,
five passes, then one full sweep) lost samples in six, each a single burst
(one UHD `O`): 16 MS/s 3 of 12, 102–132 thousand samples; 8 MS/s 2 of 12;
4 MS/s 1 of 2; 2 MS/s none. Not tied to FFT size, and present without
`--iq-log` (five runs at 16 MS/s / 65536: 0 to 2 `O` each).

Larger bursts (7 to 71 million samples, 46 to 447 `O`, 4 of 6 runs at
16 MS/s / 65536, build `b389cba`) went with `--iq-log` while the capture write
ran on the receive thread (~19 % of a core at 16 MS/s before any disk wait).
With the write on the logger's own thread (`6fce657`): 16 MS/s / 65536 clean
in 4 of 4 runs, a full sweep clean up to 16 MS/s at both sizes, and more
arrives at 32 and 56 MS/s (64–66 % and 84–85 % lost, against 69–73 % and
88–89 %). The host ceiling is unchanged.

## 2026-07-17 · fast-math on PocketFFT: rejected · T470, 12700H

`-funsafe-math-optimizations -fno-math-errno` on `fft_analyzer.o` only (the
one object that includes PocketFFT; the other hot loops are AVX2 intrinsics
and gave byte-identical output). Never global `-ffast-math`: it adds
`-ffinite-math-only`, which folds `isnan`/`isinf` to constants.

Microbench of the transform alone (12700H, WSL2, GCC 15.2): −25% at 4096,
−28% at 16384/65536; max dB error 0.00347 → 0.00365 at 65536.

Whole app on the T470: two same-day CI AppImages (GCC 13), same capture,
7 sizes × 2 reps × 40 s, SMT off:

| FFT | instructions | rep noise | cycles | rep noise |
|-----|-------------:|----------:|-------:|----------:|
| 1024–8192 | +0.1…+0.7% | 0.1–0.4% | flat | 1.0–3.5% |
| 16384 | +0.8% | 0.3% | −2.2% | 1.9% |
| 32768 | **+2.3%** | 0.3% | −0.2% | 0.8% |
| 65536 | **+2.4%** | 0.3% | −1.8% | 8.7% |

No win on the target. The DSP is ~75% of instructions at 65536 (2.69 M per
frame at 1024 to 10.62 M at 65536), so a 25% cheaper transform would have
shown as ~−19%. Likely the compiler and µarch (GCC 15.2 on Alder Lake vs the
shipped GCC 13 on Kaby Lake); not pursued, because CI is what ships. `cpu`
could not have decided it: 6.97/7.88 ms base vs 7.44/8.80 ms test at 65536.

## 2026-07-15 · FFT sweep, v3.9.0 vs v3.9.1 · T470

`./bench.sh`, both binaries in one session. 7 sizes × 2 reps × 40 s, `--play`
of a 21 s capture at 2.048 MS/s, `--max-fps 30`, SMT off, `perf` counters
exact (nothing multiplexed). Canvas 1050×576 (the shipped canvas is now
1280×720; `cpu` barely moves with it).

`cpu` per-run median, ms:

| FFT | v3.9.0 | v3.9.1 | verdict |
|-----|--------|--------|---------|
| 1024 | 0.13, 0.12 | 0.11, 0.11 | v3.9.1 faster |
| 2048 | 0.21, 0.22 | 0.19, 0.19 | v3.9.1 faster |
| **4096** (default) | 0.53, 0.55 | **0.47, 0.47** | **v3.9.1 faster** |
| 8192 | 1.42, 1.34 | 0.95, 0.94 | v3.9.1 faster |
| 16384 | 2.64, 2.60 | 2.53, 1.87 | v3.9.1 faster |
| 32768 | 4.42, 4.83 | 3.77, 4.44 | unresolved, runs overlap |
| 65536 | 7.06, 6.85 | 6.73, 7.63 | unresolved, runs overlap |

`fps` 30.0 at every size in both builds. `render_build` 0.98–1.61 ms and
`present` 0.93–1.62 ms, flat across FFT size. Per-frame work
(`cpu` + `render_build` + `present`) is ~3.5 ms at 4096 and ~9 ms at 65536,
against a 33 ms frame.

Cycles, whole process over 40 s (v3.9.1):

| FFT | 4096 | 16384 | 32768 | 65536 |
|-----|-----:|------:|------:|------:|
| cycles | 3,163 M | 4,106 M | 5,733 M | 9,125 M |
| vs 4096 | — | +30% | +81% | **+189%** |

**PocketFFT plan cache** (`POCKETFFT_CACHE_SIZE` 0 → 1, the v3.9.0 → v3.9.1
difference): the 4096 default drops 0.55 → 0.47 ms (−14%); 32768/65536
unresolved. Instructions fall −1.5% at 1024 to −12.7% at 65536 while cycles
move −3.4%…+0.8%: no power win. Page faults at 65536 are PocketFFT's per-call
scratch buffer, untouched by the cache (164 468 → 161 898, and bimodal across
runs). A 10-entry cache measured no faster for 7× the twiddle memory. A
WSL2 microbench had predicted −26%.

## 2026-07-15 · Windows spot check, live RTL-SDR · `a772ae7` (v3.9.1) · 12700H

Direct3D 11, `--max-fps 30`, SMT on, one session, no reps, no `perf`.
Per-second `FRAME-TIMING` medians:

| FFT | cpu | render_build | present | windows |
|-----|----:|-------------:|--------:|--------:|
| 4096 (default) | **0.13** (0.25 incl. waterfall fill) | 1.15 | 0.46 | 13 |
| 65536 | **3.32** (p25–p75 3.23–3.38) | 1.26 | 0.51 | 73 |

Render-bound through 16384, DSP-bound at 32768/65536, on both machines (which
stage dominates, not what fails: 30.0 fps either way). The 12700H's
interquartile spread at 65536 is 4.6%, against 13–18% on the T470.

## 2026-07-15 · Where the per-frame work is · 12700H

Per-stage cost at FFT 65536, tight loop over the project sources. Relative
shares of real work, not per-frame cost on any machine:

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

`get_max_db` is the only scalar loop left (GCC will not vectorize a float max
without fast-math), worth 0.05 ms of a 33 ms frame.

The median in `update_spectrum`, copy + select (2026-07-15, machine not
recorded): 0.039 ms unstrided vs 0.004 ms at stride 4 at FFT 16384, 0.153 vs
0.058 ms at 65536.

## 2026-07-15 · Software renderer · T470

Linux, 1050×576, `--max-fps 30`, FFT 32768: `render_build` + `present` ≈
5.9 ms (3.17 + 2.77; vsync off, so `present` is real blit work) against
~2.3 ms on OpenGL. The gap is a flat +3.6 ms per frame and does not grow with
FFT size.

## 2026-07-14 · Memory · `99257e3` · 12700H

Native Windows, Direct3D 11: ~51 MB resident at FFT 16384, +8 MB going 4096 →
65536 (the frame pool is 12 frames × `fft_size` × 8 B). A WSL2 profile reads
much higher, mostly the Mesa software-GL backing store; not representative.

## 2026-07-10..13 · Panel-text regression, v3.8.0 → v3.8.1 · 12700H, T470

v3.8.0 drew every panel string by creating, uploading and destroying an
`SDL_Texture` per string per frame (~15–40 a frame). Invisible in fps under
vsync; visible in `render_build` and counters. Builds v3.7.0 (`6c8e173`),
v3.8.0 (`7785c84`) and v3.8.1, SMT off, `--play` of an FM capture.

| `render_build` (ms), Direct3D 11, 12700H | v3.7.0 | v3.8.0 | v3.8.1 |
|---|---:|---:|---:|
| `--max-fps 30` | 2.95 | 5.80 | **2.74** |
| `--max-fps 60` | 2.30 | 4.09 | **2.16** |

| `perf stat`, T470, OpenGL, FFT 16384, `--max-fps 60`, same ~34.5 s workload | v3.7.0 | v3.8.0 | v3.8.1 |
|---|---:|---:|---:|
| instructions | 10.93 B | 17.57 B | **9.78 B** |
| cycles | 10.39 B | 17.60 B | **8.12 B** |
| task-clock | 15.56 s | 20.59 s | **12.71 s** |

Task Manager on the 12700H at FFT 16384, CPU / GPU: 1% / 4.5% (v3.7.0) →
2% / 9% (v3.8.0) → 0.8% / 3.5% (v3.8.1). On the software renderer
`render_build` went 9.7 → 6.7 ms at `--max-fps 30` (12700H).

## 2026-07-07 · FFT size vs renderer, uncapped · `148b0a4` (v3.7.0) · T470

Ubuntu, `--play` of a 21 s capture, vsync off, `--max-fps 0`, FFT size stepped
live, SMT on, first ~1 s after each change dropped. Steady-state averages of
`FRAME-TIMING`:

| FFT | Frame supply¹ | OpenGL fps | Software fps | cpu (ms) | Limited by |
|----:|-----------:|-----------:|-------------:|----------:|------------|
| 1024  | 2000 | ~530 | ~277 | 0.05 | renderer |
| 2048  | 1000 | ~529 | ~262 | 0.10 | renderer |
| 4096  |  500 | ~498 | ~257 | 0.15 | renderer (OpenGL at supply) |
| 8192  |  250 | ~241 | ~248 | 0.35 | supply |
| 16384 |  125 | ~124 | ~120 | 1.6  | supply |

¹ `rate / FFT` at 2.048 MS/s: the renderer cannot draw a frame that does not
exist yet. `render_build` ~1.4 ms on OpenGL vs ~3.0 ms on software. Below the
crossover (~4096 OpenGL, ~8192 software) the backend sets the ceiling; above
it both converge to the supply. Higher fps means more power, so `--max-fps` is
the power control and FFT size a second-order one.
