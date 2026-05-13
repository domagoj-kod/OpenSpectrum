// SPDX-License-Identifier: MIT
#pragma once

#include "spectrum_display.h"

#include <cstdint>
#include <deque>
#include <memory>

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
  void set_color_map(SpectrumPalette::ColorMap map) {
    m_palette.set_color_map(map);
  }
  void set_db_range(float min_db, float max_db);
  void set_autoscale(bool enabled) { m_autoscale = enabled; }

  // Get current dB range
  float min_db() const noexcept { return m_min_db; }
  float max_db() const noexcept { return m_max_db; }

  // Reset history (clear waterfall)
  void reset();

private:
  size_t m_width;
  size_t m_height;
  size_t m_history_lines;
  PixelBuffer m_pixels; // Phase 3: RGBA format

  // Circular buffer for history
  std::deque<std::vector<float>> m_history;
  size_t m_history_capacity;

  SpectrumPalette m_palette;

  float m_min_db = -120.0f;
  float m_max_db = 0.0f;
  bool m_autoscale = true;
  float m_global_min = -120.0f;
  float m_global_max = 0.0f;

  void render();
  void update_global_range();
};

} // namespace openspectrum