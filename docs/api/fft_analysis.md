# FFT Analysis API

Defined in: `src/fft/fft_analyzer.h`

The FFT Analysis module provides Fast Fourier Transform computation using the KissFFT library, with support for power spectrum, magnitude spectrum, and phase spectrum extraction. It includes DC centering for real signals and window gain compensation.

---

## Namespace

```cpp
namespace openspectrum {
  // All types and classes defined here
}
```

---

## Dependencies

This module depends on the KissFFT library:
- Header: `kiss_fft.h` (from `third_party/kissfft/`)
- Types: `kiss_fft_cfg`, `kiss_fft_cpx`

See [Third Party Types](third_party.md) for KissFFT type documentation.

---

## FftAnalyzer Class

The `FftAnalyzer` class wraps the KissFFT library with RAII (Resource Acquisition Is Initialization) management and provides convenient access to various spectrum representations.

```cpp
class FftAnalyzer {
public:
  explicit FftAnalyzer(size_t fft_size, bool inverse = false);
  ~FftAnalyzer();

  // Move semantics (non-copyable)
  FftAnalyzer(FftAnalyzer &&other) noexcept;
  FftAnalyzer &operator=(FftAnalyzer &&other) noexcept;

  // FFT execution
  void execute(const std::vector<std::complex<float>> &input,
               std::vector<std::complex<float>> &output);
  void execute(const std::vector<std::complex<float>> &input);

  // Spectrum access
  const std::vector<float> &get_power_spectrum() const;
  const std::vector<float> &get_magnitude_spectrum() const;
  const std::vector<float> &get_db_spectrum() const;
  const std::vector<float> &get_phase_spectrum() const;
  const std::vector<float> &get_frequency_bins() const;

  // Configuration
  size_t fft_size() const noexcept;
  void enable_dc_center(bool enabled) noexcept;
  void set_window_coherent_gain(float gain);

private:
  // Internal state
  size_t m_fft_size;
  bool m_center_dc;
  bool m_inverse;
  kiss_fft_cfg m_cfg;
  float m_window_coherent_gain;
  // ... internal buffers and cached results
};
```

### Constructor

```cpp
explicit FftAnalyzer(size_t fft_size, bool inverse = false);
```

Constructs an FFT analyzer with the specified size.

**Parameters:**
- `fft_size` - The FFT size (must be a power of two for best performance with KissFFT)
- `inverse` - If `true`, creates an inverse FFT configuration (default: `false`)

**Initial State:**
- DC centering: disabled
- Window coherent gain: 1.0 (rectangular window)
- Internal buffers are allocated for the specified size
- KissFFT configuration is created

**Note:** For real-time spectrum analysis, use `inverse = false` (forward FFT).

### Destructor

```cpp
~FftAnalyzer();
```

Destructor that properly cleans up KissFFT resources.

**Behavior:**
- Frees the KissFFT configuration (`kiss_fft_cfg`)
- Internal buffers are automatically cleaned up (std::vector)

### Copy and Move Semantics

| Operation | Supported | Description |
|-----------|-----------|-------------|
| Copy constructor | ❌ No | Deleted (non-copyable due to raw pointer) |
| Copy assignment | ❌ No | Deleted |
| Move constructor | ✅ Yes | Transfers ownership of resources |
| Move assignment | ✅ Yes | Transfers ownership of resources |

The class is non-copyable because it contains a raw pointer to the KissFFT configuration (`kiss_fft_cfg`), but it supports move semantics for efficient transfer of resources.

```cpp
// Moving is efficient
FftAnalyzer fft1(4096);
FftAnalyzer fft2 = std::move(fft1);  // Move constructor
FftAnalyzer fft3(2048);
fft3 = std::move(fft2);              // Move assignment
```

---

### FFT Execution Methods

#### `execute` (with output)

```cpp
void execute(const std::vector<std::complex<float>> &input,
             std::vector<std::complex<float>> &output);
```

Executes the FFT on input samples and stores the result in the output vector.

**Parameters:**
- `input` - Time-domain complex samples (size must match FFT size)
- `output` - Frequency-domain complex bins (will be resized to FFT size if needed)

**Preconditions:**
- `input.size()` should equal `fft_size()`
- `output` vector will be resized to `fft_size()` if necessary

**Behavior:**
- Copies input to internal buffer
- Applies DC centering if enabled
- Executes forward FFT
- Stores result in output vector
- Updates cached spectrum representations (power, magnitude, dB, phase)
- Updates frequency bins

**Complexity:** O(N log N) where N is the FFT size

#### `execute` (without output)

```cpp
void execute(const std::vector<std::complex<float>> &input);
```

Executes the FFT using pre-allocated internal buffers.

**Parameters:**
- `input` - Time-domain complex samples (size must match FFT size)

