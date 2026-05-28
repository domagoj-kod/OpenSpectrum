// SPDX-License-Identifier: GPL-3.0-or-later

#include "openspectrum/iq_logger.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <ctime>
#include <iomanip>
#include <sstream>
#include <utility>
#include <sys/stat.h>
#include <sys/types.h>

#ifdef _WIN32
#include <direct.h>
#define mkdir(dir, mode) _mkdir(dir)
#else
#include <unistd.h>
#endif

namespace openspectrum {

// Helper function to get current ISO 8601 timestamp
static std::string get_iso8601_timestamp() {
  auto now = std::chrono::system_clock::now();
  auto in_time_t = std::chrono::system_clock::to_time_t(now);

  std::stringstream ss;
  ss << std::put_time(std::gmtime(&in_time_t), "%Y%m%dT%H%M%SZ");
  return ss.str();
}

// Helper function to get current Unix timestamp with microseconds
static double get_unix_timestamp() {
  auto now = std::chrono::system_clock::now();
  auto duration = now.time_since_epoch();
  return static_cast<double>(
             std::chrono::duration_cast<std::chrono::microseconds>(duration)
                 .count()) /
         1000000.0;
}

// Helper to format frequency for display
static std::string format_frequency(uint32_t hz) {
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

// Helper to escape string for JSON
static std::string escape_json_string(const std::string &str) {
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
      if (c >= 0 && c < 32) {
        // Control characters - skip or escape
        result += "";
      } else {
        result += c;
      }
    }
  }
  return result;
}

IqLogger::IqLogger(const IqLoggerConfig &config) : m_config(config) {
  // Initialize buffer
  m_buffer.resize(m_config.buffer_size_bytes);
  m_buffer_pos = 0;
}

IqLogger::~IqLogger() { stop_capture(); }

bool IqLogger::create_output_directory() {
  if (m_config.output_directory.empty()) {
    return true;
  }

  // Check if directory exists
  struct stat info;
  if (stat(m_config.output_directory.c_str(), &info) == 0 &&
      (info.st_mode & S_IFDIR)) {
    return true;
  }

  // Try to create directory
#ifdef _WIN32
  if (_mkdir(m_config.output_directory.c_str()) == 0) {
    return true;
  }
#else
  if (mkdir(m_config.output_directory.c_str(), 0755) == 0) {
    return true;
  }
#endif

  return false;
}

std::string IqLogger::generate_filename(const std::string &extension) const {
  std::string timestamp = get_iso8601_timestamp();
  std::string freq_str = format_frequency(m_center_freq_hz);
  // Replace spaces in frequency string
  freq_str.erase(std::remove(freq_str.begin(), freq_str.end(), ' '),
                 freq_str.end());

  std::string filename = m_config.filename_prefix + "_" + timestamp;
  if (!freq_str.empty()) {
    filename += "_" + freq_str;
  }
  filename += "." + extension;

  // Prepend output directory
  if (!m_config.output_directory.empty()) {
    filename = m_config.output_directory + "/" + filename;
  }

  return filename;
}

bool IqLogger::start_capture(uint32_t center_freq_hz, uint32_t sample_rate_hz,
                             float gain_db, size_t fft_size,
                             const std::string &window_function,
                             const std::string &notes) {
  std::lock_guard<std::mutex> lock(m_mutex);

  // Already capturing?
  if (m_capturing) {
    return false;
  }

  // Store capture parameters
  m_center_freq_hz = center_freq_hz;
  m_sample_rate_hz = sample_rate_hz;
  m_gain_db = gain_db;
  m_fft_size = fft_size;
  m_window_function = window_function;
  m_notes = notes;

  // Reset statistics
  m_stats = IqCaptureStats{};
  m_start_time = get_unix_timestamp();

  // Create output directory
  if (!create_output_directory()) {
    return false;
  }

  // Generate filenames
  m_data_filename = generate_filename("iq");
  m_metadata_filename = generate_filename("meta.json");

  // Open files
  open_files();

  if (m_data_file == nullptr) {
    close_files();
    return false;
  }

  // Write initial metadata (will be updated on stop)
  write_metadata();

  m_capturing = true;
  return true;
}

