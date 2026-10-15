// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "utils/frame_pool.h"

#include <chrono>
#include <condition_variable>
#include <cstddef>
#include <memory>
#include <mutex>
#include <queue>

namespace openspectrum {

// Frames from the producer thread to the render loop. Deliveries of any length
// are gathered into FFT-sized frames from a pool, so the hot path allocates
// nothing; the render loop takes only the newest frame.
class SampleQueue {
public:
  // Queued frames before the producer drops the oldest. The render loop takes
  // only the newest, so this just absorbs scheduling jitter. Captures tap the
  // stream before the queue and are not affected.
  static constexpr size_t MAX_SAMPLE_QUEUE_SIZE = 8;

  // Peak in flight: the queue + accumulator + the frame the render loop holds.
  // The pool grows on demand past this.
  static constexpr size_t MAIN_FRAME_POOL_FRAMES = 12;

  explicit SampleQueue(size_t fft_size);

  // Producer thread: adds a delivery, and queues the frame it completes.
  void push(FrameHandle samples);

  // Render thread: the newest complete frame, waiting up to `timeout` for
  // one; empty if none came. Older frames are dropped.
  auto pop_newest(std::chrono::milliseconds timeout) -> FrameHandle;

  // Drops every queued frame (after a retune they show the old channel).
  void clear();

  // A new FFT size: drops everything and replaces the pool. Only while the
  // producer is stopped.
  void reconfigure(size_t fft_size);

private:
  // Declared first so it is destroyed last. shared_ptr: FrameHandles hold a
  // weak_ptr to it.
  std::shared_ptr<FramePool> m_pool;
  size_t m_fft_size;

  std::queue<FrameHandle> m_queue;
  std::mutex m_queue_mutex;
  std::condition_variable m_cv;

  // Collects deliveries shorter than the FFT size into one frame.
  FrameHandle m_accumulator;
  std::mutex m_accumulator_mutex;

  // How many deliveries have been logged; only the first few are.
  size_t m_log_count = 0;
};

} // namespace openspectrum
