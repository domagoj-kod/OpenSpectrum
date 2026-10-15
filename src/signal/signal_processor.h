// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <complex>
#include <span>
#include <vector>

#include "utils/attributes.h"

namespace openspectrum {

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

  // In place. A span, so a pooled FrameHandle buffer needs no copy.
  OS_HOT void apply_window(std::span<std::complex<float>> samples);

  // Mean subtraction, in place.
  OS_HOT static void remove_dc(std::span<std::complex<float>> samples);

  // Recomputes the coefficients for the next apply_window().
  void set_window(WindowFunction window) {
    if (window == m_window)
      return;
    m_window = window;
    precompute_window(m_fft_size);
  }

  // Runs only on size or window changes.
  OS_COLD void precompute_window(size_t size);

  [[nodiscard]] size_t fft_size() const noexcept { return m_fft_size; }

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
  // [w0,w0,w1,w1,...]: the AVX2 path multiplies interleaved IQ element-wise.
  std::vector<float> m_window_coeffs_doubled;

  OS_COLD void compute_hann();
  OS_COLD void compute_hamming();
  OS_COLD void compute_blackman();
  OS_COLD void compute_blackman_harris();
  OS_COLD void compute_flat_top();
};

} // namespace openspectrum