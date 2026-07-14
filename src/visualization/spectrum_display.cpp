// SPDX-License-Identifier: GPL-3.0-or-later

#include "spectrum_display.h"

#include <cmath>
#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <vector>

#include <SDL3/SDL.h>

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
    : m_width(width), m_height(height) {}

void SpectrumDisplay::set_db_range(float min_db, float max_db) {
  m_min_db = std::min(min_db, max_db);
  m_max_db = std::max(min_db, max_db);
  // Update palette with new range for optimized get_color()
  m_palette.set_db_range(m_min_db, m_max_db);
}

void SpectrumDisplay::update_spectrum(const std::vector<float> &db_values) {
  m_spectrum_data = db_values;

  const size_t n = db_values.size();

  // Video averaging: EMA over the per-bin dB values. Seed (or re-seed after an
  // FFT-size change) by copying the first frame. Simple fma loop —
  // auto-vectorizes under -O3 -march=haswell.
  if (m_avg_enabled) {
    if (m_avg_data.size() != n) {
      m_avg_data = db_values;
    } else {
      float *avg = m_avg_data.data();
      const float *cur = db_values.data();
      for (size_t i = 0; i < n; ++i) {
        avg[i] += kAvgAlpha * (cur[i] - avg[i]);
      }
    }
  }

  // Max-hold tracks the *raw* spectrum (true peak detector semantics, even
  // while averaging smooths the drawn bars).
  if (m_max_hold_enabled) {
    if (m_max_hold_data.size() != n) {
      m_max_hold_data = db_values;
    } else {
      float *hold = m_max_hold_data.data();
      const float *cur = db_values.data();
      for (size_t i = 0; i < n; ++i) {
        hold[i] = std::max(hold[i], cur[i]);
      }
    }
  }

  // Autoscale. The top tracks the peak: max is dominated by real signals, and
  // for pure noise it only lands ~10 dB above the floor, so it is well behaved.
  //
  // The bottom tracks the *median* bin, not the min. A single periodogram bin
  // of Gaussian noise is exponentially distributed, so min over N bins is an
  // extreme-value statistic with no physical meaning: at 16K bins the deepest
  // null sits ~42 dB below the noise floor and re-rolls every frame, which
  // stretched the pane across a ~90 dB range (half of it empty), pinned the
  // floor at mid-screen and made the axis labels jitter. The median sits a
  // fixed 1.6 dB below the floor's mean power and is stable to ~0.05 dB.
  if (!db_values.empty()) {
    const float *ptr = db_values.data();
    float max_val = ptr[0];
    for (size_t i = 1; i < n; ++i) {
      max_val = std::max(max_val, ptr[i]);
    }
    // nth_element needs a mutable copy — m_spectrum_data must stay in bin
    // order. Noise bins are iid, so a strided subsample estimates the same
    // median: at 16K bins a quarter-sample is still stable to ~0.1 dB, for a
    // quarter of the copy + selection cost (which is otherwise ~0.2 ms/frame,
    // a third of the whole DSP budget, spent on a number that barely moves).
    // resize() is a no-op after the first frame of a given size.
    const size_t m = (n + kMedianStride - 1) / kMedianStride;
    m_scratch.resize(m);
    for (size_t i = 0; i < m; ++i) {
      m_scratch[i] = ptr[i * kMedianStride];
    }
    auto mid = m_scratch.begin() + static_cast<std::ptrdiff_t>(m / 2);
    std::nth_element(m_scratch.begin(), mid, m_scratch.end());
    set_db_range(*mid - kFloorMargin, max_val + 5.0F);
  }

  // The live render path draws the spectrum on the GPU from build_vertices();
  // there is no CPU pixel buffer. Exports capture the rendered frame directly
  // (SdlRenderer::request_capture), so nothing here needs to paint pixels.
}

