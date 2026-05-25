// SPDX-License-Identifier: MIT

#include "rtl_sdr_device.h"
#include "logger.h"

#include <complex>
#include <cstddef>
#include <cstdint>
#include <rtl-sdr.h>
#include <stdexcept>
#include <utility>
#include <vector>

RtlSdrDevice::RtlSdrDevice(uint32_t index) : m_index(index) {}

auto RtlSdrDevice::open() -> bool {
  if (m_dev != nullptr) {
    return true;
  }

  if (rtlsdr_open(&m_dev, m_index) < 0) {
    return false;
  }

  // Recommended init order for librtlsdr
  rtlsdr_set_sample_rate(m_dev, m_sample_rate);
  rtlsdr_set_center_freq(m_dev, m_center_freq);
  rtlsdr_set_tuner_gain_mode(m_dev, 0); // 0 = auto gain (safer default)
  return true;
}

void RtlSdrDevice::close() {
  if (m_dev != nullptr) {
    rtlsdr_close(m_dev);
    m_dev = nullptr;
  }
}

RtlSdrDevice::~RtlSdrDevice() {
    stop_streaming(); // Stop async thread before closing device
    close();
}

void RtlSdrDevice::reset_buffer() {
  if (m_dev != nullptr) {
    rtlsdr_reset_buffer(m_dev);
  }
}

void RtlSdrDevice::set_frequency(uint32_t freq_hz) {
  m_center_freq = freq_hz;
  if (m_dev != nullptr) {
    rtlsdr_set_center_freq(m_dev, freq_hz);
  }
}

void RtlSdrDevice::set_sample_rate(uint32_t rate_hz) {
  m_sample_rate = rate_hz;
  if (m_dev != nullptr) {
    rtlsdr_set_sample_rate(m_dev, rate_hz); // calls the C library, not itself
  }
}

void RtlSdrDevice::set_gain(float gain_db) {
  m_gain = gain_db;
  if (m_dev != nullptr) {
    rtlsdr_set_tuner_gain_mode(m_dev, 1); // switch to manual
    rtlsdr_set_tuner_gain(m_dev, static_cast<int>(gain_db * 10));
  }
}

auto RtlSdrDevice::read_samples(size_t count)
    -> std::vector<std::complex<float>> {
  std::vector<uint8_t> buf(count * 2); // I + Q = 2 bytes per sample
  int n_read = 0;
  int const ret = rtlsdr_read_sync(m_dev, buf.data(),
                                   static_cast<int>(buf.size()), &n_read);
  if (ret < 0 || n_read != static_cast<int>(buf.size())) {
    throw std::runtime_error("RTL-SDR read failed or short read");
  }

  std::vector<std::complex<float>> samples(count);
  for (size_t i = 0; i < count; ++i) {
    // RTL2832U outputs unsigned 8-bit; centre value 127/128 = DC
    samples[i] = std::complex<float>(
        (static_cast<float>(buf[i * 2]) - 127.5F) / 127.5F,
        (static_cast<float>(buf[(i * 2) + 1]) - 127.5F) / 127.5F);
  }
  return samples;
}

void RtlSdrDevice::set_callback(SampleCallback cb) {
  m_callback = std::move(cb);
}

// Async streaming control
// NOTE: rtlsdr_read_async() BLOCKS the calling thread until canceled.
// We must run it in a separate thread to avoid freezing the main thread.
void RtlSdrDevice::start_streaming(size_t buffer_count) {
  if (m_dev == nullptr || m_streaming) {
    return;
  }
  
  // Reset buffer before starting
  rtlsdr_reset_buffer(m_dev);
  
  // Start async reading in a separate thread
  // rtlsdr_read_async BLOCKS until canceled, so we can't call it from main thread
  m_thread_running = true;
  m_async_thread = std::thread([this, buffer_count]() {
    uint32_t buf_len = 0; // 0 = use default buffer length
    LOG_INFO("Starting RTL-SDR async thread...");
    int ret = rtlsdr_read_async(m_dev, static_callback, this, 
                                  static_cast<uint32_t>(buffer_count), buf_len);
    if (ret < 0) {
      LOG_ERROR("Async streaming error: " + std::to_string(ret));
    }
    LOG_INFO("RTL-SDR async thread exited.");
    m_thread_running = false;
  });
  m_streaming = true;
  LOG_INFO("RTL-SDR async streaming started from worker thread.");
}

void RtlSdrDevice::stop_streaming() {
  if (m_dev == nullptr || !m_streaming) {
    return;
  }
  
  LOG_INFO("Stopping RTL-SDR async streaming...");
  m_thread_running = false;
  
  // Cancel async mode - this will cause rtlsdr_read_async to return
  rtlsdr_cancel_async(m_dev);
  
  // Wait for the async thread to finish
  if (m_async_thread.joinable()) {
    m_async_thread.join();
  }
  m_streaming = false;
  LOG_INFO("RTL-SDR async streaming stopped.");
}

// Callback signature must match rtlsdr_read_async_cb_t
void RtlSdrDevice::static_callback(unsigned char *buf, uint32_t len,
                                   void *ctx) {
  static_cast<RtlSdrDevice *>(ctx)->process_callback(buf, len);
}

void RtlSdrDevice::process_callback(unsigned char *buf, uint32_t len) {
  if (!m_callback) {
    return;
  }
  size_t const count = len / 2;
  std::vector<std::complex<float>> samples(count);
  for (size_t i = 0; i < count; ++i) {
    samples[i] = std::complex<float>(
        (static_cast<float>(buf[i * 2]) - 127.5F) / 127.5F,
        (static_cast<float>(buf[(i * 2) + 1]) - 127.5F) / 127.5F);
  }
  m_callback(std::move(samples));
}