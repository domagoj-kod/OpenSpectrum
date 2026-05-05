// SPDX-License-Identifier: MIT
#pragma once

#include <algorithm>
#include <array>
#include <complex>
#include <cstdint>
#include <vector>

namespace openspectrum {

// Supported window functions for spectral leakage reduction
enum class WindowFunction {
  RECTANGLE,
  HANN,
  HAMMING,
  BLACKMAN,
  BLACKMAN_HARRIS,
  FLAT_TOP
};

class SignalProcessor {
public:
  explicit SignalProcessor(size_t fft_size);
  ~SignalProcessor() = default;

  // Apply selected window function to samples (in-place)
  void apply_window(std::vector<std::complex<float>> &samples);

  // Remove DC offset (mean subtraction)
  static void remove_dc(std::vector<std::complex<float>> &samples);

  // Set window function
  void set_window(WindowFunction window) noexcept { m_window = window; }

  // Pre-compute window coefficients for given size
  void precompute_window(size_t size);

  // Get window coefficient at index
  float get_window_coeff(size_t index) const;

  size_t fft_size() const noexcept { return m_fft_size; }

  // Coherent gain lookup
  static float get_coherent_gain(WindowFunction window) noexcept {
    switch (window) {
    case WindowFunction::RECTANGLE:
      return 1.0f;
    case WindowFunction::HANN:
      return 0.5f;
    case WindowFunction::HAMMING:
      return 0.54f;
    case WindowFunction::BLACKMAN:
      return 0.42659f;
    case WindowFunction::BLACKMAN_HARRIS:
      return 0.35875f;
    case WindowFunction::FLAT_TOP:
      return 1.0f;
    default:
      return 1.0f;
    }
  }

private:
  size_t m_fft_size;
  WindowFunction m_window = WindowFunction::HANN;
  std::vector<float> m_window_coeffs;

  void compute_hann();
  void compute_hamming();
  void compute_blackman();
  void compute_blackman_harris();
  void compute_flat_top();
};

} // namespace openspectrum