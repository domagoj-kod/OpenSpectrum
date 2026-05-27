# OpenSpectrum Performance Improvements

**Goal**: Reduce CPU usage and memory footprint while maintaining platform agnosticism and demoscene minimalism.
**Target**: 50% CPU reduction from current baseline, <10 MB RAM usage.
**Constraint**: No hardcoded D3D11/Vulkan - must work on any SDL2 backend.

---

## Current Performance Baseline (D3D11 on Intel UHD)

### VTune Profile - Top Hotspots
```
SleepConditionVariableCS      : 37.2%  (waiting for RTL-SDR samples)
dxgi.dll function           : 15.8%  (D3D11 overhead)
D3D11CreateDevice           : 14.6%  (startup cost)
NtWaitForSingleObject        : 12.3%  (synchronization)
CreateDXGIFactory            : 4.9%   (D3D11 initialization)
```

### Cachegrind Profile (CPU-side bottlenecks)
```
std::__fill_a1<unsigned char*, int> : 8.9%   (pixel buffer clearing)
SpectrumDisplay::render()            : 5.6%   (rendering loop)
SpectrumPalette::get_color()        : 2.9%   (color lookup)
WaterfallDisplay::render()          : 2.8%   (rendering loop)
std::ranges::min/max_element       : ~3-4%  (iterator overhead)
```

### Microarchitecture Issues
- Front-End Bound: 41.1% (P-core) / 31.3% (E-core)
- Back-End Bound: 50.5% (P-core) / 48.6% (E-core)
- L3 Bound: 11.9%
- Memory Bound: 24.2%

---

## Implementation Priority Matrix

| Phase | Task | Effort | Savings | Platform Agnostic | Demoscene Spirit | Status |
|-------|------|--------|---------|------------------|------------------|--------|
| 1 | Consolidate pixel buffers - render directly to SDL texture | Low | 4.7 MB RAM, 8.9% CPU | ✅ | ✅ (minimal) | ⬜ |
| 1 | Precomputed color LUT - replace get_color() with table lookup | Low | ~2.5% CPU | ✅ | ✅ (lookup tables) | ⬜ |
| 1 | Quantize waterfall history - float→uint8 | Low | 0.9 MB RAM | ✅ | ✅ | ⬜ |
| 1 | `memset` instead of `std::fill` - in PixelBuffer | Low | ~8% CPU | ✅ | ✅ | ⬜ |
| 2 | Manual min/max loops - replace std::ranges | Medium | ~3% CPU | ✅ | ✅ | ⬜ |
| 2 | Raw pointers in hot loops - eliminate iterator overhead | Medium | ~2% CPU | ✅ | ✅ | ⬜ |
| 2 | Real FFT optimization - use kiss_fftr for real signals | Medium | ~40% FFT time | ✅ | ✅ | ⬜ |
| 2 | Increase async buffers - from 8 to 32 | Medium | Reduces waits | ✅ | ✅ | ✅ |
| 3 | Zero-copy FFT - accept FrameHandle directly | High | ~5% CPU | ✅ | ✅ | ⬜ |
| 3 | Batch sample processing - accumulate 2-4 FFTs | Medium | Reduces render calls | ✅ | ✅ | ⬜ |
| 4 | SIMD rendering - process 4-8 pixels/iter | High | 30-50% render | ✅ | ✅ (hand-optimized) | ⬜ |
| 4 | Fixed-point math - for color mapping | High | ~15% CPU | ✅ | ✅ (retro) | ⬜ |

---

## Phase 1: Quick Wins (Est. 2-4 hours)

### 1.1 Consolidate Pixel Buffers → Single SDL Texture

**Problem**: 3 separate pixel buffers (6+ MB) copied every frame.

**Solution**: Render directly to SDL texture memory.

