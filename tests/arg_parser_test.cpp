// SPDX-License-Identifier: GPL-3.0-or-later
// Numbers from the command line and the saved settings: the whole text must
// be the number, so "433M" is an error, not 433 Hz.

#undef NDEBUG // these are asserts; they must not compile out
#include "utils/arg_parser.h"

#include <cassert>
#include <cstddef>

using namespace openspectrum;

auto main() -> int {
  double hz = 0.0;
  assert(parse_full("433e6", hz) && hz == 433e6);
  assert(!parse_full("433M", hz) && !parse_full("2.4M", hz));
  assert(!parse_full("10s", hz) && !parse_full("", hz));

  size_t n = 0;
  assert(parse_full("4096", n) && n == 4096);
  assert(!parse_full("4096x", n) && !parse_full("-1", n));

  int fps = 0;
  assert(parse_full("-3", fps) && fps == -3); // the range check is the caller's

  // Valid values reach the config.
  char prog[] = "openspectrum";
  char f[] = "-f";
  char f_val[] = "433e6";
  char r[] = "-r";
  char r_val[] = "2.4e6";
  char s[] = "-s";
  char s_val[] = "8192";
  char *argv[] = {prog, f, f_val, r, r_val, s, s_val};
  AppConfig const config = parse_arguments(7, argv);
  assert(config.center_freq_hz == 433e6 && config.sample_rate_hz == 2.4e6);
  assert(config.fft_size == 8192);
}
