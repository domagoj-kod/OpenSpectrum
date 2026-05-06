// SPDX-License-Identifier: MIT

#include "signal_processor.h"
#include <algorithm>
#include <cmath>
#include <numbers>
#include <stdexcept>

namespace openspectrum {

SignalProcessor::SignalProcessor(size_t fft_size) : m_fft_size(fft_size) {
  precompute_window(fft_size);
}

void SignalProcessor::precompute_window(size_t size) {
  m_fft_size = size;
  m_window_coeffs.resize(size);

  switch (m_window) {
  case WindowFunction::RECTANGLE:
    std::ranges::fill(m_window_coeffs, 1.0F);
    break;
  case WindowFunction::HANN:
    compute_hann();
    break;
  case WindowFunction::HAMMING:
    compute_hamming();
    break;
  case WindowFunction::BLACKMAN:
    compute_blackman();
    break;
  case WindowFunction::BLACKMAN_HARRIS:
    compute_blackman_harris();
    break;
  case WindowFunction::FLAT_TOP:
    compute_flat_top();
    break;
  }
}

void SignalProcessor::compute_hann() {
  const float pi = std::numbers::pi_v<float>;
  const auto size_minus_1 = static_cast<float>(m_fft_size - 1);
  for (size_t i = 0; i < m_fft_size; ++i) {
    m_window_coeffs[i] =
        0.5F *
        (1.0F - std::cos(2.0F * pi * static_cast<float>(i) / size_minus_1));
  }
}

void SignalProcessor::compute_hamming() {
  const float pi = std::numbers::pi_v<float>;
  const auto size_minus_1 = static_cast<float>(m_fft_size - 1);
  for (size_t i = 0; i < m_fft_size; ++i) {
    m_window_coeffs[i] =
        0.54F -
        0.46F * std::cos(2.0F * pi * static_cast<float>(i) / size_minus_1);
  }
}

void SignalProcessor::compute_blackman() {
  const float pi = std::numbers::pi_v<float>;
  const float a0 = 0.42659F;
  const float a1 = 0.49656F;
  const float a2 = 0.076849F;
  const auto size_minus_1 = static_cast<float>(m_fft_size - 1);

  for (size_t i = 0; i < m_fft_size; ++i) {
    float n = static_cast<float>(i) / size_minus_1;
    m_window_coeffs[i] =
        a0 - a1 * std::cos(2.0F * pi * n) + a2 * std::cos(4.0F * pi * n);
  }
}

void SignalProcessor::compute_blackman_harris() {
  const float pi = std::numbers::pi_v<float>;
  const float a0 = 0.35875F;
  const float a1 = 0.48829F;
  const float a2 = 0.14128F;
  const float a3 = 0.01168F;
  const auto size_minus_1 = static_cast<float>(m_fft_size - 1);

  for (size_t i = 0; i < m_fft_size; ++i) {
    float n = static_cast<float>(i) / size_minus_1;
    m_window_coeffs[i] = a0 - a1 * std::cos(2.0F * pi * n) +
                         a2 * std::cos(4.0F * pi * n) -
                         a3 * std::cos(6.0F * pi * n);
  }
}

void SignalProcessor::compute_flat_top() {
  const float pi = std::numbers::pi_v<float>;
  const float a0 = 1.0F;
  const float a1 = 1.93F;
  const float a2 = 1.29F;
  const float a3 = 0.388F;
  const float a4 = 0.032F;
  const auto size_minus_1 = static_cast<float>(m_fft_size - 1);

  for (size_t i = 0; i < m_fft_size; ++i) {
    float n = static_cast<float>(i) / size_minus_1;
    m_window_coeffs[i] =
        a0 - a1 * std::cos(2.0F * pi * n) + a2 * std::cos(4.0F * pi * n) -
        a3 * std::cos(6.0F * pi * n) + a4 * std::cos(8.0F * pi * n);
  }
}

auto SignalProcessor::get_window_coeff(size_t index) const -> float {
  // Security: bounds-checked access
  if (index >= m_window_coeffs.size()) {
    return 1.0F; // Fallback to rectangle window
  }
  return m_window_coeffs[index];
}

void SignalProcessor::apply_window(std::vector<std::complex<float>> &samples) {
  // Security: ensure samples size matches window size
  if (samples.size() != m_fft_size) {
    precompute_window(samples.size());
  }

  for (size_t i = 0; i < samples.size(); ++i) {
    float w = get_window_coeff(i);
    samples[i].real(samples[i].real() * w);
    samples[i].imag(samples[i].imag() * w);
  }
}

void SignalProcessor::remove_dc(std::vector<std::complex<float>> &samples) {
  float mean_real = 0.0F;
  float mean_imag = 0.0F;

  for (const auto &s : samples) {
    mean_real += s.real();
    mean_imag += s.imag();
  }

  mean_real /= static_cast<float>(samples.size());
  mean_imag /= static_cast<float>(samples.size());

  for (auto &s : samples) {
    s.real(s.real() - mean_real);
    s.imag(s.imag() - mean_imag);
  }
}

} // namespace openspectrum