void IqLogger::open_files() {
  // Open data file in binary mode
  m_data_file = std::fopen(m_data_filename.c_str(), "wb");
  if (m_data_file == nullptr) {
    return;
  }

  // Set buffer for the FILE* (in addition to our own buffer)
  setvbuf(m_data_file, nullptr, _IOFBF, 1048576); // 1MB buffer
}

void IqLogger::close_files() {
  // Flush any remaining data in our buffer
  flush_buffer();

  // Close and finalize metadata
  if (m_data_file != nullptr) {
    std::fclose(m_data_file);
    m_data_file = nullptr;
  }

  // Update final metadata with actual statistics
  write_metadata();
}

void IqLogger::flush_buffer() {
  if (m_buffer_pos > 0 && m_data_file != nullptr) {
    size_t written = std::fwrite(&m_buffer[0], 1, m_buffer_pos, m_data_file);
    if (written < m_buffer_pos) {
      // Handle partial write
    }
    m_buffer_pos = 0;
  }
}

void IqLogger::write_samples(const std::vector<std::complex<float>> &samples) {
  std::lock_guard<std::mutex> lock(m_mutex);

  if (!m_capturing || m_data_file == nullptr) {
    return;
  }

  // Update statistics
  update_stats(samples);

  // Write samples to buffer
  const uint8_t *data_ptr = reinterpret_cast<const uint8_t *>(samples.data());
  size_t data_size = samples.size() * sizeof(std::complex<float>);
  size_t remaining = data_size;

  while (remaining > 0) {
    size_t to_copy =
        std::min(remaining, m_config.buffer_size_bytes - m_buffer_pos);
    std::memcpy(&m_buffer[m_buffer_pos], data_ptr + (data_size - remaining),
                to_copy);
    m_buffer_pos += to_copy;
    remaining -= to_copy;

    // Flush if buffer is full
    if (m_buffer_pos >= m_config.buffer_size_bytes) {
      flush_buffer();
    }
  }

  // Check if we need to rotate files (if max size is set)
  if (m_config.max_file_size_bytes > 0) {
    size_t current_size = std::ftell(m_data_file) + m_buffer_pos;
    if (current_size >= m_config.max_file_size_bytes) {
      // Close current file
      close_files();
      // Start new file with sequential number
      std::string base_name = m_data_filename;
      size_t last_dot = base_name.find_last_of('.');
      if (last_dot != std::string::npos) {
        base_name = base_name.substr(0, last_dot);
      }
      // Find next available number
      int seq = 1;
      std::string new_name;
      do {
        new_name = base_name + "_" + std::to_string(seq++) + ".iq";
      } while (std::fopen(new_name.c_str(), "rb") != nullptr);

      m_data_filename = new_name;
      m_metadata_filename =
          base_name + "_" + std::to_string(seq - 1) + ".meta.json";
      open_files();
      write_metadata();
    }
  }
}

void IqLogger::stop_capture() {
  std::lock_guard<std::mutex> lock(m_mutex);

  if (!m_capturing) {
    return;
  }

  // Update final duration
  double end_time = get_unix_timestamp();
  m_stats.duration_seconds = end_time - m_start_time;

  // Close files and finalize metadata
  close_files();

  m_capturing = false;

  // Invoke completion callback if set
  if (m_complete_cb) {
    m_complete_cb(m_data_filename, m_metadata_filename);
  }
}

bool IqLogger::is_capturing() const noexcept {
  std::lock_guard<std::mutex> lock(m_mutex);
  return m_capturing;
}

IqCaptureStats IqLogger::get_stats() const {
  std::lock_guard<std::mutex> lock(m_mutex);
  IqCaptureStats stats = m_stats;
  if (m_capturing && m_start_time > 0.0) {
    double end_time = get_unix_timestamp();
    stats.duration_seconds = end_time - m_start_time;
  }
  return stats;
}

void IqLogger::set_progress_callback(IqLoggerProgressCallback cb) {
  std::lock_guard<std::mutex> lock(m_mutex);
  m_progress_cb = std::move(cb);
}

void IqLogger::set_complete_callback(IqLoggerCompleteCallback cb) {
  std::lock_guard<std::mutex> lock(m_mutex);
  m_complete_cb = std::move(cb);
}

