# Spectrogram Export API

Defined in: `include/openspectrum/spectrogram_exporter.h`

The Spectrogram Export module provides functionality for exporting spectrum and waterfall displays as PNG images with optional metadata JSON files. This enables shareable results and further ML analysis of captured spectrum data.

---

## Namespace

```cpp
namespace openspectrum {
  // SpectrogramExporter, SpectrogramExportConfig, ExportResult
}
```

---

## Overview

The Spectrogram Export system captures the current visualization state (spectrum display, waterfall display, or both) and saves it as a PNG image file. Optionally, it generates a companion JSON metadata file containing all capture parameters for traceability and ML analysis pipelines.

### Features

- PNG export with configurable compression level (0-9)
- Optional JSON metadata sidecar file (`.meta.json`)
- Configurable output directory and filename prefix
- Thread-safe operation via mutex protection
- Support for three export modes:
  - Combined spectrum + waterfall as single image
  - Spectrum-only export
  - Waterfall-only export
- Automatic directory creation
- Timestamp-based unique filenames

### Use Cases

- Sharing spectrum analysis results with others
- Archiving interesting signal captures
- ML training data generation with labeled metadata
- Post-processing and analysis of static spectrum images

### File Format

Each export produces one or two files:

1. **PNG File** (`.png` extension): RGBA format image
   - Contains the visualized spectrum/waterfall data
   - Standard PNG format, compatible with any image viewer
   - RGBA color channels (4 bytes per pixel)

2. **Metadata File** (`.meta.json` extension, optional): JSON format containing:
   - Export timestamp (ISO 8601 and Unix)
   - Capture parameters (frequency, sample rate, gain, FFT size, window function)
   - Image parameters (type, dimensions, format, color map, compression level)
   - Application info (name, version, platform)
   - Optional user notes

---

## SpectrogramExportConfig Struct

Configuration options for the spectrogram exporter.

```cpp
struct SpectrogramExportConfig {
  std::string output_directory = "spectrograms";
  std::string filename_prefix = "spectrogram";
  bool include_metadata = true;
  int png_compression_level = 8; // 0-9, 9 = maximum
};
```

### Members

| Member | Type | Default | Description |
|--------|------|---------|-------------|
| `output_directory` | `std::string` | "spectrograms" | Directory for output files. Created if doesn't exist. |
| `filename_prefix` | `std::string` | "spectrogram" | Base name for output files. Timestamp and extension appended. |
| `include_metadata` | `bool` | `true` | Whether to generate JSON metadata sidecar file. |
| `png_compression_level` | `int` | `8` | PNG compression level (0-9). Higher = smaller files, slower. |

---

## ExportResult Struct

Result of an export operation.

```cpp
struct ExportResult {
  bool success = false;
  std::string filename;
  std::string metadata_filename;
  std::string error_message;
};
```

### Members

| Member | Type | Description |
|--------|------|-------------|
| `success` | `bool` | Whether the export succeeded. |
| `filename` | `std::string` | Full path to the exported PNG file. |
| `metadata_filename` | `std::string` | Full path to the metadata JSON file (empty if disabled or failed). |
| `error_message` | `std::string` | Error description if export failed. |

---

## SpectrogramExporter Class

Main class for exporting spectrogram visualizations as PNG images.

```cpp
class SpectrogramExporter {
public:
  explicit SpectrogramExporter(
      const SpectrogramExportConfig &config = SpectrogramExportConfig());
  
  ~SpectrogramExporter() = default;

  // Export methods
  ExportResult export_combined(
      const PixelBuffer &spectrum_pixels,
      const PixelBuffer &waterfall_pixels,
      size_t display_width,
      size_t spectrum_height,
      size_t waterfall_height,
      uint32_t center_freq_hz,
      uint32_t sample_rate_hz,
      float gain_db,
      size_t fft_size,
      const std::string &window_function,
      const std::string &color_map = "jet",
      const std::string &notes = "");

  ExportResult export_spectrum(
      const PixelBuffer &pixels,
      size_t width,
      size_t height,
      uint32_t center_freq_hz,
      uint32_t sample_rate_hz,
      float gain_db,
      size_t fft_size,
      const std::string &window_function,
      const std::string &color_map = "jet",
      const std::string &notes = "");

  ExportResult export_waterfall(
      const PixelBuffer &pixels,
      size_t width,
      size_t height,
      uint32_t center_freq_hz,
      uint32_t sample_rate_hz,
      float gain_db,
      size_t fft_size,
      const std::string &window_function,
      const std::string &color_map = "jet",
      const std::string &notes = "");

  // Configuration
  const SpectrogramExportConfig &get_config() const;
  void set_config(const SpectrogramExportConfig &config);
};
```

### Constructor

```cpp
SpectrogramExporter::SpectrogramExporter(
    const SpectrogramExportConfig &config = SpectrogramExportConfig());
```

Creates a new SpectrogramExporter instance with the specified configuration.

**Parameters:**
- `config` - Export configuration. Defaults to sensible defaults.

**Throws:** None

**Example:**
```cpp
// Use default configuration
SpectrogramExporter exporter;

// Custom configuration
SpectrogramExportConfig config;
config.output_directory = "captures";
config.filename_prefix = "my_signal";
config.png_compression_level = 9;
SpectrogramExporter exporter(config);
```

---

### export_combined()

Exports both spectrum and waterfall displays as a single combined PNG image.

