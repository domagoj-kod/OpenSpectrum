// SPDX-License-Identifier: GPL-3.0-or-later

#include "waterfall_display.h"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <utility>
#include <vector>

#ifdef __AVX2__
#include <immintrin.h>
#endif

#include <SDL2/SDL.h>

namespace openspectrum {

WaterfallDisplay::WaterfallDisplay(size_t width, size_t height,
                                   size_t history_lines)
    : m_width(width), m_height(height),
      m_pixels(width * height * 4),
      m_history(history_lines),
      m_new_line(width * std::max<size_t>(1UL, height / history_lines) * 4),
      m_line_buf(width),
      m_quantized_buf(width) {
  rebuild_rgba_lut();
}

void WaterfallDisplay::set_color_map(SpectrumPalette::ColorMap map) {
  m_palette.set_color_map(map);
  rebuild_rgba_lut();
}

void WaterfallDisplay::set_db_range(float min_db, float max_db) {
  m_min_db = std::min(min_db, max_db);
  m_max_db = std::max(min_db, max_db);
  m_palette.set_db_range(m_min_db, m_max_db);
  rebuild_rgba_lut();
}

void WaterfallDisplay::rebuild_rgba_lut() {
  m_needs_full_render = true; // existing GPU texture is stale after a LUT change
  for (size_t q = 0; q < 256; ++q) {
    float const db = dequantize_db(static_cast<uint8_t>(q));
    RgbColor const color = m_palette.get_color(db);
    m_rgba_lut[q] = static_cast<uint32_t>(color.red)
                  | (static_cast<uint32_t>(color.green) << 8)
                  | (static_cast<uint32_t>(color.blue)  << 16)
                  | (static_cast<uint32_t>(color.alpha) << 24);
  }
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

  // Only rebuild the LUT (and mark a full re-render) when the range actually
  // changes. rebuild_rgba_lut() sets m_needs_full_render=true, which defeats
  // the GPU scroll path if called unconditionally every frame.
  if (new_min == m_global_min && new_max == m_global_max) {
    return;
  }

  m_global_min = new_min;
  m_global_max = new_max;
  m_palette.set_db_range(m_global_min, m_global_max);
  rebuild_rgba_lut();
}

void WaterfallDisplay::reset() {
  m_history.clear();
  m_global_min = m_min_db;
  m_global_max = m_max_db;
  m_palette.set_db_range(m_global_min, m_global_max);
  m_needs_full_render = true; // GPU scroll texture must be re-seeded
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
  std::vector<float> &line = m_line_buf;
  std::fill(line.begin(), line.end(), m_min_db);
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
  for (size_t i = 0; i < m_width; ++i) {
    m_quantized_buf[i] = quantize_db(line[i]);
  }

  m_history.push(m_quantized_buf);

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

// Render one quantized history line into a raw RGBA destination buffer.
// dst must have at least m_width * rows * 4 bytes capacity.
void WaterfallDisplay::render_line_into(const std::vector<uint8_t>& hist_line,
                                         uint8_t* dst_base,
                                         size_t rows) const {
  const size_t row_stride = m_width * 4;
  const uint8_t *line_ptr = hist_line.data();
  for (size_t y = 0; y < rows; ++y) {
    uint8_t *dst = dst_base + y * row_stride;
#ifdef __AVX2__
    const int *lut = reinterpret_cast<const int *>(m_rgba_lut);
    size_t x = 0;
    for (; x + 8 <= m_width; x += 8, dst += 32) {
      __m128i idx8   = _mm_loadl_epi64(reinterpret_cast<const __m128i *>(line_ptr + x));
      __m256i idx32  = _mm256_cvtepu8_epi32(idx8);
      __m256i colors = _mm256_i32gather_epi32(lut, idx32, 4);
      _mm256_storeu_si256(reinterpret_cast<__m256i *>(dst), colors);
    }
    for (; x < m_width; ++x, dst += 4) {
      memcpy(dst, &m_rgba_lut[line_ptr[x]], 4);
    }
#else
    for (size_t x = 0; x < m_width; ++x, dst += 4) {
      memcpy(dst, &m_rgba_lut[line_ptr[x]], 4);
    }
#endif
  }
}

void WaterfallDisplay::render() {
  if (m_history.empty()) {
    m_pixels.clear();
    m_dirty_rects.push_back({0, 0, static_cast<int>(m_width), static_cast<int>(m_height)});
    return;
  }

  const float db_range = m_global_max - m_global_min;
  if (db_range <= 0) {
    m_pixels.clear();
    m_dirty_rects.push_back({0, 0, static_cast<int>(m_width), static_cast<int>(m_height)});
    return;
  }

  const size_t line_height = std::max<size_t>(1UL, m_height / m_history.capacity());

  // Steady-state scroll path: render only the newest line into m_new_line.
  // SdlRenderer::render_displays_scroll() shifts the GPU texture and uploads
  // just this strip — ~5 KB instead of ~1.5 MB per frame.
  if (!m_needs_full_render && m_history.full()) {
    render_line_into(m_history.back(), m_new_line.data(), line_height);
    return; // no dirty rects — caller uses render_displays_scroll()
  }

  // Full render: rebuild m_pixels from all history lines.
  // Needed after reset(), rebuild_rgba_lut(), or on the very first frame.
  std::memset(m_pixels.data(), 0, m_pixels.size());
  const size_t row_stride = m_width * 4;
  size_t y_offset = 0;

  for (size_t i = 0; i < m_history.size(); ++i) {
    if (y_offset >= m_height) break;
    size_t const actual_lh = (y_offset + line_height <= m_height)
                                 ? line_height : (m_height - y_offset);
    render_line_into(m_history[i], m_pixels.data() + y_offset * row_stride, actual_lh);
    y_offset += line_height;
  }

  if (m_history.full()) {
    m_needs_full_render = false; // next frame: scroll path (m_wf_scroll_tex now seeded)
  }
  m_dirty_rects.push_back({0, 0, static_cast<int>(m_width), static_cast<int>(m_height)});
}

} // namespace openspectrum
