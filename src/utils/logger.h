// SPDX-License-Identifier: MIT
#pragma once

#include <chrono>
#include <memory>
#include <mutex>
#include <source_location>
#include <sstream>
#include <string>
#include <vector>

namespace openspectrum {

// Log levels
enum class LogLevel { TRACE, DEBUG, INFO, WARNING, ERROR, CRITICAL };

// Log entry structure
struct LogEntry {
  std::chrono::system_clock::time_point timestamp;
  LogLevel level;
  std::string file;
  int line;
  std::string function;
  std::string message;
  std::string thread_id;
};

// Abstract log sink interface
class ILogSink {
public:
  virtual ~ILogSink() = default;
  virtual void write(const LogEntry &entry) = 0;
  virtual void flush() = 0;
};

// Console log sink
class ConsoleSink : public ILogSink {
public:
  void write(const LogEntry &entry) override;
  void flush() override;
};

// File log sink (with rotation support)
class FileSink : public ILogSink {
public:
  explicit FileSink(const std::string &filename,
                    size_t max_size = 10485760); // 10MB default
  ~FileSink() override;

  void write(const LogEntry &entry) override;
  void flush() override;

private:
  std::string m_filename;
  size_t m_max_size;
  std::FILE *m_file = nullptr;
  size_t m_current_size = 0;
  std::mutex m_mutex;

  void rotate();
};

// Secure logger with thread-safe operations
// No dynamic allocation in hot path (pre-allocated buffers)
class Logger {
public:
  Logger() = default;
  ~Logger();

  // Singleton access
  static Logger &get_instance();

  // Add a log sink (takes ownership)
  void add_sink(std::unique_ptr<ILogSink> sink);

  // Set minimum log level
  void set_level(LogLevel level) noexcept { m_min_level = level; }

  // Log macros for convenience (use LOG_* macros instead)
  void log(LogLevel level, const std::string &file, int line,
           const std::string &function, const std::string &message);

  // Flush all sinks
  void flush();

private:
  LogLevel m_min_level = LogLevel::INFO;
  std::vector<std::unique_ptr<ILogSink>> m_sinks;
  std::mutex m_mutex;

  // Prevent copying
  Logger(const Logger &) = delete;
  Logger &operator=(const Logger &) = delete;
};

// Helper macros for easy logging
#define LOG_TRACE(msg)                                                         \
  ::openspectrum::Logger::get_instance().log(                                  \
      ::openspectrum::LogLevel::TRACE, __FILE__, __LINE__, __func__, msg)

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

#define LOG_CRITICAL(msg)                                                      \
  ::openspectrum::Logger::get_instance().log(                                  \
      ::openspectrum::LogLevel::CRITICAL, __FILE__, __LINE__, __func__, msg)

// RAII log entry builder (supports stream-like syntax)
class LogStream {
public:
  LogStream(LogLevel level, const std::string &file, int line,
            const std::string &function);
  ~LogStream();

  template <typename T> LogStream &operator<<(const T &value) {
    m_ss << value;
    return *this;
  }

private:
  LogLevel m_level;
  std::string m_file;
  int m_line;
  std::string m_function;
  std::ostringstream m_ss;
};

// Stream-style logging macros
#define LOGS_TRACE                                                             \
  ::openspectrum::LogStream(::openspectrum::LogLevel::TRACE, __FILE__,         \
                            __LINE__, __func__)
#define LOGS_DEBUG                                                             \
  ::openspectrum::LogStream(::openspectrum::LogLevel::DEBUG, __FILE__,         \
                            __LINE__, __func__)
#define LOGS_INFO                                                              \
  ::openspectrum::LogStream(::openspectrum::LogLevel::INFO, __FILE__,          \
                            __LINE__, __func__)
#define LOGS_WARNING                                                           \
  ::openspectrum::LogStream(::openspectrum::LogLevel::WARNING, __FILE__,       \
                            __LINE__, __func__)
#define LOGS_ERROR                                                             \
  ::openspectrum::LogStream(::openspectrum::LogLevel::ERROR, __FILE__,         \
                            __LINE__, __func__)
#define LOGS_CRITICAL                                                          \
  ::openspectrum::LogStream(::openspectrum::LogLevel::CRITICAL, __FILE__,      \
                            __LINE__, __func__)

} // namespace openspectrum