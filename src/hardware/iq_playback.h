// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "openspectrum/attributes.h"
#include "openspectrum/frame_pool.h"
#include "openspectrum/iq_logger.h"
#include "sample_source.h"

#include <atomic>
#include <complex>
#include <cstdint>
#include <cstdio>
#include <memory>
#include <string>
#include <thread>
#include <vector>

namespace openspectrum {

// What the IqLogger .meta.json sidecar says about a capture.
struct IqCaptureMeta {
  uint64_t center_freq_hz = 0;
  uint32_t sample_rate_hz = 0;
  IqSampleFormat format = IqSampleFormat::CF32;
};

// Replays a raw .iq capture (either IqLogger format) through the same
// FrameHandle callback path the live receiver uses, paced in real time at the
// capture's sample rate. Loops at EOF, so a capture plays forever — the app
// runs without hardware (demos, development, PGO training, regression checks
// against known captures).
class IqPlaybackSource : public SampleSource {
public:
  OS_COLD IqPlaybackSource(std::string path, uint32_t sample_rate_hz,
                           IqSampleFormat format = IqSampleFormat::CF32);
  ~IqPlaybackSource() override;

  IqPlaybackSource(const IqPlaybackSource &) = delete;
  IqPlaybackSource &operator=(const IqPlaybackSource &) = delete;

  const char *name() const noexcept override { return "IQ playback"; }
  // No tuner: any centre frequency a capture carries displays unclamped. The
  // gain range only bounds the on-screen label.
  TuningLimits limits() const noexcept override {
    return {0, TuningLimits{}.max_frequency_hz, 0.0F, 49.6F};
  }
  bool tunable() const noexcept override { return false; }

  // Opens the file; false if missing or empty.
  OS_COLD bool open() override;

  // Starts the reader thread emitting chunk_size-sample frames. Safe to call
  // again after stop() with a new chunk size (the FFT-size reconfiguration
  // path does exactly that).
  void start(size_t chunk_size) override;
  void stop() override; // joins the reader thread

  // Parses the IqLogger .meta.json sidecar next to iq_path (capture_X.iq ->
  // capture_X.meta.json). Returns true if frequency and rate were found; a
  // sidecar without sample_format is the original complex-float layout.
  OS_COLD static bool read_meta_sidecar(const std::string &iq_path,
                                        IqCaptureMeta &meta);

private:
  void thread_main();
  size_t read_samples(std::complex<float> *dst, size_t count);

  std::string m_path;
  uint32_t m_sample_rate;
  IqSampleFormat m_format;
  std::FILE *m_file = nullptr;
  size_t m_total_samples = 0;

  size_t m_chunk_size = 0;
  std::shared_ptr<FramePool> m_pool;
  std::vector<int16_t> m_raw; // CS16 read buffer, one chunk

  std::thread m_thread;
  std::atomic<bool> m_running{false};
  uint64_t m_loop_count = 0;
};

} // namespace openspectrum
