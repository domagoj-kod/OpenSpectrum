// SPDX-License-Identifier: GPL-3.0-or-later

#include "hardware/rtl_sdr_device.h"
#include "utils/logger.h"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <rtl-sdr.h>

namespace openspectrum {

// Interleaved uint8 IQ to float in [-1, 1): x/127.5 - 1, n = 2 * samples.
// A free function with __restrict so the compiler emits full-width AVX2 with
// no alias versioning. The highest-volume loop in the program.
static void convert_iq_u8_to_f32(const unsigned char *__restrict src,
                                 float *__restrict dst, size_t n) {
  for (size_t k = 0; k < n; ++k) {
    dst[k] = static_cast<float>(src[k]) * (1.0F / 127.5F) - 1.0F;
  }
}

// One frame is in flight at a time (librtlsdr delivers callbacks serially and
// the handle is released when m_frame_callback returns); the rest are spares.
static constexpr size_t kDeviceFramePoolFrames = 4;

// USB buffers in flight for rtlsdr_read_async: 32 x 64 KB is ~0.5 s at
// 2.048 MS/s, slack for slow consumers (WSL2 usbipd). librtlsdr defaults to 15.
static constexpr uint32_t kStreamBuffers = 32;

RtlSdrDevice::RtlSdrDevice(uint32_t index, size_t pool_capacity)
    : m_index(index), m_fft_size(pool_capacity) {
  m_frame_pool =
      std::make_shared<FramePool>(m_fft_size, kDeviceFramePoolFrames);
}

auto RtlSdrDevice::open() -> bool {
  if (m_dev != nullptr) {
    return true;
  }

  if (rtlsdr_open(&m_dev, m_index) < 0) {
    return false;
  }

  // librtlsdr's init order
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
  stop();
  close();
}

// R820T tuning range, or the ADC's own 0-14.4 MHz (Nyquist of the 28.8 MHz
// crystal) once direct sampling is on.
auto RtlSdrDevice::limits() const noexcept -> TuningLimits {
  if (m_direct_sampling) {
    return {0, 14'400'000, 0.0F, 49.6F};
  }
  return {500'000, 1'700'000'000, 0.0F, 49.6F};
}

void RtlSdrDevice::set_frequency(uint64_t freq_hz) {
  // librtlsdr tunes in 32 bits; limits() keeps callers far below that.
  m_center_freq = static_cast<uint32_t>(
      std::min<uint64_t>(freq_hz, std::numeric_limits<uint32_t>::max()));
  if (m_dev != nullptr) {
    rtlsdr_set_center_freq(m_dev, m_center_freq);
  }
}

void RtlSdrDevice::set_sample_rate(uint32_t rate_hz) {
  m_sample_rate = rate_hz;
  if (m_dev != nullptr) {
    rtlsdr_set_sample_rate(m_dev, rate_hz);
  }
}

void RtlSdrDevice::set_gain(float gain_db) {
  m_gain = gain_db;
  if (m_dev != nullptr) {
    rtlsdr_set_tuner_gain_mode(m_dev, 1); // manual
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
  m_direct_sampling = mode != 0;
  LOG_INFO("Direct sampling mode " + std::to_string(mode) +
           (mode == 2   ? " (Q-branch)"
            : mode == 1 ? " (I-branch)"
                        : " (off)"));
  return true;
}

void RtlSdrDevice::start(size_t chunk_size) {
  if (m_dev == nullptr || m_streaming) {
    return;
  }
  if (m_fft_size != chunk_size) {
    m_fft_size = chunk_size;
    m_frame_pool =
        std::make_shared<FramePool>(m_fft_size, kDeviceFramePoolFrames);
  }

  rtlsdr_reset_buffer(m_dev);

  // rtlsdr_read_async blocks until cancelled, hence the thread.
  m_thread_running = true;
  m_async_thread = std::thread([this]() {
    // Smaller than librtlsdr's 262144 B: at 2.048 MS/s that is a callback
    // every ~64 ms, which capped the waterfall at ~15 lines/s. 64 KB gives
    // ~16 ms. A multiple of 512 (USB packet size).
    uint32_t buf_len = 65536;
    LOG_INFO("Starting RTL-SDR async thread...");
    int ret = rtlsdr_read_async(m_dev, static_callback, this, kStreamBuffers,
                                buf_len);
    // A negative return is the expected shutdown: stop() clears
    // m_thread_running, then cancels.
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
  LOG_INFO("RTL-SDR async streaming started with " +
           std::to_string(kStreamBuffers) + " buffers.");
}

void RtlSdrDevice::stop() {
  if (m_dev == nullptr || !m_streaming) {
    return;
  }

  LOG_INFO("Stopping RTL-SDR async streaming...");
  m_thread_running = false;

  rtlsdr_cancel_async(m_dev);
  if (m_async_thread.joinable()) {
    m_async_thread.join();
  }
  m_streaming = false;
  LOG_INFO("RTL-SDR async streaming stopped.");
}

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

  // A transfer is larger than a pool frame (FFT size): emit it as a run of
  // frames. One frame per transfer would look fine on screen but keep only a
  // fraction of a capture.
  size_t offset = 0;
  while (offset < count) {
    FrameHandle frame_handle = m_frame_pool->acquire();
    if (!frame_handle) {
      // The pool grows on demand, so this is an allocation failure. Count the
      // rest of the transfer as lost; the capture sidecar reports the total.
      m_dropped.fetch_add(count - offset, std::memory_order_relaxed);
      LOG_ERROR("RTL-SDR: no frame available, dropped " +
                std::to_string(count - offset) + " samples");
      return;
    }

    size_t const chunk = std::min(count - offset, frame_handle.capacity());
    frame_handle.resize(chunk);

    // complex<float> is layout-compatible with float[2].
    convert_iq_u8_to_f32(buf + offset * 2,
                         reinterpret_cast<float *>(frame_handle.data()),
                         chunk * 2);

    m_frame_callback(std::move(frame_handle));
    offset += chunk;
  }
}

} // namespace openspectrum
