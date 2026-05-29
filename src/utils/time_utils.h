// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <ctime>

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

} // namespace openspectrum
