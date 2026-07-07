// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <chrono>
#include <ctime>
#include <iomanip>
#include <sstream>
#include <string>

namespace openspectrum {

// Thread-safe wrappers around localtime/gmtime. The C variants return a
// pointer to a process-wide static buffer; concurrent callers across
// independent classes race even when each class holds its own mutex. These
// route to the reentrant POSIX (`*_r`) or Windows (`*_s`) variants.

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

// Current UTC time as a compact ISO-8601 stamp (YYYYMMDDTHHMMSSZ) — used in
// capture/export filenames and their metadata sidecars.
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
