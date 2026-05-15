# Utilities API

Defined in: `src/utils/arg_parser.h` and `src/utils/logger.h`

The Utilities module provides cross-cutting functionality including command-line argument parsing, configuration management, and a comprehensive logging system.

---

## Namespace

```cpp
namespace openspectrum {
  // All types, classes, and functions defined here
}
```

---

## AppConfig Struct

Configuration structure for command-line arguments.

```cpp
struct AppConfig {
  size_t fft_size = 4096;
  size_t display_width = 1050;
  size_t display_height = 576;
  float center_freq_hz = 92600000.0f;    // 92.6 MHz
  float sample_rate_hz = 2048000.0f;     // 2.048 MS/s
  float gain_db = 10.0f;                 // 10 dB
  WindowFunction window_function = WindowFunction::BLACKMAN_HARRIS;
  bool show_help = false;

  // IQ logging options
  bool iq_logging_enabled = false;
  double iq_capture_duration = 0.0; // seconds (0 = manual stop via keyboard)
  std::string iq_output_file;       // Output filename prefix (without extension)
};
```

### Members

| Member | Type | Default | Description |
|--------|------|---------|-------------|
| `fft_size` | `size_t` | 4096 | FFT size (must be power of two) |
| `display_width` | `size_t` | 1050 | Display window width in pixels |
| `display_height` | `size_t` | 576 | Display window height in pixels |
| `center_freq_hz` | `float` | 92,600,000 | Center frequency in Hz |
| `sample_rate_hz` | `float` | 2,048,000 | Sample rate in Hz |
| `gain_db` | `float` | 10.0 | Gain in dB |
| `window_function` | `WindowFunction` | BLACKMAN_HARRIS | Window function for signal processing |
| `show_help` | `bool` | false | Flag to display help message |
| `iq_logging_enabled` | `bool` | false | Enable IQ data logging to file |
| `iq_capture_duration` | `double` | 0.0 | Capture duration in seconds (0 = manual stop via Ctrl+S) |
| `iq_output_file` | `std::string` | "" | Output filename prefix (auto-generated if empty) |

### Usage Example

```cpp
#include "utils/arg_parser.h"

using namespace openspectrum;

int main(int argc, char *argv[]) {
    AppConfig config = parse_arguments(argc, argv);

    if (config.show_help) {
        print_usage(argv[0]);
        return 0;
    }

    // Use configuration
    std::cout << "FFT Size: " << config.fft_size << std::endl;
    std::cout << "Frequency: " << config.center_freq_hz << " Hz" << std::endl;
    // ...
}
```

---

## Argument Parsing Functions

### `parse_arguments`

```cpp
AppConfig parse_arguments(int argc, char *argv[]);
```

Parses command-line arguments and returns an AppConfig structure.

**Parameters:**
- `argc` - Number of command-line arguments
- `argv` - Array of command-line argument strings

**Returns:**
- AppConfig structure with parsed values

**Supported Arguments:**

| Argument | Description |
|----------|-------------|
| `-h`, `--help` | Show help message |
| `-f FREQ` | Center frequency in Hz (e.g., `-f 100000000`) |
| `-r RATE` | Sample rate in Hz (e.g., `-r 2048000`) |
| `-g GAIN` | Gain in dB (e.g., `-g 20.0`) |
| `-s SIZE` | FFT size (e.g., `-s 4096`) |
| `-w WIDTH` | Display width in pixels |
| `-H HEIGHT` | Display height in pixels |
| `--window WINDOW` | Window function: rectangle, hann, hamming, blackman, blackman_harris, flat_top |
| `--iq-log` | Enable IQ data logging to file |
| `--iq-duration SEC` | Capture duration in seconds (0 = manual stop via Ctrl+S) |
| `--iq-output FILE` | Output filename prefix (default: auto-generated) |

