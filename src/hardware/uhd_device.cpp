// SPDX-License-Identifier: GPL-3.0-or-later

#include "uhd_device.h"
#include "logger.h"

#include <cassert>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <uhd/exception.hpp>
#include <uhd/stream.hpp>
#include <uhd/types/metadata.hpp>
#include <uhd/types/stream_cmd.hpp>
#include <uhd/types/tune_request.hpp>
#include <vector>

// Same reasoning as RtlSdrDevice: one frame is in flight at a time.
static constexpr size_t kDeviceFramePoolFrames = 4;

// The samples are taken from UHD as int16 and scaled here, by a power of two,
// rather than asking UHD for floats. The capture writer turns a float back
// into cs16 as x * 32768, rounded; that returns the receiver's own integer only
// if the scale on the way in was exactly 1/32768, and the scale UHD applies is
// its own business. open() asserts the round trip in debug builds.
static void convert_iq_s16_to_f32(const int16_t *__restrict src,
                                  float *__restrict dst, size_t n) {
  for (size_t k = 0; k < n; ++k) {
    dst[k] = static_cast<float>(src[k]) * (1.0F / 32768.0F);
  }
}

UhdDevice::~UhdDevice() { stop(); }

auto UhdDevice::open() -> bool {
#ifndef NDEBUG
  // The round trip the comment above promises, over every int16.
  for (int v = -32768; v <= 32767; ++v) {
    assert(std::lrintf(static_cast<float>(v) * (1.0F / 32768.0F) * 32768.0F) ==
           v);
  }
#endif
  try {
    // Not an empty address: that also searches the network for X-series
    // devices, and that search never returned on a WSL2 host with nothing
    // attached. Another USRP family needs its type here.
    m_usrp = uhd::usrp::multi_usrp::make(std::string("type=b200"));
    m_name = "USRP " + m_usrp->get_mboard_name();
    const uhd::freq_range_t freq = m_usrp->get_rx_freq_range();
    const uhd::gain_range_t gain = m_usrp->get_rx_gain_range();
    m_limits = {
        static_cast<uint64_t>(freq.start()), static_cast<uint64_t>(freq.stop()),
        static_cast<float>(gain.start()), static_cast<float>(gain.stop())};
    LOG_INFO("Opened " + m_name + ", RX antenna " + m_usrp->get_rx_antenna());
    return true;
  } catch (const uhd::exception &e) {
    LOG_ERROR(std::string("UHD: ") + e.what());
    return false;
  }
}

auto UhdDevice::set_sample_rate(uint32_t rate_hz) -> bool {
  try {
    m_usrp->set_rx_rate(rate_hz);
    m_usrp->set_rx_bandwidth(rate_hz);
    m_rate = m_usrp->get_rx_rate();
  } catch (const uhd::exception &e) {
    LOG_ERROR(std::string("UHD: ") + e.what());
    return false;
  }
  if (std::abs(m_rate - rate_hz) > 0.5) {
    LOG_ERROR("UHD: asked for " + std::to_string(rate_hz) +
              " S/s, the device gives " + std::to_string(m_rate));
    return false;
  }
  return true;
}

void UhdDevice::set_frequency(uint64_t freq_hz) {
  try {
    m_usrp->set_rx_freq(uhd::tune_request_t(static_cast<double>(freq_hz)));
  } catch (const uhd::exception &e) {
    LOG_ERROR(std::string("UHD: ") + e.what());
  }
}

void UhdDevice::set_gain(float gain_db) {
  try {
    m_usrp->set_rx_gain(gain_db);
  } catch (const uhd::exception &e) {
    LOG_ERROR(std::string("UHD: ") + e.what());
  }
}

void UhdDevice::start(size_t chunk_size) {
  if (!m_usrp || m_running) {
    return;
  }
  m_frame_pool = std::make_shared<openspectrum::FramePool>(
      chunk_size, kDeviceFramePoolFrames);
  m_running = true;
  m_thread = std::thread([this, chunk_size]() { receive_loop(chunk_size); });
}

void UhdDevice::stop() {
  m_running = false;
  if (m_thread.joinable()) {
    m_thread.join();
  }
}

void UhdDevice::receive_loop(size_t chunk_size) {
  uint64_t received = 0;
  try {
    // A new streamer on every start: one left over from before a stop would
    // deliver stale packets, and the gap count below would call the pause a
    // loss.
    uhd::stream_args_t args("sc16", "sc16");
    const uhd::rx_streamer::sptr stream = m_usrp->get_rx_stream(args);
    std::vector<int16_t> raw(chunk_size * 2);

    uhd::stream_cmd_t cmd(uhd::stream_cmd_t::STREAM_MODE_START_CONTINUOUS);
    cmd.stream_now = true;
    stream->issue_stream_cmd(cmd);

    // Every packet carries the device's sample clock. The next packet must
    // start where the last one ended; whatever is missing between them was
    // lost, whether UHD flagged an overflow or not. This counts samples, not
    // overflow events.
    bool have_expected = false;
    long long expected = 0;
    int timeouts = 0;
    uhd::rx_metadata_t md;

    while (m_running) {
      const size_t n = stream->recv(raw.data(), chunk_size, md, 0.1);
      if (md.error_code == uhd::rx_metadata_t::ERROR_CODE_TIMEOUT) {
        if (++timeouts == 10) {
          LOG_ERROR("UHD: no samples for 1 s (device unplugged?)");
        }
        continue;
      }
      timeouts = 0;
      if (md.error_code != uhd::rx_metadata_t::ERROR_CODE_NONE &&
          md.error_code != uhd::rx_metadata_t::ERROR_CODE_OVERFLOW) {
        LOG_ERROR("UHD: receive error " + md.strerror());
      }
      if (n == 0) {
        continue;
      }
      if (md.has_time_spec) {
        const long long first = md.time_spec.to_ticks(m_rate);
        if (have_expected && first > expected) {
          m_dropped.fetch_add(static_cast<uint64_t>(first - expected),
                              std::memory_order_relaxed);
        }
        expected = first + static_cast<long long>(n);
        have_expected = true;
      }

      openspectrum::FrameHandle frame = m_frame_pool->acquire();
      if (!frame) {
        m_dropped.fetch_add(n, std::memory_order_relaxed);
        LOG_ERROR("UHD: no frame available, dropped " + std::to_string(n) +
                  " samples");
        continue;
      }
      frame.resize(n);
      convert_iq_s16_to_f32(raw.data(), reinterpret_cast<float *>(frame.data()),
                            n * 2);
      received += n;
      if (m_frame_callback) {
        m_frame_callback(std::move(frame));
      }
    }
    stream->issue_stream_cmd(
        uhd::stream_cmd_t(uhd::stream_cmd_t::STREAM_MODE_STOP_CONTINUOUS));
  } catch (const std::exception &e) {
    LOG_ERROR(std::string("UHD: receive thread stopped: ") + e.what());
  }
  LOG_INFO("UHD: received " + std::to_string(received) + " samples, lost " +
           std::to_string(m_dropped.load()) + " since open");
}
