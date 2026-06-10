// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "openspectrum/attributes.h"
#include "openspectrum/frame_pool.h"

#include <atomic>
#include <cstdint>
#include <cstdio>
#include <functional>
#include <memory>
#include <string>
#include <thread>

namespace openspectrum {

// Replays a raw .iq capture (interleaved complex<float>, the IqLogger format)
// through the same FrameHandle callback path the RTL-SDR async stream uses,
// paced in real time at the capture's sample rate. Loops at EOF, so a capture
// plays forever — the app runs without hardware (demos, development, PGO
// training, regression checks against known captures).
class IqPlaybackSource {
public:
  using FrameCallback = std::function<void(FrameHandle)>;

  OS_COLD IqPlaybackSource(std::string path, uint32_t sample_rate_hz);
  ~IqPlaybackSource();

  IqPlaybackSource(const IqPlaybackSource &) = delete;
  IqPlaybackSource &operator=(const IqPlaybackSource &) = delete;

  // Opens the file; false if missing or empty.
  OS_COLD bool open();

  void set_frame_callback(FrameCallback cb) { m_callback = std::move(cb); }

  // Starts the reader thread emitting chunk_size-sample frames from pool.
  // Safe to call again after stop() with a new chunk size / pool (the FFT-size
  // reconfiguration path does exactly that).
  void start(size_t chunk_size, std::shared_ptr<FramePool> pool);
  void stop(); // joins the reader thread

  // Parses the IqLogger .meta.json sidecar next to iq_path (capture_X.iq ->
  // capture_X.meta.json). Returns true and fills both values if found.
  OS_COLD static bool read_meta_sidecar(const std::string &iq_path,
                                        uint32_t &center_freq_hz,
                                        uint32_t &sample_rate_hz);

private:
  void thread_main();

  std::string m_path;
  uint32_t m_sample_rate;
  std::FILE *m_file = nullptr;
  size_t m_total_samples = 0;

  FrameCallback m_callback;
  size_t m_chunk_size = 0;
  std::shared_ptr<FramePool> m_pool;

  std::thread m_thread;
  std::atomic<bool> m_running{false};
  uint64_t m_loop_count = 0;
};

} // namespace openspectrum