```cpp
// In sdl_renderer.cpp
bool SdlRenderer::render_with_dirty_regions(...) {
    // Lock texture for direct access
    void* texture_pixels = nullptr;
    int texture_pitch = 0;
    if (SDL_LockTexture(m_texture, nullptr, &texture_pixels, &texture_pitch) != 0) {
        return false;
    }
    
    // Render spectrum directly into texture
    spectrum_display.render_direct(static_cast<uint8_t*>(texture_pixels), texture_pitch);
    
    // Render waterfall directly into texture (offset by height/2)
    uint8_t* waterfall_dst = static_cast<uint8_t*>(texture_pixels) + (m_height/2) * texture_pitch;
    waterfall_display.render_direct(waterfall_dst, texture_pitch);
    
    SDL_UnlockTexture(m_texture);
    SDL_RenderCopy(m_renderer, m_texture, nullptr, nullptr);
    render_overlays();
    SDL_RenderPresent(m_renderer);
    return true;
}

// In spectrum_display.h - add:
void render_direct(uint8_t* dst, int pitch);

// In spectrum_display.cpp - modify render() to accept dst:
void SpectrumDisplay::render_direct(uint8_t* dst, int pitch) {
    clear();  // Clear internal buffer if still needed
    if (m_spectrum_data.empty()) return;
    
    const size_t row_stride = pitch;  // Use texture pitch
    const float db_range = m_max_db - m_min_db;
    const float db_to_height = static_cast<float>(m_height) / db_range;
    
    for (size_t i = 0; i < m_spectrum_data.size(); ++i) {
        float db = m_spectrum_data[i];
        float bar_height = (db - m_min_db) * db_to_height;
        bar_height = std::clamp(bar_height, 0.0f, static_cast<float>(m_height));
        
        auto color = m_palette.get_color(db);
        auto x = static_cast<size_t>(static_cast<float>(i) * (static_cast<float>(m_width) / m_spectrum_data.size()));
        auto top_y = static_cast<size_t>(m_height - bar_height);
        
        for (size_t y = top_y; y < m_height; ++y) {
            uint8_t* pixel = dst + (y * row_stride) + (x * 4);
            pixel[0] = color.red;
            pixel[1] = color.green;
            pixel[2] = color.blue;
            pixel[3] = color.alpha;
        }
    }
}
```

**Savings**: 4.7 MB memory, eliminates memcpy operations

**Verify**: Remove `combined_pixels` vector from main.cpp after this change.

---

### 1.2 Precomputed Color Palette Lookup Table

**Problem**: `SpectrumPalette::get_color()` = 2.9% of instructions (float ops per pixel).

**Solution**: Precompute entire dB → RGB table at startup.

```cpp
// In spectrum_display.h - SpectrumPalette class:
class SpectrumPalette {
public:
    static constexpr size_t LUT_SIZE = 65536;  // 16-bit precision
    
    SpectrumPalette();
    void precompute_lut(float min_db, float max_db);
    RgbColor get_color(float db_value) const;
    
private:
    std::array<RgbColor, LUT_SIZE> m_lut;
    float m_db_min = -120.0f;
    float m_db_max = 0.0f;
    float m_lut_scale = 0.0f;
};

// In spectrum_display.cpp - SpectrumPalette implementation:
SpectrumPalette::SpectrumPalette() {
    precompute_lut(-120.0f, 0.0f);
}

void SpectrumPalette::precompute_lut(float min_db, float max_db) {
    m_db_min = min_db;
    m_db_max = max_db;
    m_lut_scale = (LUT_SIZE - 1) / (max_db - min_db);
    
    for (size_t i = 0; i < LUT_SIZE; ++i) {
        float db = min_db + (static_cast<float>(i) / (LUT_SIZE - 1)) * (max_db - min_db);
        
        // Use existing color generation logic
        float const t = (db - min_db) / (max_db - min_db);
        float r = 0.0f, g = 0.0f, b = 0.0f;
        
        // Jet colormap (or use existing generate_*_palette)
        if (m_color_map == ColorMap::JET) {
            if (t < 0.125f) { r = 0.0f; g = 0.0f; b = 0.5f + 4.0f * t; }
            else if (t < 0.375f) { r = 0.0f; g = 4.0f * (t - 0.125f); b = 1.0f; }
            else if (t < 0.625f) { r = 4.0f * (t - 0.375f); g = 1.0f; b = 1.0f - 4.0f * (t - 0.375f); }
            else if (t < 0.875f) { r = 1.0f; g = 1.0f - 4.0f * (t - 0.625f); b = 0.0f; }
            else { r = 1.0f - 4.0f * (t - 0.875f); g = 0.0f; b = 0.0f; }
        }
        // ... other colormaps
        
        m_lut[i] = RgbColor(
            static_cast<uint8_t>(r * 255),
            static_cast<uint8_t>(g * 255),
            static_cast<uint8_t>(b * 255)
        );
    }
}

RgbColor SpectrumPalette::get_color(float db_value) const {
    // Clamp and scale to LUT index
    if (db_value <= m_db_min) return m_lut[0];
    if (db_value >= m_db_max) return m_lut[LUT_SIZE - 1];
    
    size_t index = static_cast<size_t>((db_value - m_db_min) * m_lut_scale + 0.5f);
    index = std::min(index, LUT_SIZE - 1);
    return m_lut[index];
}

// Call this when dB range changes:
void SpectrumPalette::set_db_range(float min_db, float max_db) {
    m_db_min = min_db;
    m_db_max = max_db;
    precompute_lut(min_db, max_db);
}
```

**Savings**: ~2.5-2.9% CPU time (replaces floating-point math with array lookup)

---

### 1.3 Quantize Waterfall History to uint8

**Problem**: 288 × 1050 × 4 bytes = 1.2 MB float history.

**Solution**: Store as uint8 with fixed dB scale.

