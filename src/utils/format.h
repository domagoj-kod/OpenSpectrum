// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

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

// A frequency with auto-scaled units. The fraction is zero-padded to three
// digits: 1_090_000_000 is "1.090 GHz", not "1.90 GHz". For display, not for
// round-tripping. 64-bit: 32 bits end at 4.29 GHz.
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

// JSON string escape: quotes, backslashes, whitespace; other control characters
// are dropped.
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
