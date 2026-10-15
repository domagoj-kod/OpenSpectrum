// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <complex>
#include <vector>

// Cache the FFT plan. Upstream defaults this to 0, which reconstructs the plan
// -- factorize, recompute the twiddle tables, allocate and free them -- on
// every c2c call, 30x/s, for a plan that never changes. Do not set it back to
// 0.
//
// The rebuild is O(N) against an O(N log N) transform, so it costs most where
// the transform is cheapest: it is measurable at the 4096 default and not at
// 32768/65536 (docs/MEASUREMENTS.md, "FFT sweep, v3.9.0 vs v3.9.1").
//
// One entry, not ten: exactly one FFT size is live at a time, so a size change
// is the only miss and it already reallocates everything. 10 measured no faster
// for 7x the twiddle memory.
//
// This header is the only include site for pocketfft_hdronly.h, so the define
// cannot skew across translation units.
#define POCKETFFT_CACHE_SIZE 1

#include "pocketfft_hdronly.h"

namespace openspectrum {

// Type compatibility with existing code
using pocketfft_cpx = std::complex<float>;

// 1D FFT convenience wrapper for complex-to-complex
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
