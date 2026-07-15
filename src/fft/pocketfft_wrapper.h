// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <complex>
#include <vector>

// Cache the FFT plan across calls. Upstream defaults this to 0, which rebuilds
// the plan (factorize + recompute the twiddle tables + alloc/free ~512 KB at
// 65536) on every c2c call, 30x/s.
//
// This is kept for POWER, not for `cpu` — and `cpu` gets WORSE. Measured on the
// T470 (i5-7300U), FFT 65536, --max-fps 30, 40 s, vs the v3.9.0 baseline in
// logs/:
//   page-faults   271568 -> 9000   (-97%)
//   instructions  15596M -> 12763M (-18%)
//   cpu-cycles    12419M ->  9955M (-20%)
//   sys time       2.34s ->  1.27s (-46%)
//   task-clock     14628 -> 12611ms(-14%)
//   cpu (wall)      6.67 ->  7.73ms(+16%)  <-- yes, up
//   fps             30.0 ->  30.0          (unchanged)
// Strictly less work, strictly more wall-clock. The core runs the DSP block at
// 849 -> 789 MHz once the sustained work is gone: less to do, so the governor
// ramps less. `cpu` is wall-clock, so it reports the slowdown and hides the
// -20% cycles. See the `cpu` caveat in docs/TECHNICAL.md — this is the worked
// example of it.
//
// Do NOT "fix" the +16% by reverting: nothing waits on `cpu` (fps is capped and
// identical, the frame uses ~9.6 ms of 33 ms, and at 65536 the sample window is
// itself 32 ms, so +1 ms of DSP is ~1.5% of the real latency chain). Fewer
// cycles at a lower clock is less energy, which is priority #2 for a
// battery-measured instrument.
//
// Two traps for whoever measures this next, both of which caught the author:
//   1. A tight-loop micro-benchmark says 0.830 -> 0.715 ms and is meaningless
//      here — cache-hot at full turbo is not the shipped duty cycle.
//   2. A WSL2/i7-12700H A/B of the real app said cpu -26%. The T470 said +16%.
//      A different governor and clock range gives a different SIGN, not just a
//      different magnitude. Deltas do not port across machines; measure on the
//      target. (The "WSL2 predicts within 5%" note in CLAUDE.md is about
//      absolute values on the *same* CPU, and does not license this.)
//
// One entry suffices: exactly one FFT size is live at a time, so a size change
// is the only miss and it already reallocates everything. A 10-entry cache
// measured no faster for 7x the twiddle memory.
//
// This header is the only include site for pocketfft_hdronly.h, so the define
// cannot skew across translation units.
#define POCKETFFT_CACHE_SIZE 1

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
