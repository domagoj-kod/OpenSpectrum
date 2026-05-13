# Third Party Types

This document describes the external library types used by OpenSpectrum, specifically from the KissFFT library.

---

## KissFFT Library

OpenSpectrum uses the [KissFFT](https://github.com/mborgerding/kissfft) library for Fast Fourier Transform computation. This is a lightweight, single-header FFT library designed for simplicity and performance.

**Location:** `third_party/kissfft/`

**Header:** `kiss_fft.h`

---

## kiss_fft_cpx Type

Defined in: `third_party/kissfft/kiss_fft.h`

The `kiss_fft_cpx` type represents a complex number for FFT computation.

```cpp
typedef struct {
    float r;
    float i;
} kiss_fft_cpx;
```

### Members

| Member | Type | Description |
|--------|------|-------------|
| `r` | `float` | Real component |
| `i` | `float` | Imaginary component |

### Usage in OpenSpectrum

The `FftAnalyzer` class uses `kiss_fft_cpx` internally for FFT computation:

```cpp
// From fft_analyzer.h
std::vector<kiss_fft_cpx> m_input_buffer;
std::vector<kiss_fft_cpx> m_output_buffer;
```

### Conversion

Converting between `std::complex<float>` and `kiss_fft_cpx`:

```cpp
// std::complex<float> to kiss_fft_cpx
std::complex<float> c(real, imag);
kiss_fft_cpx kc = {c.real(), c.imag()};

// kiss_fft_cpx to std::complex<float>
kiss_fft_cpx kc = {...};
std::complex<float> c(kc.r, kc.i);
```

---

## kiss_fft_cfg Type

Defined in: `third_party/kissfft/kiss_fft.h`

The `kiss_fft_cfg` type is an opaque pointer to FFT configuration data.

```cpp
typedef void* kiss_fft_cfg;
```

### Description

`kiss_fft_cfg` is a pointer to an internal configuration structure created by `kiss_fft_alloc()`. It contains pre-computed tables and state needed for efficient FFT computation.

### Usage in OpenSpectrum

The `FftAnalyzer` class manages a `kiss_fft_cfg`:

```cpp
// From fft_analyzer.h
kiss_fft_cfg m_cfg = nullptr;

// In constructor
m_cfg = kiss_fft_alloc(m_fft_size, m_inverse, nullptr, nullptr);

// In destructor
if (m_cfg) {
    kiss_fft_free(m_cfg);
}
```

### Lifecycle

1. **Allocation:** `kiss_fft_cfg cfg = kiss_fft_alloc(nfft, inverse, st, finverse);`
2. **Usage:** Pass to `kiss_fft()` or `kiss_ifft()` for FFT computation
3. **Free:** `kiss_fft_free(cfg)` when no longer needed

### Parameters for kiss_fft_alloc

| Parameter | Type | Description |
|-----------|------|-------------|
| `nfft` | `int` | FFT size (number of points) |
| `inverse` | `int` | 0 for forward FFT, 1 for inverse FFT |
| `st` | `kiss_fft_state*` | Optional state (usually nullptr) |
| `finverse` | `void(*)()` | Optional cleanup function (usually nullptr) |

---

## KissFFT Functions Used

### kiss_fft_alloc

```cpp
kiss_fft_cfg kiss_fft_alloc(int nfft, int inverse,
                           kiss_fft_state *st, void(*finverse)());
```

Allocates and initializes FFT configuration.

**Parameters:**
- `nfft` - FFT size (must be power of two for best performance)
- `inverse` - 0 for forward FFT, 1 for inverse FFT
- `st` - Optional state pointer (usually nullptr)
- `finverse` - Optional cleanup function (usually nullptr)

**Returns:**
- Opaque configuration pointer

### kiss_fft_free

```cpp
void kiss_fft_free(kiss_fft_cfg cfg);
```

Frees FFT configuration.

**Parameters:**
- `cfg` - Configuration pointer to free

### kiss_fft

```cpp
void kiss_fft(kiss_fft_cfg cfg, const kiss_fft_cpx *fin,
             kiss_fft_cpx *fout);
```

Performs forward FFT.

**Parameters:**
- `cfg` - FFT configuration
- `fin` - Input samples (complex)
- `fout` - Output spectrum (complex)

### kiss_ifft

```cpp
void kiss_ifft(kiss_fft_cfg cfg, const kiss_fft_cpx *fin,
              kiss_fft_cpx *fout);
```

Performs inverse FFT.

---

## How OpenSpectrum Uses KissFFT

The `FftAnalyzer` class wraps KissFFT with RAII management:

```cpp
#include "third_party/kissfft/kiss_fft.h"

class FftAnalyzer {
public:
    explicit FftAnalyzer(size_t fft_size, bool inverse = false) 
        : m_fft_size(fft_size), m_inverse(inverse) {
        m_cfg = kiss_fft_alloc(static_cast<int>(fft_size), inverse ? 1 : 0, 
                              nullptr, nullptr);
        m_input_buffer.resize(fft_size);
        m_output_buffer.resize(fft_size);
    }
    
    ~FftAnalyzer() {
        if (m_cfg) {
            kiss_fft_free(m_cfg);
        }
    }
    
    void execute(const std::vector<std::complex<float>> &input,
                 std::vector<std::complex<float>> &output) {
        // Convert input
        for (size_t i = 0; i < m_fft_size; ++i) {
            m_input_buffer[i].r = input[i].real();
            m_input_buffer[i].i = input[i].imag();
        }
        
        // Execute FFT
        kiss_fft(m_cfg, m_input_buffer.data(), m_output_buffer.data());
        
        // Convert output
        output.resize(m_fft_size);
        for (size_t i = 0; i < m_fft_size; ++i) {
            output[i] = std::complex<float>(m_output_buffer[i].r, 
                                            m_output_buffer[i].i);
        }
        
        // Update cached spectra...
    }
    
private:
    kiss_fft_cfg m_cfg;
    std::vector<kiss_fft_cpx> m_input_buffer;
    std::vector<kiss_fft_cpx> m_output_buffer;
};
```

---

## Building with KissFFT

KissFFT is included as a submodule in `third_party/kissfft/`. The build system (Makefile) includes it:

```makefile
# From Makefile
CXXFLAGS += -Ithird_party/kissfft
```

### Manual Include

To use KissFFT directly in your code:

```cpp
#include "third_party/kissfft/kiss_fft.h"

// Allocate FFT
kiss_fft_cfg cfg = kiss_fft_alloc(4096, 0, nullptr, nullptr);

// Prepare input
std::vector<kiss_fft_cpx> input(4096);
std::vector<kiss_fft_cpx> output(4096);

// Fill input with data...

// Execute FFT
kiss_fft(cfg, input.data(), output.data());

// Use output...

// Clean up
kiss_fft_free(cfg);
```

---

## KissFFT Features Used by OpenSpectrum

| Feature | Usage |
|---------|-------|
| Forward FFT | Spectrum analysis |
| Complex I/O | Standard FFT operation |
| Power-of-two sizes | Optimal performance |
| Single-precision (float) | 32-bit floating point |

### Features NOT Used

| Feature | Reason |
|---------|--------|
| Inverse FFT | Not needed for spectrum analysis |
| Double precision | Float precision is sufficient |
| Real-optimized FFT | Generic complex FFT is simpler |
| Multi-dimensional FFT | Only 1D FFT needed |

---

## Performance Considerations

1. **FFT Size:** KissFFT works best with power-of-two sizes
2. **Memory:** Pre-allocated buffers avoid repeated allocations
3. **Configuration:** `kiss_fft_cfg` should be reused for multiple FFTs of the same size
4. **Alignment:** KissFFT may benefit from aligned memory allocation
5. **SIMD:** KissFFT can use SIMD instructions on some platforms

---

## See Also

- [FftAnalyzer](fft_analysis.md) - OpenSpectrum's FFT wrapper
- [KissFFT GitHub](https://github.com/mborgerding/kissfft) - Official repository
- [KissFFT Documentation](https://github.com/mborgerding/kissfft/blob/master/README.md) - Library documentation