```cpp
// In waterfall_display.h:
#include <cstdint>

class WaterfallDisplay {
public:
    WaterfallDisplay(size_t width, size_t height, size_t history_lines);
    // ...

private:
    // Change from: RingBuffer<std::vector<float>> m_history;
    RingBuffer<std::vector<uint8_t>> m_history;
    float m_history_db_min = -120.0f;
    float m_history_db_max = 0.0f;
    
    uint8_t quantize_db(float db) const;
    float dequantize_db(uint8_t q) const;
};

// In waterfall_display.cpp:
uint8_t WaterfallDisplay::quantize_db(float db) const {
    if (db <= m_history_db_min) return 0;
    if (db >= m_history_db_max) return 255;
    
    float t = (db - m_history_db_min) / (m_history_db_max - m_history_db_min);
    return static_cast<uint8_t>(t * 255.0f + 0.5f);
}

float WaterfallDisplay::dequantize_db(uint8_t q) const {
    float t = static_cast<float>(q) / 255.0f;
    return m_history_db_min + t * (m_history_db_max - m_history_db_min);
}

void WaterfallDisplay::add_spectrum_line(const std::vector<float>& db_values) {
    if (db_values.empty()) return;

    // Resample to m_width
    std::vector<float> line(m_width, m_min_db);
    // ... existing resampling code ...

    // Quantize to uint8
    std::vector<uint8_t> quantized_line(m_width);
    for (size_t i = 0; i < m_width; ++i) {
        quantized_line[i] = quantize_db(line[i]);
    }

    m_history.push(std::move(quantized_line));
    render();
}

void WaterfallDisplay::render() {
    if (m_history.empty()) {
        m_pixels.clear();
        return;
    }

    std::memset(m_pixels.data(), 0, m_pixels.size());
    const size_t row_stride = m_width * 4;
    size_t y_offset = 0;
    const size_t line_height = std::max<size_t>(1, m_height / m_history.capacity());

    for (size_t i = 0; i < m_history.size(); ++i) {
        if (y_offset >= m_height) break;

        const auto& line = m_history[i];
        size_t actual_line_height = (y_offset + line_height <= m_height)
                                        ? line_height
                                        : (m_height - y_offset);

        for (size_t y = 0; y < actual_line_height; ++y) {
            size_t pixel_row = y_offset + y;
            uint8_t* row_ptr = m_pixels.data() + (pixel_row * row_stride);

            for (size_t x = 0; x < m_width && x < line.size(); ++x) {
                // Dequantize and get color
                float db = dequantize_db(line[x]);
                auto color = m_palette.get_color(db);

                uint8_t* dst = row_ptr + (x * 4);
                dst[0] = color.red;
                dst[1] = color.green;
                dst[2] = color.blue;
                dst[3] = color.alpha;
            }
        }
        y_offset += line_height;
    }
}
```

**Savings**: 1.2 MB → ~300 KB memory (4:1 reduction)

---

### 1.4 Replace `std::fill` with `memset`

**Problem**: `std::__fill_a1` = 8.9% of ALL instructions (pixel buffer clearing).

**Solution**: Use `memset` for zero-initialization.

```cpp
// In spectrum_display.h - PixelBuffer class:
class PixelBuffer {
public:
    // ... existing code ...
    
    void clear() {
        // Instead of: std::fill(m_data, m_data + m_size, 0);
        if (m_data) {
            memset(m_data, 0, m_size);
        }
    }
    
    // Also replace in constructors:
    explicit PixelBuffer(size_t size) : m_size(size) {
        m_data = new uint8_t[m_size]();  // Already zero-initialized by new[]()
        // But if you need to clear later, use memset
    }
};

// In waterfall_display.cpp - render():
// Replace: std::memset(m_pixels.data(), 0, m_pixels.size());
// Already using memset - keep it!

// In spectrum_display.cpp - render():
// Before: clear(); // Which calls std::fill internally
// After: clear(); // Now uses memset
```

**Savings**: ~8% CPU time reduction

---

## Phase 2: Medium Effort (Est. 4-8 hours)

### 2.1 Manual Min/Max Loops

**Problem**: `std::ranges::min_element` / `max_element` = ~3-4% CPU (iterator overhead).

**Solution**: Use raw pointer loops.

