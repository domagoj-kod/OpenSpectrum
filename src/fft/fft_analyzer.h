// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <complex>
#include <cstddef>
#include <span>
#include <vector>

#include "openspectrum/attributes.h"
#include "pocketfft_wrapper.h"

namespace openspectrum {

// Secure wrapper around PocketFFT with RAII and pre-allocated buffers
class FftAnalyzer {
public:
  OS_COLD explicit FftAnalyzer(size_t fft_size);
  ~FftAnalyzer();

  // Non-copyable: buffers are large and the move ctor is the intended path.
  FftAnalyzer(const FftAnalyzer &) = delete;
  FftAnalyzer &operator=(const FftAnalyzer &) = delete;

  // Enable moving
  FftAnalyzer(FftAnalyzer &&other) noexcept;
  FftAnalyzer &operator=(FftAnalyzer &&other) noexcept;

  // Execute the FFT and refresh get_db_spectrum().
  // Input: time-domain complex samples (size = fft_size). A span so a pooled
  // FrameHandle buffer can be passed without copying (a std::vector binds too).
  OS_HOT void execute(std::span<const std::complex<float>> input);

  // Get magnitude spectrum in dB from last FFT result
  [[nodiscard]] const std::vector<float> &get_db_spectrum() const {
    return m_db_spectrum;
  }

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

  // Internal buffers for efficiency (avoid repeated allocations)
  std::vector<pocketfft_cpx> m_input_buffer;
  std::vector<pocketfft_cpx> m_output_buffer;

  // Cached results (updated after each execute)
  std::vector<float> m_db_spectrum;

  bool m_center_dc{false};
  float m_window_coherent_gain{1.0F}; // Default to rectangular window
};

} // namespace openspectrum