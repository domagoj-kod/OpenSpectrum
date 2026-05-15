# IQ Logging API

Defined in: `include/openspectrum/iq_logger.h`

The IQ Logging module provides functionality for capturing and saving raw IQ sample data to files for post-processing, analysis, or sharing. This feature enables users to record radio signals and analyze them later using other tools.

---

## Namespace

```cpp
namespace openspectrum {
  // All types and classes defined here
}
```

---

## Overview

The IQ Logging system captures raw complex IQ samples from the SDR device and saves them to disk in a binary format, along with metadata in JSON format. This allows for:

- Post-processing of captured signals
- Offline analysis without the SDR hardware
- Sharing captured data with others
- Long-duration recording for later review

### Features

- Configurable output directory and filename prefix
- Automatic file naming with timestamps
- Configurable buffer sizes for performance
- Optional maximum file size limit (for splitting large captures)
- Capture statistics tracking (peak, average, min, max dB)
- Progress and completion callbacks
- Thread-safe sample writing

### File Format

Each capture produces two files:

1. **Data File** (`.iq` extension): Binary format containing interleaved float32 I and Q samples
   - Format: Little-endian float32 pairs (I, Q, I, Q, ...)
   - Can be read by most SDR analysis tools

2. **Metadata File** (`.json` extension): JSON format containing capture parameters
   - Center frequency
   - Sample rate
   - Gain
   - FFT size
   - Window function
   - Timestamps
   - Capture statistics
   - Optional notes

---

## IqCaptureStats Struct

Captures statistics for the current or completed IQ capture.

```cpp
struct IqCaptureStats {
  double peak_db = -140.0;
  double average_db = -140.0;
  double min_db = -140.0;
  double max_db = -140.0;
  size_t sample_count = 0;
  double duration_seconds = 0.0;
};
```

### Members

| Member | Type | Default | Description |
|--------|------|---------|-------------|
| `peak_db` | `double` | -140.0 | Peak signal level in dB |
| `average_db` | `double` | -140.0 | Average signal level in dB |
| `min_db` | `double` | -140.0 | Minimum signal level in dB |
| `max_db` | `double` | -140.0 | Maximum signal level in dB |
| `sample_count` | `size_t` | 0 | Total number of samples captured |
| `duration_seconds` | `double` | 0.0 | Duration of capture in seconds |

---

## IqLoggerConfig Struct

Configuration options for the IQ logger.

```cpp
struct IqLoggerConfig {
  std::string output_directory = "data";
  std::string filename_prefix = "capture";
  size_t max_file_size_bytes = 0;     // 0 = unlimited
  size_t buffer_size_bytes = 1048576; // 1 MB default buffer
};
```

### Members

| Member | Type | Default | Description |
|--------|------|---------|-------------|
| `output_directory` | `std::string` | "data" | Directory where capture files will be saved |
| `filename_prefix` | `std::string` | "capture" | Prefix for auto-generated filenames |
| `max_file_size_bytes` | `size_t` | 0 | Maximum file size before splitting (0 = unlimited) |
| `buffer_size_bytes` | `size_t` | 1,048,576 | Buffer size in bytes for efficient writing (1 MB default) |

### Usage Example

```cpp
#include "openspectrum/iq_logger.h"

using namespace openspectrum;

// Create configuration
IqLoggerConfig config;
config.output_directory = "captures";
config.filename_prefix = "scan_2024";
config.buffer_size_bytes = 4 * 1024 * 1024; // 4 MB buffer

// Create logger with configuration
IqLogger logger(config);
```

---

## Callback Types

The IqLogger supports two callback types for monitoring capture progress.

### IqLoggerProgressCallback

```cpp
using IqLoggerProgressCallback = std::function<void(size_t bytes_written, size_t total_bytes)>;
```

Callback invoked periodically during capture to report progress.

**Parameters:**
- `bytes_written` - Number of bytes written in this update
- `total_bytes` - Total number of bytes written so far

**Usage:**
```cpp
logger.set_progress_callback([](size_t bytes_written, size_t total_bytes) {
    std::cout << "Progress: " << total_bytes << " bytes written\n";
});
```

