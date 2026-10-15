// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <complex>
#include <vector>

// Cache the FFT plan. Upstream defaults this to 0, which rebuilds the plan on
// every c2c call for a plan that never changes. Do not set it back to 0
// (docs/MEASUREMENTS.md, "FFT sweep, v3.9.0 vs v3.9.1"). One entry: one FFT
// size is live at a time.
//
// The only include site for pocketfft_hdronly.h, so the define cannot skew
// across translation units.
#define POCKETFFT_CACHE_SIZE 1

#include "pocketfft_hdronly.h"

namespace openspectrum {

using pocketfft_cpx = std::complex<float>;

inline void pocketfft_forward(const std::vector<pocketfft_cpx> &in,
                              std::vector<pocketfft_cpx> &out) {
  if (in.empty())
    return;
  const pocketfft::shape_t shape = {in.size()};
  const pocketfft::stride_t stride_in = {sizeof(pocketfft_cpx)};
  const pocketfft::stride_t stride_out = {sizeof(pocketfft_cpx)};
  const pocketfft::shape_t axes = {0};
  pocketfft::c2c(shape, stride_in, stride_out, axes, pocketfft::FORWARD,
                 in.data(), out.data(), 1.0f);
}

} // namespace openspectrum
