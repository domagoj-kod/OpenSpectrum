// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <complex>
#include <vector>

#include "pocketfft_hdronly.h"

// Type compatibility with existing code
using pocketfft_cpx = std::complex<float>;

// 1D FFT convenience wrapper for complex-to-complex
inline void pocketfft_forward(const std::vector<pocketfft_cpx> &in,
                              std::vector<pocketfft_cpx> &out) {
  if (in.empty()) return;
  const pocketfft::shape_t shape = {in.size()};
  const pocketfft::stride_t stride_in = {sizeof(pocketfft_cpx)};
  const pocketfft::stride_t stride_out = {sizeof(pocketfft_cpx)};
  const pocketfft::shape_t axes = {0};
  pocketfft::c2c(shape, stride_in, stride_out, axes, pocketfft::FORWARD,
                in.data(), out.data(), 1.0f);
}
