// SPDX-License-Identifier: GPL-3.0-or-later

#include "openspectrum/iq_logger.h"

#include "format.h"
#include "logger.h"
#include "time_utils.h"

#include <algorithm>
#include <cassert>
#include <cerrno>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <iomanip>
#include <sstream>
#include <utility>

namespace openspectrum {

IqLogger::IqLogger(const IqLoggerConfig &config) : m_config(config) {
  // The write-coalescing buffer (buffer_size_bytes, e.g. 1 MB) is allocated
  // lazily in start_capture and released in stop_capture, so an idle logger —
  // the common case, capture is opt-in via Ctrl+S — holds no buffer memory.
  m_buffer_pos = 0;
}

IqLogger::~IqLogger() { stop_capture(); }

bool IqLogger::create_output_directory() {
  if (m_config.output_directory.empty()) {
    return true;
  }
  std::error_code ec;
  std::filesystem::create_directories(m_config.output_directory, ec);
  if (!std::filesystem::is_directory(m_config.output_directory)) {
    LOG_ERROR("Cannot create capture directory " + m_config.output_directory +
              ": " + ec.message());
    return false;
  }
  return true;
}

std::string IqLogger::generate_filename(const std::string &extension) const {
  const std::string &timestamp = m_start_iso8601;
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

bool IqLogger::start_capture(uint64_t center_freq_hz, uint32_t sample_rate_hz,
                             float gain_db, size_t fft_size,
                             const std::string &window_function,
                             const std::string &notes,
                             const std::string &device) {
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
  m_device = device;

  // Reset statistics
  m_stats = IqCaptureStats{};
  m_dropped_samples = 0;
  m_write_error = false;
  m_first_write_time = 0.0;
  m_last_write_time = 0.0;
  m_first_write_count = 0;
  m_received_samples = 0;
  m_ring_dropped = 0;
  m_start_time = get_unix_timestamp();
  // Taken once: the sidecar is rewritten at stop, and stamping it then put the
  // end time under a field every reader takes for the start.
  m_start_iso8601 = get_iso8601_timestamp();

  // Allocate the write-coalescing buffer for the duration of this capture.
  m_buffer.resize(m_config.buffer_size_bytes);
  m_buffer_pos = 0;

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

  // ponytail: half a second of samples, capped at 128 MB; size it from a
  // measured stall if a disk ever stalls longer.
  m_ring.resize(std::clamp<size_t>(sample_rate_hz / 2, 1, size_t{1} << 24));
  m_head.store(0, std::memory_order_relaxed);
  m_tail.store(0, std::memory_order_relaxed);
  m_stop.store(false, std::memory_order_relaxed);
  m_writer = std::thread(&IqLogger::writer_loop, this);

  std::lock_guard<std::mutex> lock(m_push_mutex);
  m_capturing = true;
  return true;
}

void IqLogger::open_files() {
  // Open data file in binary mode
  m_data_file = std::fopen(m_data_filename.c_str(), "wb");
  if (m_data_file == nullptr) {
    // --iq-output is a file-name prefix inside the capture directory; a
    // subdirectory in it must already exist.
    LOG_ERROR("Cannot open " + m_data_filename + ": " + std::strerror(errno));
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
      // Partial write — almost always out-of-space or a hard I/O error.
      // Retrying won't help, and silently dropping bytes would corrupt the
      // interleaved IQ stream (subsequent writes would land at a misaligned
      // sample boundary, breaking offline parsing). Mark the capture dead,
      // close the file, and let the operator see the error in the log.
      LOG_ERROR("IQ flush short write: " + std::to_string(written) + "/" +
                std::to_string(m_buffer_pos) +
                " bytes — stopping capture to preserve file integrity");
      m_stats.duration_seconds = get_unix_timestamp() - m_start_time;
      std::fclose(m_data_file);
      m_data_file = nullptr;
      // main sees is_capturing() go false and calls stop_capture(), which
      // writes the sidecar.
      m_write_error.store(true, std::memory_order_relaxed);
    }
    m_buffer_pos = 0;
  }
}

void IqLogger::append_bytes(const uint8_t *data, size_t size) {
  size_t done = 0;
  while (done < size && m_data_file != nullptr) {
    size_t const to_copy =
        std::min(size - done, m_config.buffer_size_bytes - m_buffer_pos);
    std::memcpy(&m_buffer[m_buffer_pos], data + done, to_copy);
    m_buffer_pos += to_copy;
    done += to_copy;

    // Flush if buffer is full
    if (m_buffer_pos >= m_config.buffer_size_bytes) {
      flush_buffer();
    }
  }
}

void IqLogger::write_samples(const std::complex<float> *samples, size_t count) {
  std::lock_guard<std::mutex> lock(m_push_mutex);
  if (!m_capturing || count == 0) {
    return;
  }

  m_last_write_time = get_unix_timestamp();
  if (m_received_samples == 0) {
    m_first_write_time = m_last_write_time;
    m_first_write_count = count;
  }
  m_received_samples += count;

  size_t const head = m_head.load(std::memory_order_relaxed);
  size_t const size = m_ring.size();
  if (size - (head - m_tail.load(std::memory_order_acquire)) < count) {
    m_ring_dropped += count;
    return;
  }
  size_t const at = head % size;
  size_t const first = std::min(count, size - at);
  std::copy_n(samples, first, m_ring.data() + at);
  std::copy_n(samples + first, count - first, m_ring.data());
  m_head.store(head + count, std::memory_order_release);
}

void IqLogger::writer_loop() {
  for (;;) {
    // m_stop before m_head: once m_stop reads true the producer has made its
    // last push, so an empty ring then means everything is written.
    bool const stopping = m_stop.load(std::memory_order_acquire);
    size_t const tail = m_tail.load(std::memory_order_relaxed);
    size_t const head = m_head.load(std::memory_order_acquire);
    if (head == tail) {
      if (stopping) {
        return;
      }
      // ponytail: polls every 5 ms; a condition variable if wake latency
      // ever matters.
      std::this_thread::sleep_for(std::chrono::milliseconds(5));
      continue;
    }
    size_t const size = m_ring.size();
    size_t const at = tail % size;
    size_t const n = std::min(head - tail, size - at); // the rest after a wrap
    write_block(m_ring.data() + at, n);                // comes next pass
    m_tail.store(tail + n, std::memory_order_release);
  }
}

void IqLogger::write_block(const std::complex<float> *samples, size_t count) {
  if (m_data_file == nullptr) { // a short write ended the capture
    return;
  }

  // Update statistics
  update_stats(samples, count);

  if (m_config.format == IqSampleFormat::CF32) {
    append_bytes(reinterpret_cast<const uint8_t *>(samples),
                 count * sizeof(std::complex<float>));
    return;
  }

  // CS16: scale to int16 a block at a time. The clamp only bites at exactly
  // +1.0, which 32768 would wrap.
  const auto *in = reinterpret_cast<const float *>(samples);
  int16_t block[1024];
  for (size_t done = 0; done < count * 2;) {
    size_t const n = std::min(count * 2 - done, std::size(block));
    for (size_t i = 0; i < n; ++i) {
      block[i] = static_cast<int16_t>(
          std::clamp(std::lrintf(in[done + i] * 32768.0F), -32768L, 32767L));
    }
    append_bytes(reinterpret_cast<const uint8_t *>(block), n * sizeof(int16_t));
    done += n;
  }
}

void IqLogger::set_dropped_samples(uint64_t count) {
  m_dropped_samples = count;
}

void IqLogger::stop_capture() {
  if (!m_capturing) {
    return;
  }
  {
    std::lock_guard<std::mutex> lock(m_push_mutex);
    m_capturing = false;
  }
  m_stop.store(true, std::memory_order_release);
  m_writer.join(); // drains the ring

  if (!m_write_error.load(std::memory_order_relaxed)) {
    m_stats.duration_seconds = get_unix_timestamp() - m_start_time;
  }
  if (m_ring_dropped > 0) {
    LOG_WARNING("IQ writer fell behind: " + std::to_string(m_ring_dropped) +
                " samples dropped from the capture");
  }

  // Close files and finalize metadata (flushes any buffered tail first).
  close_files();

  // An idle logger holds no buffer memory.
  m_buffer.clear();
  m_buffer.shrink_to_fit();
  m_buffer_pos = 0;
  m_ring.clear();
  m_ring.shrink_to_fit();

  // Invoke completion callback if set
  if (m_complete_cb) {
    m_complete_cb(m_data_filename, m_metadata_filename);
  }
}

bool IqLogger::is_capturing() const noexcept {
  return m_capturing && !m_write_error.load(std::memory_order_relaxed);
}

void IqLogger::set_complete_callback(IqLoggerCompleteCallback cb) {
  m_complete_cb = std::move(cb);
}

std::string IqLogger::get_data_filename() const { return m_data_filename; }

std::string IqLogger::get_metadata_filename() const {
  return m_metadata_filename;
}

std::string IqLogger::generate_metadata_json() const {
  std::stringstream ss;
  double end_time = get_unix_timestamp();
  double duration =
      m_capturing ? (end_time - m_start_time) : m_stats.duration_seconds;

  // Fixed notation: the default six significant digits printed the Unix start
  // time as 1.79181e+09, a resolution of about three hours.
  ss << std::fixed << std::setprecision(3);

  ss << "{\n";
  ss << "  \"version\": \"1.1\",\n";

  // Capture info
  ss << "  \"capture\": {\n";
  ss << R"(    "timestamp_iso8601": ")" << m_start_iso8601 << "\",\n";
  ss << "    \"timestamp_unix\": " << m_start_time << ",\n";
  ss << "    \"duration_seconds\": " << duration << ",\n";
  ss << R"(    "sample_format": ")"
     << (m_config.format == IqSampleFormat::CS16 ? "cs16_le" : "cf32_le")
     << "\",\n";
  ss << "    \"sample_count\": " << m_stats.sample_count << ",\n";
  ss << "    \"dropped_samples\": " << m_dropped_samples + m_ring_dropped
     << ",\n";
  ss << "    \"write_error\": " << (m_write_error ? "true" : "false") << "\n";
  ss << "  },\n";

  // Device info
  ss << "  \"device\": {\n";
  ss << R"(    "type": ")" << escape_json_string(m_device) << "\",\n";
  ss << "    \"index\": 0\n";
  ss << "  },\n";

  // Signal parameters
  ss << "  \"signal\": {\n";
  ss << "    \"center_frequency_hz\": " << m_center_freq_hz << ",\n";
  ss << "    \"sample_rate_hz\": " << m_sample_rate_hz << ",\n";
  // Rate the stream delivered, from the samples after the first batch over the
  // time between the first and last write. Well below sample_rate_hz means
  // samples were lost upstream. Batches arrive in bursts, so it settles only
  // over captures of tens of seconds.
  if (m_last_write_time > m_first_write_time) {
    ss << "    \"measured_sample_rate_hz\": "
       << static_cast<double>(m_received_samples - m_first_write_count) /
              (m_last_write_time - m_first_write_time)
       << ",\n";
  }
  ss << "    \"gain_db\": " << m_gain_db;
  if (m_fft_size > 0) {
    ss << ",\n    \"fft_size\": " << m_fft_size;
  }
  if (!m_window_function.empty()) {
    ss << ",\n" << R"(    "window_function": ")" << m_window_function << "\"";
  }
  ss << "\n  },\n";

  // Environment
  ss << "  \"environment\": {\n";
  ss << "    \"application\": \"OpenSpectrum\",\n";
  ss << R"(    "version": ")" << OS_STRINGIFY(OPENSPECTRUM_VERSION) << "\",\n";
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
  // min/max start at +-inf, which is not JSON; an empty capture has none.
  ss << "    \"average_signal_db\": " << m_stats.average_db << ",\n";
  if (m_stats.sample_count > 0) {
    ss << "    \"min_signal_db\": " << m_stats.min_db << ",\n";
    ss << "    \"max_signal_db\": " << m_stats.max_db << "\n";
  } else {
    ss << "    \"min_signal_db\": null,\n";
    ss << "    \"max_signal_db\": null\n";
  }
  ss << "  },\n";

  ss << R"(  "notes": ")" << escape_json_string(m_notes) << "\"\n";
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

