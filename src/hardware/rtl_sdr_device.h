// SPDX-License-Identifier: MIT
#pragma once

#include <atomic>
#include <complex>
#include <cstdint>
#include <functional>
#include <thread>
#include <rtl-sdr.h>
#include <vector>

class RtlSdrDevice {
public:
  explicit RtlSdrDevice(uint32_t index = 0);
  ~RtlSdrDevice();

  bool open();
  void close();
  void reset_buffer();
  bool is_open() const { return m_dev != nullptr; }

  void set_frequency(uint32_t freq_hz);
  void set_sample_rate(uint32_t rate_hz);
  void set_gain(float gain_db); // 0-49.6 dB

  // Blocking read
  std::vector<std::complex<float>> read_samples(size_t count);

  // Async callback
  void start_streaming(size_t buffer_count = 8);
  void stop_streaming();
  using SampleCallback = std::function<void(std::vector<std::complex<float>>)>;
  void set_callback(SampleCallback cb);

private:
  static void static_callback(unsigned char *buf, uint32_t len, void *ctx);
  void process_callback(unsigned char *buf, uint32_t len);

  rtlsdr_dev_t *m_dev = nullptr;
  uint32_t m_index = 0;
  uint32_t m_center_freq = 100000000; // 100 MHz default
  uint32_t m_sample_rate = 2048000;   // 2.048 MS/s
  float m_gain = 29.0f;               // dB
  SampleCallback m_callback;          // std::function, not a raw pointer
  bool m_streaming = false;
  
  // Async thread support
  std::thread m_async_thread;
  std::atomic<bool> m_thread_running{false};
  
  // Check if streaming mode is active
  bool is_streaming() const { return m_streaming; }
};