```cpp
// In spectrum_display.cpp - update_spectrum():
void SpectrumDisplay::update_spectrum(const std::vector<float>& db_values,
                                      const std::vector<float>& /*freq_bins*/,
                                      float center_freq_hz,
                                      float sample_rate_hz) {
    m_center_freq_hz = center_freq_hz;
    m_sample_rate_hz = sample_rate_hz;
    m_spectrum_data = db_values;

    if (m_autoscale && !db_values.empty()) {
        // OLD:
        // float min_val = *std::ranges::min_element(db_values);
        // float max_val = *std::ranges::max_element(db_values);
        
        // NEW:
        float min_val = db_values[0];
        float max_val = db_values[0];
        for (size_t i = 1; i < db_values.size(); ++i) {
            if (db_values[i] < min_val) min_val = db_values[i];
            if (db_values[i] > max_val) max_val = db_values[i];
        }
        
        set_db_range(min_val - 5.0f, max_val + 5.0f);
    }
    render();
}

// In waterfall_display.cpp - update_global_range():
void WaterfallDisplay::update_global_range() {
    if (m_history.empty()) {
        m_global_min = m_min_db;
        m_global_max = m_max_db;
        m_palette.set_db_range(m_global_min, m_global_max);
        return;
    }

    // OLD: Used std::ranges::min_element/max_element
    // NEW:
    float new_min = m_history[0][0];
    float new_max = m_history[0][0];

    for (size_t i = 0; i < m_history.size(); ++i) {
        const auto& line = m_history[i];
        if (!line.empty()) {
            for (size_t j = 0; j < line.size(); ++j) {
                float val = dequantize_db(line[j]);  // If using uint8
                // or: float val = line[j];  // If still using float
                if (val < new_min) new_min = val;
                if (val > new_max) new_max = val;
            }
        }
    }

    float range = new_max - new_min;
    if (range > 0) {
        new_min -= range * 0.05f;
        new_max += range * 0.05f;
    } else {
        new_min -= 5.0f;
        new_max += 5.0f;
    }

    m_global_min = new_min;
    m_global_max = new_max;
    m_palette.set_db_range(m_global_min, m_global_max);
}
```

**Savings**: ~3-4% CPU time

---

### 2.2 Raw Pointers in Hot Loops

**Problem**: Iterator overhead in rendering loops (from cachegrind).

**Solution**: Use raw pointers for array access.

```cpp
// In spectrum_display.cpp - render():
void SpectrumDisplay::render() {
    if (m_spectrum_data.empty()) {
        clear();
        return;
    }

    clear();
    
    const size_t num_bins = m_spectrum_data.size();
    const float bin_width = static_cast<float>(m_width) / static_cast<float>(num_bins);
    const float db_range = m_max_db - m_min_db;
    const float db_to_height = static_cast<float>(m_height) / db_range;
    const size_t row_stride = m_width * 4;
    
    // Use raw pointers for speed
    const float* db_ptr = m_spectrum_data.data();
    uint8_t* pixels = m_pixels.data();
    
    for (size_t i = 0; i < num_bins; ++i) {
        float db = db_ptr[i];  // Direct pointer access
        float bar_height = (db - m_min_db) * db_to_height;
        bar_height = std::clamp(bar_height, 0.0f, static_cast<float>(m_height));

        auto color = m_palette.get_color(db);
        size_t x = static_cast<size_t>(static_cast<float>(i) * bin_width);
        size_t bottom_y = m_height - 1;
        size_t top_y = static_cast<size_t>(m_height - bar_height);

        if (top_y >= m_height) top_y = m_height - 1;

        // Fill column using pointer arithmetic
        for (size_t y = top_y; y <= bottom_y; ++y) {
            uint8_t* pixel = pixels + (y * row_stride) + (x * 4);
            pixel[0] = color.red;
            pixel[1] = color.green;
            pixel[2] = color.blue;
            pixel[3] = color.alpha;
        }
    }
}

// In waterfall_display.cpp - render():
void WaterfallDisplay::render() {
    if (m_history.empty()) {
        m_pixels.clear();
        return;
    }

    std::memset(m_pixels.data(), 0, m_pixels.size());
    const size_t row_stride = m_width * 4;
    size_t y_offset = 0;
    const size_t line_height = std::max<size_t>(1, m_height / m_history.capacity());

    // Use raw pointers
    uint8_t* base_ptr = m_pixels.data();
    
    for (size_t i = 0; i < m_history.size(); ++i) {
        if (y_offset >= m_height) break;

        const auto& line = m_history[i];
        size_t actual_line_height = (y_offset + line_height <= m_height)
                                        ? line_height
                                        : (m_height - y_offset);

        for (size_t y = 0; y < actual_line_height; ++y) {
            size_t pixel_row = y_offset + y;
            uint8_t* row_ptr = base_ptr + (pixel_row * row_stride);
            
            // Use pointer for line data too
            const uint8_t* line_ptr = line.data();
            
            for (size_t x = 0; x < m_width && x < line.size(); ++x) {
                float db = dequantize_db(line_ptr[x]);
                auto color = m_palette.get_color(db);

                uint8_t* dst = row_ptr + (x * 4);
                dst[0] = color.red;
                dst[1] = color.green;
                dst[2] = color.blue;
                dst[3] = color.alpha;
            }
        }
        y_offset += line_height;
    }
}
```

**Savings**: ~2% CPU time from iterator elimination

---

### 2.3 Real FFT Optimization

