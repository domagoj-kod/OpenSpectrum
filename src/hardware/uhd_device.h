// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "openspectrum/frame_pool.h"
#include "sample_source.h"

#include <atomic>
#include <cstdint>
#include <memory>
#include <string>
#include <thread>
#include <uhd/usrp/multi_usrp.hpp>

// A B200-series USRP (B200, B210, B200mini, B205mini) through UHD. Built only
// with `make UHD=1`.
// Receives on channel 0 and the device's default RX antenna (RX2 on a B210).
class UhdDevice : public openspectrum::SampleSource {
public:
  ~UhdDevice() override;

  const char *name() const noexcept override { return m_name.c_str(); }
  openspectrum::TuningLimits limits() const noexcept override {
    return m_limits;
  }

  bool open() override;
  void start(size_t chunk_size) override;
  void stop() override;

  void set_frequency(uint64_t freq_hz) override;
  void set_gain(float gain_db) override;
  // After open(), before start(). False if the device cannot produce exactly
  // this rate: a capture labelled with a rate it was not sampled at is wrong.
  bool set_sample_rate(uint32_t rate_hz);

  uint64_t dropped_samples() const noexcept override {
    return m_dropped.load(std::memory_order_relaxed);
  }

private:
  void receive_loop(size_t chunk_size);

  uhd::usrp::multi_usrp::sptr m_usrp;
  std::string m_name = "USRP";
  openspectrum::TuningLimits m_limits;
  double m_rate = 0.0;

  std::shared_ptr<openspectrum::FramePool> m_frame_pool;
  std::atomic<uint64_t> m_dropped{0};
  std::thread m_thread;
  std::atomic<bool> m_running{false};
};
