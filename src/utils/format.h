// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstdint>
#include <string>

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

} // namespace openspectrum
