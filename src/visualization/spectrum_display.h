// SPDX-License-Identifier: MIT
#pragma once

#include <algorithm>
#include <array>
#include <cstdint>
#include <string>
#include <vector>

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
  RgbColor get_color(float db_value, float min_db, float max_db) const;

  // Color map presets
  enum class ColorMap { JET, VIRIDIS, HOT, GRAyscale, BLUE_RED };

  void set_color_map(ColorMap map);

private:
  ColorMap m_color_map = ColorMap::JET;
  std::array<RgbColor, PALETTE_SIZE> m_palette;

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
  void update_spectrum(const std::vector<float> &db_values,
                       const std::vector<float> /*freq_bins*/,
                       float center_freq_hz, float sample_rate_hz);

  // Get rendered pixel buffer (RGB32 format: RGBA interleaved)
  const std::vector<uint8_t> &get_pixels() const { return m_pixels; }

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

private:
  size_t m_width;
  size_t m_height;
  std::vector<uint8_t> m_pixels; // RGBA format: 4 bytes per pixel

  std::vector<float> m_spectrum_data;
  SpectrumPalette m_palette;

  float m_center_freq_hz = 0.0f;
  float m_sample_rate_hz = 0.0f;
  float m_min_db = -120.0f;
  float m_max_db = 0.0f;
  bool m_autoscale = true;

  void render();
  void clear();
};

} // namespace openspectrum