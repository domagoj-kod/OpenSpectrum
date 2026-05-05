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