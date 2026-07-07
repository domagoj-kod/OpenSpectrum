// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "openspectrum/frame_pool.h"

#include <atomic>
#include <cstdint>
#include <functional>
#include <memory>
#include <rtl-sdr.h>
#include <thread>

class RtlSdrDevice {
public:
  explicit RtlSdrDevice(uint32_t index = 0, size_t pool_capacity = 8192);
  ~RtlSdrDevice();

  bool open();
  void close();
  void reset_buffer();

  void set_frequency(uint32_t freq_hz);
  void set_sample_rate(uint32_t rate_hz);
  void set_gain(float gain_db); // 0-49.6 dB

  // One-shot device options, applied after open() and before streaming.
  void set_freq_correction(int ppm);  // crystal error, parts-per-million
  void set_bias_tee(bool on);         // 4.5 V antenna-port power (RTL-SDR v3)
  bool set_direct_sampling(int mode); // 0=off, 1=I-branch, 2=Q-branch (HF)

  // Set FFT size for buffer pooling (should match FFT analyzer size)
  void set_fft_size(size_t fft_size);

  // Async callback - uses FrameHandle for zero-allocation
  void start_streaming(size_t buffer_count = 8);
  void stop_streaming();

  // Callback using FrameHandle (cache-friendly)
  using FrameCallback = std::function<void(openspectrum::FrameHandle)>;
  void set_frame_callback(FrameCallback cb);

private:
  static void static_callback(unsigned char *buf, uint32_t len, void *ctx);
  void process_callback_with_pool(unsigned char *buf, uint32_t len);

  rtlsdr_dev_t *m_dev = nullptr;
  uint32_t m_index = 0;
  uint32_t m_center_freq = 100000000; // 100 MHz default
  uint32_t m_sample_rate = 2048000;   // 2.048 MS/s
  float m_gain = 29.0f;               // dB

  // Buffer pooling for zero-allocation sample processing
  size_t m_fft_size = 8192;
  // shared_ptr is required: FramePool relies on enable_shared_from_this so
  // FrameHandles can hold a weak_ptr to the pool and survive its destruction.
  std::shared_ptr<openspectrum::FramePool> m_frame_pool;

  FrameCallback m_frame_callback;
  bool m_streaming = false;

  // Async thread support
  std::thread m_async_thread;
  std::atomic<bool> m_thread_running{false};
};