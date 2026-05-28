// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <array>
#include <complex>
#include <cstddef>
#include <memory>
#include <vector>

#include "pocketfft_wrapper.h"

namespace openspectrum {

// Secure wrapper around PocketFFT with RAII and pre-allocated buffers
class FftAnalyzer {
public:
  explicit FftAnalyzer(size_t fft_size, bool inverse = false);
  ~FftAnalyzer();

  // Non-copyable: buffers are large and the move ctor is the intended path.
  FftAnalyzer(const FftAnalyzer &) = delete;
  FftAnalyzer &operator=(const FftAnalyzer &) = delete;

  // Enable moving
  FftAnalyzer(FftAnalyzer &&other) noexcept;
  FftAnalyzer &operator=(FftAnalyzer &&other) noexcept;

  // Execute FFT on input samples, store result in output
  // Input: time-domain complex samples (size = fft_size)
  // Output: frequency-domain complex bins (size = fft_size)
  void execute(const std::vector<std::complex<float>> &input,
               std::vector<std::complex<float>> &output);

  // Execute FFT with pre-allocated internal buffers (zero-copy for output)
  void execute(const std::vector<std::complex<float>> &input);

  // Get power spectrum (magnitude squared) from last FFT result
  [[nodiscard]] const std::vector<float> &get_power_spectrum() const {
    return m_power_spectrum;
  }

  // Get magnitude spectrum (linear) from last FFT result
  [[nodiscard]] const std::vector<float> &get_magnitude_spectrum() const {
    return m_magnitude_spectrum;
  }

  // Get magnitude spectrum in dB from last FFT result
  [[nodiscard]] const std::vector<float> &get_db_spectrum() const { return m_db_spectrum; }

  // Get phase spectrum in radians from last FFT result
  [[nodiscard]] const std::vector<float> &get_phase_spectrum() const {
    return m_phase_spectrum;
  }

  // Get normalized frequency bins (0 to 1, where 1 = sample rate)
  [[nodiscard]] const std::vector<float> &get_frequency_bins() const { return m_freq_bins; }

  // Amplitude analysis: get maximum dB value from last FFT result
  [[nodiscard]] float get_max_db() const;

  [[nodiscard]] size_t fft_size() const noexcept { return m_fft_size; }

  // Shift FFT output so DC is centered (for real signals)
  // Apply (-1)^n shift to input before FFT, or shift output after FFT
  void enable_dc_center(bool enabled) noexcept { m_center_dc = enabled; }

  // Window gain setter
  void set_window_coherent_gain(float gain) { m_window_coherent_gain = gain; }

private:
  size_t m_fft_size;
  bool m_inverse;

  // Internal buffers for efficiency (avoid repeated allocations)
  std::vector<pocketfft_cpx> m_input_buffer;
  std::vector<pocketfft_cpx> m_output_buffer;

  // Cached results (updated after each execute)
  std::vector<float> m_power_spectrum;
  std::vector<float> m_magnitude_spectrum;
  std::vector<float> m_db_spectrum;
  std::vector<float> m_phase_spectrum;
  std::vector<float> m_freq_bins;

  // PocketFFT workspace
  std::vector<pocketfft_cpx> m_workspace;

  bool m_center_dc;
  float m_window_coherent_gain; // Default to rectangular window

  // Pre-compute frequency bins
  void compute_frequency_bins();
};

} // namespace openspectrum