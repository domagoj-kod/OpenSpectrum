# OpenSpectrum Types and Enums

This document describes the common types, enumerations, and constants used throughout the OpenSpectrum API.

## Namespace

All types described in this document are in the `openspectrum` namespace unless otherwise specified.

---

## WindowFunction Enum

Defined in: `src/signal/signal_processor.h`

The `WindowFunction` enum defines the supported window functions for spectral leakage reduction in FFT analysis.

```cpp
namespace openspectrum {

enum class WindowFunction {
  RECTANGLE,        // Uniform window (no windowing)
  HANN,             // Hann (Hanning) window
  HAMMING,          // Hamming window
  BLACKMAN,         // Blackman window
  BLACKMAN_HARRIS,  // Blackman-Harris window
  FLAT_TOP          // Flat-top window
};

}
```

### Values

| Value | Description | Coherent Gain | Use Case |
|-------|-------------|---------------|----------|
| `RECTANGLE` | Uniform window, no tapering | 1.0 | Maximum frequency resolution, highest sidelobes |
| `HANN` | Hann (Hanning) window | 0.5 | Good balance between main lobe width and side lobe suppression |
| `HAMMING` | Hamming window | 0.54 | Similar to Hann but with slightly better side lobe suppression |
| `BLACKMAN` | Blackman window | 0.42659 | Better side lobe suppression, wider main lobe |
| `BLACKMAN_HARRIS` | Blackman-Harris window | 0.35875 | Very good side lobe suppression, wider main lobe |
| `FLAT_TOP` | Flat-top window | 1.0 | Accurate amplitude measurement, poor frequency resolution |

### Usage Example

```cpp
#include "signal/signal_processor.h"

using namespace openspectrum;

// Create a signal processor with Blackman-Harris window
SignalProcessor processor(4096);
processor.set_window(WindowFunction::BLACKMAN_HARRIS);

// Convert enum to string for display
const char* window_name = SignalProcessor::window_function_to_string(
    WindowFunction::HANN);
// window_name = "Hann"

// Get coherent gain for normalization
float gain = SignalProcessor::get_coherent_gain(WindowFunction::HAMMING);
// gain = 0.54f
```

### See Also

- [SignalProcessor](signal_processing.md) - Uses WindowFunction for windowing
- [FftAnalyzer](fft_analysis.md) - Uses window coherent gain for normalization
- [RuntimeControls](runtime_controls.md) - Allows changing window function at runtime

---

## ColorMap Enum

Defined in: `src/visualization/spectrum_display.h`

The `ColorMap` enum defines the available color palettes for spectrum and waterfall visualization.

```cpp
namespace openspectrum {

class SpectrumPalette {
public:
  enum class ColorMap { 
    JET,       // Rainbow colormap (default)
    VIRIDIS,   // Perceptually uniform sequential colormap
    HOT,       // Heat map (black-red-yellow-white)
    GRAyscale, // Grayscale
    BLUE_RED   // Blue to red gradient
  };
};

}
```

### Values

| Value | Description |
|-------|-------------|
| `JET` | Classic rainbow colormap (blue-cyan-green-yellow-red), good for visual distinction |
| `VIRIDIS` | Perceptually uniform, colorblind-friendly, good for data visualization |
| `HOT` | Heat map from black through red, orange, yellow to white |
| `GRAyscale` | Simple grayscale from black to white |
| `BLUE_RED` | Blue to red gradient, good for highlighting peaks |

### Usage Example

```cpp
#include "visualization/spectrum_display.h"
#include "visualization/waterfall_display.h"

using namespace openspectrum;

// Create displays with different color maps
SpectrumDisplay spectrum(1024, 512);
spectrum.set_color_map(SpectrumPalette::ColorMap::VIRIDIS);

WaterfallDisplay waterfall(1024, 512, 256);
waterfall.set_color_map(SpectrumPalette::ColorMap::JET);
```

### See Also

- [SpectrumPalette](visualization.md#spectrumpalette) - Manages color palettes
- [SpectrumDisplay](visualization.md#spectrumdisplay) - Uses color maps for rendering
- [WaterfallDisplay](visualization.md#waterfalldisplay) - Uses color maps for rendering

---

## LogLevel Enum

Defined in: `src/utils/logger.h`

The `LogLevel` enum defines the severity levels for logging messages.

```cpp
namespace openspectrum {

enum class LogLevel {
  TRACE,    // Very detailed debugging information
  DEBUG,    // Debugging information
  INFO,     // Informational messages
  WARNING,  // Warning messages
  ERROR,    // Error messages
  CRITICAL  // Critical errors that may cause termination
};

}
```

### Values

| Value | Description | Typical Usage |
|-------|-------------|---------------|
| `TRACE` | Very verbose, fine-grained debugging | Function entry/exit, variable values |
| `DEBUG` | Debug information | State changes, internal flow |
| `INFO` | Informational messages (default) | Startup, configuration, major events |
| `WARNING` | Warning messages | Recoverable issues, unexpected conditions |
| `ERROR` | Error messages | Non-recoverable issues, API misuse |
| `CRITICAL` | Critical errors | Fatal errors requiring immediate attention |

### Usage Example

```cpp
#include "utils/logger.h"

using namespace openspectrum;

// Set log level (only messages at or above this level will be logged)
Logger::get_instance().set_level(LogLevel::DEBUG);

// Log at different levels
LOG_TRACE("Entering function with param: " + std::to_string(value));
LOG_DEBUG("Processing sample batch");
LOG_INFO("Device initialized successfully");
LOG_WARNING("Sample rate higher than recommended");
LOG_ERROR("Failed to open device");
LOG_CRITICAL("Out of memory!");
```

### See Also

- [Logger](utilities.md#logger) - The logging system
- [Logging Macros](utilities.md#logging-macros) - All available logging macros

---

## Other Types

### RgbColor Struct

Defined in: `src/visualization/spectrum_display.h`

Represents an RGBA color value. See [Visualization](visualization.md#rgbcolor) for details.

### kiss_fft_cpx Type

Defined in: `third_party/kissfft/kiss_fft.h`

A complex number type used by the KissFFT library. See [Third Party Types](third_party.md) for details.

### kiss_fft_cfg Type

Defined in: `third_party/kissfft/kiss_fft.h`

An opaque configuration pointer type used by KissFFT. See [Third Party Types](third_party.md) for details.
