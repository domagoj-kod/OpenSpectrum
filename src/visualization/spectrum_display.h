// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <array>
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
  OS_HOT void update_spectrum(const std::vector<float> &db_values);

  // Build GPU draw geometry for the current spectrum: one colored quad per bin
  // (4 vertices + 6 indices), positioned in the spectrum region [0,region_w) x
  // [0,region_h). Drawn by SdlRenderer::render_spectrum via SDL_RenderGeometry,
  // replacing the old per-frame CPU pixel paint + full texture upload. The out
  // buffers are cleared and refilled (caller reuses them frame-to-frame so the
  // capacity is retained — no per-frame allocation).
  OS_HOT void build_vertices(float region_w, float region_h,
                             std::vector<SDL_Vertex> &verts,
                             std::vector<int> &indices) const;

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

  // Get dimensions
  [[nodiscard]] size_t width() const noexcept { return m_width; }
  [[nodiscard]] size_t height() const noexcept { return m_height; }

  // Configuration
  void set_color_map(SpectrumPalette::ColorMap map) {
    m_palette.set_color_map(map);
  }
  void set_db_range(float min_db, float max_db);

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

  std::vector<float> m_spectrum_data;
  SpectrumPalette m_palette;

  // Mutable copy for the autoscale median (nth_element reorders in place).
  std::vector<float> m_scratch;

  // Trace state. m_avg_data is the EMA accumulator; m_max_hold_data the
  // running raw-spectrum maximum. Empty vector == not yet seeded.
  std::vector<float> m_avg_data;
  std::vector<float> m_max_hold_data;
  // Averaging defaults on: with the per-column mean detector it takes the
  // noise-floor spread to ~0.5 dB, which is what turns the floor into a line.
  // ControlState owns the user-facing default; this keeps the class consistent
  // on its own.
  bool m_avg_enabled = true;
  bool m_max_hold_enabled = false;
  // EMA weight for new data: ~6-frame settling, smooths the noise floor
  // without visibly lagging real signal changes at 60 fps.
  static constexpr float kAvgAlpha = 0.25F;
  // Headroom below the median bin for the autoscaled pane bottom. The median
  // sits ~1.6 dB under the noise floor, so this leaves the floor ~12 dB up
  // from the bottom edge — enough to read, without wasting the pane.
  static constexpr float kFloorMargin = 10.0F;
  // Take every Nth bin for the median estimate. Noise bins are iid, so this
  // costs accuracy the axis cannot show and saves most of the selection work.
  static constexpr size_t kMedianStride = 4;

  float m_min_db = -120.0f;
  float m_max_db = 0.0f;
};

} // namespace openspectrum
