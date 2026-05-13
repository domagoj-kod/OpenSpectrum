# OpenSpectrum API Documentation

> **Modular SDR Spectrum Analyzer - Public API Reference**

OpenSpectrum is a modular Software-Defined Radio (SDR) spectrum analyzer built with extensibility at its core. This documentation covers all public APIs for integrating with, extending, or understanding the OpenSpectrum codebase.

---

## Architecture Overview

The project follows a **divide and conquer** (divide et impera) architecture, separating concerns into distinct, interchangeable modules:

```
┌─────────────────────────────────────────────────────────────────┐
│                         OpenSpectrum                              │
├─────────────────────────────────────────────────────────────────┤
│                                                                     │
│  ┌──────────────┐    ┌──────────────┐    ┌──────────────────┐   │
│  │  Hardware     │    │   Signal      │    │    FFT            │   │
│  │  (RTL-SDR)    │───▶│ Processing    │───▶│  Analysis         │   │
│  └──────────────┘    └──────────────┘    └──────────────────┘   │
│          ▲                    │                    │               │
│          │                    ▼                    ▼               │
│          │            ┌──────────────────────────────────┐        │
│          │            │         Visualization             │        │
│          │            │  (Spectrum, Waterfall)             │        │
│          │            └──────────────────────────────────┘        │
│          │                            │                           │
│          │                            ▼                           │
│          │            ┌──────────────────────────────────┐        │
│          └────────────│            GUI (SDL2)               │        │
│                       └──────────────────────────────────┘        │
│                                                                     │
│  ┌─────────────────────────────────────────────────────────────┐│
│  │                    Runtime Controls                           ││
│  │         (User input for frequency, gain, FFT settings)       ││
│  └─────────────────────────────────────────────────────────────┘│
│                                                                     │
│  ┌─────────────────────────────────────────────────────────────┐│
│  │                    Utilities                                   ││
│  │  (Logging, Configuration Parsing, Argument Handling)          ││
│  └─────────────────────────────────────────────────────────────┘│
└─────────────────────────────────────────────────────────────────┘
```

### Data Flow

1. **Hardware Layer** (`RtlSdrDevice`): Reads raw IQ samples from SDR hardware
2. **Signal Processing** (`SignalProcessor`): Applies DC removal and windowing
3. **FFT Analysis** (`FftAnalyzer`): Computes FFT and power spectrum
4. **Visualization** (`SpectrumDisplay`, `WaterfallDisplay`): Renders spectral data
5. **GUI** (`SdlRenderer`, `TextRenderer`): Handles rendering and user input
6. **Runtime Controls** (`RuntimeControls`): Manages user-adjustable parameters
7. **Utilities** (`Logger`, `AppConfig`, etc.): Cross-cutting concerns

---

## Quick Start

### Basic Usage Pattern

```cpp
#include "hardware/rtl_sdr_device.h"
#include "signal/signal_processor.h"
#include "fft/fft_analyzer.h"
#include "visualization/spectrum_display.h"

using namespace openspectrum;

// 1. Initialize hardware
RtlSdrDevice device;
device.open();
device.set_frequency(100000000);  // 100 MHz
device.set_sample_rate(2048000);   // 2.048 MS/s

// 2. Create signal processor with windowing
SignalProcessor processor(4096);
processor.set_window(WindowFunction::BLACKMAN_HARRIS);

// 3. Create FFT analyzer
FftAnalyzer fft(4096);

// 4. In main loop:
std::vector<std::complex<float>> samples = device.read_samples(4096);
SignalProcessor::remove_dc(samples);
processor.apply_window(samples);
fft.execute(samples);
const auto& spectrum = fft.get_db_spectrum();
```

---

## Module Documentation

