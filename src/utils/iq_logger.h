// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <atomic>
#include <complex>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <limits>
#include <mutex>
#include <string>
#include <thread>
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

// On-disk sample layout, interleaved I/Q, little-endian. CF32 is two float32
// per sample (8 bytes); CS16 is two int16 per sample (4 bytes), full scale
// 32768, which loses nothing against a receiver of 16 bits or fewer and halves
// the disk rate that a wide capture is bound by.
enum class IqSampleFormat { CF32, CS16 };

// Configuration for IQ logging
struct IqLoggerConfig {
  std::string output_directory = "data";
  std::string filename_prefix = "capture";
  size_t buffer_size_bytes = 1048576; // 1 MB default buffer
  IqSampleFormat format = IqSampleFormat::CF32;
};

// Callback type
using IqLoggerCompleteCallback = std::function<void(
    const std::string &filename, const std::string &metadata_filename)>;

// write_samples() is called from the producer thread; everything else from one
// other thread (main). The producer only copies into a ring: statistics,
// sample conversion and disk I/O run on the logger's own writer thread, so a
// slow disk never stalls the receiver.
class IqLogger {
public:
  explicit IqLogger(const IqLoggerConfig &config = IqLoggerConfig());
  ~IqLogger();

  // Start a new capture
  // Returns true if successfully started
  bool start_capture(uint64_t center_freq_hz, uint32_t sample_rate_hz,
                     float gain_db, size_t fft_size = 0,
                     const std::string &window_function = "",
                     const std::string &notes = "",
                     const std::string &device = "");

  // Queue IQ samples for the writer thread. Never blocks on I/O and never
  // allocates. A batch that does not fit in the ring (the writer is half a
  // second behind) is dropped whole and counted in capture.dropped_samples.
  void write_samples(const std::complex<float> *samples, size_t count);

  // Samples the source reported lost during this capture. Call before
  // stop_capture(); written to the sidecar as capture.dropped_samples.
  void set_dropped_samples(uint64_t count);

  // Stop current capture and finalize files
  void stop_capture();

  // False once a short write has ended the capture; stop_capture() then
  // finalizes the sidecar.
  bool is_capturing() const noexcept;

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
  void writer_loop();
  void write_block(const std::complex<float> *samples, size_t count);
  void flush_buffer();
  void append_bytes(const uint8_t *data, size_t size);
  std::string generate_metadata_json() const;

  IqLoggerConfig m_config;
  IqCaptureStats m_stats; // writer-owned while capturing

  // Capture parameters
  uint64_t m_center_freq_hz = 0;
  uint32_t m_sample_rate_hz = 0;
  float m_gain_db = 0.0f;
  size_t m_fft_size = 0;
  std::string m_window_function;
  std::string m_notes;
  std::string m_device;

  // Integrity. A short write ends the capture (m_write_error); losses the
  // source saw arrive through set_dropped_samples().
  uint64_t m_dropped_samples = 0;
  std::atomic<bool> m_write_error{false};

  // Timing. The first and last write times, with the samples that arrived
  // after the first batch, give the rate the stream actually delivered — the
  // only loss check that works for a source that cannot report its own drops.
  // Stamped on arrival by the producer, under m_push_mutex.
  double m_start_time = 0.0;
  std::string m_start_iso8601;
  double m_first_write_time = 0.0;
  double m_last_write_time = 0.0;
  size_t m_first_write_count = 0;
  size_t m_received_samples = 0;
  uint64_t m_ring_dropped = 0;

  // Files
  FILE *m_data_file = nullptr;
  std::string m_data_filename;
  std::string m_metadata_filename;

  // Buffer for efficient writing
  std::vector<uint8_t> m_buffer;
  size_t m_buffer_pos = 0;

  // Completion callback
  IqLoggerCompleteCallback m_complete_cb;

  // Producer -> writer ring, single producer and single consumer. m_head and
  // m_tail only grow; a sample's slot is its index modulo the ring size.
  std::vector<std::complex<float>> m_ring;
  std::atomic<size_t> m_head{0};
  std::atomic<size_t> m_tail{0};
  std::atomic<bool> m_stop{false};
  std::thread m_writer;

  // State. m_push_mutex keeps stop_capture() from releasing the ring under a
  // producer mid-copy; it is never held across I/O.
  bool m_capturing = false;
  std::mutex m_push_mutex;
};

} // namespace openspectrum
