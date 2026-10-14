// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "openspectrum/frame_pool.h"

#include <cstddef>
#include <cstdint>
#include <functional>
#include <limits>
#include <utility>

namespace openspectrum {

// Tuning range of a source. ControlState clamps the frequency and gain keys to
// it, so a receiver's range lives with the receiver, not in main().
struct TuningLimits {
  uint64_t min_frequency_hz = 0;
  uint64_t max_frequency_hz = std::numeric_limits<uint64_t>::max();
  float min_gain_db = 0.0F;
  float max_gain_db = 0.0F;
};

// Everything main() needs from a producer of IQ samples. A source owns its
// producer thread and hands the pipeline pooled FrameHandles of at most
// chunk_size samples through the frame callback. Adding a receiver is one
// subclass plus the line in main() that constructs it; see
// docs/TECHNICAL.md, "Adding a new SDR device".
class SampleSource {
public:
  using FrameCallback = std::function<void(FrameHandle)>;

  virtual ~SampleSource() = default;

  // Written to the capture sidecar as device.type.
  virtual const char *name() const noexcept = 0;
  virtual TuningLimits limits() const noexcept = 0;
  // False for sources with no tuner (file playback): main() then pins the
  // frequency and gain to the capture's, since they label new captures.
  virtual bool tunable() const noexcept { return true; }

  virtual bool open() = 0;
  virtual void start(size_t chunk_size) = 0;
  virtual void stop() = 0; // joins the producer thread

  virtual void set_frequency(uint64_t /*freq_hz*/) {}
  virtual void set_gain(float /*gain_db*/) {}

  // Samples the source knows it lost since open(). A source that cannot see
  // its own losses (librtlsdr reports no USB overruns) returns what it can;
  // the sidecar's measured_sample_rate_hz is the check that covers the rest.
  virtual uint64_t dropped_samples() const noexcept { return 0; }

  void set_frame_callback(FrameCallback cb) {
    m_frame_callback = std::move(cb);
  }

protected:
  FrameCallback m_frame_callback;
};

} // namespace openspectrum
