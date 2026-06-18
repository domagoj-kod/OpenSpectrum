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
  OS_COLD explicit FftAnalyzer(size_t fft_size, bool inverse = false);
  ~FftAnalyzer();

  // Non-copyable: buffers are large and the move ctor is the intended path.
  FftAnalyzer(const FftAnalyzer &) = delete;
  FftAnalyzer &operator=(const FftAnalyzer &) = delete;

  // Enable moving
  FftAnalyzer(FftAnalyzer &&other) noexcept;
  FftAnalyzer &operator=(FftAnalyzer &&other) noexcept;

  // Execute FFT on input samples, store result in output
  // Input: time-domain complex samples (size = fft_size). A span so a pooled
  // FrameHandle buffer can be passed without copying (a std::vector binds too).
  // Output: frequency-domain complex bins (size = fft_size)
  OS_HOT void execute(std::span<const std::complex<float>> input,
                      std::vector<std::complex<float>> &output);

  // Get power spectrum (magnitude squared) from last FFT result
  [[nodiscard]] const std::vector<float> &get_power_spectrum() const {
    return m_power_spectrum;
  }

  // Get magnitude spectrum (linear) from last FFT result
  [[nodiscard]] const std::vector<float> &get_magnitude_spectrum() const {
    return m_magnitude_spectrum;
  }

  // Get magnitude spectrum in dB from last FFT result
  [[nodiscard]] const std::vector<float> &get_db_spectrum() const {
    return m_db_spectrum;
  }

  // Get phase spectrum in radians from last FFT result
  [[nodiscard]] const std::vector<float> &get_phase_spectrum() const {
    return m_phase_spectrum;
  }

  // Get normalized frequency bins (0 to 1, where 1 = sample rate)
  [[nodiscard]] const std::vector<float> &get_frequency_bins() const {
    return m_freq_bins;
  }

  // Amplitude analysis: get maximum dB value from last FFT result
  [[nodiscard]] float get_max_db() const;

  [[nodiscard]] size_t fft_size() const noexcept { return m_fft_size; }

  // Shift FFT output so DC is centered (for real signals)
  // Apply (-1)^n shift to input before FFT, or shift output after FFT
  void enable_dc_center(bool enabled) noexcept { m_center_dc = enabled; }

  // Window gain setter
  void set_window_coherent_gain(float gain) { m_window_coherent_gain = gain; }

  // Toggle computation of magnitude/power/phase spectra. Defaults off — the
  // dB spectrum is the only consumed output in the default pipeline. Enable
  // before calling execute() if you need any of the secondary spectra.
  // Saves one sqrt + two stores per bin and an entire scalar atan2 pass, and
  // (since the buffers are sized lazily here) their fft_size*4 bytes each.
  void set_extra_spectra_enabled(bool enabled) {
    if (enabled && m_power_spectrum.empty()) {
      m_power_spectrum.resize(m_fft_size);
      m_magnitude_spectrum.resize(m_fft_size);
      m_phase_spectrum.resize(m_fft_size);
    }
    m_extra_spectra_enabled = enabled;
  }
  [[nodiscard]] bool extra_spectra_enabled() const noexcept {
    return m_extra_spectra_enabled;
  }

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

  bool m_center_dc{false};
  bool m_extra_spectra_enabled = false;
  float m_window_coherent_gain{1.0F}; // Default to rectangular window

  // Pre-compute frequency bins
  void compute_frequency_bins();
};

} // namespace openspectrum