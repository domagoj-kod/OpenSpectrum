// SPDX-License-Identifier: MIT
#pragma once

#include <algorithm>
#include <array>
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

namespace openspectrum {

// RGB color structure for pixel data
struct RgbColor {
  uint8_t red;
  uint8_t green;
  uint8_t blue;
  uint8_t alpha;

  constexpr RgbColor() : red(0), green(0), blue(0), alpha(255) {}
  constexpr RgbColor(uint8_t r, uint8_t g, uint8_t b, uint8_t a = 255)
      : red(r), green(g), blue(b), alpha(a) {}

  // Fast copy to pixel buffer (4 bytes: RGBA)
  inline void copy_to(uint8_t *dst) const {
    dst[0] = red;
    dst[1] = green;
    dst[2] = blue;
    dst[3] = alpha;
  }
};

// Phase 3: Optimized pixel buffer for direct pointer access
// Replaces std::vector<uint8_t> for pixel storage
class PixelBuffer {
public:
  PixelBuffer() : m_data(nullptr), m_size(0) {}
  explicit PixelBuffer(size_t size) : m_size(size) {
    m_data = new uint8_t[m_size]();
  }
  ~PixelBuffer() { delete[] m_data; }

  // No copying (use std::move instead)
  PixelBuffer(const PixelBuffer &) = delete;
  PixelBuffer &operator=(const PixelBuffer &) = delete;

  PixelBuffer(PixelBuffer &&other) noexcept
      : m_data(other.m_data), m_size(other.m_size) {
    other.m_data = nullptr;
    other.m_size = 0;
  }

  PixelBuffer &operator=(PixelBuffer &&other) noexcept {
    if (this != &other) {
      delete[] m_data;
      m_data = other.m_data;
      m_size = other.m_size;
      other.m_data = nullptr;
      other.m_size = 0;
    }
    return *this;
  }

  // Direct pointer access (no bounds checking)
  uint8_t *data() noexcept { return m_data; }
  const uint8_t *data() const noexcept { return m_data; }
  size_t size() const noexcept { return m_size; }

  // Subscript operator (for compatibility, but prefer direct pointer access)
  uint8_t &operator[](size_t index) noexcept { return m_data[index]; }
  const uint8_t &operator[](size_t index) const noexcept {
    return m_data[index];
  }

  // Clear buffer (set to black/transparent)
  void clear() { std::fill(m_data, m_data + m_size, 0); }

  // Get pixel at (x, y) for RGBA format (4 bytes per pixel)
  uint8_t *pixel_ptr(size_t x, size_t y, size_t width) noexcept {
    return m_data + (y * width + x) * 4;
  }
  const uint8_t *pixel_ptr(size_t x, size_t y, size_t width) const noexcept {
    return m_data + (y * width + x) * 4;
  }

  // Fill a vertical column with a color
  inline void fill_column(size_t x, size_t y_start, size_t y_end, size_t width,
                          const RgbColor &color) noexcept {
    uint8_t *dst = pixel_ptr(x, y_start, width);
    const size_t stride = width * 4; // bytes per row

    for (size_t y = y_start; y <= y_end; ++y) {
      color.copy_to(dst);
      dst += stride;
    }
  }

  // Fast memset-style clear
  void memset_clear() {
    if (m_data)
      std::fill(m_data, m_data + m_size, 0);
  }

private:
  uint8_t *m_data;
  size_t m_size;
};

// Color palette for spectrum display (rainbow/jet colormap)
class SpectrumPalette {
public:
  static constexpr size_t PALETTE_SIZE = 256;

  SpectrumPalette();

  // Set dB range for color mapping (call when range changes for optimization)
  void set_db_range(float min_db, float max_db);

  // Get color for a dB value (uses precomputed range for efficiency)
  RgbColor get_color(float db_value) const;

  // Legacy version with inline range parameters (slower, keeps backward compat)
  RgbColor get_color(float db_value, float min_db, float max_db) const;

  // Color map presets
  enum class ColorMap { JET, VIRIDIS, HOT, GRAyscale, BLUE_RED };

  void set_color_map(ColorMap map);

private:
  ColorMap m_color_map = ColorMap::JET;
  std::array<RgbColor, PALETTE_SIZE> m_palette;

  // Precomputed values for fast color lookup (Phase 2: integer quantization)
  // Using direct integer mapping: index = (int)((db - min) * scale + 0.5f)
  float m_scale_to_index = 1.0f; // (PALETTE_SIZE-1) / (max_db - min_db)
  float m_db_min = -120.0f;      // Cached min for clamp
  float m_db_max = 0.0f;         // Cached max for clamp

  void generate_jet_palette();
  void generate_viridis_palette();
  void generate_hot_palette();
  void generate_grayscale_palette();
  void generate_blue_red_palette();
};

// 2D Spectrum Display: Amplitude vs Frequency
// Outputs to a 2D RGB buffer for rendering
class SpectrumDisplay {
public:
  SpectrumDisplay(size_t width, size_t height);
  ~SpectrumDisplay() = default;

  // Update spectrum data (frequency bins in dB)
  void update_spectrum(const std::vector<float> &db_values,
                       const std::vector<float> & /*freq_bins*/,
                       float center_freq_hz, float sample_rate_hz);

  // Get rendered pixel buffer (RGB32 format: RGBA interleaved)
  // Phase 3: Returns PixelBuffer for direct access; has .data() and .size()
  // methods
  const PixelBuffer &get_pixels() const { return m_pixels; }
  uint8_t *pixel_data() { return m_pixels.data(); }
  const uint8_t *pixel_data() const { return m_pixels.data(); }

  // Get dimensions
  size_t width() const noexcept { return m_width; }
  size_t height() const noexcept { return m_height; }

  // Configuration
  void set_color_map(SpectrumPalette::ColorMap map) {
    m_palette.set_color_map(map);
  }
  void set_db_range(float min_db, float max_db);
  void set_autoscale(bool enabled) { m_autoscale = enabled; }

  // Get current dB range
  float min_db() const noexcept { return m_min_db; }
  float max_db() const noexcept { return m_max_db; }

private:
  size_t m_width;
  size_t m_height;
  PixelBuffer m_pixels; // Phase 3: Optimized pixel buffer

  std::vector<float> m_spectrum_data;
  SpectrumPalette m_palette;

  float m_center_freq_hz = 0.0f;
  float m_sample_rate_hz = 0.0f;
  float m_min_db = -120.0f;
  float m_max_db = 0.0f;
  bool m_autoscale = true;

  void render();
  void clear();
};

} // namespace openspectrum