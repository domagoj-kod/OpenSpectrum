// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <mutex>
#include <string>

namespace openspectrum {

// Log levels
enum class LogLevel { DEBUG, INFO, WARNING, ERROR };

// Thread-safe console logger (singleton). Formats each entry with a timestamp,
// coloured level tag, thread id, and source location, and writes it to stdout.
class Logger {
public:
  // Singleton access
  static Logger &get_instance();

  // Set minimum log level
  void set_level(LogLevel level) noexcept { m_min_level = level; }

  // Log a message (use the LOG_* macros instead of calling directly)
  void log(LogLevel level, const char *file, int line, const char *function,
           const std::string &message);

private:
  Logger() = default;

  LogLevel m_min_level = LogLevel::INFO;
  std::mutex m_mutex;

  // Prevent copying
  Logger(const Logger &) = delete;
  Logger &operator=(const Logger &) = delete;
};

// Helper macros for easy logging
#define LOG_DEBUG(msg)                                                         \
  ::openspectrum::Logger::get_instance().log(                                  \
      ::openspectrum::LogLevel::DEBUG, __FILE__, __LINE__, __func__, msg)

#define LOG_INFO(msg)                                                          \
  ::openspectrum::Logger::get_instance().log(                                  \
      ::openspectrum::LogLevel::INFO, __FILE__, __LINE__, __func__, msg)

#define LOG_WARNING(msg)                                                       \
  ::openspectrum::Logger::get_instance().log(                                  \
      ::openspectrum::LogLevel::WARNING, __FILE__, __LINE__, __func__, msg)

#define LOG_ERROR(msg)                                                         \
  ::openspectrum::Logger::get_instance().log(                                  \
      ::openspectrum::LogLevel::ERROR, __FILE__, __LINE__, __func__, msg)

} // namespace openspectrum
