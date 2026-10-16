// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "hardware/sample_source.h"
#include "utils/frame_pool.h"

#include <atomic>
#include <cstdint>
#include <memory>
#include <string>
#include <thread>
#include <uhd/usrp/multi_usrp.hpp>

namespace openspectrum {

// A B200-series USRP (B200, B210, B200mini, B205mini) through UHD. Built only
// with `make UHD=1`.
// Receives on channel 0 and the device's default RX antenna (RX2 on a B210).
class UhdDevice : public SampleSource {
public:
  ~UhdDevice() override;

  const char *name() const noexcept override { return m_name.c_str(); }
  TuningLimits limits() const noexcept override { return m_limits; }

  bool open() override;
  void start(size_t chunk_size) override;
  void stop() override;

  void set_frequency(uint64_t freq_hz) override;
  void set_gain(float gain_db) override;
  bool set_sample_rate(uint32_t rate_hz) override;

  uint64_t dropped_samples() const noexcept override {
    return m_dropped.load(std::memory_order_relaxed);
  }

private:
  void receive_loop(size_t chunk_size);

  uhd::usrp::multi_usrp::sptr m_usrp;
  std::string m_name = "USRP";
  TuningLimits m_limits;
  double m_rate = 0.0;

  std::shared_ptr<FramePool> m_frame_pool;
  std::atomic<uint64_t> m_dropped{0};
  std::thread m_thread;
  std::atomic<bool> m_running{false};
};

} // namespace openspectrum
