# OpenSpectrum: Modular SDR Spectrum Analyzer

*Divide et impera — Modular architecture for SDR signal processing*

[![GPL v3 License](https://img.shields.io/badge/License-GPL%20v3-green.svg)](LICENSE)

OpenSpectrum is a **modular Software-Defined Radio (SDR) spectrum analyzer** —
a lightweight, real-time platform built with extensibility at its core. The
**divide et impera** design separates hardware abstraction, signal processing,
FFT analysis, visualization, and rendering into distinct modules, so new SDR
backends (RTL2832U today, proprietary hardware tomorrow) drop in without
touching the pipeline.


https://github.com/user-attachments/assets/f758865f-75fa-4cd9-9109-d0f49247ab4c

> [!IMPORTANT]
> RTL-SDR outputs complex IQ samples, so the observed bandwidth is roughly equal
> to the sample rate. The RF/DSP concepts, architecture, performance profile,
> and how to add a device live in **[docs/TECHNICAL.md](docs/TECHNICAL.md)**.

**Documentation:** usage below · RF/DSP, architecture & performance in
[docs/TECHNICAL.md](docs/TECHNICAL.md) · maintainer/hot-path notes in
[CLAUDE.md](CLAUDE.md).

## Features

| Feature | Description |
|--------|-------------|
| **Multi-Device Support** | RTL2832U (via librtlsdr) with architecture ready for proprietary SDR hardware |
| **Real-Time FFT Analysis** | Configurable FFT size (1024–65536), six window functions, and DC removal |
| **Dual Visualization** | GPU-rendered spectrum display + waterfall, with five color palettes |
| **Cursor Readout** | Hover for a live frequency + amplitude (or time-ago) readout under the cursor |
| **Instrument Panel** | Right-side readout — frequency/gain/FFT/window/palette, PEAK, live FPS/CPU timing, and the marker list |
| **Frequency Markers** | Click to drop persistent reference lines (with live level) that track frequency as you tune |
| **Trace Modes** | Max-hold and video (EMA) averaging traces overlaid on the live spectrum |
| **Amplitude Trigger** | Shift-drag a dB threshold; a bin peak crossing it freezes the display to catch fast transients (`Space` resumes) |
| **HF Reception** | `--ppm` correction, `--bias-t` antenna power, and `--direct-sampling` for the HF range |
| **IQ Capture & Playback** | Record raw IQ to disk (`Ctrl+S`) and replay captures with `--play` — no hardware needed |
| **PNG Spectrogram Export** | One-key export of the current spectrum + waterfall to an image |
| **Power-Aware Rendering** | Vsync-paced with an optional `--max-fps` cap (default 30) that throttles below refresh for long unattended runs |
| **Security-Hardened** | Compiled with `-D_FORTIFY_SOURCE=2`, stack protection, RELRO, and more |

## Installation

### Prebuilt binaries (recommended)

Grab the latest Windows or Linux build from the
[Releases](https://github.com/domagoj-kod/OpenSpectrum/releases) page.

> [!NOTE]
> The Windows `.exe` is currently **unsigned**, so Windows Defender's ML
> heuristic may flag it as `Trojan:Win32/Wacatac.C!ml` and quarantine it. This
> is a **false positive** (typically ~1/61 on VirusTotal) common to unsigned,
> freshly-built C/C++ tools — the source is open for inspection. Restore the
> file from quarantine (or submit the false positive to Microsoft). Signed
> releases are planned.

**Windows driver:** on Windows 10+, install the RTL2832U WinUSB driver with
[Zadig](https://www.rtl-sdr.com/rtl-sdr-quick-start-guide/) before first run. If
the device stops being detected after a Windows Update, re-run Zadig; you may
also need to disable **Core Isolation → Memory Integrity**. On Linux and macOS,
librtlsdr talks to the device directly — no driver step.

### Build from source (advanced)

Requires a **C++20 compiler** and **AVX2** (Haswell, 2013+). For older CPUs,
remove `-march=haswell` from `CXXFLAGS` in the Makefile.

| Dependency | Purpose | Ubuntu/Debian |
|-----------|---------|---------------|
| `g++` / `clang++` | C++20 compiler | `sudo apt install build-essential` |
| `make` | Build system | `sudo apt install make` |
| `librtlsdr-dev` | RTL-SDR support | `sudo apt install librtlsdr-dev` |
| `libsdl3-dev` | GUI rendering | `sudo apt install libsdl3-dev` |
| `pkg-config` | Dependency detection | `sudo apt install pkg-config` |

- **macOS (Homebrew):** `brew install librtlsdr sdl3 pkg-config`
- **Windows (MSYS2):** `pacman -S mingw-w64-x86_64-{gcc,make,rtl-sdr,sdl3}`

```bash
git clone https://github.com/domagoj-kod/OpenSpectrum.git
cd OpenSpectrum
make release      # optimized build → ./openspectrum
```

| Target | Optimization | Use case |
|--------|--------------|----------|
| `make` / `make debug` | `-O0 -g` | Development, debugging |
| `make release` | `-O3 -flto -march=haswell` | Production |
| `make profile` | `-O2 -pg` | Performance analysis |

## Quick Start

```bash
./openspectrum --help
Usage: ./openspectrum [OPTIONS]

Options:
  -f, --freq HZ       Center frequency in Hz (default: 92600000)
  -r, --rate HZ       Sample rate in Hz (default: 2048000)
  -g, --gain DB       Gain in dB (default: 10.0)
  -s, --fft-size N    FFT size: 1024, 2048, 4096, 8192, 16384,
                      32768, 65536 (default: 4096; larger = finer
                      bins and more CPU)
  -w, --width N       Display width in pixels (default: 1050)
  -H, --height N      Display height in pixels (default: 576)
  -W, --window NAME   Window function: rectangle, hann, hamming,
                      blackman, blackman-harris, flat-top
                      (default: blackman-harris)
  --max-fps N         Cap render rate to N fps (default: 30; 0 = uncapped)
  --ppm N             Crystal frequency correction in ppm (default: 0)
  --bias-t            Power the antenna port (4.5 V bias tee)
  --direct-sampling   Q-branch direct sampling for HF (tunes 0-14.4 MHz)
  --iq-log            Enable IQ data logging to file
  --iq-duration SEC   Capture duration in seconds (default: 0 = manual)
  --iq-output FILE    Output filename prefix (default: auto-generated)
  --play FILE.iq      Replay a recorded IQ capture instead of opening hardware
  --help              Show this help message
```

```bash
# Examples
./openspectrum -f 100000000 -g 20
./openspectrum --freq 144500000 --gain 15 --fft-size 8192
./openspectrum --direct-sampling -f 7100000      # 40 m band, HF
./openspectrum --play data/capture_20260610.iq   # no hardware needed
```

Tuning steps default to **1 MHz / 1 dB**; hold **Shift** for fine
(**0.1 MHz / 0.1 dB**) or **Ctrl** for coarse (**10 MHz / 10 dB**).

### Keyboard Controls

| Key | Action |
|-----|--------|
| `+/=` · `-/_` | Increase / decrease center frequency |
| `r` · `f` | Increase / decrease gain |
| `1`–`7` | Set FFT size (1024, 2048, 4096, 8192, 16384, 32768, 65536) |
| `Ctrl` · `Shift` | Coarse (10 MHz/dB) · fine (0.1 MHz/dB) modifier |
| `UP` / `DOWN` | Cycle window functions forward / backward |
| `c` / `Shift+C` | Cycle color palette (JET, VIRIDIS, HOT, GRAY, BLU-RED) |
| `m` · `a` · `x` | Max-hold trace · video averaging · reset traces |
| `Ctrl+S` · `e` | Toggle IQ logging · export spectrogram PNG |
| `Space` | Resume from an amplitude-trigger freeze (re-arms) |
| `ESC` / `q` · `Ctrl+C` | Exit · graceful shutdown (terminal) |

### Mouse Controls

- **Hover** the spectrum or waterfall for a live readout (frequency +
  amplitude, or how long ago the line was captured).
- **Left-click** drops a persistent frequency marker (up to 14, listed in the
  panel) through both panes; it tracks frequency as you retune. **Right-click**
  removes the nearest; **Delete** clears all.
- **Shift + left-drag** in the spectrum pane sets the **amplitude trigger** — a
  dB threshold line. When a bin peak crosses it, the display **freezes** on the
  triggering frame; press **`Space`** to resume. Drag the line to the pane's
  bottom edge to disarm.

## License

Licensed under the **GNU General Public License v3.0 (or later)** — see
[LICENSE](LICENSE).

## Contributing

Contributions welcome:

1. **Modularity first** — add features as separate modules where possible (see
   the [architecture](docs/TECHNICAL.md#architecture)).
2. **Keep the security flags** in the Makefile.
3. **Validation** — there is no automated test suite; verify changes by building
   and running against live hardware or a recorded capture (`--play`), and note
   what you checked.
4. **Update docs** for significant changes.

Workflow: fork → feature branch (`git checkout -b feature/x`) → commit → PR.

## Acknowledgments

- **librtlsdr** — RTL-SDR device library
- **pocketfft** — FFT implementation by Martin Reinecke
- **SDL3** — cross-platform rendering
- **GNU Radio** — inspiration for the modular SDR pipeline

---

*Built with ❤️ for the SDR community*