**Example:**
```bash
# Basic usage
./openspectrum -f 100000000 -r 2048000 -g 20.0 -s 8192 --window hann

# With IQ logging (10 second capture)
./openspectrum -f 100000000 --iq-log --iq-duration 10

# With IQ logging (manual stop with Ctrl+S)
./openspectrum -f 100000000 --iq-log --iq-output my_capture
```

### `print_usage`

```cpp
void print_usage(const char *argv0);
```

Prints usage information to the console.

**Parameters:**
- `argv0` - The program name (typically `argv[0]`)

**Behavior:**
- Prints a formatted help message
- Includes all supported command-line options
- Includes default values
- Includes a brief description of each option

**Example Output:**
```
Usage: ./openspectrum [OPTIONS]

OpenSpectrum - SDR Spectrum Analyzer

Options:
  -f, --freq HZ       Center frequency in Hz (default: 92600000)
  -r, --rate HZ       Sample rate in Hz (default: 2048000)
  -g, --gain DB       Gain in dB (default: 10.0)
  -s, --fft-size N    FFT size (power of 2, default: 4096)
  -w, --width N       Display width in pixels (default: 1050)
  -H, --height N      Display height in pixels (default: 576)
  -W, --window NAME   Window function: rectangle, hann, hamming, blackman, blackman-harris, flat-top (default: blackman-harris)
  --iq-log            Enable IQ data logging to file
  --iq-duration SEC   Capture duration in seconds (default: 0 = manual)
  --iq-output FILE    Output filename prefix (default: auto-generated)
  --help              Show this help message

Examples:
  ./openspectrum -f 100000000 -g 20
  ./openspectrum --freq 144500000 --gain 15 --fft-size 8192
  ./openspectrum --iq-log --iq-duration 10 --iq-output my_capture
```

### `is_power_of_two`

```cpp
inline bool is_power_of_two(size_t n);
```

Checks if a number is a power of two.

**Parameters:**
- `n` - The number to check

**Returns:**
- `true` if n is a power of two (1, 2, 4, 8, 16, ...)
- `false` otherwise

**Implementation:**
```cpp
return n > 0 && (n & (n - 1)) == 0;
```

**Usage:**
```cpp
if (!is_power_of_two(config.fft_size)) {
    std::cerr << "FFT size must be a power of two" << std::endl;
    return 1;
}
```

---

## Logging System

### LogLevel Enum

