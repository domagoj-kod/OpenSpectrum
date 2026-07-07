// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <complex>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <limits>
#include <mutex>
#include <string>
#include <vector>

namespace openspectrum {

// Capture statistics for metadata.
// min_db starts at +inf so the first real sample replaces it; max_db starts
// at -inf for the symmetric reason. average_db starts at 0 and is built
// up by the online-mean update in IqLogger::update_stats.
struct IqCaptureStats {
  double average_db = 0.0;
  double min_db = std::numeric_limits<double>::infinity();
  double max_db = -std::numeric_limits<double>::infinity();
  size_t sample_count = 0;
  double duration_seconds = 0.0;
};

// Configuration for IQ logging
struct IqLoggerConfig {
  std::string output_directory = "data";
  std::string filename_prefix = "capture";
  size_t buffer_size_bytes = 1048576; // 1 MB default buffer
};

// Callback type
using IqLoggerCompleteCallback = std::function<void(
    const std::string &filename, const std::string &metadata_filename)>;

class IqLogger {
public:
  explicit IqLogger(const IqLoggerConfig &config = IqLoggerConfig());
  ~IqLogger();

  // Start a new capture
  // Returns true if successfully started
  bool start_capture(uint32_t center_freq_hz, uint32_t sample_rate_hz,
                     float gain_db, size_t fft_size = 0,
                     const std::string &window_function = "",
                     const std::string &notes = "");

  // Write IQ samples (thread-safe). Pointer + count keeps the producer-thread
  // hot path allocation-free.
  void write_samples(const std::complex<float> *samples, size_t count);

  // Stop current capture and finalize files
  void stop_capture();

  // Check if currently capturing
  bool is_capturing() const noexcept;

  // Get current capture statistics
  IqCaptureStats get_stats() const;

  // Set completion callback
  void set_complete_callback(IqLoggerCompleteCallback cb);

  // Get current filenames
  std::string get_data_filename() const;
  std::string get_metadata_filename() const;

  // Get configuration
  const IqLoggerConfig &get_config() const { return m_config; }

private:
  // Internal methods
  bool create_output_directory();
  std::string generate_filename(const std::string &extension) const;
  void open_files();
  void close_files();
  void write_metadata();
  void update_stats(const std::complex<float> *samples, size_t count);
  void flush_buffer();
  std::string generate_metadata_json() const;

  IqLoggerConfig m_config;
  mutable IqCaptureStats m_stats;

  // Capture parameters
  uint32_t m_center_freq_hz = 0;
  uint32_t m_sample_rate_hz = 0;
  float m_gain_db = 0.0f;
  size_t m_fft_size = 0;
  std::string m_window_function;
  std::string m_notes;

  // Timing
  double m_start_time = 0.0;

  // Files
  FILE *m_data_file = nullptr;
  std::string m_data_filename;
  std::string m_metadata_filename;

  // Buffer for efficient writing
  std::vector<uint8_t> m_buffer;
  size_t m_buffer_pos = 0;

  // Completion callback
  IqLoggerCompleteCallback m_complete_cb;

  // State
  bool m_capturing = false;
  mutable std::mutex m_mutex;
};

} // namespace openspectrum