// One colored quad per bin, emitted as 4 vertices + 6 indices into the caller's
// reusable buffers. Positions are in spectrum-region coords: x spans the bin's
// horizontal slice, y runs from the bar top (region_h - bar_height) down to the
// region baseline (region_h). High dB => tall bar reaching toward the top.
// Map display column c (0..cols) to its [lo, hi) FFT-bin range. Shared by
// build_vertices and sample_at_x so the drawn bar and the cursor/marker readout
// reduce the same bins.
static inline void column_bins(size_t c, size_t cols, size_t num_bins,
                               size_t &lo, size_t &hi) {
  lo = c * num_bins / cols;
  hi = (c + 1) * num_bins / cols;
  if (hi <= lo) {
    hi = lo + 1;
  }
  if (hi > num_bins) {
    hi = num_bins;
  }
}

// Mean dB across a column's bins — the display's detector. Averaging in the dB
// domain is a log-average (geometric mean of power), the same "video average" a
// swept analyzer applies; it reads a noise floor 2.51 dB below its true mean
// power, which is the conventional price for a trace that sits still.
static inline float column_mean(const float *bars, size_t lo, size_t hi) {
  float sum = 0.0F;
  for (size_t i = lo; i < hi; ++i) {
    sum += bars[i];
  }
  return sum / static_cast<float>(hi - lo);
}

void SpectrumDisplay::build_vertices(float region_w, float region_h,
                                     std::vector<SDL_Vertex> &verts,
                                     std::vector<int> &indices) const {
  verts.clear();
  indices.clear();

  // Averaging swaps the bar source; the raw spectrum still drives autoscale
  // and the max-hold trace (see update_spectrum).
  const std::vector<float> &bars =
      (m_avg_enabled && m_avg_data.size() == m_spectrum_data.size())
          ? m_avg_data
          : m_spectrum_data;

  const size_t num_bins = bars.size();
  const float db_range = m_max_db - m_min_db;
  if (num_bins == 0 || db_range <= 0.0F || region_w <= 0.0F || region_h <= 0.0F) {
    return;
  }

  // More bins than pixels wastes the GPU on sub-pixel overdraw (16K FFT → ~12
  // bars/px at the default width). Collapse to one bar per pixel column,
  // averaging the bins that land in it. Averaging is what makes the floor a
  // line rather than grass: mean-of-12 leaves ~1.25 dB of spread, where the
  // max-of-12 this used to do read ~4.9 dB high (E[max of n exponentials] =
  // H_n) and still spread ~1.8 dB. Spikes are not lost — max-hold (below) is
  // the peak detector, and it is the trace that promises peak semantics.
  // cols == num_bins when the FFT is already narrower than the pane, so
  // smaller sizes are unchanged.
  const size_t cols =
      std::min(num_bins, std::max<size_t>(1, static_cast<size_t>(region_w)));
  const float col_w = region_w / static_cast<float>(cols);
  const float db_to_height = region_h / db_range;

  const bool draw_hold =
      m_max_hold_enabled && m_max_hold_data.size() == num_bins;
  verts.reserve(cols * (draw_hold ? 8 : 4));
  indices.reserve(cols * (draw_hold ? 12 : 6));

  for (size_t cidx = 0; cidx < cols; ++cidx) {
    size_t lo = 0;
    size_t hi = 0;
    column_bins(cidx, cols, num_bins, lo, hi);
    float const db = column_mean(bars.data(), lo, hi);
    float const bar_height =
        std::clamp((db - m_min_db) * db_to_height, 0.0F, region_h);

    RgbColor const c = m_palette.get_color(db);
    // SDL3: SDL_Vertex uses SDL_FColor (float r,g,b,a) instead of SDL_Color
    // (Uint8)
    SDL_FColor const col = {static_cast<float>(c.red) / 255.0f,
                            static_cast<float>(c.green) / 255.0f,
                            static_cast<float>(c.blue) / 255.0f,
                            static_cast<float>(c.alpha) / 255.0f};

    float const x0 = static_cast<float>(cidx) * col_w;
    float const x1 = x0 + col_w;
    float const y_top = region_h - bar_height;
    float const y_bot = region_h;

    auto const base = static_cast<int>(verts.size());
    verts.push_back({SDL_FPoint{x0, y_top}, col, SDL_FPoint{0.0F, 0.0F}});
    verts.push_back({SDL_FPoint{x1, y_top}, col, SDL_FPoint{0.0F, 0.0F}});
    verts.push_back({SDL_FPoint{x1, y_bot}, col, SDL_FPoint{0.0F, 0.0F}});
    verts.push_back({SDL_FPoint{x0, y_bot}, col, SDL_FPoint{0.0F, 0.0F}});

    indices.push_back(base + 0);
    indices.push_back(base + 1);
    indices.push_back(base + 2);
    indices.push_back(base + 0);
    indices.push_back(base + 2);
    indices.push_back(base + 3);
  }

  // Max-hold overlay: one thin horizontal segment per column at the held peak,
  // drawn after (= on top of) the bars in the same SDL_RenderGeometry call.
  // White reads against every palette; the stepped segments mirror bar tops.
  if (draw_hold) {
    constexpr float kHoldThickness = 2.0F;
    constexpr SDL_FColor kHoldColor = {1.0F, 1.0F, 1.0F, 1.0F};
    for (size_t cidx = 0; cidx < cols; ++cidx) {
      size_t lo = 0;
      size_t hi = 0;
      column_bins(cidx, cols, num_bins, lo, hi);
      float hold = m_max_hold_data[lo];
      for (size_t i = lo + 1; i < hi; ++i) {
        hold = std::max(hold, m_max_hold_data[i]);
      }
      float const hold_height =
          std::clamp((hold - m_min_db) * db_to_height, 0.0F, region_h);
      float const x0 = static_cast<float>(cidx) * col_w;
      float const x1 = x0 + col_w;
      float const y0 = std::max(region_h - hold_height - kHoldThickness, 0.0F);
      float const y1 = y0 + kHoldThickness;

      auto const base = static_cast<int>(verts.size());
      verts.push_back({SDL_FPoint{x0, y0}, kHoldColor, SDL_FPoint{0.0F, 0.0F}});
      verts.push_back({SDL_FPoint{x1, y0}, kHoldColor, SDL_FPoint{0.0F, 0.0F}});
      verts.push_back({SDL_FPoint{x1, y1}, kHoldColor, SDL_FPoint{0.0F, 0.0F}});
      verts.push_back({SDL_FPoint{x0, y1}, kHoldColor, SDL_FPoint{0.0F, 0.0F}});

      indices.push_back(base + 0);
      indices.push_back(base + 1);
      indices.push_back(base + 2);
      indices.push_back(base + 0);
      indices.push_back(base + 2);
      indices.push_back(base + 3);
    }
  }
}

