# Hardware API

Defined in: `src/hardware/rtl_sdr_device.h`

The Hardware module provides an abstraction for Software-Defined Radio (SDR) hardware devices, currently supporting RTL2832U-based devices through the librtlsdr library.

---

## Dependencies

This module requires:
- librtlsdr library (`librtlsdr-dev` or equivalent)
- Header: `rtl-sdr.h`

---

## RtlSdrDevice Class

The `RtlSdrDevice` class provides a C++ wrapper around the librtlsdr API for RTL2832U-based SDR devices.

```cpp
class RtlSdrDevice {
public:
  explicit RtlSdrDevice(uint32_t index = 0);
  ~RtlSdrDevice();

  // Device management
  bool open();
  void close();
  void reset_buffer();
  bool is_open() const;

  // Configuration
  void set_frequency(uint32_t freq_hz);
  void set_sample_rate(uint32_t rate_hz);
  void set_gain(float gain_db);

  // Data acquisition
  std::vector<std::complex<float>> read_samples(size_t count);

  // Async streaming
  static void start_streaming(size_t buffer_count = 8);
  static void stop_streaming();
  using SampleCallback = std::function<void(std::vector<std::complex<float>>)>;
  void set_callback(SampleCallback cb);

private:
  // Internal state and callbacks
  static void static_callback(const uint8_t *buf, uint32_t len, void *ctx);
  void process_callback(const uint8_t *buf, uint32_t len);

  rtlsdr_dev_t *m_dev = nullptr;
  uint32_t m_index = 0;
  uint32_t m_center_freq = 100000000;
  uint32_t m_sample_rate = 2048000;
  float m_gain = 29.0f;
  SampleCallback m_callback;
  bool m_streaming = false;
};
```

---

### Constructor

```cpp
explicit RtlSdrDevice(uint32_t index = 0);
```

Constructs an RtlSdrDevice for a specific device index.

**Parameters:**
- `index` - Device index (0 for first device, 1 for second, etc.) (default: 0)

**Initial State:**
- Device not opened (`m_dev = nullptr`)
- Default center frequency: 100,000,000 Hz (100 MHz)
- Default sample rate: 2,048,000 Hz (2.048 MS/s)
- Default gain: 29.0 dB
- Not streaming
- No callback set

### Destructor

```cpp
~RtlSdrDevice();
```

Destructor that properly cleans up the device.

**Behavior:**
- Calls `close()` if the device is open

### Device Management Methods

#### `open`

```cpp
bool open();
```

Opens the RTL-SDR device.

**Returns:**
- `true` on success
- `false` on failure

**Behavior:**
- Calls `rtlsdr_open()` with the stored device index
- Stores the device pointer in `m_dev`
- If successful, applies the stored configuration (frequency, sample rate, gain)

**Error Conditions:**
- No device found at the specified index
- Device already in use by another application
- Insufficient permissions
- Driver not loaded

**Diagnostics:**
```cpp
#include <iostream>
#include <rtl-sdr.h>

RtlSdrDevice device;
if (!device.open()) {
    const char* error = rtlsdr_get_last_error();
    std::cerr << "Failed to open device: " << (error ? error : "unknown error") << std::endl;
}
```

#### `close`

```cpp
void close();
```

Closes the RTL-SDR device.

**Behavior:**
- If device is open, calls `rtlsdr_close()`
- Sets `m_dev = nullptr`
- If streaming, calls `stop_streaming()` first

**Safe to Call:** Multiple times, even if device is not open

#### `reset_buffer`

```cpp
void reset_buffer();
```

Resets the USB buffer by discarding any pending data.

**Behavior:**
- Calls `rtlsdr_reset_buffer()` if device is open

**Purpose:**
- Critical for avoiding stale data at startup
- Should be called after opening the device and before the first read
- Clears any data that may have been buffered during device initialization

**Usage:**
```cpp
RtlSdrDevice device;
device.open();
device.reset_buffer();  // Clear USB buffer before first read
// Now safe to read fresh samples
```

#### `is_open`

```cpp
bool is_open() const;
```

Checks if the device is currently open.

**Returns:**
- `true` if device is open and ready for use
- `false` if device is closed

