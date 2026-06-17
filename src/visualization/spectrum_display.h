// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <array>
#include <cstring>
#include <cstdint>
#include <vector>

#include "openspectrum/attributes.h"

// Forward declaration for SDL types
#include <SDL3/SDL.h>

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
  [[nodiscard]] const uint8_t *data() const noexcept { return m_data; }
  [[nodiscard]] size_t size() const noexcept { return m_size; }

  // Subscript operator (for compatibility, but prefer direct pointer access)
  uint8_t &operator[](size_t index) noexcept { return m_data[index]; }
  const uint8_t &operator[](size_t index) const noexcept {
    return m_data[index];
  }

  // Clear buffer (set to black/transparent)
  void clear() {
    if (m_data) memset(m_data, 0, m_size);
  }

  // Get pixel at (x, y) for RGBA format (4 bytes per pixel)
  uint8_t *pixel_ptr(size_t x, size_t y, size_t width) noexcept {
    return m_data + (y * width + x) * 4;
  }
  [[nodiscard]] const uint8_t *pixel_ptr(size_t x, size_t y, size_t width) const noexcept {
    return m_data + (y * width + x) * 4;
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
  [[nodiscard]] RgbColor get_color(float db_value) const;

  // Color map presets
  enum class ColorMap { JET, VIRIDIS, HOT, GRAYSCALE, BLUE_RED };

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
  OS_HOT void update_spectrum(const std::vector<float> &db_values,
                              const std::vector<float> & /*freq_bins*/,
                              float center_freq_hz, float sample_rate_hz);

  // Build GPU draw geometry for the current spectrum: one colored quad per bin
  // (4 vertices + 6 indices), positioned in the spectrum region [0,region_w) x
  // [0,region_h). Drawn by SdlRenderer::render_spectrum via SDL_RenderGeometry,
  // replacing the old per-frame CPU pixel paint + full texture upload. The out
  // buffers are cleared and refilled (caller reuses them frame-to-frame so the
  // capacity is retained — no per-frame allocation).
  OS_HOT void build_vertices(float region_w, float region_h,
                             std::vector<SDL_Vertex> &verts,
                             std::vector<int> &indices) const;

  // Paint the current spectrum into m_pixels on demand. Cold path: only the
  // spectrogram PNG exporter consumes the pixel buffer (via get_pixels()); the
  // live render path uses build_vertices() instead.
  OS_COLD void render_to_pixels();

  // Cursor readout sample: the displayed amplitude and the snapped trace point
  // for a horizontal cursor position. Mirrors build_vertices() exactly (same
  // averaging source, dB range, and bin->x / dB->y mapping) so the marker dot
  // lands on the drawn curve.
  struct CursorSample {
    float db = 0.0F;     // displayed dB at the cursor's bin
    float dot_y = 0.0F;  // y of the trace point within [0, region_h]
    size_t bin = 0;      // bin index under the cursor
  };
  [[nodiscard]] bool sample_at_x(float x, float region_w, float region_h,
                                 CursorSample &out) const;

  // Get rendered pixel buffer (RGB32 format: RGBA interleaved). Only valid after
  // render_to_pixels(); consumed by the spectrogram exporter.
  [[nodiscard]] const PixelBuffer &get_pixels() const { return m_pixels; }
  uint8_t *pixel_data() { return m_pixels.data(); }
  [[nodiscard]] const uint8_t *pixel_data() const { return m_pixels.data(); }

  // Get dimensions
  [[nodiscard]] size_t width() const noexcept { return m_width; }
  [[nodiscard]] size_t height() const noexcept { return m_height; }

  // Configuration
  void set_color_map(SpectrumPalette::ColorMap map) {
    m_palette.set_color_map(map);
  }
  void set_db_range(float min_db, float max_db);
  void set_autoscale(bool enabled) { m_autoscale = enabled; }

  // Get current dB range
  [[nodiscard]] float min_db() const noexcept { return m_min_db; }
  [[nodiscard]] float max_db() const noexcept { return m_max_db; }

  // Trace modes. Averaging replaces the drawn bars with an exponential moving
  // average of the per-bin dB values (video averaging); max-hold overlays a
  // line at the running per-bin maximum of the *raw* spectrum. Both traces
  // re-seed automatically when the bin count changes (FFT size switch).
  void set_averaging(bool on) {
    if (on != m_avg_enabled) {
      m_avg_enabled = on;
      m_avg_data.clear(); // re-seed from the next update
    }
  }
  void set_max_hold(bool on) {
    if (on != m_max_hold_enabled) {
      m_max_hold_enabled = on;
      m_max_hold_data.clear();
    }
  }
  void reset_traces() {
    m_avg_data.clear();
    m_max_hold_data.clear();
  }

private:
  size_t m_width;
  size_t m_height;
  PixelBuffer m_pixels; // Phase 3: Optimized pixel buffer

  std::vector<float> m_spectrum_data;
  SpectrumPalette m_palette;

  // Trace state. m_avg_data is the EMA accumulator; m_max_hold_data the
  // running raw-spectrum maximum. Empty vector == not yet seeded.
  std::vector<float> m_avg_data;
  std::vector<float> m_max_hold_data;
  bool m_avg_enabled = false;
  bool m_max_hold_enabled = false;
  // EMA weight for new data: ~6-frame settling, smooths the noise floor
  // without visibly lagging real signal changes at 60 fps.
  static constexpr float kAvgAlpha = 0.25F;

  float m_center_freq_hz = 0.0f;
  float m_sample_rate_hz = 0.0f;
  float m_min_db = -120.0f;
  float m_max_db = 0.0f;
  bool m_autoscale = true;

  void clear();
};

} // namespace openspectrum
