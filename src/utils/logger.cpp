// SPDX-License-Identifier: MIT

#include "logger.h"
#include <chrono>
#include <cstdio>
#include <cstring>
#include <ctime>
#include <iomanip>
#include <iostream>
#include <memory>
#include <mutex>
#include <sstream>
#include <string>
#include <thread>
#include <utility>

namespace openspectrum {

// --- ConsoleSink Implementation ---

void ConsoleSink::write(const LogEntry &entry) {
  std::time_t const time =
      std::chrono::system_clock::to_time_t(entry.timestamp);
  std::tm const tm = *std::localtime(&time);

  const char *level_str = "";
  switch (entry.level) {
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

  std::cout << std::put_time(&tm, "[%Y-%m-%d %H:%M:%S") << "." << std::setw(3)
            << std::setfill('0')
            << std::chrono::duration_cast<std::chrono::milliseconds>(
                   entry.timestamp.time_since_epoch())
                       .count() %
                   1000
            << "][" << level_str << "][" << entry.thread_id << "] "
            << entry.file << ":" << entry.line << " (" << entry.function
            << "): " << entry.message << "\n";
}

void ConsoleSink::flush() { std::cout << std::flush; }

// --- FileSink Implementation ---

FileSink::FileSink(const std::string &filename, size_t max_size)
    : m_filename(filename), m_max_size(max_size),
      m_file(std::fopen(filename.c_str(), "a")) {
  // Open file in append mode

  if (m_file != nullptr) {
    // Get current file size
    std::fseek(m_file, 0, SEEK_END);
    m_current_size = std::ftell(m_file);
  } else {
    std::cerr << "Failed to open log file: " << filename << '\n';
  }
}

FileSink::~FileSink() {
  if (m_file != nullptr) {
    std::fclose(m_file);
  }
}

void FileSink::rotate() {
  if (m_file == nullptr) {
    return;
  }

  std::fclose(m_file);
  m_file = nullptr;

  // Create rotated filename with timestamp
  std::time_t const now = std::time(nullptr);
  std::tm const tm = *std::localtime(&now);
  char timestamp[20];
  std::strftime(timestamp, sizeof(timestamp), "%Y%m%d_%H%M%S", &tm);

  std::string const rotated_name = m_filename + "." + timestamp;

  // Rename current file
  std::rename(m_filename.c_str(), rotated_name.c_str());

  // Reopen original file
  m_file = std::fopen(m_filename.c_str(), "a");
  if (m_file == nullptr) {
    std::cerr << "Failed to reopen log file after rotation: " << m_filename
              << '\n';
  }
  m_current_size = 0;
}

void FileSink::write(const LogEntry &entry) {
  if (m_file == nullptr) {
    return;
  }

  std::lock_guard<std::mutex> const lock(m_mutex);

  // Check if rotation is needed
  if (m_current_size >= m_max_size) {
    rotate();
    if (m_file == nullptr) {
      return;
    }
  }

  std::time_t const time =
      std::chrono::system_clock::to_time_t(entry.timestamp);
  std::tm const tm = *std::localtime(&time);

  const char *level_str = "";
  switch (entry.level) {
  case LogLevel::TRACE:
    level_str = "TRACE";
    break;
  case LogLevel::DEBUG:
    level_str = "DEBUG";
    break;
  case LogLevel::INFO:
    level_str = "INFO";
    break;
  case LogLevel::WARNING:
    level_str = "WARN";
    break;
  case LogLevel::ERROR:
    level_str = "ERROR";
    break;
  case LogLevel::CRITICAL:
    level_str = "CRIT";
    break;
  }

  // Estimate message size (rough approximation)
  size_t const msg_size = 64 + entry.message.size() + entry.file.size() + 32;
  if (m_current_size + msg_size >= m_max_size) {
    rotate();
    if (m_file == nullptr) {
      return;
    }
  }

  std::fprintf(
      m_file, "[%04d-%02d-%02d %02d:%02d:%02d.%03d][%s][%s] %s:%d (%s): %s\n",
      tm.tm_year + 1900, tm.tm_mon + 1, tm.tm_mday, tm.tm_hour, tm.tm_min,
      tm.tm_sec,
      static_cast<int>(std::chrono::duration_cast<std::chrono::milliseconds>(
                           entry.timestamp.time_since_epoch())
                           .count() %
                       1000),
      level_str, entry.thread_id.c_str(), entry.file.c_str(), entry.line,
      entry.function.c_str(), entry.message.c_str());

  std::fflush(m_file);
  m_current_size += msg_size;
}

void FileSink::flush() {
  if (m_file != nullptr) {
    std::fflush(m_file);
  }
}

// --- Logger Implementation ---

auto Logger::get_instance() -> Logger & {
  static Logger instance;
  return instance;
}

Logger::~Logger() { flush(); }

void Logger::add_sink(std::unique_ptr<ILogSink> sink) {
  std::lock_guard<std::mutex> const lock(m_mutex);
  m_sinks.push_back(std::move(sink));
}

void Logger::log(LogLevel level, const std::string &file, int line,
                 const std::string &function, const std::string &message) {
  if (level < m_min_level) {
    return;
  }

  LogEntry entry;
  entry.timestamp = std::chrono::system_clock::now();
  entry.level = level;
  entry.file = file;
  entry.line = line;
  entry.function = function;
  entry.message = message;

  // Get thread ID
  std::ostringstream tid_ss;
  tid_ss << std::this_thread::get_id();
  entry.thread_id = tid_ss.str();

  std::lock_guard<std::mutex> const lock(m_mutex);
  for (auto &sink : m_sinks) {
    sink->write(entry);
  }
}

void Logger::flush() {
  std::lock_guard<std::mutex> const lock(m_mutex);
  for (auto &sink : m_sinks) {
    sink->flush();
  }
}

// --- LogStream Implementation ---

LogStream::LogStream(LogLevel level, std::string file, int line,
                     std::string function)
    : m_level(level), m_file(std::move(file)), m_line(line),
      m_function(std::move(function)) {}

LogStream::~LogStream() {
  Logger::get_instance().log(m_level, m_file, m_line, m_function, m_ss.str());
}

} // namespace openspectrum