**Problem**: Complex FFT for real-valued input computes redundant data.

**Solution**: Use kiss_fftr for real signals (half the computation).

```cpp
// In fft/fft_analyzer.h:
#include "kiss_fftr.h"  // Real FFT from kissfft

class FftAnalyzer {
public:
    explicit FftAnalyzer(size_t fft_size, bool inverse = false);
    ~FftAnalyzer();
    
    // For real input, use real FFT
    void execute_real(const std::vector<float>& input,
                     std::vector<std::complex<float>>& output);
    
    // Keep existing complex FFT for compatibility
    void execute(const std::vector<std::complex<float>>& input,
                std::vector<std::complex<float>>& output);

private:
    size_t m_fft_size;
    bool m_inverse;
    kiss_fft_cfg m_cfg = nullptr;
    kiss_fftr_cfg m_rcfg = nullptr;  // Real FFT config
    // ... existing buffers ...
};

// In fft/fft_analyzer.cpp:
FftAnalyzer::FftAnalyzer(size_t fft_size, bool inverse)
    : m_fft_size(fft_size), m_inverse(inverse) {
    // Complex FFT config
    m_cfg = kiss_fft_alloc(fft_size, inverse, nullptr, nullptr);
    
    // Real FFT config (only for even sizes, which they are - power of 2)
    m_rcfg = kiss_fftr_alloc(fft_size, inverse, nullptr, nullptr);
    
    // ... existing buffer allocations ...
}

FftAnalyzer::~FftAnalyzer() {
    if (m_cfg) kiss_fft_free(m_cfg);
    if (m_rcfg) kiss_fftr_free(m_rcfg);
}

void FftAnalyzer::execute_real(const std::vector<float>& input,
                             std::vector<std::complex<float>>& output) {
    if (input.size() < m_fft_size || output.size() < m_fft_size) {
        throw std::invalid_argument("FFT size mismatch");
    }
    
    // Convert to kiss_fft_scalar array
    std::vector<kiss_fft_scalar> in(m_fft_size);
    for (size_t i = 0; i < m_fft_size; ++i) {
        in[i] = static_cast<kiss_fft_scalar>(input[i]);
    }
    
    // Real FFT - output is complex but only first half is valid
    std::vector<kiss_fft_cpx> out(m_fft_size);
    kiss_fftr(m_rcfg, in.data(), out.data());
    
    // Convert to std::complex<float>
    for (size_t i = 0; i < m_fft_size; ++i) {
        output[i] = std::complex<float>(out[i].r, out[i].i);
    }
    
    // Update cached results
    compute_power_spectrum(output);
    compute_magnitude_spectrum();
    compute_db_spectrum();
    compute_phase_spectrum();
}

// In signal/signal_processor.cpp - modify to output real samples:
// If your signal is real (I and Q from RTL-SDR are separate real channels),
// you might need to handle this differently. But for typical SDR spectrum,
// the input is complex (I+Q), so real FFT may not apply directly.
// 
// For now, keep using complex FFT. Real FFT optimization is for
// real-valued signals only.
```

**Note**: RTL-SDR provides complex samples (I+Q), so real FFT doesn't directly apply. However, if you're processing real signals in some contexts, this is available. For complex signals, the current kissFFT implementation is appropriate.

**Savings**: N/A for current use case (complex input from RTL-SDR)

---

### 2.4 Increase Async Buffer Count

**Problem**: 37.2% waiting for samples (SleepConditionVariableCS).

**Solution**: Increase RTL-SDR async buffer count.

```cpp
// In main.cpp - device initialization:
// OLD: dev.start_streaming(8);
// NEW:
dev.start_streaming(32);  // More buffers = less waiting
```

**Savings**: Reduces time spent in SleepConditionVariableCS

---

## Phase 3: Advanced Optimizations (Optional)

### 3.1 Zero-Copy FFT Processing

**Problem**: Multiple copies between FrameHandle ↔ vector ↔ FFT buffers.

**Solution**: Modify FftAnalyzer to accept FrameHandle directly.