**Preconditions:**
- `input.size()` should equal `fft_size()`

**Behavior:**
- Similar to the two-parameter version, but uses internal output buffer
- Cached results (power spectrum, magnitude spectrum, etc.) are still updated

**Note:** Use this version when you only need the cached spectrum representations and don't need the raw FFT output.

---

### Spectrum Access Methods

These methods provide access to cached spectrum representations computed during the last `execute()` call.

#### `get_power_spectrum`

```cpp
const std::vector<float> &get_power_spectrum() const;
```

Returns the power spectrum (magnitude squared) from the last FFT result.

**Returns:**
- Reference to vector of power values (size = FFT size)
- Power = |X[k]|² = Re{X[k]}² + Im{X[k]}²

**Units:** Linear power (not dB)

**Note:** The power spectrum is useful for detecting signal energy at specific frequencies.

#### `get_magnitude_spectrum`

```cpp
const std::vector<float> &get_magnitude_spectrum() const;
```

Returns the magnitude spectrum (linear) from the last FFT result.

**Returns:**
- Reference to vector of magnitude values (size = FFT size)
- Magnitude = |X[k]| = sqrt(Re{X[k]}² + Im{X[k]}²)

**Units:** Linear amplitude

**Note:** Magnitude is always non-negative.

#### `get_db_spectrum`

```cpp
const std::vector<float> &get_db_spectrum() const;
```

Returns the magnitude spectrum in decibels from the last FFT result.

**Returns:**
- Reference to vector of dB values (size = FFT size)
- dB = 20 * log10(magnitude) if magnitude > 0, otherwise -infinity or minimum value

**Units:** Decibels (dB)

**Purpose:** dB scale is useful for visualizing signals with wide dynamic range, as the human visual system perceives logarithmic scales more linearly.

#### `get_phase_spectrum`

```cpp
const std::vector<float> &get_phase_spectrum() const;
```

Returns the phase spectrum in radians from the last FFT result.

**Returns:**
- Reference to vector of phase values in radians (size = FFT size)
- Phase = atan2(Im{X[k]}, Re{X[k]})

**Range:** [-π, +π] radians

**Purpose:** Phase information is useful for certain signal analysis tasks, though for spectrum visualization, magnitude/dB are more commonly used.

#### `get_frequency_bins`

```cpp
const std::vector<float> &get_frequency_bins() const;
```

Returns the normalized frequency bins corresponding to each FFT bin.

**Returns:**
- Reference to vector of normalized frequency values (size = FFT size)
- Values range from 0.0 to 1.0, where 1.0 corresponds to the sample rate

**Normalization:**
- Bin k corresponds to frequency: `k * sample_rate / fft_size`
- With DC centering enabled, bin 0 corresponds to -sample_rate/2 (negative frequencies)

**Usage:**
```cpp
const auto& freq_bins = fft.get_frequency_bins();
const auto& db_spectrum = fft.get_db_spectrum();

for (size_t i = 0; i < freq_bins.size(); ++i) {
    float normalized_freq = freq_bins[i];  // 0.0 to 1.0
    float actual_freq = normalized_freq * sample_rate;
    float db_value = db_spectrum[i];
    // ... process or display ...
}
```

---

### Configuration Methods

#### `fft_size`

```cpp
size_t fft_size() const noexcept;
```

Returns the FFT size this analyzer was configured with.

**Returns:**
- The FFT size (number of bins)

#### `enable_dc_center`

```cpp
void enable_dc_center(bool enabled) noexcept;
```

Enables or disables DC centering for real signals.

**Parameters:**
- `enabled` - `true` to enable DC centering, `false` to disable

**Behavior:**
- When enabled, applies a (-1)^n shift to the FFT output so that DC (0 Hz) is centered in the spectrum
- This is useful for real signals where the spectrum is symmetric around DC
- When disabled, DC appears at bin 0

**Note:** For complex signals, DC centering is typically not needed.

#### `set_window_coherent_gain`

```cpp
void set_window_coherent_gain(float gain);
```

Sets the window coherent gain for amplitude normalization.

**Parameters:**
- `gain` - The coherent gain value (e.g., 0.5 for Hann window)

**Default:** 1.0 (rectangular window)

**Purpose:**
When a window function is applied to time-domain samples, the FFT magnitude is scaled by the window's coherent gain. To get accurate amplitude measurements, this gain must be compensated for.

**Usage:**
```cpp
#include "signal/signal_processor.h"
#include "fft/fft_analyzer.h"

using namespace openspectrum;

FftAnalyzer fft(4096);
float gain = SignalProcessor::get_coherent_gain(WindowFunction::HANN);
fft.set_window_coherent_gain(gain);
```

---

## Usage Example