std::string IqLogger::get_data_filename() const {
  std::lock_guard<std::mutex> lock(m_mutex);
  return m_data_filename;
}

std::string IqLogger::get_metadata_filename() const {
  std::lock_guard<std::mutex> lock(m_mutex);
  return m_metadata_filename;
}

std::string IqLogger::generate_metadata_json() const {
  std::stringstream ss;
  double end_time = get_unix_timestamp();
  double duration =
      m_capturing ? (end_time - m_start_time) : m_stats.duration_seconds;

  ss << "{\n";
  ss << "  \"version\": \"1.0\",\n";

  // Capture info
  ss << "  \"capture\": {\n";
  ss << R"(    "timestamp_iso8601": ")" << get_iso8601_timestamp() << "\",\n";
  ss << "    \"timestamp_unix\": " << m_start_time << ",\n";
  ss << "    \"duration_seconds\": " << duration << ",\n";
  ss << "    \"sample_count\": " << m_stats.sample_count << "\n";
  ss << "  },\n";

  // Device info
  ss << "  \"device\": {\n";
  ss << "    \"type\": \"RTL2832U\",\n";
  ss << "    \"index\": 0\n";
  ss << "  },\n";

  // Signal parameters
  ss << "  \"signal\": {\n";
  ss << "    \"center_frequency_hz\": " << m_center_freq_hz << ",\n";
  ss << "    \"sample_rate_hz\": " << m_sample_rate_hz << ",\n";
  ss << "    \"gain_db\": " << m_gain_db << ",\n";
  if (m_fft_size > 0) {
    ss << "    \"fft_size\": " << m_fft_size << ",\n";
  }
  if (!m_window_function.empty()) {
    ss << R"(    "window_function": ")" << m_window_function << "\"\n";
  }
  ss << "  },\n";

  // Environment
  ss << "  \"environment\": {\n";
  ss << "    \"application\": \"OpenSpectrum\",\n";
  ss << "    \"version\": \"1.0.0-nightly\",\n";
#ifdef _WIN32
  ss << "    \"platform\": \"Windows\"\n";
#elif __APPLE__
  ss << "    \"platform\": \"macOS\"\n";
#else
  ss << "    \"platform\": \"Linux\"\n";
#endif
  ss << "  },\n";

  // Statistics
  ss << "  \"statistics\": {\n";
  ss << "    \"peak_signal_db\": " << m_stats.peak_db << ",\n";
  ss << "    \"average_signal_db\": " << m_stats.average_db << ",\n";
  ss << "    \"min_signal_db\": " << m_stats.min_db << ",\n";
  ss << "    \"max_signal_db\": " << m_stats.max_db << "\n";
  ss << "  },\n";

  // Notes
  if (!m_notes.empty()) {
    ss << R"(  "notes": ")" << escape_json_string(m_notes) << "\"\n";
  }

  ss << "}";

  return ss.str();
}

void IqLogger::write_metadata() {
  std::string json = generate_metadata_json();

  FILE *meta_file = std::fopen(m_metadata_filename.c_str(), "w");
  if (meta_file != nullptr) {
    std::fwrite(json.c_str(), 1, json.size(), meta_file);
    std::fclose(meta_file);
  }
}

void IqLogger::update_stats(const std::vector<std::complex<float>> &samples) {
  // Welford-style online mean: increment count per sample so the divisor in
  // avg += (x - avg) / n is the correct running sample index. The previous
  // implementation incremented sample_count once for the whole batch, which
  // made the divisor constant inside the loop and biased the average.
  for (const auto &sample : samples) {
    float const magnitude = std::abs(sample);
    float const mag_db =
        magnitude > 0.00001f ? 20.0f * std::log10(magnitude) : -140.0f;

    if (mag_db > m_stats.peak_db) m_stats.peak_db = mag_db;
    if (mag_db > m_stats.max_db)  m_stats.max_db  = mag_db;
    if (mag_db < m_stats.min_db)  m_stats.min_db  = mag_db;

    ++m_stats.sample_count;
    m_stats.average_db +=
        (static_cast<double>(mag_db) - m_stats.average_db) /
        static_cast<double>(m_stats.sample_count);
  }
}

} // namespace openspectrum