```cpp
ExportResult SpectrogramExporter::export_combined(
    const PixelBuffer &spectrum_pixels,
    const PixelBuffer &waterfall_pixels,
    size_t display_width,
    size_t spectrum_height,
    size_t waterfall_height,
    uint32_t center_freq_hz,
    uint32_t sample_rate_hz,
    float gain_db,
    size_t fft_size,
    const std::string &window_function,
    const std::string &color_map = "jet",
    const std::string &notes = "");
```

**Parameters:**
- `spectrum_pixels` - Pixel buffer from SpectrumDisplay (RGBA format)
- `waterfall_pixels` - Pixel buffer from WaterfallDisplay (RGBA format)
- `display_width` - Width of the display in pixels
- `spectrum_height` - Height of the spectrum display in pixels
- `waterfall_height` - Height of the waterfall display in pixels
- `center_freq_hz` - Center frequency in Hz
- `sample_rate_hz` - Sample rate in Hz
- `gain_db` - Gain in dB
- `fft_size` - FFT size used
- `window_function` - Window function name (e.g., "blackman-harris")
- `color_map` - Color map name (e.g., "jet")
- `notes` - Optional user notes for metadata

**Returns:** `ExportResult` with success status and filenames

**Example:**
```cpp
// In main processing loop
auto result = spectrogram_exporter.export_combined(
    spectrum_display.get_pixels(),
    waterfall_display.get_pixels(),
    DISPLAY_WIDTH,
    spectrum_display.height(),
    waterfall_display.height(),
    control_state.get_frequency(),
    sample_rate_hz,
    control_state.get_gain(),
    current_fft_size,
    SignalProcessor::window_function_to_string(control_state.get_window()),
    "jet");

if (result.success) {
    std::cout << "Exported: " << result.filename << std::endl;
}
```

---

### export_spectrum()

Exports only the spectrum display as a PNG image.

```cpp
ExportResult SpectrogramExporter::export_spectrum(
    const PixelBuffer &pixels,
    size_t width,
    size_t height,
    uint32_t center_freq_hz,
    uint32_t sample_rate_hz,
    float gain_db,
    size_t fft_size,
    const std::string &window_function,
    const std::string &color_map = "jet",
    const std::string &notes = "");
```

**Parameters:** Same as `export_combined()` but for spectrum-only.

**Returns:** `ExportResult` with success status and filenames

---

### export_waterfall()

Exports only the waterfall display as a PNG image.

```cpp
ExportResult SpectrogramExporter::export_waterfall(
    const PixelBuffer &pixels,
    size_t width,
    size_t height,
    uint32_t center_freq_hz,
    uint32_t sample_rate_hz,
    float gain_db,
    size_t fft_size,
    const std::string &window_function,
    const std::string &color_map = "jet",
    const std::string &notes = "");
```

**Parameters:** Same as `export_combined()` but for waterfall-only.

**Returns:** `ExportResult` with success status and filenames

---

### get_config() / set_config()

```cpp
const SpectrogramExportConfig &get_config() const;
void set_config(const SpectrogramExportConfig &config);
```

Get or set the exporter configuration. Configuration changes take effect on the next export.

**Thread Safety:** These methods are thread-safe (protected by mutex).

---

## Usage Example

```cpp
#include "openspectrum/spectrogram_exporter.h"

using namespace openspectrum;

// Initialize exporter
SpectrogramExportConfig config;
config.output_directory = "exports";
config.png_compression_level = 9;
SpectrogramExporter exporter(config);

// In rendering loop, when export is requested
if (export_requested) {
    auto result = exporter.export_combined(
        spectrum_display.get_pixels(),
        waterfall_display.get_pixels(),
        display_width,
        spectrum_height,
        waterfall_height,
        frequency_hz,
        sample_rate_hz,
        gain_db,
        fft_size,
        window_function_name);
    
    if (result.success) {
        LOG_INFO("Export successful: " + result.filename);
    } else {
        LOG_ERROR("Export failed: " + result.error_message);
    }
}
```

---

## Metadata JSON Schema

The metadata file (`.meta.json`) follows this schema:

```json
{
  "version": "1.0",
  "export_timestamp_iso8601": "20260115T103000Z",
  "export_timestamp_unix": 1736941000.123,
  "capture": {
    "center_frequency_hz": 100000000,
    "center_frequency_formatted": "100.0 MHz",
    "sample_rate_hz": 2048000,
    "gain_db": 20.0,
    "fft_size": 4096,
    "window_function": "blackman-harris"
  },
  "image": {
    "type": "combined",
    "width": 800,
    "height": 480,
    "format": "PNG",
    "color_map": "jet",
    "compression_level": 8
  },
  "application": {
    "name": "OpenSpectrum",
    "version": "1.0.0-nightly",
    "platform": "Linux"
  }
}
```

---

## Thread Safety

The `SpectrogramExporter` class is thread-safe. All public methods use a mutex to protect internal state. Multiple threads can safely call export methods concurrently.

---

## Dependencies

- **stb_image_write.h** - Single-header PNG/BMP/TGA/JPEG/HDR writer (public domain)
- ** PixelBuffer** - From `spectrum_display.h`

---

## See Also

- [IQ Logging API](iq_logging.md) - For raw IQ sample capture
- [Visualization API](visualization.md) - For SpectrumDisplay and WaterfallDisplay
- [Control State API](control_state.md) - For user parameter management
