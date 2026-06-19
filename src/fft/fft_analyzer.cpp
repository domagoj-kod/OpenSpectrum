// SPDX-License-Identifier: GPL-3.0-or-later

#include "fft_analyzer.h"

#include <cmath>
#include <complex>
#include <cstddef>
#include <cstring>
#include <stdexcept>
#include <utility>
#include <vector>

#ifdef __AVX2__
#include <immintrin.h>
#endif

namespace openspectrum {

#ifdef __AVX2__
// Vectorized log10 for 8 positive floats. Range reduces x = m * 2^e and
// computes log via 2 * atanh((m-1)/(m+1)). Polynomial through y^7 gives
// max relative error ~1.7e-5 (~0.00015 dB) — well below display resolution.
static inline __m256 fast_log10_avx2(__m256 x) {
  __m256i const bits = _mm256_castps_si256(x);
  __m256i const exp_int = _mm256_sub_epi32(
      _mm256_srli_epi32(_mm256_and_si256(bits, _mm256_set1_epi32(0x7F800000)),
                        23),
      _mm256_set1_epi32(127));
  __m256 const e = _mm256_cvtepi32_ps(exp_int);
  __m256 const m = _mm256_castsi256_ps(
      _mm256_or_si256(_mm256_and_si256(bits, _mm256_set1_epi32(0x007FFFFF)),
                      _mm256_set1_epi32(0x3F800000)));

  // y = (m - 1) / (m + 1), y in [0, 1/3]
  __m256 const num = _mm256_sub_ps(m, _mm256_set1_ps(1.0F));
  __m256 const den = _mm256_add_ps(m, _mm256_set1_ps(1.0F));
  __m256 const y = _mm256_div_ps(num, den);
  __m256 const y2 = _mm256_mul_ps(y, y);

  // p = 1 + y^2/3 + y^4/5 + y^6/7  (Horner over y^2)
  __m256 p = _mm256_set1_ps(1.0F / 7.0F);
  p = _mm256_fmadd_ps(p, y2, _mm256_set1_ps(1.0F / 5.0F));
  p = _mm256_fmadd_ps(p, y2, _mm256_set1_ps(1.0F / 3.0F));
  p = _mm256_fmadd_ps(p, y2, _mm256_set1_ps(1.0F));

  // log10(m) = (2 / ln(10)) * y * p
  __m256 const log10_m =
      _mm256_mul_ps(_mm256_mul_ps(y, p), _mm256_set1_ps(0.8685889638065035F));

  // log10(x) = log10(m) + e * log10(2)
  return _mm256_fmadd_ps(e, _mm256_set1_ps(0.30102999566398120F), log10_m);
}
#endif

FftAnalyzer::FftAnalyzer(size_t fft_size)
    : m_fft_size(fft_size), m_input_buffer(fft_size), m_output_buffer(fft_size),
      // Complex IQ input → the FFT is two-sided: all N bins are meaningful and
      // span the full [center-rate/2, center+rate/2]. (A real-input rfft would
      // only need N/2+1, but truncating a complex transform to N/2+1 drops half
      // the band and makes the spectrum 2x-zoomed vs the frequency axis.)
      m_db_spectrum(fft_size), m_freq_bins(fft_size) {
  // Security: validate FFT size is a power of 2
  if (fft_size == 0 || (fft_size & (fft_size - 1)) != 0) {
    throw std::invalid_argument("FFT size must be a power of 2");
  }

  compute_frequency_bins();
}

FftAnalyzer::~FftAnalyzer() = default;

FftAnalyzer::FftAnalyzer(FftAnalyzer &&other) noexcept
    : m_fft_size(other.m_fft_size),
      m_input_buffer(std::move(other.m_input_buffer)),
      m_output_buffer(std::move(other.m_output_buffer)),
      m_db_spectrum(std::move(other.m_db_spectrum)),
      m_freq_bins(std::move(other.m_freq_bins)), m_center_dc(other.m_center_dc),
      m_window_coherent_gain(other.m_window_coherent_gain) {
  other.m_fft_size = 0;
}

auto FftAnalyzer::operator=(FftAnalyzer &&other) noexcept -> FftAnalyzer & {
  if (this != &other) {
    m_fft_size = other.m_fft_size;
    m_center_dc = other.m_center_dc;
    m_input_buffer = std::move(other.m_input_buffer);
    m_output_buffer = std::move(other.m_output_buffer);
    m_db_spectrum = std::move(other.m_db_spectrum);
    m_freq_bins = std::move(other.m_freq_bins);
    m_window_coherent_gain = other.m_window_coherent_gain;

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

void FftAnalyzer::execute(std::span<const std::complex<float>> input,
                          std::vector<std::complex<float>> &output) {
  // Security: validate input size
  if (input.size() != m_fft_size) {
    throw std::invalid_argument("Input size must match FFT size");
  }

  // --- 1. Fill input buffer ----------------------------------------------
  // Non-centered path: pocketfft_cpx == std::complex<float>, so this is a
  // straight bitwise copy.
  if (!m_center_dc) {
    std::memcpy(m_input_buffer.data(), input.data(),
                m_fft_size * sizeof(pocketfft_cpx));
  } else {
#ifdef __AVX2__
    // Sign pattern repeats every 2 complex samples ([+,+,-,-]) and is
    // therefore constant across the 4-complex-wide AVX register. dst/src are
    // declared inside the guard so scalar (non-AVX2, e.g. debug) builds don't
    // warn on them being unused — the fallback below indexes the buffers
    // directly.
    auto *dst = reinterpret_cast<float *>(m_input_buffer.data());
    const auto *src = reinterpret_cast<const float *>(input.data());
    __m256 const sign_vec =
        _mm256_setr_ps(1.0F, 1.0F, -1.0F, -1.0F, 1.0F, 1.0F, -1.0F, -1.0F);
    size_t const simd_n = m_fft_size & ~size_t{3};
    for (size_t i = 0; i < simd_n; i += 4) {
      __m256 const s = _mm256_loadu_ps(src + 2 * i);
      _mm256_storeu_ps(dst + 2 * i, _mm256_mul_ps(s, sign_vec));
    }
    for (size_t i = simd_n; i < m_fft_size; ++i) {
      float const sign = (i % 2 == 0) ? 1.0F : -1.0F;
      m_input_buffer[i] =
          pocketfft_cpx(sign * input[i].real(), sign * input[i].imag());
    }
#else
    for (size_t i = 0; i < m_fft_size; ++i) {
      float const sign = (i % 2 == 0) ? 1.0F : -1.0F;
      m_input_buffer[i] =
          pocketfft_cpx(sign * input[i].real(), sign * input[i].imag());
    }
#endif
  }

  // --- 2. Execute FFT ----------------------------------------------------
  pocketfft_forward(m_input_buffer, m_output_buffer);

  // --- 3. DC centering is done by the pre-FFT sign trick -----------------
  // Multiplying the input by (-1)^n shifts the spectrum by N/2, so DC already
  // lands at bin N/2 (screen center) and bin 0 = center-rate/2. Do NOT also
  // swap the output halves: that is a second fftshift, which cancels the first
  // and leaves DC at bin 0 — misaligning the spectrum with the frequency axis
  // by half the span. The full N two-sided bins below then map 1:1 onto the
  // axis [center-rate/2, center+rate/2].

  // --- 4. Power / dB (no sqrt) -------------------------------------------
  // dB is computed directly from power: 20·log10(sqrt(p)·c) = 10·log10(p·c²).
  // Epsilon also squared so the power=0 case matches the prior formula.
  // Two-sided complex spectrum: every one of the N bins is single-sided
  // (scale=1). There is no negative-frequency mirror to fold in, so the
  // real-rfft +6 dB doubling on bins 1..N/2-1 does NOT apply here.
  size_t const out_bins = m_fft_size;
  float const inv_ng =
      1.0F / (static_cast<float>(m_fft_size) * m_window_coherent_gain);
  float const c_sq = inv_ng * inv_ng;
  float const eps_sq = 1e-24F;

  size_t simd_bins = 0;

#ifdef __AVX2__
  const auto *out_data =
      reinterpret_cast<const float *>(m_output_buffer.data());
  __m256 const c_sq_v = _mm256_set1_ps(c_sq);
  __m256 const eps_sq_v = _mm256_set1_ps(eps_sq);
  __m256 const ten_v = _mm256_set1_ps(10.0F);

  // Process 8 bins (16 floats / 2 AVX regs) per iteration over all N bins; the
  // scalar tail below covers the remainder. No DC/Nyquist patch is needed since
  // every bin uses the same scale.
  simd_bins = (out_bins / 8) * 8;

  for (size_t i = 0; i < simd_bins; i += 8) {
    __m256 const z0 = _mm256_loadu_ps(out_data + 2 * i);
    __m256 const z1 = _mm256_loadu_ps(out_data + 2 * i + 8);
    __m256 const zsq0 = _mm256_mul_ps(z0, z0);
    __m256 const zsq1 = _mm256_mul_ps(z1, z1);
    __m256 const h = _mm256_hadd_ps(zsq0, zsq1);
    __m256 const power =
        _mm256_castpd_ps(_mm256_permute4x64_pd(_mm256_castps_pd(h), 0xD8));
    __m256 const db_arg = _mm256_fmadd_ps(power, c_sq_v, eps_sq_v);
    __m256 const db = _mm256_mul_ps(fast_log10_avx2(db_arg), ten_v);
    _mm256_storeu_ps(&m_db_spectrum[i], db);
  }
#endif

  // Scalar tail (also covers the entire range on non-AVX2 builds).
  for (size_t i = simd_bins; i < out_bins; ++i) {
    float const r = m_output_buffer[i].real();
    float const im = m_output_buffer[i].imag();
    float const power = r * r + im * im;
    m_db_spectrum[i] = 10.0F * std::log10(power * c_sq + eps_sq);
  }

  // --- 5. Optional output copy (buffer is already rotated) --------------
  if (output.size() >= m_fft_size) {
    std::memcpy(output.data(), m_output_buffer.data(),
                m_fft_size * sizeof(std::complex<float>));
  }
}

auto FftAnalyzer::get_max_db() const -> float {
  if (m_db_spectrum.empty())
    return -140.0F;
  const float *ptr = m_db_spectrum.data();
  const size_t n = m_db_spectrum.size();
  float max_val = ptr[0];
  for (size_t i = 1; i < n; ++i) {
    if (ptr[i] > max_val)
      max_val = ptr[i];
  }
  return max_val;
}

} // namespace openspectrum