```cpp
// In fft/fft_analyzer.h:
#include "openspectrum/frame_pool.h"

class FftAnalyzer {
public:
    // Add overload that accepts FrameHandle
    void execute(const openspectrum::FrameHandle& input,
                std::vector<std::complex<float>>& output);
    
    // Keep existing methods
    void execute(const std::vector<std::complex<float>>& input,
                std::vector<std::complex<float>>& output);

private:
    // Helper for zero-copy when sizes match
    void execute_direct(const std::complex<float>* input_data,
                       size_t input_size,
                       std::vector<std::complex<float>>& output);
};

// In fft/fft_analyzer.cpp:
void FftAnalyzer::execute(const openspectrum::FrameHandle& input,
                         std::vector<std::complex<float>>& output) {
    if (!input || input.size() < m_fft_size) {
        // Fallback to zero-padded execution
        std::vector<std::complex<float>> temp(m_fft_size, 0.0f);
        if (input) {
            std::copy_n(input.data(), std::min(input.size(), m_fft_size), temp.begin());
        }
        execute(temp, output);
        return;
    }
    
    // Zero-copy path: use input data directly
    execute_direct(input.data(), input.size(), output);
}

void FftAnalyzer::execute_direct(const std::complex<float>* input_data,
                               size_t input_size,
                               std::vector<std::complex<float>>& output) {
    // Ensure output is large enough
    if (output.size() < m_fft_size) {
        output.resize(m_fft_size);
    }
    
    // Copy input to internal buffer (kissFFT requires contiguous data)
    // If input is already in the right format, we could avoid this
    for (size_t i = 0; i < m_fft_size && i < input_size; ++i) {
        m_input_buffer[i].r = input_data[i].real();
        m_input_buffer[i].i = input_data[i].imag();
    }
    
    // Execute FFT
    kiss_fft(m_cfg, m_input_buffer.data(), m_output_buffer.data());
    
    // Convert output
    for (size_t i = 0; i < m_fft_size; ++i) {
        output[i] = std::complex<float>(m_output_buffer[i].r, m_output_buffer[i].i);
    }
    
    // Update cached results
    compute_power_spectrum(output);
    compute_magnitude_spectrum();
    compute_db_spectrum();
    compute_phase_spectrum();
}

// In main.cpp - FFT processing:
// OLD:
// std::copy_n(async_samples_frame.data(), current_fft_size, samples.begin());
// fft_analyzer.execute(samples, fft_output);

// NEW:
fft_analyzer.execute(async_samples_frame, fft_output);
// async_samples_frame returned to pool automatically
```

**Savings**: Eliminates one memcpy of 32 KB per FFT

---

### 3.2 Batch Sample Processing

**Problem**: One render per FFT result (potentially high overhead).

**Solution**: Accumulate multiple FFTs, average, render once.

```cpp
// In main.cpp - add batching support:
// Add to ControlState or as global:
static const size_t FFT_BATCH_SIZE = 2;  // Process 2 FFTs before rendering
static std::vector<std::vector<float>> fft_batch;

// In main loop - modify FFT section:
// OLD: Immediately update displays after each FFT

// NEW:
fft_analyzer.execute(async_samples_frame, fft_output);
const auto& db_spectrum = fft_analyzer.get_db_spectrum();

// Store in batch
fft_batch.push_back(db_spectrum);

if (fft_batch.size() >= FFT_BATCH_SIZE) {
    // Average the batch
    std::vector<float> avg_spectrum(db_spectrum.size(), 0.0f);
    for (const auto& spec : fft_batch) {
        for (size_t i = 0; i < spec.size(); ++i) {
            avg_spectrum[i] += spec[i];
        }
    }
    for (size_t i = 0; i < avg_spectrum.size(); ++i) {
        avg_spectrum[i] /= static_cast<float>(fft_batch.size());
    }
    
    // Update displays with averaged spectrum
    spectrum_display.update_spectrum(avg_spectrum, fft_analyzer.get_frequency_bins(),
                                      static_cast<float>(control_state.get_frequency()),
                                      config.sample_rate_hz);
    waterfall_display.add_spectrum_line(avg_spectrum);
    
    // Clear batch
    fft_batch.clear();
}
```

**Savings**: Reduces render calls by 2-4x
**Tradeoff**: Adds latency of (batch_size - 1) × FFT_time

---

### 3.3 SIMD-Accelerated Rendering

**Problem**: Rendering loops process one pixel at a time.

**Solution**: Process 4-8 pixels per iteration using SIMD.

