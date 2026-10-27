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
tracks the peak (`max + 5 dB`) — `max` is signal-dominated and well behaved —
but only on the way up: it falls at most 0.5 dB per frame (`kTopDecayDb`). In a
bursty band a frame-by-frame top swung ~10 dB at 16 MS/s (2.44 GHz, B210, FFT
65536; a frame there is 4 ms of every 33) and rescaled the whole trace with it.

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
│   ├── hardware/       # SampleSource: RTL2832U, B200 USRP, capture playback
│   ├── signal/         # DC removal, windowing
│   ├── fft/            # FFT & spectral analysis (PocketFFT)
│   ├── visualization/  # Spectrum & waterfall rendering logic
│   ├── gui/            # SDL3 window and event management
│   └── utils/          # Logging, config, utilities
├── tests/              # make check: programs of asserts
└── third_party/        # stb_image_write (PNG export), pocketfft
```

A single-threaded render loop is fed by a producer thread — the RTL-SDR async
callback, the USRP receive loop, or an `IqPlaybackSource` under `--play` that
replays a capture in real time. All push FFT-sized frames into a bounded queue; the renderer drains to
the newest each paint (a frame-rate throttle that prevents latency buildup). The
signal chain downstream of the callback is source-agnostic. See CLAUDE.md for
the pipeline diagram and what is easy to break.

## Performance

Figures, each with its date, machine and build, are in
[MEASUREMENTS.md](MEASUREMENTS.md); read its "Reading `cpu`" before acting on
any `cpu` number. What follows from the design:

- **Latency is one frame**: ~33 ms at the default `--max-fps 30`, ~16.7 ms at
  60 fps. The render loop takes the newest frame and drops the backlog, so no
  latency builds up, and the per-frame work is a fraction of a frame at every
  FFT size.
- **Frame supply is `rate / fft_size`**: 31.25/s at 2.048 MS/s and 65536, just
  above the 30 fps cap. At that size raising `--max-fps` above 31 does nothing,
  and any dip in supply drops a frame. 131072 would need four RTL callbacks per
  frame (~15.6 lines/s) and is not offered.
- **`--max-fps` is the power control.** FFT size is a second-order one at
  every size: 65536 runs more than twice the cycles of the 4096 default, yet
  adds only ~0.07 W of package power on the T470. The large sizes are for
  narrow-signal work, not the default.
- **The transform is the DSP.** PocketFFT is ~70% of the per-frame work at
  65536; everything around it is AVX2 and already negligible.
- **Memory is bounded**: the frame pool is 12 frames × `fft_size` × 8 B, the
  waterfall one byte per cell, and the capture ring (half a second of samples)
  exists only while a capture runs.
- **Renderer**: keep the default, Direct3D 11 on Windows and OpenGL on Linux.
  Vulkan on old Intel iGPUs (HD 6xx / Gen9) is much slower; do not force it.
  The software renderer is a fallback and the battery worst case.
- **Text is cached.** `SdlRenderer::blit_text` keeps one texture per colour and
  string and evicts what was not drawn last frame; glyph textures are
  `STATIC`. A texture created and destroyed per frame is invisible in fps
  under vsync and shows only in `render_build` and instruction counts.
- **Clang is the second compiler** (`CLANG=1`): `clang++` from the same LLVM
  as `lint.sh`'s clang-tidy, ThinLTO, lld. Under ThinLTO the loop vectorizer
  runs at link time, so compile-time remarks come out empty; read them from
  one file built without LTO, e.g. `make CLANG=1 CONFIG=remarks
  CXXFLAGS="-std=c++20 -O3 -march=haswell -Rpass=loop-vectorize
  -Rpass-missed=loop-vectorize" build/remarks-clang/fft_analyzer.o`. Most
  misses there are PocketFFT's, from its header.

## Adding a new SDR device

Every producer of IQ samples implements `SampleSource`
(`src/hardware/sample_source.h`). There are three today: `RtlSdrDevice` (live
RTL2832U), `UhdDevice` (a B200-series USRP, `--device uhd`, see below) and
`IqPlaybackSource` (`--play`). After the block in `main.cpp` that
constructs one of them, nothing refers to a concrete receiver: tuning limits,
retuning, the FFT-size restart, the capture sidecar's device name and the drop
count all go through the interface.

### 1. Implement the interface

