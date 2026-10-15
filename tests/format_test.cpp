// SPDX-License-Identifier: GPL-3.0-or-later
// Pins the zero-pad that 1.09 GHz collapsing to "1.9 GHz" slipped past.

#undef NDEBUG // these are asserts; they must not compile out
#include "format.h"

#include <cassert>

using openspectrum::format_frequency;

auto main() -> int {
  assert(format_frequency(1009000000) == "1.009 GHz"); // reported bug
  assert(format_frequency(1090000000) == "1.090 GHz"); // reported bug
  assert(format_frequency(1000000000) == "1.000 GHz");
  assert(format_frequency(100009000) == "100.009 MHz"); // latent MHz pad
  assert(format_frequency(1005) == "1.005 kHz");        // latent kHz pad
  assert(format_frequency(999) == "999 Hz");
  assert(format_frequency(5800000000) == "5.800 GHz"); // past 32 bits
}
