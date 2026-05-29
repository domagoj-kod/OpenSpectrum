// SPDX-License-Identifier: GPL-3.0-or-later

#include "spectrum_display.h"

#include <cmath>
#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <vector>

#include <SDL2/SDL.h>

namespace openspectrum {

// --- SpectrumPalette Implementation ---

SpectrumPalette::SpectrumPalette() {
  generate_jet_palette();
  // Initialize with default dB range
  set_db_range(-120.0F, 0.0F);
}

void SpectrumPalette::set_color_map(ColorMap map) {
  m_color_map = map;
  switch (map) {
  case ColorMap::JET:
    generate_jet_palette();
    break;
  case ColorMap::VIRIDIS:
    generate_viridis_palette();
    break;
  case ColorMap::HOT:
    generate_hot_palette();
    break;
  case ColorMap::GRAYSCALE:
    generate_grayscale_palette();
    break;
  case ColorMap::BLUE_RED:
    generate_blue_red_palette();
    break;
  }
}

void SpectrumPalette::generate_jet_palette() {
  for (size_t i = 0; i < PALETTE_SIZE; ++i) {
    float const t = static_cast<float>(i) / (PALETTE_SIZE - 1);
    float r = 0.0F;
    float g = 0.0F;
    float b = 0.0F;

    if (t < 0.125F) {
      r = 0.0F;
      g = 0.0F;
      b = 0.5F + 4.0F * t;
    } else if (t < 0.375F) {
      r = 0.0F;
      g = 4.0F * (t - 0.125F);
      b = 1.0F;
    } else if (t < 0.625F) {
      r = 4.0F * (t - 0.375F);
      g = 1.0F;
      b = 1.0F - 4.0F * (t - 0.375F);
    } else if (t < 0.875F) {
      r = 1.0F;
      g = 1.0F - 4.0F * (t - 0.625F);
      b = 0.0F;
    } else {
      r = 1.0F - 4.0F * (t - 0.875F);
      g = 0.0F;
      b = 0.0F;
    }

    m_palette[i] =
        RgbColor(static_cast<uint8_t>(r * 255), static_cast<uint8_t>(g * 255),
                 static_cast<uint8_t>(b * 255));
  }
}

void SpectrumPalette::generate_viridis_palette() {
  // Simplified viridis approximation
  for (size_t i = 0; i < PALETTE_SIZE; ++i) {
    float const t = static_cast<float>(i) / (PALETTE_SIZE - 1);
    float const r = std::clamp(
        0.267F + (0.329F * t) + (1.453F * t * t) - (1.099F * t * t * t), 0.0F, 1.0F);
    float const g = std::clamp(
        0.005F + (1.404F * t) - (0.598F * t * t) + (0.189F * t * t * t), 0.0F, 1.0F);
    float const b = std::clamp(
        0.329F + (1.509F * t) - (2.814F * t * t) + (1.976F * t * t * t), 0.0F, 1.0F);

    m_palette[i] =
        RgbColor(static_cast<uint8_t>(r * 255), static_cast<uint8_t>(g * 255),
                 static_cast<uint8_t>(b * 255));
  }
}

void SpectrumPalette::generate_hot_palette() {
  for (size_t i = 0; i < PALETTE_SIZE; ++i) {
    float const t = static_cast<float>(i) / (PALETTE_SIZE - 1);
    m_palette[i] = RgbColor(static_cast<uint8_t>(255 * t),
                            static_cast<uint8_t>(255 * t * t),
                            static_cast<uint8_t>(255 * t * t * t));
  }
}

void SpectrumPalette::generate_grayscale_palette() {
  for (size_t i = 0; i < PALETTE_SIZE; ++i) {
    auto val = static_cast<uint8_t>(i);
    m_palette[i] = RgbColor(val, val, val);
  }
}

void SpectrumPalette::generate_blue_red_palette() {
  for (size_t i = 0; i < PALETTE_SIZE; ++i) {
    float const t = static_cast<float>(i) / (PALETTE_SIZE - 1);
    m_palette[i] = RgbColor(static_cast<uint8_t>(255 * t), 0,
                            static_cast<uint8_t>(255 * (1 - t)));
  }
}

void SpectrumPalette::set_db_range(float min_db, float max_db) {
  m_db_min = min_db;
  m_db_max = max_db;

  // Phase 2: Integer quantization - precompute scale for direct int mapping
  // index = (int)((db - min) * scale + 0.5f) where scale =
  // (PALETTE_SIZE-1)/range
  float const range = max_db - min_db;
  if (range > 0.0F) {
    m_scale_to_index =
        (static_cast<float>(PALETTE_SIZE - 1)) / (range + 1e-10F);
  } else {
    m_scale_to_index = 1.0F;
  }
}

auto SpectrumPalette::get_color(float db_value) const -> RgbColor {
  // Phase 2: Integer quantization - direct int mapping with rounding
  // index = (int)((db - min) * scale + 0.5f)
  // Uses fma (fused multiply-add) for: (db - min) * scale + 0.5f
  float const scaled = std::fma(db_value - m_db_min, m_scale_to_index, 0.5F);

  // Convert to int (rounds to nearest due to +0.5f)
  int index = static_cast<int>(scaled);

  // Clamp to [0, PALETTE_SIZE-1]
  index = std::max(index, 0);
  if (index >= static_cast<int>(PALETTE_SIZE)) {
    index = static_cast<int>(PALETTE_SIZE - 1);
  }

  return m_palette[static_cast<size_t>(index)];
}

// --- SpectrumDisplay Implementation ---

SpectrumDisplay::SpectrumDisplay(size_t width, size_t height)
    : m_width(width), m_height(height),
      m_pixels(width * height * 4) // Phase 3: RGBA: 4 bytes per pixel
{
  clear();
}

void SpectrumDisplay::set_db_range(float min_db, float max_db) {
  m_min_db = std::min(min_db, max_db);
  m_max_db = std::max(min_db, max_db);
  // Update palette with new range for optimized get_color()
  m_palette.set_db_range(m_min_db, m_max_db);
}

void SpectrumDisplay::clear() { m_pixels.clear(); }

void SpectrumDisplay::update_spectrum(const std::vector<float> &db_values,
                                      const std::vector<float> & /*freq_bins*/,
                                      float center_freq_hz,
                                      float sample_rate_hz) {
  m_center_freq_hz = center_freq_hz;
  m_sample_rate_hz = sample_rate_hz;
  m_spectrum_data = db_values;

  if (m_autoscale && !db_values.empty()) {
    const float *ptr = db_values.data();
    const size_t n = db_values.size();
    float min_val = ptr[0];
    float max_val = ptr[0];
    for (size_t i = 1; i < n; ++i) {
      if (ptr[i] < min_val) min_val = ptr[i];
      if (ptr[i] > max_val) max_val = ptr[i];
    }
    set_db_range(min_val - 5.0F, max_val + 5.0F);
  }

  render();
  
  // Mark entire spectrum as dirty (simplified for now)
  m_dirty_rects.push_back({0, 0, static_cast<int>(m_width), static_cast<int>(m_height)});
}

void SpectrumDisplay::render() {
  if (m_spectrum_data.empty()) {
    clear();
    return;
  }

  // Clear background (black)
  clear();

  // Draw spectrum line or filled area
  const size_t num_bins = m_spectrum_data.size();
  const float bin_width =
      static_cast<float>(m_width) / static_cast<float>(num_bins);

  // Draw filled spectrum (from bottom to spectrum line)
  const float db_range = m_max_db - m_min_db;
  const float db_to_height = static_cast<float>(m_height) / db_range;

  // Phase 3: Precompute row stride in bytes (4 bytes per pixel)
  const size_t row_stride = m_width * 4;

  for (size_t i = 0; i < num_bins; ++i) {
    float const db = m_spectrum_data[i];
    // Invert Y-axis: higher dB = higher on screen
    float bar_height = (db - m_min_db) * db_to_height;
    bar_height = std::clamp(bar_height, 0.0F, static_cast<float>(m_height));

    // Get color for this dB value (optimized: uses precomputed range)
    auto color = m_palette.get_color(db);

    // Fill from bottom to spectrum line
    auto x = static_cast<size_t>(static_cast<float>(i) * bin_width);
    size_t const bottom_y = m_height - 1;
    auto top_y =
        static_cast<size_t>(static_cast<float>(m_height) - bar_height);

    if (top_y >= m_height) {
      top_y = m_height - 1;
    }

    // Phase 3: Direct pointer access - precompute starting pixel pointer
    // idx = (y * m_width + x) * 4 = y * (m_width * 4) + x * 4
    uint8_t *row_start = m_pixels.data() + (top_y * row_stride) + (x * 4);
    uint8_t *row_end = m_pixels.data() + ((bottom_y + 1) * row_stride) + (x * 4);

    // Fill vertical column using pointer arithmetic
    for (uint8_t *dst = row_start; dst < row_end; dst += row_stride) {
      dst[0] = color.red;
      dst[1] = color.green;
      dst[2] = color.blue;
      dst[3] = color.alpha;
    }
  }
}

} // namespace openspectrum
