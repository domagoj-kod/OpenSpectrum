// SPDX-License-Identifier: GPL-3.0-or-later

#include "signal_processor.h"

#include <algorithm>
#include <cmath>
#include <complex>
#include <cstddef>
#include <numbers>
#include <vector>

#ifdef __AVX2__
#include <immintrin.h>
#endif

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

  m_window_coeffs_doubled.resize(size * 2);
  for (size_t i = 0; i < size; ++i) {
    m_window_coeffs_doubled[2 * i] = m_window_coeffs[i];
    m_window_coeffs_doubled[2 * i + 1] = m_window_coeffs[i];
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
    float const n = static_cast<float>(i) / size_minus_1;
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
    float const n = static_cast<float>(i) / size_minus_1;
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
    float const n = static_cast<float>(i) / size_minus_1;
    m_window_coeffs[i] =
        a0 - a1 * std::cos(2.0F * pi * n) + a2 * std::cos(4.0F * pi * n) -
        a3 * std::cos(6.0F * pi * n) + a4 * std::cos(8.0F * pi * n);
  }
}

void SignalProcessor::apply_window(std::vector<std::complex<float>> &samples) {
  // Security: ensure samples size matches window size
  if (samples.size() != m_fft_size) {
    precompute_window(samples.size());
  }

  const size_t n = samples.size();
  auto *data = reinterpret_cast<float *>(samples.data());
  const float *win = m_window_coeffs_doubled.data();

#ifdef __AVX2__
  // 4 complex samples = 8 floats per AVX register.
  // Window is pre-doubled as [w0,w0,w1,w1,...], so it's a plain element-wise mul.
  const size_t simd_n = n & ~size_t{3};
  for (size_t i = 0; i < simd_n; i += 4) {
    __m256 const s = _mm256_loadu_ps(data + 2 * i);
    __m256 const w = _mm256_loadu_ps(win + 2 * i);
    _mm256_storeu_ps(data + 2 * i, _mm256_mul_ps(s, w));
  }
  for (size_t i = simd_n; i < n; ++i) {
    const float w = m_window_coeffs[i];
    samples[i] = std::complex<float>(samples[i].real() * w, samples[i].imag() * w);
  }
#else
  for (size_t i = 0; i < n; ++i) {
    const float w = m_window_coeffs[i];
    samples[i] = std::complex<float>(samples[i].real() * w, samples[i].imag() * w);
  }
#endif
}

void SignalProcessor::remove_dc(std::vector<std::complex<float>> &samples) {
  const size_t n = samples.size();
  if (n == 0) return;

  auto *data = reinterpret_cast<float *>(samples.data());
  float mean_real = 0.0F;
  float mean_imag = 0.0F;

#ifdef __AVX2__
  // Four independent accumulators break the latency chain (FMA/ADD lat=4-5,
  // throughput=1 on Haswell — 4 chains saturate the port).
  __m256 acc0 = _mm256_setzero_ps();
  __m256 acc1 = _mm256_setzero_ps();
  __m256 acc2 = _mm256_setzero_ps();
  __m256 acc3 = _mm256_setzero_ps();

  // Process 16 complex floats (32 scalars) per iteration.
  const size_t simd_n = n & ~size_t{15};
  for (size_t i = 0; i < simd_n; i += 16) {
    acc0 = _mm256_add_ps(acc0, _mm256_loadu_ps(data + 2 * i));
    acc1 = _mm256_add_ps(acc1, _mm256_loadu_ps(data + 2 * i + 8));
    acc2 = _mm256_add_ps(acc2, _mm256_loadu_ps(data + 2 * i + 16));
    acc3 = _mm256_add_ps(acc3, _mm256_loadu_ps(data + 2 * i + 24));
  }
  __m256 const acc = _mm256_add_ps(_mm256_add_ps(acc0, acc1),
                                   _mm256_add_ps(acc2, acc3));

  // Lanes are interleaved [r,i,r,i,r,i,r,i]. Reduce to one (r,i) pair.
  alignas(32) float lanes[8];
  _mm256_store_ps(lanes, acc);
  mean_real = lanes[0] + lanes[2] + lanes[4] + lanes[6];
  mean_imag = lanes[1] + lanes[3] + lanes[5] + lanes[7];

  for (size_t i = simd_n; i < n; ++i) {
    mean_real += samples[i].real();
    mean_imag += samples[i].imag();
  }
#else
  for (const auto &s : samples) {
    mean_real += s.real();
    mean_imag += s.imag();
  }
#endif

  mean_real /= static_cast<float>(n);
  mean_imag /= static_cast<float>(n);

#ifdef __AVX2__
  __m256 const mean_vec = _mm256_setr_ps(mean_real, mean_imag, mean_real, mean_imag,
                                         mean_real, mean_imag, mean_real, mean_imag);
  const size_t simd_n2 = n & ~size_t{15};
  for (size_t i = 0; i < simd_n2; i += 16) {
    _mm256_storeu_ps(data + 2 * i,
                     _mm256_sub_ps(_mm256_loadu_ps(data + 2 * i), mean_vec));
    _mm256_storeu_ps(data + 2 * i + 8,
                     _mm256_sub_ps(_mm256_loadu_ps(data + 2 * i + 8), mean_vec));
    _mm256_storeu_ps(data + 2 * i + 16,
                     _mm256_sub_ps(_mm256_loadu_ps(data + 2 * i + 16), mean_vec));
    _mm256_storeu_ps(data + 2 * i + 24,
                     _mm256_sub_ps(_mm256_loadu_ps(data + 2 * i + 24), mean_vec));
  }
  const std::complex<float> mean_c(mean_real, mean_imag);
  for (size_t i = simd_n2; i < n; ++i) {
    samples[i] -= mean_c;
  }
#else
  for (auto &s : samples) {
    s.real(s.real() - mean_real);
    s.imag(s.imag() - mean_imag);
  }
#endif
}

} // namespace openspectrum
