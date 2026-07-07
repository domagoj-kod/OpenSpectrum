// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstdint>
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
// The textual form is intentionally lossy (one decimal at the next-lower
// unit boundary) — it's intended for status bars, log lines, and JSON
// "*_formatted" fields, not for round-tripping a numeric value.
inline std::string format_frequency(uint32_t hz) {
  if (hz >= 1000000000) {
    return std::to_string(hz / 1000000000) + "." +
           std::to_string((hz % 1000000000) / 1000000) + " GHz";
  }
  if (hz >= 1000000) {
    return std::to_string(hz / 1000000) + "." +
           std::to_string((hz % 1000000) / 1000) + " MHz";
  }
  if (hz >= 1000) {
    return std::to_string(hz / 1000) + "." + std::to_string(hz % 1000) + " kHz";
  }
  return std::to_string(hz) + " Hz";
}

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