See [Types - LogLevel](types.md#loglevel-enum) for complete documentation.

The supported log levels are:
- `TRACE` - Very detailed debugging information
- `DEBUG` - Debugging information
- `INFO` - Informational messages (default)
- `WARNING` - Warning messages
- `ERROR` - Error messages
- `CRITICAL` - Critical errors

---

### LogEntry Struct

Represents a single log entry with all metadata.

```cpp
struct LogEntry {
  std::chrono::system_clock::time_point timestamp;
  LogLevel level;
  std::string file;
  int line;
  std::string function;
  std::string message;
  std::string thread_id;
};
```

### Members

| Member | Type | Description |
|--------|------|-------------|
| `timestamp` | `std::chrono::system_clock::time_point` | When the log entry was created |
| `level` | `LogLevel` | Severity level of the message |
| `file` | `std::string` | Source file name |
| `line` | `int` | Line number in the source file |
| `function` | `std::string` | Function name |
| `message` | `std::string` | The log message |
| `thread_id` | `std::string` | Thread identifier |

---

### ILogSink Abstract Class

Abstract base class for log output destinations.

```cpp
class ILogSink {
public:
  virtual ~ILogSink() = default;
  virtual void write(const LogEntry &entry) = 0;
  virtual void flush() = 0;
};
```

### Methods

#### `write` (pure virtual)

```cpp
virtual void write(const LogEntry &entry) = 0;
```

Writes a log entry to the sink.

**Parameters:**
- `entry` - The log entry to write

**Implemented by:** Concrete sink classes (ConsoleSink, FileSink)

#### `flush` (pure virtual)

```cpp
virtual void flush() = 0;
```

Flushes any buffered output to the destination.

**Implemented by:** Concrete sink classes

---

### ConsoleSink Class

Outputs log entries to the console (stdout/stderr).

```cpp
class ConsoleSink : public ILogSink {
public:
  void write(const LogEntry &entry) override;
  void flush() override;
};
```

#### `write`

Writes a log entry to the console.

**Behavior:**
- Formats the log entry with timestamp, level, location, and message
- Uses color coding for different log levels (if terminal supports it)
- Writes to stdout by default
- Error and critical messages may go to stderr

#### `flush`

Flushes the console output.

**Behavior:**
- Calls `std::cout.flush()` and/or `std::cerr.flush()`

---

### FileSink Class

Outputs log entries to a file with rotation support.

```cpp
class FileSink : public ILogSink {
public:
  explicit FileSink(const std::string &filename,
                    size_t max_size = 10485760); // 10MB default
  ~FileSink() override;

  void write(const LogEntry &entry) override;
  void flush() override;

private:
  void rotate();
};
```

### Constructor

```cpp
explicit FileSink(const std::string &filename, size_t max_size = 10485760);
```

Constructs a FileSink that writes to the specified file.

**Parameters:**
- `filename` - Path to the log file
- `max_size` - Maximum file size in bytes before rotation (default: 10,485,760 = 10 MB)

**Initial State:**
- Opens the file for appending
- Tracks current file size

### Destructor

```cpp
~FileSink() override;
```

Destructor that closes the file and flushes any buffered data.

**Behavior:**
- Calls `flush()`
- Closes the file with `fclose()`

### Methods

#### `write`

Writes a log entry to the file.

**Behavior:**
- Formats the log entry
- Writes to the file
- Updates current file size
- If file size exceeds max_size, calls `rotate()`

#### `flush`

Flushes the file output.

**Behavior:**
- Calls `fflush()` on the file

#### `rotate` (private)

Rotates the log file by renaming the current file and creating a new one.

**Behavior:**
- Closes the current file
- Renames it with a timestamp suffix (e.g., `spectrum.log.20240513_123456`)
- Opens a new file with the original name
- Resets current file size

---

### Logger Class

Thread-safe logging system with support for multiple sinks.

```cpp
class Logger {
public:
  Logger() = default;
  ~Logger();

  // Singleton access
  static Logger &get_instance();

  // Sink management
  void add_sink(std::unique_ptr<ILogSink> sink);

  // Configuration
  void set_level(LogLevel level) noexcept;

  // Logging
  void log(LogLevel level, const std::string &file, int line,
           const std::string &function, const std::string &message);

  // Flush
  void flush();

private:
  LogLevel m_min_level = LogLevel::INFO;
  std::vector<std::unique_ptr<ILogSink>> m_sinks;
  std::mutex m_mutex;

  Logger(const Logger &) = delete;
  Logger &operator=(const Logger &) = delete;
};
```

### Singleton Access

#### `get_instance` (static)

```cpp
static Logger &get_instance();
```

Returns a reference to the singleton Logger instance.

**Returns:**
- Reference to the global Logger instance

**Thread Safety:**
- Uses magic static pattern (C++11) for thread-safe initialization

**Usage:**
```cpp
Logger::get_instance().log(LogLevel::INFO, "file.cpp", 42, "main", "Hello");
// Or use macros (recommended)
LOG_INFO("Hello");
```

### Sink Management

#### `add_sink`

```cpp
void add_sink(std::unique_ptr<ILogSink> sink);
```

Adds a log sink to the logger.

**Parameters:**
- `sink` - Unique pointer to a log sink (ConsoleSink, FileSink, or custom)

**Behavior:**
- Takes ownership of the sink
- Stores it in the vector of sinks
- The logger will forward all log entries to this sink

**Usage:**
```cpp
// Add console sink
Logger::get_instance().add_sink(std::make_unique<ConsoleSink>());

// Add file sink
Logger::get_instance().add_sink(
    std::make_unique<FileSink>("spectrum.log"));
```

### Configuration

#### `set_level`

```cpp
void set_level(LogLevel level) noexcept;
```

Sets the minimum log level.

**Parameters:**
- `level` - The minimum level to log (messages below this level are discarded)

**Behavior:**
- Stores the minimum level
- Only messages at or above this level will be logged

**Default:** `LogLevel::INFO`

**Usage:**
```cpp
// Log only errors and above
Logger::get_instance().set_level(LogLevel::ERROR);

// Log everything (very verbose)
Logger::get_instance().set_level(LogLevel::TRACE);
```

### Logging Methods

#### `log`

```cpp
void log(LogLevel level, const std::string &file, int line,
         const std::string &function, const std::string &message);
```

Logs a message with full metadata.

**Parameters:**
- `level` - Log level
- `file` - Source file name
- `line` - Line number
- `function` - Function name
- `message` - The log message

**Behavior:**
- Checks if level >= minimum level
- Creates a LogEntry with timestamp, level, location, message, thread ID
- Forwards to all registered sinks

**Thread Safety:**
- Uses mutex to protect sink access

**Note:** Most users should use the logging macros instead of calling this directly.

### Flush Method

#### `flush`

```cpp
void flush();
```

Flushes all registered sinks.

**Behavior:**
- Calls `flush()` on each sink
- Useful before program exit to ensure all log messages are written

---

## Logging Macros

### Basic Logging Macros

These macros provide convenient short forms for logging at specific levels.

| Macro | Equivalent | Description |
|-------|-----------|-------------|
| `LOG_TRACE(msg)` | `Logger::get_instance().log(LogLevel::TRACE, ...)` | Trace level |
| `LOG_DEBUG(msg)` | `Logger::get_instance().log(LogLevel::DEBUG, ...)` | Debug level |
| `LOG_INFO(msg)` | `Logger::get_instance().log(LogLevel::INFO, ...)` | Info level |
| `LOG_WARNING(msg)` | `Logger::get_instance().log(LogLevel::WARNING, ...)` | Warning level |
| `LOG_ERROR(msg)` | `Logger::get_instance().log(LogLevel::ERROR, ...)` | Error level |
| `LOG_CRITICAL(msg)` | `Logger::get_instance().log(LogLevel::CRITICAL, ...)` | Critical level |

**Usage:**
```cpp
#include "utils/logger.h"

using namespace openspectrum;

LOG_INFO("Application started");
LOG_DEBUG("Processing sample batch: " + std::to_string(batch_size));
LOG_WARNING("Sample rate is very high: " + std::to_string(rate));
LOG_ERROR("Failed to open device");
LOG_CRITICAL("Out of memory!");
```

### Stream-Style Logging Macros

These macros provide a stream-like interface for building log messages incrementally.

| Macro | Description |
|-------|-------------|
| `LOGS_TRACE` | Stream at trace level |
| `LOGS_DEBUG` | Stream at debug level |
| `LOGS_INFO` | Stream at info level |
| `LOGS_WARNING` | Stream at warning level |
| `LOGS_ERROR` | Stream at error level |
| `LOGS_CRITICAL` | Stream at critical level |

**Usage:**
```cpp
#include "utils/logger.h"

using namespace openspectrum;

// Single value
LOGS_INFO << "Frequency: " << frequency << " Hz";

// Multiple values
LOGS_DEBUG << "Processing " << samples.size() << " samples, "
           << "FFT size: " << fft_size;

// Complex expressions
LOGS_WARNING << "Device " << device_index << " reported error: "
             << error_code << " (" << error_message << ")";
```

**Implementation:**
These macros create a temporary `LogStream` object that captures the streamed values and logs them when destroyed (at the end of the statement).

---

### LogStream Class

RAII-based log entry builder for stream-style logging.

```cpp
class LogStream {
public:
  LogStream(LogLevel level, std::string file, int line,
            std::string function);
  ~LogStream();

  template <typename T>
  LogStream &operator<<(const T &value);

private:
  LogLevel m_level;
  std::string m_file;
  int m_line;
  std::string m_function;
  std::ostringstream m_ss;
};
```

### Constructor

```cpp
LogStream(LogLevel level, std::string file, int line,
          std::string function);
```

Constructs a LogStream with the specified metadata.

### Destructor

```cpp
~LogStream();
```

Flushes the accumulated message to the logger.

**Behavior:**
- Gets the message from the string stream
- Calls `Logger::get_instance().log()` with all metadata

### Operator<<

```cpp
template <typename T>
LogStream &operator<<(const T &value);
```

Appends a value to the log message stream.

**Parameters:**
- `value` - Value to append (any type with `operator<<` for ostream)

**Returns:**
- Reference to this LogStream for chaining

---

## Complete Logging Example

```cpp
#include "utils/logger.h"
#include "hardware/rtl_sdr_device.h"

using namespace openspectrum;

int main() {
    // Configure logger
    Logger::get_instance().set_level(LogLevel::DEBUG);
    Logger::get_instance().add_sink(std::make_unique<ConsoleSink>());

    // Basic logging
    LOG_INFO("Starting application");

    // Stream-style logging
    LOGS_DEBUG << "Initializing device with frequency: " << 100000000
             << " Hz, sample rate: " << 2048000 << " Hz";

    // Log with context
    RtlSdrDevice device;
    if (!device.open()) {
        LOGS_ERROR << "Failed to open device #" << 0
                  << ". Check if device is connected and not in use.";
        return 1;
    }

    LOG_INFO("Device opened successfully");

    // Conditional logging
    float gain = 20.0f;
    if (gain > 30.0f) {
        LOGS_WARNING << "High gain setting (" << gain << " dB) may cause saturation";
    }

    // Always flush before exit
    Logger::get_instance().flush();

    return 0;
}
```

---

## Custom Log Sinks

You can create custom log sinks by inheriting from `ILogSink`:

```cpp
#include "utils/logger.h"
#include <fstream>

class JsonSink : public openspectrum::ILogSink {
public:
    JsonSink(const std::string& filename) : m_file(filename, std::ios::app) {}
    
    void write(const openspectrum::LogEntry& entry) override {
        m_file << "{\"timestamp\":\"" << format_time(entry.timestamp) << "\","
               << "\"level\":\"" << level_to_string(entry.level) << "\","
               << "\"file\":\"" << entry.file << "\","
               << "\"line\":" << entry.line << ","
               << "\"function\":\"" << entry.function << "\","
               << "\"message\":\"" << escape_json(entry.message) << "\"}" << std::endl;
    }
    
    void flush() override {
        m_file.flush();
    }

private:
    std::ofstream m_file;
    // ... helper methods ...
};

// Usage:
Logger::get_instance().add_sink(std::make_unique<JsonSink>("logs.json"));
```

---

## Performance Considerations

1. **Thread Safety:** Logger uses mutex for thread-safe access
2. **Level Filtering:** Messages below the minimum level are discarded early
3. **Sink Overhead:** Each sink adds overhead; only enable necessary sinks
4. **File I/O:** FileSink performs I/O on every log message; consider buffering for high-frequency logging
5. **String Construction:** Use `LOGS_*` macros to avoid temporary string allocations

---

## See Also

- [Types - LogLevel](types.md#loglevel-enum) - Log level enumeration
- [RtlSdrDevice](hardware.md) - Uses logging for error reporting
- [ControlState](control_state.md) - Uses logging for status changes
- [IqLogger](iq_logging.md) - IQ data logging with progress/completion callbacks
- [RuntimeControls (Deprecated)](runtime_controls.md) - Deprecated class, use ControlState
