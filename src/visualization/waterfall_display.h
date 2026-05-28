// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "spectrum_display.h"
#include "ring_buffer.h"

#include <cstdint>
#include <memory>
#include <vector>

// Forward declaration for SDL types (to avoid including SDL.h in header)
struct SDL_Rect;

namespace openspectrum {

// Waterfall display: Time (Y-axis) vs Frequency (X-axis) with color intensity
// Implements a circular buffer for efficient history management
class WaterfallDisplay {
public:
  WaterfallDisplay(size_t width, size_t height, size_t history_lines);
  ~WaterfallDisplay() = default;

  // Update with new spectrum line
  // db_values: dB values for each frequency bin
  void add_spectrum_line(const std::vector<float> &db_values);

  // Get rendered pixel buffer (RGB32 format)
  // Phase 3: Returns PixelBuffer for direct access; has .data() and .size()
  // methods
  const PixelBuffer &get_pixels() const { return m_pixels; }

  // Get dimensions
  size_t width() const noexcept { return m_width; }
  size_t height() const noexcept { return m_height; }

  // Configuration
  void set_color_map(SpectrumPalette::ColorMap map);
  void set_db_range(float min_db, float max_db);
  void set_autoscale(bool enabled) { m_autoscale = enabled; }

  // Get current dB range
  float min_db() const noexcept { return m_min_db; }
  float max_db() const noexcept { return m_max_db; }

  // Reset history (clear waterfall)
  void reset();

  // Get dirty rectangles for incremental rendering
  const std::vector<SDL_Rect>& get_dirty_rects() const { return m_dirty_rects; }
  void clear_dirty_rects() { m_dirty_rects.clear(); }

  // GPU scroll support: true after reset/LUT-rebuild (full upload needed once),
  // false in steady state where only the newest line changes.
  bool needs_full_render() const noexcept { return m_needs_full_render || !m_history.full(); }

  // Pixel data for just the newest history line (m_width * get_line_height() * 4 bytes).
  // Valid when needs_full_render() == false.
  const uint8_t* get_new_line_rgba() const noexcept { return m_new_line.data(); }

  size_t get_line_height() const noexcept {
    return std::max<size_t>(1UL, m_height / m_history.capacity());
  }

private:
  size_t m_width;
  size_t m_height;
  PixelBuffer m_pixels; // Phase 3: RGBA format

  // Ring buffer for history — stored as uint8 (0.47 dB/step over -120..0 dB)
  RingBuffer<std::vector<uint8_t>> m_history;

  SpectrumPalette m_palette;

  float m_min_db = -120.0f;
  float m_max_db = 0.0f;
  bool m_autoscale = true;
  float m_global_min = -120.0f;
  float m_global_max = 0.0f;

  // Dirty rectangles for incremental rendering
  mutable std::vector<SDL_Rect> m_dirty_rects;

  // GPU scroll state
  bool m_needs_full_render = true;
  std::vector<uint8_t> m_new_line; // m_width * get_line_height() * 4 bytes

  // Reusable scratch buffers for add_spectrum_line — avoids per-frame heap alloc
  std::vector<float>   m_line_buf;
  std::vector<uint8_t> m_quantized_buf;

  // Fixed quantization range: -120..0 dB → 0..255
  static constexpr float HIST_DB_MIN = -120.0f;
  static constexpr float HIST_DB_MAX = 0.0f;
  static constexpr float HIST_DB_RANGE = HIST_DB_MAX - HIST_DB_MIN;

  static uint8_t quantize_db(float db) noexcept;
  static float dequantize_db(uint8_t q) noexcept;

  // Precomputed RGBA table: lut[q] = packed uint32 (R|G<<8|B<<16|A<<24).
  // Scalar path: memcpy 4 bytes. AVX2 path: _mm256_i32gather_epi32 processes
  // 8 pixels per instruction. Rebuilt on palette or dB range change.
  uint32_t m_rgba_lut[256]{};
  void rebuild_rgba_lut();

  void render();
  void render_line_into(const std::vector<uint8_t>& hist_line,
                        uint8_t* dst_base, size_t rows) const;
  void update_global_range();

  SDL_Rect line_to_rect(size_t line_index) const;
};

} // namespace openspectrum