### IqLoggerCompleteCallback

```cpp
using IqLoggerCompleteCallback = std::function<void(
    const std::string &filename, const std::string &metadata_filename)>;
```

Callback invoked when a capture is stopped and files are finalized.

**Parameters:**
- `filename` - Path to the data file
- `metadata_filename` - Path to the metadata file

**Usage:**
```cpp
logger.set_complete_callback([](const std::string &data_file, 
                                  const std::string &meta_file) {
    std::cout << "Capture saved to: " << data_file << "\n";
    std::cout << "Metadata saved to: " << meta_file << "\n";
});
```

---

## IqLogger Class

The main class for managing IQ data capture.

```cpp
class IqLogger {
public:
  explicit IqLogger(const IqLoggerConfig &config = IqLoggerConfig());
  ~IqLogger();

  // Capture control
  bool start_capture(uint32_t center_freq_hz, uint32_t sample_rate_hz,
                     float gain_db, size_t fft_size = 0,
                     const std::string &window_function = "",
                     const std::string &notes = "");
  void write_samples(const std::vector<std::complex<float>> &samples);
  void write_sample(std::complex<float> sample);
  void stop_capture();
  bool is_capturing() const noexcept;

  // Statistics
  IqCaptureStats get_stats() const;

  // Callbacks
  void set_progress_callback(IqLoggerProgressCallback cb);
  void set_complete_callback(IqLoggerCompleteCallback cb);

  // Filename access
  std::string get_data_filename() const;
  std::string get_metadata_filename() const;

  // Configuration
  const IqLoggerConfig &get_config() const;
};
```

### Constructor

```cpp
explicit IqLogger(const IqLoggerConfig &config = IqLoggerConfig());
```

Constructs an IqLogger with the specified configuration.

**Parameters:**
- `config` - Configuration options (default: IqLoggerConfig())

**Initial State:**
- Not capturing
- Output directory will be created if it doesn't exist
- No callbacks set
- Statistics reset to defaults

### Destructor

```cpp
~IqLogger();
```

Destructor that ensures any active capture is properly stopped and files are finalized.

---

### Capture Control Methods

#### `start_capture`

```cpp
bool start_capture(uint32_t center_freq_hz, uint32_t sample_rate_hz,
                   float gain_db, size_t fft_size = 0,
                   const std::string &window_function = "",
                   const std::string &notes = "");
```

Starts a new IQ data capture.

**Parameters:**
- `center_freq_hz` - Center frequency in Hz
- `sample_rate_hz` - Sample rate in Hz
- `gain_db` - Gain in dB
- `fft_size` - FFT size used (for metadata, optional)
- `window_function` - Window function name (for metadata, optional)
- `notes` - User notes (for metadata, optional)

