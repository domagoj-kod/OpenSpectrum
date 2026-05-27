// SPDX-License-Identifier: MIT

#include "waterfall_display.h"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <utility>
#include <vector>

#include <SDL2/SDL.h>

namespace openspectrum {

WaterfallDisplay::WaterfallDisplay(size_t width, size_t height,
                                   size_t history_lines)
    : m_width(width), m_height(height),
      m_history_lines(std::min(history_lines, height)),
      m_pixels(width * height * 4), // Phase 3: RGBA format
      m_history(history_lines) { // Ring buffer with fixed capacity
  // Start with empty history - lines will be added via add_spectrum_line()
  // This ensures proper dirty rect tracking from the first line
}

void WaterfallDisplay::set_db_range(float min_db, float max_db) {
  m_min_db = std::min(min_db, max_db);
  m_max_db = std::max(min_db, max_db);
  // Update palette range for optimized color lookup
  m_palette.set_db_range(m_min_db, m_max_db);
}

uint8_t WaterfallDisplay::quantize_db(float db) noexcept {
  if (db <= HIST_DB_MIN) return 0;
  if (db >= HIST_DB_MAX) return 255;
  return static_cast<uint8_t>((db - HIST_DB_MIN) * (255.0F / HIST_DB_RANGE) + 0.5F);
}

float WaterfallDisplay::dequantize_db(uint8_t q) noexcept {
  return HIST_DB_MIN + static_cast<float>(q) * (HIST_DB_RANGE / 255.0F);
}

void WaterfallDisplay::update_global_range() {
  if (m_history.empty()) {
    m_global_min = m_min_db;
    m_global_max = m_max_db;
    m_palette.set_db_range(m_global_min, m_global_max);
    return;
  }

  uint8_t q_min = 255;
  uint8_t q_max = 0;

  for (size_t i = 0; i < m_history.size(); ++i) {
    const uint8_t *ptr = m_history[i].data();
    const size_t n = m_history[i].size();
    for (size_t j = 0; j < n; ++j) {
      if (ptr[j] < q_min) q_min = ptr[j];
      if (ptr[j] > q_max) q_max = ptr[j];
    }
  }

  float new_min = dequantize_db(q_min);
  float new_max = dequantize_db(q_max);

  float const range = new_max - new_min;
  if (range > 0) {
    new_min -= range * 0.05F;
    new_max += range * 0.05F;
  } else {
    new_min -= 5.0F;
    new_max += 5.0F;
  }

  m_global_min = new_min;
  m_global_max = new_max;
  m_palette.set_db_range(m_global_min, m_global_max);
}

void WaterfallDisplay::reset() {
  m_history.clear();
  m_global_min = m_min_db;
  m_global_max = m_max_db;
  // Update palette with reset range
  m_palette.set_db_range(m_global_min, m_global_max);
  
  // Mark entire waterfall as dirty
  m_dirty_rects.push_back({0, 0, static_cast<int>(m_width), static_cast<int>(m_height)});
}

SDL_Rect WaterfallDisplay::line_to_rect(size_t line_index) const {
  if (line_index >= m_history.size()) {
    return {0, 0, 0, 0};
  }
  
  size_t line_height = std::max<size_t>(1UL, m_height / m_history.capacity());
  size_t y_offset = line_index * line_height;
  
  // Handle last line
  size_t actual_height = line_height;
  if (y_offset + line_height > m_height) {
    actual_height = m_height - y_offset;
  }
  
  return {0, static_cast<int>(y_offset), static_cast<int>(m_width), static_cast<int>(actual_height)};
}

void WaterfallDisplay::add_spectrum_line(const std::vector<float> &db_values) {
  if (db_values.empty()) {
    return; // Invalid input
  }

  // Resample if needed to match width
  std::vector<float> line(m_width, m_min_db);
  if (db_values.size() == m_width) {
    line = db_values;
  } else if (db_values.size() > m_width) {
    // Downsample by averaging
    float const scale =
        static_cast<float>(db_values.size()) / static_cast<float>(m_width);
    for (size_t i = 0; i < m_width; ++i) {
      auto start = static_cast<size_t>(static_cast<float>(i) * scale);
      auto end = static_cast<size_t>(static_cast<float>(i + 1) * scale);
      end = std::min(end, db_values.size());
      float sum = 0.0F;
      for (size_t j = start; j < end; ++j) {
        sum += db_values[j];
      }
      line[i] = (end > start) ? sum / static_cast<float>(end - start)
                              : db_values[start];
    }
  } else {
    // Upsample using nearest-neighbor interpolation
    float const scale =
        static_cast<float>(db_values.size()) / static_cast<float>(m_width);
    for (size_t i = 0; i < m_width; ++i) {
      auto src_idx = static_cast<size_t>(static_cast<float>(i) * scale);
      src_idx = std::min(src_idx, db_values.size() - 1);
      line[i] = db_values[src_idx];
    }
  }

  // Track if we're wrapping around (overwriting old data)
  bool was_full = m_history.full();
  size_t old_size = m_history.size();

  // Quantize float dB values to uint8 before storing (4:1 memory reduction)
  std::vector<uint8_t> quantized(m_width);
  for (size_t i = 0; i < m_width; ++i) {
    quantized[i] = quantize_db(line[i]);
  }

  m_history.push(std::move(quantized));

  // Mark dirty rectangles
  if (old_size < m_history.capacity()) {
    // Buffer was not full, new line added at end
    if (old_size > 0) {
      m_dirty_rects.push_back(line_to_rect(old_size));
    } else {
      // First line - mark all
      m_dirty_rects.push_back({0, 0, static_cast<int>(m_width), static_cast<int>(m_height)});
    }
  } else if (was_full) {
    // Buffer was full - all logical indices have shifted due to circular overwrite
    // Need to mark entire waterfall as dirty since all lines moved
    m_dirty_rects.push_back({0, 0, static_cast<int>(m_width), static_cast<int>(m_height)});
  }

  // Update ranges if autoscale
  if (m_autoscale) {
    update_global_range();
  }

  render();
}

void WaterfallDisplay::render() {
  if (m_history.empty()) {
    m_pixels.clear();
    // Mark entire display as needing update
    m_dirty_rects.push_back({0, 0, static_cast<int>(m_width), static_cast<int>(m_height)});
    return;
  }

  const float db_range = m_global_max - m_global_min;
  if (db_range <= 0) {
    m_pixels.clear();
    m_dirty_rects.push_back({0, 0, static_cast<int>(m_width), static_cast<int>(m_height)});
    return;
  }

  // Fix for white horizontal lines: Clear entire pixel buffer to background
  // (black) before rendering. When history is not full, the top portion
  // would otherwise contain stale pixel data from previous renders.
  // This happens especially at large FFT sizes where processing is slower.
  std::memset(m_pixels.data(), 0, m_pixels.size());

  // Phase 3: Precompute row stride in bytes (4 bytes per pixel)
  const size_t row_stride = m_width * 4;

  // Render history lines from bottom to top (oldest at bottom, newest at top)
  size_t y_offset = 0;
  const size_t line_height =
      std::max<size_t>(1UL, m_height / m_history.capacity());

  for (size_t i = 0; i < m_history.size(); ++i) {
    if (y_offset >= m_height) {
      break;
    }

    const auto& line = m_history[i];
    size_t const actual_line_height = (y_offset + line_height <= m_height)
                                          ? line_height
                                          : (m_height - y_offset);

    for (size_t y = 0; y < actual_line_height; ++y) {
      size_t const pixel_row = y_offset + y;

      // Phase 3: Precompute row start pointer
      uint8_t *row_ptr = m_pixels.data() + (pixel_row * row_stride);

      const uint8_t *line_ptr = line.data();
      for (size_t x = 0; x < m_width && x < line.size(); ++x) {
        float const db = dequantize_db(line_ptr[x]);
        auto color = m_palette.get_color(db);

        // Phase 3: Direct pointer access (no bounds check)
        uint8_t *dst = row_ptr + (x * 4);
        dst[0] = color.red;
        dst[1] = color.green;
        dst[2] = color.blue;
        dst[3] = color.alpha;
      }
    }
    
    y_offset += line_height;
  }
}

} // namespace openspectrum
