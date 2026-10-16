// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "hardware/sample_source.h"
#include "utils/frame_pool.h"

#include <atomic>
#include <cstdint>
#include <memory>
#include <rtl-sdr.h>
#include <thread>

namespace openspectrum {

class RtlSdrDevice : public SampleSource {
public:
  explicit RtlSdrDevice(uint32_t index = 0, size_t pool_capacity = 8192);
  ~RtlSdrDevice() override;

  const char *name() const noexcept override { return "RTL2832U"; }
  TuningLimits limits() const noexcept override;

  bool open() override;
  void close();

  void set_frequency(uint64_t freq_hz) override;
  bool set_sample_rate(uint32_t rate_hz) override;
  void set_gain(float gain_db) override; // 0-49.6 dB

  // One-shot device options, applied after open() and before streaming.
  void set_freq_correction(int ppm);  // crystal error, parts-per-million
  void set_bias_tee(bool on);         // 4.5 V antenna-port power (RTL-SDR v3)
  bool set_direct_sampling(int mode); // 0=off, 1=I-branch, 2=Q-branch (HF)

  // Async streaming; frames carry at most chunk_size samples (the FFT size).
  void start(size_t chunk_size) override;
  void stop() override;

  uint64_t dropped_samples() const noexcept override {
    return m_dropped.load(std::memory_order_relaxed);
  }

private:
  static void static_callback(unsigned char *buf, uint32_t len, void *ctx);
  void process_callback_with_pool(unsigned char *buf, uint32_t len);

  rtlsdr_dev_t *m_dev = nullptr;
  uint32_t m_index = 0;
  uint32_t m_center_freq = 100000000; // 100 MHz default
  uint32_t m_sample_rate = 2048000;   // 2.048 MS/s
  float m_gain = 29.0f;               // dB
  bool m_direct_sampling = false;

  size_t m_fft_size = 8192;
  // shared_ptr: FrameHandles hold a weak_ptr to the pool.
  std::shared_ptr<FramePool> m_frame_pool;

  bool m_streaming = false;
  std::atomic<uint64_t> m_dropped{0};

  std::thread m_async_thread;
  std::atomic<bool> m_thread_running{false};
};

} // namespace openspectrum
