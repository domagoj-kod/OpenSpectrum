// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <complex>
#include <cstddef>
#include <span>
#include <vector>

#include "fft/pocketfft_wrapper.h"
#include "utils/attributes.h"

namespace openspectrum {

// PocketFFT with pre-allocated buffers.
class FftAnalyzer {
public:
  OS_COLD explicit FftAnalyzer(size_t fft_size);

  // Runs the FFT on fft_size samples and refreshes get_db_spectrum(). A span,
  // so a pooled FrameHandle buffer needs no copy.
  OS_HOT void execute(std::span<const std::complex<float>> input);

  [[nodiscard]] const std::vector<float> &get_db_spectrum() const {
    return m_db_spectrum;
  }

  [[nodiscard]] float get_max_db() const;

  [[nodiscard]] size_t fft_size() const noexcept { return m_fft_size; }

  void set_window_coherent_gain(float gain) { m_window_coherent_gain = gain; }

private:
  size_t m_fft_size;

  std::vector<pocketfft_cpx> m_input_buffer;
  std::vector<pocketfft_cpx> m_output_buffer;

  std::vector<float> m_db_spectrum;

  float m_window_coherent_gain{1.0F};
};

} // namespace openspectrum