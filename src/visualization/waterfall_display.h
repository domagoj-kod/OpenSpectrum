// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "utils/attributes.h"
#include "utils/ring_buffer.h"
#include "visualization/spectrum_display.h"

#include <cstdint>
#include <vector>

struct SDL_Rect;

namespace openspectrum {

// Time (y) vs frequency (x) with colour intensity, over a ring of history.
class WaterfallDisplay {
public:
  WaterfallDisplay(size_t width, size_t height, size_t history_lines);
  ~WaterfallDisplay() = default;

  OS_HOT void add_spectrum_line(const std::vector<float> &db_values);

  [[nodiscard]] const std::vector<uint8_t> &get_pixels() const {
    return m_pixels;
  }

  [[nodiscard]] size_t width() const noexcept { return m_width; }
  [[nodiscard]] size_t height() const noexcept { return m_height; }

  void set_color_map(SpectrumPalette::ColorMap map);
  void set_db_range(float min_db, float max_db);

  [[nodiscard]] float min_db() const noexcept { return m_min_db; }
  [[nodiscard]] float max_db() const noexcept { return m_max_db; }

  void reset();

  // CPU-side memcpy bounds, so SDL_Rect, not SDL_FRect.
  [[nodiscard]] const std::vector<SDL_Rect> &get_dirty_rects() const {
    return m_dirty_rects;
  }
  // main() calls this once the renderer has taken the frame. Only then is a
  // pending full upload done: cleared any earlier, a LUT change goes down the
  // scroll path and the rows already on screen keep their old colours.
  void mark_rendered() {
    m_dirty_rects.clear();
    m_needs_full_render = false;
  }

  // True while history fills and for one frame after a reset or LUT rebuild;
  // false in steady state, where only the newest line changes.
  [[nodiscard]] bool needs_full_render() const noexcept {
    return m_needs_full_render || !m_history.full();
  }

  // Pixels of the newest line only; valid when needs_full_render() is false.
  [[nodiscard]] const uint8_t *get_new_line_rgba() const noexcept {
    return m_new_line.data();
  }

  [[nodiscard]] size_t get_line_height() const noexcept {
    return std::max<size_t>(1UL, m_height / m_history.capacity());
  }

  // Cursor readout: how long ago the line at y_in_region (0 = top of the pane)
  // was captured. False outside the filled history.
  struct CursorTime {
    double seconds_ago = 0.0;
  };
  [[nodiscard]] bool sample_at_y(float y_in_region, CursorTime &out) const;

private:
  size_t m_width;
  size_t m_height;
  std::vector<uint8_t> m_pixels; // RGBA

  // History as uint8: 0.47 dB per step over -120..0 dB.
  RingBuffer<std::vector<uint8_t>> m_history;

  // Capture time per history line (steady_clock seconds), in lockstep with
  // m_history.
  RingBuffer<double> m_line_times;

  SpectrumPalette m_palette;

  float m_min_db = -120.0f;
  float m_max_db = 0.0f;
  float m_global_min = -120.0f;
  float m_global_max = 0.0f;

  // Autoscale histogram: history pixels per quantized level, kept in lockstep
  // with m_history by add_spectrum_line.
  int32_t m_hist_counts[256]{};

  mutable std::vector<SDL_Rect> m_dirty_rects;

  bool m_needs_full_render = true;
  std::vector<uint8_t> m_new_line;

  std::vector<float> m_line_buf;
  std::vector<uint8_t> m_quantized_buf;

  static constexpr float HIST_DB_MIN = -120.0f;
  static constexpr float HIST_DB_MAX = 0.0f;
  static constexpr float HIST_DB_RANGE = HIST_DB_MAX - HIST_DB_MIN;

  static uint8_t quantize_db(float db) noexcept;
  static float dequantize_db(uint8_t q) noexcept;

  // lut[q] = packed R|G<<8|B<<16|A<<24. Rebuilt on palette or dB range change.
  uint32_t m_rgba_lut[256]{};
  void rebuild_rgba_lut();

  void render();
  void render_line_into(const std::vector<uint8_t> &hist_line,
                        uint8_t *dst_base, size_t rows) const;
  void update_global_range();

  [[nodiscard]] SDL_Rect line_to_rect(size_t line_index) const;
};

} // namespace openspectrum