```cpp
// src/hardware/new_sdr_device.h
#pragma once
#include "sample_source.h"

class NewSdrDevice : public openspectrum::SampleSource {
public:
    const char *name() const noexcept override { return "NewSDR"; }
    openspectrum::TuningLimits limits() const noexcept override;

    bool open() override;
    void start(size_t chunk_size) override; // spawn the producer thread
    void stop() override;                   // join it

    bool set_sample_rate(uint32_t rate_hz) override;
    void set_frequency(uint64_t freq_hz) override;
    void set_gain(float gain_db) override;
    uint64_t dropped_samples() const noexcept override;
};
```

What the pipeline expects of it:

- **Frames.** The producer thread calls `m_frame_callback` with pooled
  `FrameHandle`s of at most `chunk_size` complex-float samples, scaled to
  [-1, 1). The source owns its pool (see `RtlSdrDevice::start`); `start` is
  called again with a new `chunk_size` after every FFT-size change.
- **Frequency is 64-bit** end to end (CLI, `ControlState`, sidecar, scale).
  32 bits end at 4.29 GHz, below the 5.8 GHz band.
- **`limits()`** is read once after `open()` and clamps the tuning and gain
  keys. One-shot options such as the RTL bias tee stay setters of the
  concrete class.
- **`set_sample_rate()`** is called once, after `open()` and before
  `start()`. It fails if the device answers with a different rate than
  asked, because the sidecar records the asked one.
- **`dropped_samples()`** counts what the receiver's API reports as lost
  (overflow flags, sequence gaps). It is sampled at capture start and stop and
  written to the sidecar. `RtlSdrDevice` can only count its own allocation
  failures, because librtlsdr does not report USB overruns.

### 2. Wire it in

