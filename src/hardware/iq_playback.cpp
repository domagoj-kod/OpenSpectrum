// SPDX-License-Identifier: GPL-3.0-or-later

#include "hardware/iq_playback.h"
#include "utils/logger.h"

#include <chrono>
#include <cmath>
#include <complex>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <limits>
#include <string>
#include <utility>

namespace openspectrum {

// One frame is in flight at a time.
static constexpr size_t kPlaybackPoolFrames = 4;

static auto sample_bytes(IqSampleFormat format) -> size_t {
  return format == IqSampleFormat::CS16 ? 2 * sizeof(int16_t)
                                        : sizeof(std::complex<float>);
}

IqPlaybackSource::IqPlaybackSource(std::string path, uint32_t sample_rate_hz,
                                   IqSampleFormat format)
    : m_path(std::move(path)), m_sample_rate(sample_rate_hz), m_format(format) {
}

IqPlaybackSource::~IqPlaybackSource() {
  stop();
  if (m_file != nullptr) {
    std::fclose(m_file);
  }
}

auto IqPlaybackSource::open() -> bool {
  m_file = std::fopen(m_path.c_str(), "rb");
  if (m_file == nullptr) {
    LOG_ERROR("Cannot open IQ file: " + m_path);
    return false;
  }
  std::fseek(m_file, 0, SEEK_END);
  long const bytes = std::ftell(m_file);
  std::fseek(m_file, 0, SEEK_SET);
  if (bytes < static_cast<long>(sample_bytes(m_format))) {
    LOG_ERROR("IQ file is empty: " + m_path);
    std::fclose(m_file);
    m_file = nullptr;
    return false;
  }
  m_total_samples = static_cast<size_t>(bytes) / sample_bytes(m_format);
  double const seconds =
      static_cast<double>(m_total_samples) / static_cast<double>(m_sample_rate);
  char buf[64];
  std::snprintf(buf, sizeof(buf), "%zu samples, %.1f s", m_total_samples,
                seconds);
  LOG_INFO("IQ playback file: " + m_path + " (" + buf + ")");
  return true;
}

void IqPlaybackSource::start(size_t chunk_size) {
  stop();
  if (m_file == nullptr || chunk_size == 0 || m_sample_rate == 0) {
    return;
  }
  if (m_chunk_size != chunk_size || !m_pool) {
    m_chunk_size = chunk_size;
    m_pool = std::make_shared<FramePool>(m_chunk_size, kPlaybackPoolFrames);
    if (m_format == IqSampleFormat::CS16) {
      m_raw.resize(m_chunk_size * 2);
    }
  }
  m_running = true;
  m_thread = std::thread(&IqPlaybackSource::thread_main, this);
}

void IqPlaybackSource::stop() {
  m_running = false;
  if (m_thread.joinable()) {
    m_thread.join();
  }
}

// Reads up to count samples at the file position, converting CS16 to the
// pipeline's complex float. Returns the samples read (short at EOF).
auto IqPlaybackSource::read_samples(std::complex<float> *dst, size_t count)
    -> size_t {
  if (m_format != IqSampleFormat::CS16) {
    return std::fread(dst, sizeof(std::complex<float>), count, m_file);
  }
  size_t const got =
      std::fread(m_raw.data(), 2 * sizeof(int16_t), count, m_file);
  auto *out = reinterpret_cast<float *>(dst);
  for (size_t i = 0; i < got * 2; ++i) {
    out[i] = static_cast<float>(m_raw[i]) * (1.0F / 32768.0F);
  }
  return got;
}

// Emit one chunk, sleep to the next real-time deadline: the cadence of live
// hardware, so the render loop behaves the same for files.
void IqPlaybackSource::thread_main() {
  using clock = std::chrono::steady_clock;
  auto const chunk_period = std::chrono::nanoseconds(
      static_cast<uint64_t>(1e9) * m_chunk_size / m_sample_rate);
  auto next_deadline = clock::now() + chunk_period;

  while (m_running.load(std::memory_order_relaxed)) {
    FrameHandle frame = m_pool->acquire();
    if (!frame) {
      // Allocation failure (the pool grows on demand). Wait and retry.
      std::this_thread::sleep_for(std::chrono::milliseconds(1));
      continue;
    }
    frame.resize(m_chunk_size);

    size_t got = read_samples(frame.data(), m_chunk_size);
    if (got < m_chunk_size) {
      // EOF: rewind and top the chunk up, so the loop is seamless.
      std::fseek(m_file, 0, SEEK_SET);
      ++m_loop_count;
      if (m_loop_count == 1) {
        LOG_INFO("IQ playback: end of file, looping");
      }
      got += read_samples(frame.data() + got, m_chunk_size - got);
      frame.resize(got);
      if (got == 0) {
        continue; // unreadable file; frame returns to pool
      }
    }

    // The file is the trust boundary: one NaN would poison autoscale and the
    // waterfall quantization and freeze the display. Zero non-finite values.
    auto *as_floats = reinterpret_cast<float *>(frame.data());
    for (size_t i = 0; i < got * 2; ++i) {
      if (!std::isfinite(as_floats[i])) {
        as_floats[i] = 0.0F;
      }
    }

    if (m_frame_callback) {
      m_frame_callback(std::move(frame));
    }

    std::this_thread::sleep_until(next_deadline);
    next_deadline += chunk_period;
    // After a long stall, resynchronize instead of catching up.
    auto const now = clock::now();
    if (now > next_deadline + std::chrono::milliseconds(200)) {
      next_deadline = now + chunk_period;
    }
  }
}

auto IqPlaybackSource::read_meta_sidecar(const std::string &iq_path,
                                         IqCaptureMeta &meta) -> bool {
  // capture_X.iq -> capture_X.meta.json; anything else gets it appended.
  std::string meta_path = iq_path;
  constexpr const char *kExt = ".iq";
  if (meta_path.size() > 3 && meta_path.ends_with(kExt)) {
    meta_path.replace(meta_path.size() - 3, 3, ".meta.json");
  } else {
    meta_path += ".meta.json";
  }

  std::FILE *f = std::fopen(meta_path.c_str(), "rb");
  if (f == nullptr) {
    return false;
  }
  std::fseek(f, 0, SEEK_END);
  long const bytes = std::ftell(f);
  std::fseek(f, 0, SEEK_SET);
  if (bytes <= 0 || bytes > 65536) { // sanity bound
    std::fclose(f);
    return false;
  }
  std::string text(static_cast<size_t>(bytes), '\0');
  size_t const got = std::fread(text.data(), 1, text.size(), f);
  std::fclose(f);
  text.resize(got);

  // Scrape, not parse: the sidecar is flat machine-written JSON. strtoull, not
  // strtoul: unsigned long is 32 bits on Windows.
  auto scrape = [&text](const char *key, uint64_t &out) -> bool {
    size_t pos = text.find(key);
    if (pos == std::string::npos) {
      return false;
    }
    pos = text.find(':', pos + 1);
    if (pos == std::string::npos) {
      return false;
    }
    out = std::strtoull(text.c_str() + pos + 1, nullptr, 10);
    return out != 0;
  };

  uint64_t rate = 0;
  bool const have_freq = scrape("\"center_frequency_hz\"", meta.center_freq_hz);
  bool const have_rate = scrape("\"sample_rate_hz\"", rate) &&
                         rate <= std::numeric_limits<uint32_t>::max();
  meta.sample_rate_hz = static_cast<uint32_t>(rate);
  if (size_t pos = text.find("\"gain_db\"");
      pos != std::string::npos &&
      (pos = text.find(':', pos)) != std::string::npos) {
    meta.gain_db = std::strtof(text.c_str() + pos + 1, nullptr);
  }
  meta.format = text.find("\"cs16_le\"") != std::string::npos
                    ? IqSampleFormat::CS16
                    : IqSampleFormat::CF32;
  return have_freq && have_rate;
}

} // namespace openspectrum