```cpp
#include "fft/fft_analyzer.h"
#include "signal/signal_processor.h"
#include "hardware/rtl_sdr_device.h"

using namespace openspectrum;

int main() {
  const size_t FFT_SIZE = 4096;
  const float SAMPLE_RATE = 2048000.0f;  // 2.048 MHz
  
  // Create FFT analyzer
  FftAnalyzer fft(FFT_SIZE);
  
  // Enable DC centering for real signals
  fft.enable_dc_center(true);
  
  // Set window coherent gain
  fft.set_window_coherent_gain(
      SignalProcessor::get_coherent_gain(WindowFunction::BLACKMAN_HARRIS));
  
  // Initialize device
  RtlSdrDevice device;
  device.open();
  device.set_sample_rate(static_cast<uint32_t>(SAMPLE_RATE));
  
  // Read samples
  std::vector<std::complex<float>> samples = device.read_samples(FFT_SIZE);
  
  // Pre-process samples (DC removal and windowing)
  SignalProcessor::remove_dc(samples);
  
  SignalProcessor processor(FFT_SIZE);
  processor.set_window(WindowFunction::BLACKMAN_HARRIS);
  processor.apply_window(samples);
  
  // Execute FFT
  fft.execute(samples);
  
  // Access spectrum data
  const auto& db_spectrum = fft.get_db_spectrum();
  const auto& freq_bins = fft.get_frequency_bins();
  const auto& power = fft.get_power_spectrum();
  const auto& magnitude = fft.get_magnitude_spectrum();
  const auto& phase = fft.get_phase_spectrum();
  
  // Find peak frequency
  auto max_it = std::max_element(db_spectrum.begin(), db_spectrum.end());
  size_t peak_bin = std::distance(db_spectrum.begin(), max_it);
  float peak_freq = freq_bins[peak_bin] * SAMPLE_RATE;
  float peak_db = *max_it;
  
  std::cout << "Peak at " << peak_freq << " Hz, " << peak_db << " dB" << std::endl;
  
  // For visualization, use frequency bins and dB spectrum
  for (size_t i = 0; i < FFT_SIZE; ++i) {
    float frequency = freq_bins[i] * SAMPLE_RATE;
    float db_value = db_spectrum[i];
    // Render at position i with height corresponding to db_value
  }
  
  return 0;
}
```

---

## Advanced Usage: Reinitialization

Since `FftAnalyzer` is non-copyable, reinitialization with a different FFT size requires creating a new instance:

```cpp
#include "fft/fft_analyzer.h"

using namespace openspectrum;

// Initial FFT size
size_t current_size = 4096;
FftAnalyzer fft(current_size);

// Need to change FFT size (e.g., based on user input)
size_t new_size = 8192;

// Reinitialize with new size using move semantics
fft = FftAnalyzer(new_size);  // Move assignment

// Or create a new instance
FftAnalyzer new_fft(new_size);
fft = std::move(new_fft);
```

---

## Performance Considerations

1. **Internal Buffers:** `FftAnalyzer` maintains internal buffers for efficiency, avoiding repeated allocations
2. **Cached Results:** Power, magnitude, dB, and phase spectra are computed once during `execute()` and cached
3. **Frequency Bins:** Computed once during construction or when FFT size changes
4. **Thread Safety:** The class is NOT thread-safe. Each thread should have its own instance
5. **Move Semantics:** Use move semantics when transferring ownership to avoid resource leaks

---

## Mathematical Background

### FFT
The Fast Fourier Transform converts time-domain samples to frequency-domain representation:

```
X[k] = Σ[n=0 to N-1] x[n] * e^(-j*2π*kn/N)
```

Where:
- `x[n]` - Time-domain samples (complex)
- `X[k]` - Frequency-domain bins (complex)
- `N` - FFT size
- `k` - Frequency bin index (0 to N-1)

### Power Spectrum
```
Power[k] = |X[k]|² = Re{X[k]}² + Im{X[k]}²
```

### Magnitude Spectrum
```
Magnitude[k] = |X[k]| = sqrt(Re{X[k]}² + Im{X[k]}²)
```

### dB Spectrum
```
dB[k] = 20 * log10(Magnitude[k])  if Magnitude[k] > 0
     = -infinity (or min value)   otherwise
```

### Phase Spectrum
```
Phase[k] = atan2(Im{X[k]}, Re{X[k]})
```
Range: [-π, +π] radians

---

## See Also

- [SignalProcessor](signal_processing.md) - Pre-processes signals before FFT
- [ControlState](control_state.md) - Manages FFT size changes
- [RuntimeControls (Deprecated)](runtime_controls.md) - Deprecated, use ControlState
- [Third Party - KissFFT](third_party.md) - Underlying FFT library
- [SpectrumDisplay](visualization.md#spectrumdisplay) - Visualizes FFT results
- [WaterfallDisplay](visualization.md#waterfalldisplay) - Shows FFT results over time
