# Signal Processing API

Defined in: `src/signal/signal_processor.h`

The Signal Processing module handles signal conditioning before FFT analysis, including DC offset removal and window function application to reduce spectral leakage.

---

## Namespace

```cpp
namespace openspectrum {
  // All types and classes defined here
}
```

---

## WindowFunction Enum

See [Types - WindowFunction](types.md#windowfunction-enum) for complete documentation.

The supported window functions are:
- `RECTANGLE` - No windowing (uniform)
- `HANN` - Hann (Hanning) window
- `HAMMING` - Hamming window
- `BLACKMAN` - Blackman window
- `BLACKMAN_HARRIS` - Blackman-Harris window
- `FLAT_TOP` - Flat-top window

---

## SignalProcessor Class

The `SignalProcessor` class provides signal conditioning operations including window function application and DC removal.

```cpp
class SignalProcessor {
public:
  explicit SignalProcessor(size_t fft_size);
  ~SignalProcessor() = default;

  // Processing
  void apply_window(std::vector<std::complex<float>> &samples);
  static void remove_dc(std::vector<std::complex<float>> &samples);

  // Configuration
  void set_window(WindowFunction window) noexcept;
  void precompute_window(size_t size);
  float get_window_coeff(size_t index) const;
  size_t fft_size() const noexcept;

  // Window utilities
  static float get_coherent_gain(WindowFunction window) noexcept;
  static const char* window_function_to_string(WindowFunction window) noexcept;
};
```

### Constructor

```cpp
explicit SignalProcessor(size_t fft_size);
```

Constructs a SignalProcessor with the specified FFT size.

**Parameters:**
- `fft_size` - The FFT size (number of samples to process)

**Initial State:**
- Default window function: `WindowFunction::HANN`
- Window coefficients are pre-computed for the given FFT size

### Destructor

```cpp
~SignalProcessor() = default;
```

Default destructor. The class is designed to be trivially destructible.

---

### Processing Methods

#### `apply_window`

```cpp
void apply_window(std::vector<std::complex<float>> &samples);
```

Applies the currently configured window function to the samples in-place.

**Parameters:**
- `samples` - Reference to vector of complex samples (modified in-place)

**Preconditions:**
- `samples.size()` should match the processor's FFT size, or window coefficients should be pre-computed for that size

**Behavior:**
- Multiplies each sample by the corresponding window coefficient
- For real signals, both real and imaginary components are multiplied by the same coefficient

**Complexity:** O(N) where N is the number of samples

#### `remove_dc` (static)

```cpp
static void remove_dc(std::vector<std::complex<float>> &samples);
```

Removes the DC offset (mean value) from the samples in-place.

**Parameters:**
- `samples` - Reference to vector of complex samples (modified in-place)

**Behavior:**
- Computes the mean of all samples
- Subtracts the mean from each sample
- Works with both real and complex signals

**Mathematical Operation:**
```
mean = (sum of all samples) / N
for each sample: sample = sample - mean
```

**Complexity:** O(N) where N is the number of samples

**Usage Example:**
```cpp
#include "signal/signal_processor.h"

using namespace openspectrum;

std::vector<std::complex<float>> samples = device.read_samples(4096);

// Remove DC offset
SignalProcessor::remove_dc(samples);

// Apply window function
SignalProcessor processor(4096);
processor.apply_window(samples);
```

---

### Configuration Methods

#### `set_window`

```cpp
void set_window(WindowFunction window) noexcept;
```

Sets the window function to use for signal processing.

**Parameters:**
- `window` - The window function to use

**Behavior:**
- Changes the current window function
- Does NOT automatically recompute window coefficients
- Call `precompute_window()` if you need to update coefficients for a different size

**Noexcept:** This method is marked `noexcept` (does not throw exceptions)

#### `precompute_window`

```cpp
void precompute_window(size_t size);
```

Pre-computes window coefficients for a specific size.

**Parameters:**
- `size` - The size to pre-compute coefficients for

**Behavior:**
- Computes the window function values for the specified size
- Stores coefficients in an internal vector
- Required before calling `get_window_coeff()`

**Note:** The constructor automatically pre-computes coefficients for the initial FFT size.

#### `get_window_coeff`

```cpp
float get_window_coeff(size_t index) const;
```

Returns the window coefficient at the specified index.

**Parameters:**
- `index` - The index of the coefficient to retrieve (0 to size-1)

**Returns:**
- The window coefficient value (float between 0.0 and 1.0 typically)

**Preconditions:**
- Coefficients must be pre-computed for a size greater than index

#### `fft_size`

```cpp
size_t fft_size() const noexcept;
```

Returns the FFT size this processor was constructed with.

**Returns:**
- The FFT size (number of samples)

---

### Window Utility Methods (Static)

#### `get_coherent_gain`

```cpp
static float get_coherent_gain(WindowFunction window) noexcept;
```

Returns the coherent gain for a specific window function.

**Parameters:**
- `window` - The window function

**Returns:**
- Coherent gain value (float)

**Coherent Gain Values:**

| Window Function | Coherent Gain | Formula |
|-----------------|---------------|---------|
| `RECTANGLE` | 1.0 | 1.0 |
| `HANN` | 0.5 | 0.5 |
| `HAMMING` | 0.54 | 0.54 |
| `BLACKMAN` | 0.42659 | 0.42 |
| `BLACKMAN_HARRIS` | 0.35875 | 0.35875 |
| `FLAT_TOP` | 1.0 | 1.0 |

**Purpose:**
The coherent gain is used to normalize FFT results when using window functions. Without normalization, the magnitude of FFT results would be reduced by the window's coherent gain.

**Usage:**
```cpp
float gain = SignalProcessor::get_coherent_gain(WindowFunction::HANN);
fft_analyzer.set_window_coherent_gain(gain);
```

#### `window_function_to_string`

```cpp
static const char* window_function_to_string(WindowFunction window) noexcept;
```

Converts a window function enum value to a human-readable string.

**Parameters:**
- `window` - The window function

**Returns:**
- C-string with the window function name

**Return Values:**

| Window Function | Return Value |
|-----------------|--------------|
| `RECTANGLE` | `"Rectangle"` |
| `HANN` | `"Hann"` |
| `HAMMING` | `"Hamming"` |
| `BLACKMAN` | `"Blackman"` |
| `BLACKMAN_HARRIS` | `"B-Harris"` |
| `FLAT_TOP` | `"Flat-Top"` |
| Any other | `"Unknown"` |

---

## Complete Usage Example

```cpp
#include "signal/signal_processor.h"
#include "fft/fft_analyzer.h"
#include "hardware/rtl_sdr_device.h"

using namespace openspectrum;

int main() {
  const size_t FFT_SIZE = 4096;
  
  // Create signal processor with Blackman-Harris window
  SignalProcessor processor(FFT_SIZE);
  processor.set_window(WindowFunction::BLACKMAN_HARRIS);
  
  // Create FFT analyzer and set coherent gain
  FftAnalyzer fft(FFT_SIZE);
  float coherent_gain = SignalProcessor::get_coherent_gain(
      WindowFunction::BLACKMAN_HARRIS);
  fft.set_window_coherent_gain(coherent_gain);
  
  // Initialize device
  RtlSdrDevice device;
  device.open();
  device.set_sample_rate(2048000);
  
  // Read and process samples
  std::vector<std::complex<float>> samples = device.read_samples(FFT_SIZE);
  
  // Remove DC offset
  SignalProcessor::remove_dc(samples);
  
  // Apply window function
  processor.apply_window(samples);
  
  // Execute FFT
  std::vector<std::complex<float>> fft_output(FFT_SIZE);
  fft.execute(samples, fft_output);
  
  // Get spectrum
  const auto& db_spectrum = fft.get_db_spectrum();
  
  // Display window function name
  const char* window_name = SignalProcessor::window_function_to_string(
      processor.get_window());
  std::cout << "Using window: " << window_name << std::endl;
  
  return 0;
}
```

---

## Implementation Notes

### Window Functions

The following window functions are implemented with their standard definitions:

- **RECTANGLE**: w(n) = 1.0 for all n
- **HANN**: w(n) = 0.5 * (1 - cos(2πn/N))
- **HAMMING**: w(n) = 0.54 - 0.46 * cos(2πn/N)
- **BLACKMAN**: w(n) = 0.42659 - 0.49656 * cos(2πn/N) + 0.076849 * cos(4πn/N)
- **BLACKMAN_HARRIS**: w(n) = 0.35875 - 0.48829 * cos(2πn/N) + 0.14128 * cos(4πn/N) - 0.01168 * cos(6πn/N)
- **FLAT_TOP**: Special design for accurate amplitude measurement

### DC Removal

The DC removal is performed by subtracting the mean of all samples:
```cpp
std::complex<float> mean = std::accumulate(samples.begin(), samples.end(), 
    std::complex<float>(0.0f, 0.0f)) / static_cast<float>(samples.size());
for (auto& s : samples) {
    s -= mean;
}
```

This is important for SDR applications because:
1. RTL-SDR devices often have a DC offset
2. DC offset appears as a large spike at 0 Hz in the spectrum
3. Removing it improves dynamic range and makes weak signals more visible

---

## See Also

- [Types - WindowFunction](types.md#windowfunction-enum) - Window function enumeration
- [FFT Analysis](fft_analysis.md) - FFT analyzer that uses processed signals
- [Runtime Controls](runtime_controls.md) - Allows changing window function at runtime
- [Visualization](visualization.md) - Displays processed spectrum data
