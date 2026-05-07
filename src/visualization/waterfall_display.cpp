// SPDX-License-Identifier: MIT

#include "waterfall_display.h"
#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <utility>
#include <vector>

namespace openspectrum {

WaterfallDisplay::WaterfallDisplay(size_t width, size_t height,
                                   size_t history_lines)
    : m_width(width), m_height(height),
      m_history_lines(std::min(history_lines, height)),
      m_pixels(width * height * 4), // Phase 3: RGBA format
      m_history_capacity(m_history_lines) {
  // Initialize history with empty lines
  for (size_t i = 0; i < m_history_capacity; ++i) {
    m_history.emplace_back(width, -120.0F); // Default to noise floor
  }
}

void WaterfallDisplay::set_db_range(float min_db, float max_db) {
  m_min_db = std::min(min_db, max_db);
  m_max_db = std::max(min_db, max_db);
  // Update palette range for optimized color lookup
  m_palette.set_db_range(m_min_db, m_max_db);
}

void WaterfallDisplay::update_global_range() {
  if (m_history.empty()) {
    return;
  }

  float new_min = m_history.front()[0];
  float new_max = m_history.front()[0];

  for (const auto &line : m_history) {
    if (!line.empty()) {
      float const line_min = *std::ranges::min_element(line);
      float const line_max = *std::ranges::max_element(line);
      new_min = std::min(new_min, line_min);
      new_max = std::max(new_max, line_max);
    }
  }

  // Add margin
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

  // Update palette with new global range for optimized get_color()
  m_palette.set_db_range(m_global_min, m_global_max);
}

void WaterfallDisplay::reset() {
  m_history.clear();
  for (size_t i = 0; i < m_history_capacity; ++i) {
    m_history.emplace_back(m_width, m_min_db);
  }
  m_global_min = m_min_db;
  m_global_max = m_max_db;
  // Update palette with reset range
  m_palette.set_db_range(m_global_min, m_global_max);
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

  // Add to history (circular buffer)
  m_history.push_back(std::move(line));
  if (m_history.size() > m_history_capacity) {
    m_history.pop_front();
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
    return;
  }

  const float db_range = m_global_max - m_global_min;
  if (db_range <= 0) {
    m_pixels.clear();
    return;
  }

  // Phase 3: Precompute row stride in bytes (4 bytes per pixel)
  const size_t row_stride = m_width * 4;

  // Render history lines from bottom to top (oldest at bottom, newest at top)
  size_t y_offset = 0;
  const size_t line_height =
      std::max<size_t>(1UL, m_height / m_history_capacity);

  for (auto it = m_history.begin(); it != m_history.end();
       ++it, y_offset += line_height) {
    if (y_offset >= m_height) {
      break;
    }

    const auto &line = *it;
    size_t const actual_line_height = (y_offset + line_height <= m_height)
                                          ? line_height
                                          : (m_height - y_offset);

    for (size_t y = 0; y < actual_line_height; ++y) {
      size_t const pixel_row = y_offset + y;

      // Phase 3: Precompute row start pointer
      uint8_t *row_ptr = m_pixels.data() + (pixel_row * row_stride);

      for (size_t x = 0; x < m_width && x < line.size(); ++x) {
        float const db = line[x];
        auto color = m_palette.get_color(db);

        // Phase 3: Direct pointer access (no bounds check)
        uint8_t *dst = row_ptr + (x * 4);
        dst[0] = color.red;
        dst[1] = color.green;
        dst[2] = color.blue;
        dst[3] = color.alpha;
      }
    }
  }
}

} // namespace openspectrum