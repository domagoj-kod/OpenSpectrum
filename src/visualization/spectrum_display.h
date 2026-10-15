// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <array>
#include <cstdint>
#include <limits>
#include <vector>

#include "utils/attributes.h"

#include <SDL3/SDL.h>

namespace openspectrum {

struct RgbColor {
  uint8_t red;
  uint8_t green;
  uint8_t blue;
  uint8_t alpha;

  constexpr RgbColor() : red(0), green(0), blue(0), alpha(255) {}
  constexpr RgbColor(uint8_t r, uint8_t g, uint8_t b, uint8_t a = 255)
      : red(r), green(g), blue(b), alpha(a) {}
};

class SpectrumPalette {
public:
  static constexpr size_t PALETTE_SIZE = 256;

  SpectrumPalette();

  void set_db_range(float min_db, float max_db);

  [[nodiscard]] RgbColor get_color(float db_value) const;

  enum class ColorMap { JET, VIRIDIS, HOT, GRAYSCALE, BLUE_RED };

  void set_color_map(ColorMap map);

private:
  ColorMap m_color_map = ColorMap::JET;
  std::array<RgbColor, PALETTE_SIZE> m_palette;

  // index = (int)((db - min) * scale + 0.5f)
  float m_scale_to_index = 1.0f; // (PALETTE_SIZE-1) / (max_db - min_db)
  float m_db_min = -120.0f;      // Cached min for clamp
  float m_db_max = 0.0f;         // Cached max for clamp

  void generate_jet_palette();
  void generate_viridis_palette();
  void generate_hot_palette();
  void generate_grayscale_palette();
  void generate_blue_red_palette();
};

// Amplitude vs frequency: the trace geometry, autoscale and trace modes.
class SpectrumDisplay {
public:
  SpectrumDisplay(size_t width, size_t height);
  ~SpectrumDisplay() = default;

  OS_HOT void update_spectrum(const std::vector<float> &db_values);

  // GPU geometry for the spectrum region [0,region_w) x [0,region_h), drawn by
  // SdlRenderer::render_spectrum. The out buffers are cleared and refilled; the
  // caller reuses them so there is no per-frame allocation.
  OS_HOT void build_vertices(float region_w, float region_h,
                             std::vector<SDL_Vertex> &verts,
                             std::vector<int> &indices) const;

  // Cursor readout: the displayed amplitude and the snapped trace point.
  // Mirrors build_vertices() exactly so the dot lands on the drawn trace.
  struct CursorSample {
    float db = 0.0F;
    float dot_y = 0.0F;
    size_t bin = 0;
  };
  [[nodiscard]] bool sample_at_x(float x, float region_w, float region_h,
                                 CursorSample &out) const;

  [[nodiscard]] size_t width() const noexcept { return m_width; }
  [[nodiscard]] size_t height() const noexcept { return m_height; }

  void set_color_map(SpectrumPalette::ColorMap map) {
    m_palette.set_color_map(map);
  }
  void set_db_range(float min_db, float max_db);

  [[nodiscard]] float min_db() const noexcept { return m_min_db; }
  [[nodiscard]] float max_db() const noexcept { return m_max_db; }

  // Averaging draws an EMA of the per-bin dB values; max-hold overlays the
  // running maximum of the *raw* spectrum. Both re-seed when the bin count
  // changes.
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

  std::vector<float> m_spectrum_data;
  SpectrumPalette m_palette;

  // Copy for the autoscale median: nth_element reorders it.
  std::vector<float> m_scratch;

  // Empty means not yet seeded.
  std::vector<float> m_avg_data;
  std::vector<float> m_max_hold_data;
  // ControlState owns the user-facing default.
  bool m_avg_enabled = true;
  bool m_max_hold_enabled = false;
  // EMA weight for new data: ~6-frame settling.
  static constexpr float kAvgAlpha = 0.25F;
  // Headroom below the median bin: the floor sits ~12 dB above the pane bottom.
  static constexpr float kFloorMargin = 10.0F;
  // Every Nth bin feeds the median; noise bins are iid.
  static constexpr size_t kMedianStride = 4;
  // How far the autoscaled top may fall per frame; it rises at once.
  static constexpr float kTopDecayDb = 0.5F;
  // -inf until the first frame.
  float m_top_db = -std::numeric_limits<float>::infinity();

  float m_min_db = -120.0f;
  float m_max_db = 0.0f;
};

} // namespace openspectrum