---

### Configuration Methods

#### `set_frequency`

```cpp
void set_frequency(uint32_t freq_hz);
```

Sets the center frequency of the device.

**Parameters:**
- `freq_hz` - Center frequency in Hertz

**Valid Range:**
- Typical RTL2832U: 500,000 Hz to 1,700,000,000 Hz
- Actual range depends on tuner and hardware

**Behavior:**
- Calls `rtlsdr_set_center_freq()` if device is open
- Stores the frequency in `m_center_freq`
- If device is not open, frequency will be applied when `open()` is called

**Note:** Frequency changes may cause a small delay as the PLL reconfigures.

#### `set_sample_rate`

```cpp
void set_sample_rate(uint32_t rate_hz);
```

Sets the sample rate of the device.

**Parameters:**
- `rate_hz` - Sample rate in Hertz

**Valid Range:**
- RTL2832U typical: 225,000 Hz to 3,200,000 Hz
- Common values: 900,000, 1,024,000, 1,800,000, 2,048,000, 2,400,000

**Behavior:**
- Calls `rtlsdr_set_sample_rate()` if device is open
- Stores the rate in `m_sample_rate`
- If device is not open, rate will be applied when `open()` is called

**Note:** Higher sample rates provide wider bandwidth but require more CPU for processing.

#### `set_gain`

```cpp
void set_gain(float gain_db);
```

Sets the gain of the device in decibels.

**Parameters:**
- `gain_db` - Gain in decibels

**Valid Range:**
- RTL2832U typical: 0.0 dB to 49.6 dB
- Actual range depends on tuner

**Behavior:**
- If gain is 0.0, enables automatic gain control (AGC)
- Otherwise, enables manual gain control and sets the gain
- Calls `rtlsdr_set_tuner_gain_mode()` and `rtlsdr_set_tuner_gain()` if device is open
- Stores the gain in `m_gain`

**Note:**
- Higher gain increases sensitivity but may also increase noise
- AGC (gain = 0.0) automatically adjusts gain based on signal strength

---

### Data Acquisition Methods

#### `read_samples`

```cpp
std::vector<std::complex<float>> read_samples(size_t count);
```

Reads a specified number of samples from the device (blocking).

**Parameters:**
- `count` - Number of samples to read (each sample is a complex float: I and Q)

**Returns:**
- Vector of complex samples (size = count)

**Behavior:**
- Allocates a buffer for the requested number of samples
- Calls `rtlsdr_read_sync()` to read samples synchronously
- Converts from the device's native format (uint8_t I/Q pairs) to complex<float>
- Normalizes values to the range [-1.0, +1.0]

**Blocking:** This call blocks until the requested number of samples is available

**Performance:**
- For real-time applications, consider using async streaming instead
- Each call allocates a new vector (may cause heap fragmentation)

**Conversion:**
```cpp
// Native RTL-SDR format: interleaved uint8_t I/Q pairs
// uint8_t buffer: [I0, Q0, I1, Q1, I2, Q2, ...]
// Each I/Q pair is 8 bits (0-255)

// Converted to complex<float>:
// float I = (uint8_I - 127.5f) / 127.5f;  // Normalize to [-1.0, +1.0]
// float Q = (uint8_Q - 127.5f) / 127.5f;
// std::complex<float> sample(I, Q);
```

**Usage:**
```cpp
RtlSdrDevice device;
device.open();
device.set_sample_rate(2048000);

const size_t FFT_SIZE = 4096;
std::vector<std::complex<float>> samples = device.read_samples(FFT_SIZE);
// samples.size() == FFT_SIZE
```

---

### Async Streaming Methods

#### `start_streaming` (static)

```cpp
static void start_streaming(size_t buffer_count = 8);
```

Starts asynchronous streaming mode.

**Parameters:**
- `buffer_count` - Number of buffers to use (default: 8)

**Behavior:**
- Calls `rtlsdr_start_streaming()` (static, affects all devices)
- Sets up async callback mechanism

**Note:** This is a static method that affects the library globally. Use with caution in multi-device scenarios.

#### `stop_streaming` (static)

```cpp
static void stop_streaming();
```