bool SpectrumDisplay::sample_at_x(float x, float region_w, float region_h,
                                  CursorSample &out) const {
  // Use the same source the bars draw from (averaged when averaging is on).
  const std::vector<float> &bars =
      (m_avg_enabled && m_avg_data.size() == m_spectrum_data.size())
          ? m_avg_data
          : m_spectrum_data;

  const size_t num_bins = bars.size();
  const float db_range = m_max_db - m_min_db;
  if (num_bins == 0 || db_range <= 0.0F || region_w <= 0.0F ||
      region_h <= 0.0F || x < 0.0F || x >= region_w) {
    return false;
  }

  // Mirror build_vertices' column decimation so the dot snaps onto the drawn
  // bar (which is the mean of its pixel column, not a single raw bin).
  const size_t cols =
      std::min(num_bins, std::max<size_t>(1, static_cast<size_t>(region_w)));
  const float col_w = region_w / static_cast<float>(cols);
  size_t col = static_cast<size_t>(x / col_w);
  if (col >= cols) {
    col = cols - 1;
  }
  size_t lo = 0;
  size_t hi = 0;
  column_bins(col, cols, num_bins, lo, hi);
  float const db = column_mean(bars.data(), lo, hi);

  const float db_to_height = region_h / db_range;
  const float bar_height =
      std::clamp((db - m_min_db) * db_to_height, 0.0F, region_h);

  out.db = db;
  out.dot_y = region_h - bar_height;
  out.bin = col;
  return true;
}

} // namespace openspectrum
