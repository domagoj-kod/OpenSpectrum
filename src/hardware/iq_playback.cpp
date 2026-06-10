// SPDX-License-Identifier: GPL-3.0-or-later

#include "iq_playback.h"
#include "logger.h"

#include <chrono>
#include <complex>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <string>
#include <utility>

namespace openspectrum {

IqPlaybackSource::IqPlaybackSource(std::string path, uint32_t sample_rate_hz)
    : m_path(std::move(path)), m_sample_rate(sample_rate_hz) {}

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
  if (bytes < static_cast<long>(sizeof(std::complex<float>))) {
    LOG_ERROR("IQ file is empty: " + m_path);
    std::fclose(m_file);
    m_file = nullptr;
    return false;
  }
  m_total_samples = static_cast<size_t>(bytes) / sizeof(std::complex<float>);
  double const seconds =
      static_cast<double>(m_total_samples) / static_cast<double>(m_sample_rate);
  char buf[64];
  std::snprintf(buf, sizeof(buf), "%zu samples, %.1f s", m_total_samples,
                seconds);
  LOG_INFO("IQ playback file: " + m_path + " (" + buf + ")");
  return true;
}

void IqPlaybackSource::start(size_t chunk_size, std::shared_ptr<FramePool> pool) {
  stop();
  m_chunk_size = chunk_size;
  m_pool = std::move(pool);
  if (m_file == nullptr || !m_pool || m_chunk_size == 0 ||
      m_sample_rate == 0) {
    return;
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

// Paced reader: emit one chunk, sleep to the next real-time deadline, repeat.
// Mirrors the live-hardware cadence, so the render loop's drain-to-newest
// throttle behaves identically for files and dongles.
void IqPlaybackSource::thread_main() {
  using clock = std::chrono::steady_clock;
  auto const chunk_period =
      std::chrono::nanoseconds(static_cast<uint64_t>(1e9) * m_chunk_size /
                               m_sample_rate);
  auto next_deadline = clock::now() + chunk_period;

  while (m_running.load(std::memory_order_relaxed)) {
    FrameHandle frame = m_pool->acquire();
    if (!frame) {
      // Pool transiently exhausted (renderer behind). Wait briefly and retry
      // the same chunk; the drain-to-newest throttle frees frames every
      // render iteration, so this never persists.
      std::this_thread::sleep_for(std::chrono::milliseconds(1));
      continue;
    }
    frame.resize(m_chunk_size);

    size_t got = std::fread(frame.data(), sizeof(std::complex<float>),
                            m_chunk_size, m_file);
    if (got < m_chunk_size) {
      // EOF: rewind and top the chunk up from the file start, so playback
      // loops seamlessly. (A file shorter than one chunk simply emits a
      // partial frame; the accumulator in the callback path reassembles.)
      std::fseek(m_file, 0, SEEK_SET);
      ++m_loop_count;
      if (m_loop_count == 1) {
        LOG_INFO("IQ playback: end of file, looping");
      }
      got += std::fread(frame.data() + got, sizeof(std::complex<float>),
                        m_chunk_size - got, m_file);
      frame.resize(got);
      if (got == 0) {
        continue; // unreadable file; frame returns to pool
      }
    }

    if (m_callback) {
      m_callback(std::move(frame));
    }

    std::this_thread::sleep_until(next_deadline);
    next_deadline += chunk_period;
    // Stall guard: after a long scheduler stall don't death-spiral trying to
    // catch up; resynchronize the deadline to now.
    auto const now = clock::now();
    if (now > next_deadline + std::chrono::milliseconds(200)) {
      next_deadline = now + chunk_period;
    }
  }
}

auto IqPlaybackSource::read_meta_sidecar(const std::string &iq_path,
                                         uint32_t &center_freq_hz,
                                         uint32_t &sample_rate_hz) -> bool {
  // capture_X.iq -> capture_X.meta.json (IqLogger naming); anything else gets
  // .meta.json appended.
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
  if (bytes <= 0 || bytes > 65536) { // metadata is ~1 KB; sanity bound
    std::fclose(f);
    return false;
  }
  std::string text(static_cast<size_t>(bytes), '\0');
  size_t const got = std::fread(text.data(), 1, text.size(), f);
  std::fclose(f);
  text.resize(got);

  // Minimal scrape — the sidecar is machine-generated flat JSON, a parser
  // dependency is not warranted for two integer fields.
  auto scrape = [&text](const char *key, uint32_t &out) -> bool {
    size_t pos = text.find(key);
    if (pos == std::string::npos) {
      return false;
    }
    pos = text.find(':', pos + 1);
    if (pos == std::string::npos) {
      return false;
    }
    out = static_cast<uint32_t>(std::strtoul(text.c_str() + pos + 1, nullptr, 10));
    return out != 0;
  };

  bool const have_freq = scrape("\"center_frequency_hz\"", center_freq_hz);
  bool const have_rate = scrape("\"sample_rate_hz\"", sample_rate_hz);
  return have_freq && have_rate;
}

} // namespace openspectrum