Stops asynchronous streaming mode.

**Behavior:**
- Calls `rtlsdr_stop_streaming()` (static)
- Should be called before destroying devices

#### `SampleCallback`

```cpp
using SampleCallback = std::function<void(std::vector<std::complex<float>>)>;
```

Type alias for the callback function used in async streaming.

**Signature:**
```cpp
void callback(std::vector<std::complex<float>> samples);
```

**Parameters:**
- `samples` - Vector of complex samples received from the device

#### `set_callback`

```cpp
void set_callback(SampleCallback cb);
```

Sets the callback function for async streaming.

**Parameters:**
- `cb` - Callback function to be called with each batch of samples

**Behavior:**
- Stores the callback in `m_callback`
- The callback will be invoked from the static callback when streaming is active

**Usage:**
```cpp
RtlSdrDevice device;
device.open();

// Set up callback
device.set_callback([](std::vector<std::complex<float>> samples) {
    // Process samples as they arrive
    static size_t counter = 0;
    counter += samples.size();
    std::cout << "Received " << samples.size() << " samples (total: " << counter << ")" << std::endl;
});

// Start streaming
RtlSdrDevice::start_streaming(8);

// ... do other work ...

// Stop streaming
RtlSdrDevice::stop_streaming();
```

---

## Private Members

| Member | Type | Description |
|--------|------|-------------|
| `m_dev` | `rtlsdr_dev_t*` | Pointer to the RTL-SDR device |
| `m_index` | `uint32_t` | Device index |
| `m_center_freq` | `uint32_t` | Current center frequency in Hz |
| `m_sample_rate` | `uint32_t` | Current sample rate in Hz |
| `m_gain` | `float` | Current gain in dB |
| `m_callback` | `SampleCallback` | Callback function for async streaming |
| `m_streaming` | `bool` | Whether streaming is active |

---

## Private Callback Methods

### `static_callback`

```cpp
static void static_callback(const uint8_t *buf, uint32_t len, void *ctx);
```

Static callback function that receives data from librtlsdr.

**Parameters:**
- `buf` - Pointer to the received data buffer
- `len` - Length of the data in bytes
- `ctx` - Context pointer (points to the RtlSdrDevice instance)

**Behavior:**
- Casts `ctx` to `RtlSdrDevice*`
- Calls `process_callback()` on that instance

### `process_callback`

```cpp
void process_callback(const uint8_t *buf, uint32_t len);
```

Processes received data and invokes the user callback.

**Parameters:**
- `buf` - Pointer to the received data buffer (interleaved uint8_t I/Q pairs)
- `len` - Length of the data in bytes

**Behavior:**
- Converts the uint8_t buffer to complex<float> samples
- Normalizes values to [-1.0, +1.0]
- If `m_callback` is set, invokes it with the converted samples

**Conversion:**
- Each pair of uint8_t values (I, Q) becomes one complex<float>
- Number of samples = len / 2

---

## Usage Example: Basic

```cpp
#include "hardware/rtl_sdr_device.h"

using namespace openspectrum;

int main() {
    // Create and open device
    RtlSdrDevice device;
    if (!device.open()) {
        std::cerr << "Failed to open RTL-SDR device" << std::endl;
        return 1;
    }

    // Configure device
    device.set_frequency(100000000);    // 100 MHz
    device.set_sample_rate(2048000);   // 2.048 MS/s
    device.set_gain(20.0f);            // 20 dB

    // Reset USB buffer before first read
    device.reset_buffer();

    // Read samples
    const size_t NUM_SAMPLES = 4096;
    std::vector<std::complex<float>> samples = device.read_samples(NUM_SAMPLES);

    std::cout << "Read " << samples.size() << " samples" << std::endl;

    // Close device
    device.close();

    return 0;
}
```

---

## Usage Example: Streaming

