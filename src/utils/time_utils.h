// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <chrono>
#include <ctime>
#include <iomanip>
#include <sstream>
#include <string>

namespace openspectrum {

// Thread-safe localtime/gmtime: the C variants share a static buffer. These
// use the POSIX `*_r` or Windows `*_s` ones.

inline std::tm safe_localtime(std::time_t t) {
  std::tm out{};
#ifdef _WIN32
  ::localtime_s(&out, &t);
#else
  ::localtime_r(&t, &out);
#endif
  return out;
}

inline std::tm safe_gmtime(std::time_t t) {
  std::tm out{};
#ifdef _WIN32
  ::gmtime_s(&out, &t);
#else
  ::gmtime_r(&t, &out);
#endif
  return out;
}

// Compact UTC ISO-8601 stamp (YYYYMMDDTHHMMSSZ) for capture/export filenames.
inline std::string get_iso8601_timestamp() {
  std::time_t const t =
      std::chrono::system_clock::to_time_t(std::chrono::system_clock::now());
  std::tm const tm = safe_gmtime(t);
  std::stringstream ss;
  ss << std::put_time(&tm, "%Y%m%dT%H%M%SZ");
  return ss.str();
}

// Current Unix timestamp with microsecond resolution.
inline double get_unix_timestamp() {
  return static_cast<double>(
             std::chrono::duration_cast<std::chrono::microseconds>(
                 std::chrono::system_clock::now().time_since_epoch())
                 .count()) /
         1000000.0;
}

} // namespace openspectrum
