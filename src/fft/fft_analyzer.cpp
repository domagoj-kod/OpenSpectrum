// SPDX-License-Identifier: MIT

#include "fft_analyzer.h"

#include <algorithm>
#include <cmath>
#include <complex>
#include <cstddef>
#include <stdexcept>
#include <utility>
#include <vector>

#include "kiss_fft.h"

namespace openspectrum {

FftAnalyzer::FftAnalyzer(size_t fft_size, bool inverse)
    : m_fft_size(fft_size), m_inverse(inverse), m_input_buffer(fft_size),
      m_output_buffer(fft_size), m_power_spectrum((fft_size / 2) + 1),
      m_magnitude_spectrum((fft_size / 2) + 1),
      m_db_spectrum((fft_size / 2) + 1), m_phase_spectrum((fft_size / 2) + 1),
      m_freq_bins((fft_size / 2) + 1) {
  // Security: validate FFT size is a power of 2
  if (fft_size == 0 || (fft_size & (fft_size - 1)) != 0) {
    throw std::invalid_argument("FFT size must be a power of 2");
  }

  // Allocate kissFFT configuration
  // Security: kiss_fft_alloc returns nullptr on failure
  m_cfg = kiss_fft_alloc(static_cast<int>(fft_size), m_inverse ? 1 : 0, nullptr,
                         nullptr);
  if (m_cfg == nullptr) {
    throw std::runtime_error("Failed to allocate FFT configuration");
  }

  compute_frequency_bins();
}

FftAnalyzer::~FftAnalyzer() {
  // Security: always free allocated configuration
  if (m_cfg != nullptr) {
    kiss_fft_free(m_cfg);
    m_cfg = nullptr;
  }
}

FftAnalyzer::FftAnalyzer(FftAnalyzer &&other) noexcept
    : m_fft_size(other.m_fft_size), m_center_dc(other.m_center_dc),
      m_inverse(other.m_inverse), m_cfg(other.m_cfg),
      m_window_coherent_gain(other.m_window_coherent_gain),
      m_input_buffer(std::move(other.m_input_buffer)),
      m_output_buffer(std::move(other.m_output_buffer)),
      m_power_spectrum(std::move(other.m_power_spectrum)),
      m_magnitude_spectrum(std::move(other.m_magnitude_spectrum)),
      m_db_spectrum(std::move(other.m_db_spectrum)),
      m_phase_spectrum(std::move(other.m_phase_spectrum)),
      m_freq_bins(std::move(other.m_freq_bins)) {
  other.m_cfg = nullptr;
  other.m_fft_size = 0;
}

auto FftAnalyzer::operator=(FftAnalyzer &&other) noexcept -> FftAnalyzer & {
  if (this != &other) {
    // Clean up existing resources
    if (m_cfg != nullptr) {
      kiss_fft_free(m_cfg);
    }

    m_fft_size = other.m_fft_size;
    m_center_dc = other.m_center_dc;
    m_inverse = other.m_inverse;
    m_cfg = other.m_cfg;
    m_input_buffer = std::move(other.m_input_buffer);
    m_output_buffer = std::move(other.m_output_buffer);
    m_power_spectrum = std::move(other.m_power_spectrum);
    m_magnitude_spectrum = std::move(other.m_magnitude_spectrum);
    m_db_spectrum = std::move(other.m_db_spectrum);
    m_phase_spectrum = std::move(other.m_phase_spectrum);
    m_freq_bins = std::move(other.m_freq_bins);
    m_window_coherent_gain = other.m_window_coherent_gain;

    other.m_cfg = nullptr;
    other.m_fft_size = 0;
  }
  return *this;
}

void FftAnalyzer::compute_frequency_bins() {
  const float scale = 1.0F / static_cast<float>(m_fft_size);
  for (size_t i = 0; i < m_freq_bins.size(); ++i) {
    // For real signals, we only care about first half + DC
    m_freq_bins[i] = static_cast<float>(i) * scale;
  }
}

void FftAnalyzer::execute(const std::vector<std::complex<float>> &input,
                          std::vector<std::complex<float>> &output) {
  // Security: validate input size
  if (input.size() != m_fft_size) {
    throw std::invalid_argument("Input size must match FFT size");
  }

  // Fill input buffer (apply DC centering if enabled)
  for (size_t i = 0; i < m_fft_size; ++i) {
    float const sign = m_center_dc ? ((i % 2 == 0) ? 1.0F : -1.0F) : 1.0F;
    m_input_buffer[i].r = sign * input[i].real();
    m_input_buffer[i].i = sign * input[i].imag();
  }

  // Execute FFT
  // Security: kiss_fft does not throw, returns void
  kiss_fft(m_cfg, m_input_buffer.data(), m_output_buffer.data());

  // Process output and cache results
  size_t const half_size = (m_fft_size / 2) + 1;

  for (size_t i = 0; i < half_size; ++i) {
    // Get output bin (may need reordering for centered DC)
    size_t out_idx = i;
    if (m_center_dc) {
      out_idx = (i + m_fft_size / 2) % m_fft_size;
    }

    float const real = m_output_buffer[out_idx].r;
    float const imag = m_output_buffer[out_idx].i;

    m_power_spectrum[i] = real * real + imag * imag;
    m_magnitude_spectrum[i] = std::sqrt(m_power_spectrum[i]);
    m_phase_spectrum[i] = std::atan2(imag, real);

    // dB conversion with floor to avoid log(0)
    // Security: prevent NaN from log(0) with epsilon
    float const scale = (i != 0 && i != m_fft_size / 2) ? 2.0F : 1.0F;
    m_db_spectrum[i] =
        20.0F *
        std::log10((m_magnitude_spectrum[i] * scale /
                    (static_cast<float>(m_fft_size) * m_window_coherent_gain)) +
                   1e-12F);
  }

  // Copy to output if provided
  if (output.size() >= m_fft_size) {
    for (size_t i = 0; i < half_size; ++i) {
      size_t out_idx = i;
      if (m_center_dc) {
        out_idx = (i + m_fft_size / 2) % m_fft_size;
      }
      output[i] = std::complex<float>(m_output_buffer[out_idx].r,
                                      m_output_buffer[out_idx].i);
    }
  }
}

void FftAnalyzer::execute(const std::vector<std::complex<float>> &input) {
  std::vector<std::complex<float>> dummy;
  execute(input, dummy);
}

auto FftAnalyzer::get_max_db() const -> float {
  if (m_db_spectrum.empty()) {
    // Return sentinel value below typical noise floor when no data
    return -140.0F;
  }
  return *std::ranges::max_element(m_db_spectrum);
}

} // namespace openspectrum