| Module | Description | Key Components |
|--------|-------------|-----------------|
| **[Types](types.md)** | Common enumerations and type definitions | WindowFunction, ColorMap, LogLevel |
| **[Runtime Controls](runtime_controls.md)** | User-adjustable parameters for real-time control | DeviceConstraints, RuntimeControls |
| **[Signal Processing](signal_processing.md)** | Signal conditioning and windowing | SignalProcessor |
| **[FFT Analysis](fft_analysis.md)** | Fast Fourier Transform computation | FftAnalyzer |
| **[Visualization](visualization.md)** | Spectrum and waterfall rendering | RgbColor, PixelBuffer, SpectrumPalette, SpectrumDisplay, WaterfallDisplay |
| **[GUI](gui.md)** | SDL2-based rendering and input | SdlRenderer, TextRenderer |
| **[Hardware](hardware.md)** | SDR device abstraction | RtlSdrDevice |
| **[Utilities](utilities.md)** | Logging, configuration, utilities | Logger, AppConfig, Logging Macros |
| **[Third Party](third_party.md)** | External library types | kissfft types |

---

## API by Category

### Core Processing Pipeline

- **[RtlSdrDevice](hardware.md)** - Hardware interface for RTL-SDR devices
- **[SignalProcessor](signal_processing.md)** - DC removal and window function application
- **[FftAnalyzer](fft_analysis.md)** - FFT computation and spectrum analysis

### Visualization

- **[SpectrumDisplay](visualization.md#spectrumdisplay)** - 2D spectrum visualization
- **[WaterfallDisplay](visualization.md#waterfalldisplay)** - Time-frequency waterfall display
- **[SpectrumPalette](visualization.md#spectrumpalette)** - Color palette management
- **[PixelBuffer](visualization.md#pixelbuffer)** - Optimized pixel buffer for rendering
- **[RgbColor](visualization.md#rgbcolor)** - RGBA color structure

### User Interface

- **[SdlRenderer](gui.md#sdlrenderer)** - SDL2 window and texture management
- **[TextRenderer](gui.md#textrenderer)** - Bitmap font text rendering
- **[RuntimeControls](runtime_controls.md)** - Keyboard-controlled parameter adjustment

### Configuration & Utilities

- **[AppConfig](utilities.md#appconfig)** - Command-line configuration structure
- **[Logger](utilities.md#logger)** - Thread-safe logging system
- **[ILogSink](utilities.md#ilogsink)** - Abstract log sink interface
- **[ConsoleSink](utilities.md#consolesink)** - Console output log sink
- **[FileSink](utilities.md#filesink)** - File-based log sink with rotation
- **[Logging Macros](utilities.md#logging-macros)** - LOG_TRACE, LOG_INFO, LOGS_DEBUG, etc.
- **[parse_arguments()](utilities.md#parse_arguments)** - Command-line argument parsing

---

## Namespace

All OpenSpectrum components are in the `openspectrum` namespace, except:

- `RtlSdrDevice` - Defined in global namespace (hardware abstraction)
- Third-party types (kissfft) - In their respective namespaces

```cpp
using namespace openspectrum;

// Access types and classes
WindowFunction window = WindowFunction::HANN;
SignalProcessor processor(4096);
Logger::get_instance().set_level(LogLevel::INFO);
```

---

## Build Configuration

The project uses security-hardened compiler flags:

- `-fstack-protector-strong` - Stack overflow protection
- `-D_FORTIFY_SOURCE=2` - Buffer overflow detection
- `-O2` - Optimization level 2
- `-Wall -Wextra` - All warnings enabled
- RELRO (Relocation Read-Only) - Protection against GOT overwrites

---

## Dependencies

| Dependency | Purpose | Header |
|------------|---------|--------|
| librtlsdr | RTL-SDR hardware support | `rtl-sdr.h` |
| SDL2 | Graphics and input | `SDL2/SDL.h` |
| KissFFT | FFT computation | `kiss_fft.h` |
| C++20 | Standard library | Various |

---

## Indices

- [All Types](types.md)
- [All Classes](index.md#module-documentation)
- [All Functions](utilities.md#functions)
- [All Macros](utilities.md#logging-macros)

---

## See Also

- [Main README](../../README.md) - Project overview and installation
- [GitHub Repository](https://github.com/domagoj-kod/OpenSpectrum) - Source code and issues