**Returns:**
- `true` if capture started successfully
- `false` if capture failed to start (e.g., files couldn't be opened)

**Behavior:**
- Creates output directory if it doesn't exist
- Generates unique filenames with timestamp
- Opens data and metadata files
- Resets statistics
- Starts timing the capture

**Note:** If a capture is already in progress, it will be stopped first.

---

#### `write_samples`

```cpp
void write_samples(const std::vector<std::complex<float>> &samples);
```

Writes a batch of IQ samples to the capture file.

**Parameters:**
- `samples` - Vector of complex float samples (I and Q)

**Thread Safety:** This method is thread-safe and can be called from multiple threads.

**Behavior:**
- Converts complex<float> samples to binary format (float32 I, float32 Q)
- Buffers samples for efficient writing
- Flushes buffer when full
- Updates capture statistics

---

#### `write_sample`

```cpp
void write_sample(std::complex<float> sample);
```

Writes a single IQ sample to the capture file.

**Parameters:**
- `sample` - Single complex float sample (I and Q)

**Thread Safety:** This method is thread-safe.

**Note:** For better performance, prefer `write_samples()` with batches of samples.

---

#### `stop_capture`

```cpp
void stop_capture();
```

Stops the current capture and finalizes the files.

**Behavior:**
- Flushes any buffered samples
- Calculates final duration
- Writes metadata JSON file
- Closes both data and metadata files
- Invokes completion callback if set
- Resets capturing state

**Safe to Call:** Multiple times, or when not capturing.

---

#### `is_capturing`

```cpp
bool is_capturing() const noexcept;
```

Checks if a capture is currently in progress.

**Returns:**
- `true` if capturing
- `false` if not capturing

---

### Statistics Methods

#### `get_stats`

```cpp
IqCaptureStats get_stats() const;
```

Returns the current capture statistics.

**Returns:**
- Copy of the current IqCaptureStats

**Use Case:**
- Display real-time statistics during capture
- Log capture parameters after completion

---

### Callback Methods

#### `set_progress_callback`

```cpp
void set_progress_callback(IqLoggerProgressCallback cb);
```

Sets the progress callback function.

**Parameters:**
- `cb` - Callback function (can be null to disable)

---

#### `set_complete_callback`

```cpp
void set_complete_callback(IqLoggerCompleteCallback cb);
```

Sets the completion callback function.

**Parameters:**
- `cb` - Callback function (can be null to disable)

---

### Filename Access Methods

#### `get_data_filename`

```cpp
std::string get_data_filename() const;
```

Returns the current data file path.

**Returns:**
- Path to the data file (empty if not capturing)

---

#### `get_metadata_filename`

```cpp
std::string get_metadata_filename() const;
```

Returns the current metadata file path.

**Returns:**
- Path to the metadata file (empty if not capturing)

---

#### `get_config`

```cpp
const IqLoggerConfig &get_config() const;
```

Returns the current configuration.

**Returns:**
- Reference to the current IqLoggerConfig

---

## CLI Integration

The IQ logging feature is integrated with the command-line interface through the argument parser.

### AppConfig Fields

The following fields were added to `AppConfig` in `src/utils/arg_parser.h`:

```cpp
// IQ logging options
bool iq_logging_enabled = false;
double iq_capture_duration = 0.0; // seconds (0 = manual stop via keyboard)
std::string iq_output_file;       // Output filename prefix (without extension)
```

### Command-Line Arguments

| Argument | Description |
|----------|-------------|
| `--iq-log` | Enable IQ data logging to file |
| `--iq-duration SEC` | Capture duration in seconds (0 = manual stop via Ctrl+S) |
| `--iq-output FILE` | Output filename prefix (default: auto-generated) |

### Usage Examples

```bash
# Start capture immediately with 10 second duration
./openspectrum --iq-log --iq-duration 10

# Manual capture (stop with Ctrl+S)
./openspectrum --iq-log

# Custom output filename
./openspectrum --iq-log --iq-output my_fm_station
```

---

## UI Integration

### Keyboard Shortcut

- **Ctrl+S**: Toggle IQ logging on/off during runtime

### Status Display

The `SdlRenderer` class displays IQ logging status in the bottom-left corner of the window:
- Shows "IQ: Idle" when not capturing
- Shows "IQ: Recording" with duration when capturing
- Shows file size and sample count during capture

---

## Usage Example: Complete Capture Workflow

### Using CLI Arguments

```cpp
#include "utils/arg_parser.h"
#include "openspectrum/iq_logger.h"
#include "hardware/rtl_sdr_device.h"

using namespace openspectrum;

int main(int argc, char *argv[]) {
    AppConfig config = parse_arguments(argc, argv);
    
    if (config.iq_logging_enabled) {
        IqLogger logger;
        IqLoggerConfig logger_config;
        
        if (!config.iq_output_file.empty()) {
            logger_config.filename_prefix = config.iq_output_file;
        }
        
        // Set up callbacks
        logger.set_progress_callback([](size_t bytes, size_t total) {
            std::cout << "IQ: " << total << " bytes written\r" << std::flush;
        });
        
        logger.set_complete_callback([](const std::string &data, 
                                          const std::string &meta) {
            std::cout << "\nCapture saved to: " << data << std::endl;
            std::cout << "Metadata: " << meta << std::endl;
        });
        
        // Start capture
        RtlSdrDevice device;
        device.open();
        device.set_frequency(static_cast<uint32_t>(config.center_freq_hz));
        device.set_sample_rate(static_cast<uint32_t>(config.sample_rate_hz));
        device.set_gain(config.gain_db);
        
        logger.start_capture(
            static_cast<uint32_t>(config.center_freq_hz),
            static_cast<uint32_t>(config.sample_rate_hz),
            config.gain_db,
            config.fft_size,
            SignalProcessor::window_function_to_string(config.window_function),
            "CLI capture"
        );
        
        // In main loop: read samples and write to logger
        while (logger.is_capturing()) {
            auto samples = device.read_samples(4096);
            logger.write_samples(samples);
            
            // Check duration limit
            if (config.iq_capture_duration > 0 && 
                logger.get_stats().duration_seconds >= config.iq_capture_duration) {
                logger.stop_capture();
            }
        }
    }
    
    return 0;
}
```

---

### Interactive Capture with Keyboard Shortcut

```cpp
#include "openspectrum/iq_logger.h"
#include "openspectrum/control_state.h"
#include "gui/sdl_renderer.h"

using namespace openspectrum;

int main() {
    SdlRenderer renderer(1024, 576);
    ControlState state;
    IqLogger iq_logger;
    
    // Main loop
    bool running = true;
    while (running) {
        if (!renderer.poll_events(&state)) {
            running = false;
        }
        
        // Check if IQ logging was toggled via Ctrl+S
        if (state.iq_logging_toggle_requested()) {
            if (iq_logger.is_capturing()) {
                iq_logger.stop_capture();
            } else {
                // Start new capture with current settings
                iq_logger.start_capture(
                    state.get_frequency(),
                    2048000, // sample rate
                    state.get_gain(),
                    state.get_fft_size(),
                    SignalProcessor::window_function_to_string(state.get_window())
                );
            }
            state.clear_iq_logging_toggle();
        }
        
        // Render IQ status
        if (iq_logger.is_capturing()) {
            auto stats = iq_logger.get_stats();
            std::string iq_status = "IQ: Recording " + 
                std::to_string(static_cast<int>(stats.duration_seconds)) + "s";
            renderer.render_iq_status(iq_status);
        } else {
            renderer.render_iq_status("IQ: Idle (Ctrl+S)");
        }
    }
    
    return 0;
}
```

---

## Metadata File Format

The metadata file (`.json`) contains all capture parameters in JSON format:

```json
{
  "capture_info": {
    "center_frequency_hz": 100000000,
    "sample_rate_hz": 2048000,
    "gain_db": 20.0,
    "fft_size": 4096,
    "window_function": "Blackman-Harris",
    "notes": "My first capture"
  },
  "statistics": {
    "peak_db": -15.5,
    "average_db": -45.2,
    "min_db": -80.0,
    "max_db": -10.0,
    "sample_count": 1048576,
    "duration_seconds": 0.5
  },
  "file_info": {
    "data_file": "capture_20240515_123456.iq",
    "timestamp": "2024-05-15T12:34:56Z"
  }
}
```

---

## Performance Considerations

1. **Buffer Size:** Larger buffers reduce I/O operations but increase memory usage. The default 1 MB buffer provides a good balance.

2. **Thread Safety:** `write_samples()` and `write_sample()` are thread-safe, allowing concurrent capture from multiple sources.

3. **File Size Limits:** Use `max_file_size_bytes` to automatically split large captures into multiple files.

4. **Duration vs Manual:** For known capture durations, use `--iq-duration`. For interactive capture, use manual mode (Ctrl+S).

5. **Disk I/O:** Capture to a fast disk (SSD recommended) for best performance, especially at high sample rates.

---

## See Also

- [ControlState](control_state.md) - Manages capture parameters (frequency, gain, etc.)
- [AppConfig](utilities.md#appconfig-struct) - Command-line configuration including IQ options
- [RtlSdrDevice](hardware.md) - Source of IQ samples
- [SignalProcessor](signal_processing.md) - Processes samples before capture
