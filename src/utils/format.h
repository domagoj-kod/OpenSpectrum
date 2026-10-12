// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cassert>
#include <cstdint>
#include <cstdio>
#include <string>

// Build version stamped into capture/export metadata. Defined by the Makefile
// (VERSION_DEF, from git describe) as a bare token, so it must be stringized
// at the point of use; "dev" is the fallback for ad-hoc builds.
#ifndef OPENSPECTRUM_VERSION
#define OPENSPECTRUM_VERSION dev
#endif
#define OS_STRINGIFY2(x) #x
#define OS_STRINGIFY(x) OS_STRINGIFY2(x)

namespace openspectrum {

// Format a frequency in Hz with auto-scaled units (Hz / kHz / MHz / GHz).
// The fractional part is the remainder in the next-lower unit, zero-padded to
// three digits (milli-unit resolution) — 1_090_000_000 → "1.090 GHz", not
// "1.90 GHz". The %03u pad is load-bearing: without it std::to_string dropped
// leading zeros and 1.009/1.090 GHz both collapsed toward "1.9". It's intended
// for status bars, log lines, and JSON "*_formatted" fields, not for
// round-tripping a numeric value.
// 64-bit on purpose: 32 bits end at 4.29 GHz, below the 5.8 GHz band.
inline std::string format_frequency(uint64_t hz) {
  char buf[40];
  auto const whole = [hz](uint64_t unit) {
    return static_cast<unsigned long long>(hz / unit);
  };
  auto const frac = [hz](uint64_t unit) {
    return static_cast<unsigned>((hz % unit) / (unit / 1000));
  };
  if (hz >= 1000000000) {
    std::snprintf(buf, sizeof buf, "%llu.%03u GHz", whole(1000000000),
                  frac(1000000000));
  } else if (hz >= 1000000) {
    std::snprintf(buf, sizeof buf, "%llu.%03u MHz", whole(1000000),
                  frac(1000000));
  } else if (hz >= 1000) {
    std::snprintf(buf, sizeof buf, "%llu.%03u kHz", whole(1000), frac(1000));
  } else {
    std::snprintf(buf, sizeof buf, "%llu Hz", whole(1));
  }
  return buf;
}

#ifndef NDEBUG
// Run-it validation: pins the zero-pad that 1.09 GHz collapsing to "1.9 GHz"
// slipped past. Called once at startup; compiled out of release.
inline void format_frequency_selftest() {
  assert(format_frequency(1009000000) == "1.009 GHz"); // reported bug
  assert(format_frequency(1090000000) == "1.090 GHz"); // reported bug
  assert(format_frequency(1000000000) == "1.000 GHz");
  assert(format_frequency(100009000) == "100.009 MHz"); // latent MHz pad
  assert(format_frequency(1005) == "1.005 kHz");        // latent kHz pad
  assert(format_frequency(999) == "999 Hz");
  assert(format_frequency(5800000000) == "5.800 GHz"); // past 32 bits
}
#endif

// Escape a string for embedding in JSON output (quotes, backslashes, named
// whitespace escapes; other control characters are dropped).
inline std::string escape_json_string(const std::string &str) {
  std::string result;
  result.reserve(str.size() * 2);
  for (char c : str) {
    switch (c) {
    case '"':
      result += "\\\"";
      break;
    case '\\':
      result += "\\\\";
      break;
    case '\b':
      result += "\\b";
      break;
    case '\f':
      result += "\\f";
      break;
    case '\n':
      result += "\\n";
      break;
    case '\r':
      result += "\\r";
      break;
    case '\t':
      result += "\\t";
      break;
    default:
      if (c < 0 || c >= 32) {
        result += c;
      }
    }
  }
  return result;
}

} // namespace openspectrum
