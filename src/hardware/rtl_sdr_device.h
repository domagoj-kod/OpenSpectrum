// SPDX-License-Identifier: MIT
#pragma once

#include "openspectrum/frame_pool.h"

#include <atomic>
#include <complex>
#include <cstdint>
#include <functional>
#include <memory>
#include <rtl-sdr.h>
#include <thread>
#include <vector>

class RtlSdrDevice {
public:
  explicit RtlSdrDevice(uint32_t index = 0, size_t pool_capacity = 8192);
  ~RtlSdrDevice();

  bool open();
  void close();
  void reset_buffer();
  [[nodiscard]] bool is_open() const { return m_dev != nullptr; }

  void set_frequency(uint32_t freq_hz);
  void set_sample_rate(uint32_t rate_hz);
  void set_gain(float gain_db); // 0-49.6 dB

  // Set FFT size for buffer pooling (should match FFT analyzer size)
  void set_fft_size(size_t fft_size);

  // Blocking read
  std::vector<std::complex<float>> read_samples(size_t count);

  // Async callback - uses FrameHandle for zero-allocation
  void start_streaming(size_t buffer_count = 8);
  void stop_streaming();

  // Callback using vector (legacy compatibility)
  using SampleCallback = std::function<void(std::vector<std::complex<float>>)>;
  void set_callback(SampleCallback cb);

  // Callback using FrameHandle (cache-friendly)
  using FrameCallback = std::function<void(openspectrum::FrameHandle)>;
  void set_frame_callback(FrameCallback cb);

private:
  static void static_callback(unsigned char *buf, uint32_t len, void *ctx);
  void process_callback(unsigned char *buf, uint32_t len);
  void process_callback_with_pool(unsigned char *buf, uint32_t len);

  rtlsdr_dev_t *m_dev = nullptr;
  uint32_t m_index = 0;
  uint32_t m_center_freq = 100000000; // 100 MHz default
  uint32_t m_sample_rate = 2048000;   // 2.048 MS/s
  float m_gain = 29.0f;               // dB

  // Buffer pooling for zero-allocation sample processing
  size_t m_fft_size = 8192;
  std::unique_ptr<openspectrum::FramePool> m_frame_pool;

  SampleCallback m_callback;
  FrameCallback m_frame_callback;
  bool m_use_frame_callback = false;

  bool m_streaming = false;

  // Async thread support
  std::thread m_async_thread;
  std::atomic<bool> m_thread_running{false};

  // Check if streaming mode is active
  [[nodiscard]] bool is_streaming() const { return m_streaming; }
};