Construct it in the source-selection block in `main.cpp` (search for "The one
place a receiver is chosen"), apply any one-shot options, and move it into
`source`. Add its library to `BASE_LDFLAGS` in the Makefile.

### USRP through UHD

`UhdDevice` (`src/hardware/uhd_device.*`) is compiled only by `make UHD=1`
(`libuhd-dev`; objects go to `build/<config>-uhd/`). The stock build and the
plain release bundles carry no UHD or Boost; the `-uhd` bundles do. A `UHD=1`
build opens the USRP by default (`--device rtl` for an RTL-SDR); `-r` is then
the USRP's rate and the three RTL-only options are ignored.

- **UHD directly, not SoapySDR.** For a USRP, SoapySDR is a wrapper around UHD,
  which has to be installed either way (it loads the firmware and FPGA image
  into the device on every power-up). SoapySDR becomes worth its two extra
  layers with the first wideband receiver that is not a USRP.
- **B200 series only.** The device is opened as `type=b200`. An empty address
  also searches the network for X-series devices, and on a WSL2 host with
  nothing attached that search never returned.
- **Samples are scaled by 1/32768 here.** They are taken from UHD as `sc16`
  and converted in `convert_iq_s16_to_f32`, so a `cs16` capture (which
  multiplies by 32768 and rounds) holds the receiver's own integers. `open()`
  asserts that round trip over every int16 in debug builds.
- **Losses are counted in samples, from the device clock.** Each received
  packet carries the sample time of its first sample. The receive loop keeps
  the time the next packet must start at; a later start is a gap, and its
  length in samples is added to `dropped_samples()`. This does not depend on
  UHD flagging an overflow.
- **A new streamer per `start()`.** A reused one delivers packets from before
  the stop, and the gap count would report the pause as a loss.

Measured capture losses on a B210, through WSL2 usbipd and on native
Windows: [MEASUREMENTS.md](MEASUREMENTS.md). The host, not the program,
sets the ceiling (about 5.6 MS/s through usbipd, 20 to 24 MS/s on the
Windows host measured). On a second laptop the USB controller mattered as
much as the machine: the same device lost 93 % at 2 × 20 MS/s on one port
and 1 % on another controller, so try every controller before calling a
host's ceiling.

## Capture files

`Ctrl+S` (or `--iq-log`) writes `data/capture_<UTC start>_<freq>.iq` and a
`.meta.json` sidecar beside it.

Samples are interleaved I/Q, little-endian, no header:

| `--iq-format` | Per sample | Read with numpy as | Disk rate at 2.048 MS/s |
|---|---|---|---|
| `cf32` (default) | 2 × float32, 8 bytes | `np.fromfile(f, np.complex64)` | 16 MB/s |
| `cs16` | 2 × int16, 4 bytes, full scale 32768 | `np.fromfile(f, np.int16)` then pair up, `/ 32768` | 8 MB/s |

`cs16` loses nothing against a receiver of 16 bits or fewer; it is the format
to use once a wide receiver makes the disk the limit (20 MS/s is 160 MB/s as
`cf32`). `--play` reads either, from the sidecar's `sample_format`.

Sidecar fields a consumer should check before trusting a file:

- `capture.timestamp_unix` / `timestamp_iso8601` — capture **start**, UTC.
- `capture.sample_format` — `cf32_le` or `cs16_le`. Sidecars older than
  version 1.1 lack it and are `cf32_le`.
- `capture.dropped_samples` — losses the source reported during the capture,
  plus batches the capture writer dropped because it was half a second behind
  (logged as `IQ writer fell behind`).
- `capture.write_error` — `true` if a short write (disk full) ended the
  capture early. `sample_count` is still exactly what the file holds.
- `signal.center_frequency_hz`, `gain_db`, `fft_size` — hold for the whole
  file: the controls that change them are held while a capture runs, and an
  FFT-size change, which restarts the stream, cannot open a gap in it.
- `signal.measured_sample_rate_hz` — samples received after the first batch,
  over the time between first and last write. A value clearly below
  `sample_rate_hz` means samples were lost before the source could count them.
  Batches arrive in bursts (16 ms each on the RTL), so read it on captures of
  tens of seconds: over 60 s the burst jitter is about 0.03 %.

## Testing & hardware checks

`make check` builds and runs `tests/`: the logic that can be checked without a
device or a window (sample queue, capture writer, formatting, plot overlays).
`make sanitize` runs them under ASan and UBSan, where any report fails the
test (Linux only: MinGW GCC ships no libasan). Everything else is verified by
running the program against live hardware or a recorded capture (`--play`).
Confirm the device is seen first:

```bash
rtl_test -t       # list available RTL-SDR devices
rtl_eeprom        # device info
```

For a USRP (`uhd-host`; `uhd_images_downloader` once, for the FPGA image):

```bash
uhd_find_devices --args type=b200
uhd_usrp_probe --args type=b200   # must report USB 3, not USB 2
/usr/libexec/uhd/examples/benchmark_rate --args type=b200 --rx_rate 10e6 --duration 60
```

`benchmark_rate` is the host's ceiling without any of our code: a rate it
cannot hold without overflows is not one to expect from OpenSpectrum. Under
WSL2 the device needs `usbipd attach --wsl --auto-attach` (it re-enumerates
after the firmware load), and the rates measured there are those of USB/IP,
not of the receiver.

On native Windows a `UHD=1` build links MSYS2's `libuhd`
(`mingw-w64-ucrt-x86_64-libuhd`, `-boost`) and the bundle carries
`libuhd.dll`; the driver, the device images and the tools come from Ettus:

1. **Driver.** From Ettus's `erllc_uhd_winusb_driver.zip`, in an elevated
   prompt: `pnputil /add-driver <folder>\erllc_uhd_b200.inf /install`. The
   `.cat` file must be in the same folder as the `.inf`. A device attached to
   WSL needs `usbipd unbind --busid <id>` first.
2. **Images and tools.** Ettus's Windows installer of the same UHD version as
   the bundle's `libuhd.dll`: UHD loads the firmware and FPGA image on every
   power-up, and they must match the library. Only 4.8.0 has been tested, on
   both sides. The program's log prints the loaded library's version when the
   device opens (`UHD_4.8.0.0-...`); `uhd_config_info --version` prints the
   installer's. The installer leaves `UHD_IMAGES_DIR` unset; with its
   default folder:

   ```powershell
   $uhd = "C:\Program Files\UHD"   # the folder chosen in the installer
   $env:UHD_IMAGES_DIR = "$uhd\share\uhd\images"
   $env:Path += ";$uhd\bin"
   uhd_config_info --version
   uhd_find_devices --args type=b200
   & "$uhd\lib\uhd\examples\benchmark_rate.exe" --args type=b200 --rx_rate 10e6 --duration 20
   ```

3. **Run** from the bundle folder: `.\openspectrum.exe --device uhd -f 2440000000
   -r 8e6`. `uhd-sweep.ps1` and `play-bench.ps1` in `UHD=1` bundles repeat the
   loss sweep and the frame timing in [MEASUREMENTS.md](MEASUREMENTS.md).

Nothing goes into the RX input by cable without at least 30 dB of
attenuation, and TX stays off.

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

`-fPIE` and the three `-Wl,-z` flags are Linux-only.

These flags guard the program's actual input surface — `.iq` captures, the
`.meta.json` sidecar, and the receivers' USB streams — where a malformed file or a
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
