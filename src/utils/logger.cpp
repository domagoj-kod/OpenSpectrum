// SPDX-License-Identifier: GPL-3.0-or-later

#include "logger.h"

#include "time_utils.h"

#include <chrono>
#include <ctime>
#include <iomanip>
#include <iostream>
#include <mutex>
#include <sstream>
#include <thread>

namespace openspectrum {

auto Logger::get_instance() -> Logger & {
  static Logger instance;
  return instance;
}

void Logger::log(LogLevel level, const char *file, int line,
                 const char *function, const std::string &message) {
  if (level < m_min_level) {
    return;
  }

  auto const now = std::chrono::system_clock::now();
  std::time_t const time = std::chrono::system_clock::to_time_t(now);
  std::tm const tm = safe_localtime(time);

  const char *level_str = "";
  switch (level) {
  case LogLevel::TRACE:
    level_str = "\x1B[0;32mTRACE\x1B[0;37m";
    break;
  case LogLevel::DEBUG:
    level_str = "\x1B[0;34mDEBUG\x1B[0;37m";
    break;
  case LogLevel::INFO:
    level_str = "\x1B[0;34mINFO\x1B[0;37m";
    break;
  case LogLevel::WARNING:
    level_str = "\x1B[0;35mWARN\x1B[0;37m";
    break;
  case LogLevel::ERROR:
    level_str = "\x1B[0;31mERROR\x1B[0;37m";
    break;
  case LogLevel::CRITICAL:
    level_str = "\x1B[0;31mCRIT\x1B[0;37m";
    break;
  }

  std::ostringstream tid_ss;
  tid_ss << std::this_thread::get_id();

  std::lock_guard<std::mutex> const lock(m_mutex);
  std::cout << std::put_time(&tm, "[%Y-%m-%d %H:%M:%S") << "." << std::setw(3)
            << std::setfill('0')
            << std::chrono::duration_cast<std::chrono::milliseconds>(
                   now.time_since_epoch())
                       .count() %
                   1000
            << "][" << level_str << "][" << tid_ss.str() << "] " << file << ":"
            << line << " (" << function << "): " << message << "\n";
}

void Logger::flush() {
  std::lock_guard<std::mutex> const lock(m_mutex);
  std::cout << std::flush;
}

} // namespace openspectrum