```cpp
#include "hardware/rtl_sdr_device.h"
#include "signal/signal_processor.h"
#include "fft/fft_analyzer.h"

using namespace openspectrum;

int main() {
    RtlSdrDevice device;
    if (!device.open()) {
        std::cerr << "Failed to open device" << std::endl;
        return 1;
    }

    device.set_frequency(92600000);    // 92.6 MHz (FM radio)
    device.set_sample_rate(2048000);
    device.set_gain(20.0f);
    device.reset_buffer();

    // Set up processing
    const size_t FFT_SIZE = 4096;
    SignalProcessor processor(FFT_SIZE);
    FftAnalyzer fft(FFT_SIZE);

    // Set up callback
    device.set_callback([&](std::vector<std::complex<float>> samples) {
        // Process batch of samples
        static std::vector<std::complex<float>> accumulated;
        accumulated.insert(accumulated.end(), samples.begin(), samples.end());

        // Process when we have enough samples
        while (accumulated.size() >= FFT_SIZE) {
            std::vector<std::complex<float>> batch(
                accumulated.begin(), accumulated.begin() + FFT_SIZE);
            accumulated.erase(accumulated.begin(), accumulated.begin() + FFT_SIZE);

            // Process
            SignalProcessor::remove_dc(batch);
            processor.apply_window(batch);
            fft.execute(batch);

            // Get spectrum
            const auto& db_spectrum = fft.get_db_spectrum();
            // ... display or analyze spectrum ...
        }
    });

    // Start streaming
    RtlSdrDevice::start_streaming(8);

    // Let it run for a while
    std::this_thread::sleep_for(std::chrono::seconds(10));

    // Stop streaming
    RtlSdrDevice::stop_streaming();
    device.close();

    return 0;
}
```

---

## RTL-SDR Device Information

You can query available devices using the librtlsdr API:

```cpp
#include <rtl-sdr.h>
#include <iostream>

int main() {
    int device_count = rtlsdr_get_device_count();
    std::cout << "Found " << device_count << " RTL-SDR device(s)" << std::endl;

    for (int i = 0; i < device_count; ++i) {
        char manufacturer[256], product[256], serial[256];
        rtlsdr_get_device_usb_strings(i, manufacturer, product, serial);
        
        std::cout << "\nDevice #" << i << ":" << std::endl;
        std::cout << "  Manufacturer: " << manufacturer << std::endl;
        std::cout << "  Product: " << product << std::endl;
        std::cout << "  Serial: " << serial << std::endl;
    }

    rtlsdr_exit();
    return 0;
}
```

---

## Performance Considerations

1. **Sample Rate:** Higher sample rates provide wider bandwidth but require more CPU
2. **USB Bandwidth:** USB 2.0 can handle up to ~3.2 MS/s reliably
3. **Blocking vs Async:** `read_samples()` is blocking; streaming is non-blocking
4. **Buffer Size:** Larger buffers reduce callback frequency but increase latency
5. **Gain Settings:** Manual gain provides consistent results; AGC adapts to signal

---

## Common Issues

| Issue | Cause | Solution |
|-------|-------|----------|
| Device not found | Driver not loaded | Install RTL-SDR drivers |
| Device in use | Another application has it open | Close other applications |
| Permission denied | No access to USB device | Run as root or add udev rules |
| No samples returned | Buffer not reset | Call `reset_buffer()` after `open()` |
| Distorted audio | Wrong sample rate | Use supported sample rates |
| Weak signals | Gain too low | Increase gain or enable AGC |
| Noisy signals | Gain too high | Reduce gain |

---

## Udev Rules (Linux)

To allow non-root access to RTL-SDR devices, create a udev rule:

```bash
# /etc/udev/rules.d/99-rtl-sdr.rules
echo 'SUBSYSTEM=="usb", ATTRS{idVendor]=="0bda", ATTRS{idProduct]=="2838", MODE="0666"' | \
  sudo tee /etc/udev/rules.d/99-rtl-sdr.rules

# Reload udev rules
sudo udevadm control --reload-rules
sudo udevadm trigger
```

---

## See Also

- [RuntimeControls](runtime_controls.md) - Controls device parameters (frequency, gain)
- [SignalProcessor](signal_processing.md) - Processes samples from the device
- [FFT Analyzer](fft_analysis.md) - Analyzes processed samples
- [librtlsdr Documentation](https://osmocom.org/projects/sdr/wiki/rtl-sdr) - RTL-SDR library documentation