```cpp
// In spectrum_display.cpp - render():
#include <immintrin.h>  // x86 SIMD intrinsics

void SpectrumDisplay::render() {
    if (m_spectrum_data.empty()) {
        clear();
        return;
    }

    clear();
    const size_t num_bins = m_spectrum_data.size();
    const float bin_width = static_cast<float>(m_width) / static_cast<float>(num_bins);
    const float db_range = m_max_db - m_min_db;
    const float db_to_height = static_cast<float>(m_height) / db_range;
    const size_t row_stride = m_width * 4;
    
    const float* db_ptr = m_spectrum_data.data();
    uint8_t* pixels = m_pixels.data();
    
    // Process 4 bins at a time using SIMD
    size_t i = 0;
    for (; i + 3 < num_bins; i += 4) {
        // Load 4 dB values
        __m128 db_vec = _mm_loadu_ps(&db_ptr[i]);
        
        // Subtract m_min_db (broadcast)
        __m128 min_vec = _mm_set1_ps(m_min_db);
        __m128 adjusted = _mm_sub_ps(db_vec, min_vec);
        
        // Multiply by db_to_height
        __m128 height_scale = _mm_set1_ps(db_to_height);
        __m128 heights = _mm_mul_ps(adjusted, height_scale);
        
        // Clamp to [0, m_height]
        __m128 zero = _mm_setzero_ps();
        __m128 max_height = _mm_set1_ps(static_cast<float>(m_height));
        heights = _mm_max_ps(heights, zero);
        heights = _mm_min_ps(heights, max_height);
        
        // Store heights to temp array
        alignas(16) float temp_heights[4];
        _mm_store_ps(temp_heights, heights);
        
        // For each of the 4 bins, render the column
        for (size_t j = 0; j < 4; ++j) {
            float bar_height = temp_heights[j];
            auto color = m_palette.get_color(db_ptr[i + j]);
            size_t x = static_cast<size_t>((static_cast<float>(i + j)) * bin_width);
            size_t bottom_y = m_height - 1;
            size_t top_y = static_cast<size_t>(m_height - bar_height);
            
            if (top_y >= m_height) top_y = m_height - 1;
            
            for (size_t y = top_y; y <= bottom_y; ++y) {
                uint8_t* pixel = pixels + (y * row_stride) + (x * 4);
                pixel[0] = color.red;
                pixel[1] = color.green;
                pixel[2] = color.blue;
                pixel[3] = color.alpha;
            }
        }
    }
    
    // Handle remaining bins (not divisible by 4)
    for (; i < num_bins; ++i) {
        // Original scalar code
        float db = db_ptr[i];
        float bar_height = (db - m_min_db) * db_to_height;
        bar_height = std::clamp(bar_height, 0.0f, static_cast<float>(m_height));
        
        auto color = m_palette.get_color(db);
        size_t x = static_cast<size_t>(static_cast<float>(i) * bin_width);
        size_t bottom_y = m_height - 1;
        size_t top_y = static_cast<size_t>(m_height - bar_height);
        
        if (top_y >= m_height) top_y = m_height - 1;
        
        for (size_t y = top_y; y <= bottom_y; ++y) {
            uint8_t* pixel = pixels + (y * row_stride) + (x * 4);
            pixel[0] = color.red;
            pixel[1] = color.green;
            pixel[2] = color.blue;
            pixel[3] = color.alpha;
        }
    }
}
```

**Platform Agnostic Note**: 
- On x86: Uses SSE/AVX2
- On ARM: Would need NEON intrinsics
- Fallback: Scalar code (already provided)
- Compiler flags: `-msse4.2 -mavx2` for best performance

**Savings**: 30-50% of rendering time

---

### 3.4 Fixed-Point Math for Color Mapping

**Problem**: Floating-point operations in color mapping.

**Solution**: Use 16.16 fixed-point for intermediate calculations.

```cpp
// In spectrum_display.h - SpectrumPalette class:
class SpectrumPalette {
public:
    // Fixed-point version
    struct FixedColor {
        uint8_t r, g, b, a;
    };
    
    static constexpr int FIXED_SHIFT = 16;
    static constexpr int FIXED_SCALE = 1 << FIXED_SHIFT;
    
    void precompute_fixed_lut(int min_db_fixed, int max_db_fixed);
    FixedColor get_color_fixed(int db_fixed) const;
    
private:
    std::array<FixedColor, 65536> m_fixed_lut;
};

// Convert float dB to fixed-point
int db_to_fixed(float db, float min_db, float max_db) {
    int value = static_cast<int>((db - min_db) * FIXED_SCALE / (max_db - min_db));
    return std::clamp(value, 0, FIXED_SCALE - 1);
}

// In rendering code:
// Instead of: auto color = m_palette.get_color(db);
// Use: auto color = m_palette.get_color_fixed(db_fixed);

// Precomputation:
void SpectrumPalette::precompute_fixed_lut(int min_db_fixed, int max_db_fixed) {
    for (size_t i = 0; i < LUT_SIZE; ++i) {
        // Map fixed-point index to dB range
        float db = min_db + (static_cast<float>(i) / FIXED_SCALE) * (max_db - min_db);
        
        // Generate color using existing logic
        RgbColor rgb = get_color(db);
        
        m_fixed_lut[i] = {rgb.red, rgb.green, rgb.blue, rgb.alpha};
    }
}

SpectrumPalette::FixedColor SpectrumPalette::get_color_fixed(int db_fixed) const {
    size_t index = static_cast<size_t>(db_fixed);
    index = std::min(index, size_t(LUT_SIZE - 1));
    return m_fixed_lut[index];
}
```

**Savings**: ~15% CPU time in color mapping

---

## Recommended Execution Plan

### Week 1: Quick Wins (Est. 2-4 hours)

**Priority**: High impact, low effort, platform agnostic

