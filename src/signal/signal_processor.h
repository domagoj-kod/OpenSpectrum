// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <algorithm>
#include <array>
#include <complex>
#include <cstdint>
#include <vector>

#include "openspectrum/attributes.h"

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
  OS_COLD explicit SignalProcessor(size_t fft_size);
  ~SignalProcessor() = default;

  // Apply selected window function to samples (in-place)
  OS_HOT void apply_window(std::vector<std::complex<float>> &samples);

  // Remove DC offset (mean subtraction)
  OS_HOT static void remove_dc(std::vector<std::complex<float>> &samples);

  // Set window function. Recomputes coefficients so the next apply_window()
  // uses the new shape; keyboard-driven window changes would otherwise silently
  // keep applying the previous window.
  void set_window(WindowFunction window) {
    if (window == m_window) return;
    m_window = window;
    precompute_window(m_fft_size);
  }

  // Pre-compute window coefficients for given size (runs only on size / window changes)
  OS_COLD void precompute_window(size_t size);

  [[nodiscard]] size_t fft_size() const noexcept { return m_fft_size; }

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

  // Convert window function enum to human-readable string
  static const char *window_function_to_string(WindowFunction window) noexcept {
    switch (window) {
    case WindowFunction::RECTANGLE:
      return "Rectangle";
    case WindowFunction::HANN:
      return "Hann";
    case WindowFunction::HAMMING:
      return "Hamming";
    case WindowFunction::BLACKMAN:
      return "Blackman";
    case WindowFunction::BLACKMAN_HARRIS:
      return "B-Harris";
    case WindowFunction::FLAT_TOP:
      return "Flat-Top";
    default:
      return "Unknown";
    }
  }

private:
  size_t m_fft_size;
  WindowFunction m_window = WindowFunction::HANN;
  std::vector<float> m_window_coeffs;
  // Window coefficients duplicated for complex multiply: [w0,w0,w1,w1,...]
  // Lets the AVX2 hot path load window weights aligned to interleaved IQ data.
  std::vector<float> m_window_coeffs_doubled;

  OS_COLD void compute_hann();
  OS_COLD void compute_hamming();
  OS_COLD void compute_blackman();
  OS_COLD void compute_blackman_harris();
  OS_COLD void compute_flat_top();
};

} // namespace openspectrum