void IqLogger::update_stats(const std::complex<float> *samples, size_t count) {
  // Welford-style online mean: increment count per sample so the divisor in
  // avg += (x - avg) / n is the correct running sample index. The previous
  // implementation incremented sample_count once for the whole batch, which
  // made the divisor constant inside the loop and biased the average.
  for (size_t i = 0; i < count; ++i) {
    std::complex<float> const &sample = samples[i];
    float const magnitude = std::abs(sample);
    float const mag_db =
        magnitude > 0.00001f ? 20.0f * std::log10(magnitude) : -140.0f;

    if (mag_db > m_stats.max_db)  m_stats.max_db  = mag_db;
    if (mag_db < m_stats.min_db)  m_stats.min_db  = mag_db;

    ++m_stats.sample_count;
    m_stats.average_db +=
        (static_cast<double>(mag_db) - m_stats.average_db) /
        static_cast<double>(m_stats.sample_count);
  }
}

#ifndef NDEBUG
void iq_logger_selftest() {
  namespace fs = std::filesystem;
  IqLoggerConfig config;
  config.output_directory =
      (fs::temp_directory_path() / "openspectrum-selftest").string();
  config.buffer_size_bytes = 4096;
  IqLogger logger(config);
  // 2000 Hz gives a ring of 1000 samples. Batches of 333 wrap it, and every
  // fourth arrives with the ring full (the writer sleeps 5 ms between polls),
  // so it is dropped whole. Timing can move which batches are dropped, not
  // what must hold of the file. The drops are the point, so their warning is
  // muted; main sets the log level after the self-checks.
  Logger::get_instance().set_level(LogLevel::ERROR);
  bool const started = logger.start_capture(0, 2000, 0.0F);
  assert(started);
  constexpr size_t kBatch = 333;
  constexpr size_t kBatches = 12;
  std::vector<std::complex<float>> batch(kBatch);
  for (size_t b = 0; b < kBatches; ++b) {
    for (size_t k = 0; k < kBatch; ++k) {
      batch[k] = {static_cast<float>(b * kBatch + k), 0.0F};
    }
    logger.write_samples(batch.data(), kBatch);
    if (b % 4 == 3) {
      std::this_thread::sleep_for(std::chrono::milliseconds(20));
    }
  }
  logger.stop_capture();

  // The file holds whole batches, in order, each one intact.
  std::vector<std::complex<float>> got(kBatch * kBatches);
  FILE *f = std::fopen(logger.get_data_filename().c_str(), "rb");
  assert(f != nullptr);
  size_t const n = std::fread(got.data(), sizeof got[0], got.size(), f);
  std::fclose(f);
  assert(n > 0 && n % kBatch == 0);
  float last = -1.0F;
  for (size_t i = 0; i < n; ++i) {
    float const v = got[i].real();
    assert(i % kBatch == 0 ? std::fmod(v, kBatch) == 0.0F && v > last
                           : v == last + 1.0F);
    last = v;
  }
  // The sidecar counts exactly the samples that are missing.
  std::string meta(4096, '\0');
  f = std::fopen(logger.get_metadata_filename().c_str(), "r");
  assert(f != nullptr);
  meta.resize(std::fread(meta.data(), 1, meta.size(), f));
  std::fclose(f);
  assert(meta.find(
             "\"dropped_samples\": " + std::to_string(kBatch * kBatches - n) +
             ",") != std::string::npos);
  fs::remove_all(config.output_directory);
}
#endif

} // namespace openspectrum
