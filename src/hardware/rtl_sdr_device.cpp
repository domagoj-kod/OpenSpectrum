// SPDX-License-Identifier: GPL-3.0-or-later

#include "rtl_sdr_device.h"
#include "logger.h"

#include <complex>
#include <cstddef>
#include <cstdint>
#include <rtl-sdr.h>
#include <stdexcept>
#include <utility>
#include <vector>

RtlSdrDevice::RtlSdrDevice(uint32_t index, size_t pool_capacity)
    : m_index(index), m_fft_size(pool_capacity) {
  m_frame_pool = std::make_shared<openspectrum::FramePool>(m_fft_size, 16);
}

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

void RtlSdrDevice::set_freq_correction(int ppm) {
  if (m_dev == nullptr || ppm == 0) {
    return;
  }
  // librtlsdr returns -2 when the value is already set; only a real failure
  // is worth surfacing.
  int const ret = rtlsdr_set_freq_correction(m_dev, ppm);
  if (ret != 0 && ret != -2) {
    LOG_ERROR("Failed to set frequency correction (" + std::to_string(ppm) +
              " ppm): " + std::to_string(ret));
  } else {
    LOG_INFO("Frequency correction: " + std::to_string(ppm) + " ppm");
  }
}

void RtlSdrDevice::set_bias_tee(bool on) {
  if (m_dev == nullptr) {
    return;
  }
  if (rtlsdr_set_bias_tee(m_dev, on ? 1 : 0) != 0) {
    LOG_ERROR("Failed to switch bias tee (dongle may not support it)");
  } else {
    LOG_INFO(std::string("Bias tee ") + (on ? "ON" : "OFF"));
  }
}

auto RtlSdrDevice::set_direct_sampling(int mode) -> bool {
  if (m_dev == nullptr) {
    return false;
  }
  if (rtlsdr_set_direct_sampling(m_dev, mode) != 0) {
    LOG_ERROR("Failed to enable direct sampling mode " + std::to_string(mode));
    return false;
  }
  LOG_INFO("Direct sampling mode " + std::to_string(mode) +
           (mode == 2   ? " (Q-branch)"
            : mode == 1 ? " (I-branch)"
                        : " (off)"));
  return true;
}

void RtlSdrDevice::set_fft_size(size_t fft_size) {
  if (m_fft_size != fft_size) {
    m_fft_size = fft_size;
    // Recreate pool with new size
    m_frame_pool = std::make_shared<openspectrum::FramePool>(m_fft_size, 16);
  }
}

void RtlSdrDevice::set_frame_callback(FrameCallback cb) {
  m_frame_callback = std::move(cb);
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
  // rtlsdr_read_async BLOCKS until canceled, so we can't call it from main
  // thread
  m_thread_running = true;
  m_async_thread = std::thread([this, buffer_count]() {
    // Smaller USB transfer buffer than the librtlsdr default (262144 B). At
    // 2.048 Msps the default delivers one callback every ~64 ms, which capped
    // the waterfall line rate at ~15.6 lines/s (one displayed line per buffer,
    // drain-to-newest). 65536 B = 32768 complex samples => a callback every
    // ~16 ms => ~62 lines/s, matching a ~60 Hz display. Must stay a multiple of
    // 512 (USB packet size); going much smaller mainly invites overruns on
    // high-latency paths (e.g. WSL2 usbipd) for no visible gain.
    uint32_t buf_len = 65536;
    LOG_INFO("Starting RTL-SDR async thread...");
    int ret = rtlsdr_read_async(m_dev, static_callback, this,
                                static_cast<uint32_t>(buffer_count), buf_len);
    // A negative return is normal on shutdown: stop_streaming() clears
    // m_thread_running and then calls rtlsdr_cancel_async(), which makes this
    // blocking call return with a libusb error (typically -5 NOT_FOUND) as the
    // in-flight transfers are torn down. Only flag a genuine error if we did
    // NOT initiate the stop.
    if (ret < 0) {
      if (m_thread_running) {
        LOG_ERROR("Async streaming error: " + std::to_string(ret));
      } else {
        LOG_DEBUG("Async streaming canceled (ret=" + std::to_string(ret) + ")");
      }
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
  static_cast<RtlSdrDevice *>(ctx)->process_callback_with_pool(buf, len);
}

void RtlSdrDevice::process_callback_with_pool(unsigned char *buf,
                                              uint32_t len) {
  if (!m_frame_callback || !m_frame_pool) {
    return;
  }

  size_t const count = len / 2; // interleaved 8-bit IQ -> complex samples

  // Forward the ENTIRE USB transfer. A 64 KB buffer is 32768 complex samples,
  // but pool frames hold capacity() (= FFT size, e.g. 4096), so emit the buffer
  // as a run of capacity-sized frames. The async callback's accumulator
  // reassembles them into FFT-sized chunks for the display, and the IQ capture
  // tap sees the full-rate stream. (This previously truncated to a single frame
  // via min(count, capacity), silently dropping ~7/8 of every transfer — fine
  // for the display, which only consumes the newest frame per render, but it
  // made IQ captures a heavily decimated record. Display line rate is unchanged
  // because the render loop's drain-to-newest throttle still governs it.)
  size_t offset = 0;
  while (offset < count) {
    openspectrum::FrameHandle frame_handle = m_frame_pool->acquire();
    if (!frame_handle) {
      LOG_DEBUG("process_callback_with_pool: pool exhausted, dropping buffer");
      return;
    }

    size_t const chunk = std::min(count - offset, frame_handle.capacity());
    frame_handle.resize(chunk);

    for (size_t i = 0; i < chunk; ++i) {
      size_t const j = offset + i;
      frame_handle[i] = std::complex<float>(
          (static_cast<float>(buf[j * 2]) - 127.5F) / 127.5F,
          (static_cast<float>(buf[(j * 2) + 1]) - 127.5F) / 127.5F);
    }

    // Pass to callback - handle returns to pool automatically when consumed.
    m_frame_callback(std::move(frame_handle));
    offset += chunk;
  }
}