1. **Consolidate pixel buffers** (1 hour)
   - Modify `SdlRenderer::render_with_dirty_regions` to accept display objects
   - Implement `render_direct()` in SpectrumDisplay and WaterfallDisplay
   - Remove `combined_pixels` from main.cpp
   
2. **Precomputed color LUT** (30 min)
   - Modify `SpectrumPalette` to precompute lookup table
   - Replace `get_color(float)` with LUT-based version
   
3. **Quantize waterfall history** (1 hour)
   - Change RingBuffer type from `vector<float>` to `vector<uint8_t>`
   - Add `quantize_db()` and `dequantize_db()` methods
   - Update `add_spectrum_line()` and `render()`
   
4. **Replace `std::fill` with `memset`** (15 min)
   - Modify `PixelBuffer::clear()` to use `memset`

**Verification**: 
```bash
# Before and after each change:
/usr/bin/time -v ./openspectrum  # Linux
vtune -collect hotspots ./openspectrum  # Windows
```

**Expected Results**: 
- RAM: ~43 MB → ~36 MB
- CPU: ~14% → ~5-8%

---

### Week 2: Medium Effort (Est. 4-8 hours)

1. **Manual min/max loops** (1 hour)
   - Replace `std::ranges::min_element/max_element` with raw loops
   - Update SpectrumDisplay and WaterfallDisplay
   
2. **Raw pointers in hot loops** (1 hour)
   - Replace iterators with raw pointers in render methods
   - Focus on SpectrumDisplay::render() and WaterfallDisplay::render()
   
3. **Increase async buffers** (5 min)
   - Change `dev.start_streaming(8)` to `dev.start_streaming(32)`
   
4. **Zero-copy FFT** (2 hours)
   - Add FrameHandle overload to FftAnalyzer
   - Update main.cpp to use it

**Verification**: Profile after each change

**Expected Results**: 
- RAM: ~36 MB → ~35 MB
- CPU: ~5-8% → ~2-4%

---

### Week 3+: Advanced (If Needed)

1. **Batch sample processing** (2 hours)
   - Implement FFT batching (2-4 FFTs before render)
   
2. **SIMD rendering** (4 hours)
   - Add SSE/AVX2 support for rendering loops
   - Add NEON support for ARM (optional)
   
3. **Fixed-point math** (2 hours)
   - Implement fixed-point color mapping

**Verification**: Final profiling

**Expected Results**: 
- RAM: ~35 MB → ~34 MB
- CPU: ~2-4% → ~1-2%

---

## Measurement Commands

### Linux (WSL2 or Native)
```bash
# Memory and CPU
/usr/bin/time -v ./openspectrum

# Detailed memory map
pmap -x $(pidof openspectrum)

# Cache analysis
valgrind --tool=cachegrind ./openspectrum
cg_annotate cachegrind.out.*

# Heap profiling
valgrind --tool=massif ./openspectrum
ms_print massif.out.*
```

### Windows (Native)
```bash
# Hotspot analysis
vtune -collect hotspots -result-dir vtune_hotspots ./openspectrum

# Microarchitecture
vtune -collect microarchitecture -result-dir vtune_uarch ./openspectrum

# Memory access
vtune -collect memory-access -result-dir vtune_memory ./openspectrum
```

---

## Status Tracking

Use this checklist to track progress:

- [ ] 1.1 Consolidate pixel buffers
- [ ] 1.2 Precomputed color LUT
- [ ] 1.3 Quantize waterfall history
- [ ] 1.4 `memset` instead of `std::fill`
- [ ] 2.1 Manual min/max loops
- [ ] 2.2 Raw pointers in hot loops
- [x] 2.3 Increase async buffers to 32
- [ ] 2.4 Zero-copy FFT
- [ ] 3.1 Batch sample processing
- [ ] 3.2 SIMD rendering
- [ ] 3.3 Fixed-point math

---

## Performance Targets

| Metric | Baseline | Target | Status |
|--------|----------|--------|--------|
| RAM Usage | ~43 MB | <10 MB | ⬜ |
| CPU Usage | ~0.8% (Windows) | <0.5% | ⬜ |
| GPU Utilization | ~1.7% | >5% | ⬜ |
| Instructions Retired | 10.05B | <8B | ⬜ |
| CPI Rate | 0.678 | <0.6 | ⬜ |

---

## Notes

1. **Platform Agnostic**: All optimizations use standard C++ or SDL2 features. No hardcoded D3D11/Vulkan.
2. **Demoscene Spirit**: Precomputation, lookup tables, manual loops over STL algorithms.
3. **Minimalistic**: Focus on reducing allocations and CPU overhead, not adding features.
4. **Weak Hardware**: Optimizations target Intel UHD Graphics (weak integrated GPU).
5. **Software Renderer**: Current software renderer is sufficient; no need for GPU compute shaders.

**Key Philosophy**: Precompute everything, minimize state, use pointers over iterators, memset/memcpy over STL algorithms.
