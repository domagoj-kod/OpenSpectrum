// SPDX-License-Identifier: GPL-3.0-or-later

#include "utils/sample_queue.h"

#include "utils/logger.h"

#include <algorithm>
#include <string>
#include <utility>

namespace openspectrum {

SampleQueue::SampleQueue(size_t fft_size)
    : m_pool(std::make_shared<FramePool>(fft_size, MAIN_FRAME_POOL_FRAMES)),
      m_fft_size(fft_size) {}

void SampleQueue::push(FrameHandle samples) {
  if (!samples) {
    return;
  }
  std::unique_lock<std::mutex> acc_lock(m_accumulator_mutex);

  if (!m_accumulator) {
    m_accumulator = m_pool->acquire();
    if (!m_accumulator) {
      return;
    }
    m_accumulator.reset();
  }

  // Add new samples to the accumulator, which holds one frame (its capacity is
  // the FFT size). Sources deliver whole frames or exact fractions of one, so
  // normally everything fits. A USRP delivers a short frame after an overflow,
  // and from then on what is held plus the next frame is more than one frame:
  // take what completes the frame and drop the rest. That costs the display a
  // few samples next to a gap it already has; captures are tapped before this
  // and lose nothing. (Dropping the held part instead never completed a frame
  // while every delivery came short, which a sustained overflow does.)
  size_t const samples_count = samples.size();
  size_t const current_accum_size = m_accumulator.size();
  size_t const take =
      std::min(samples_count, m_accumulator.capacity() - current_accum_size);
  std::copy_n(samples.data(), take, m_accumulator.data() + current_accum_size);
  m_accumulator.resize(current_accum_size + take);

  // Log first few deliveries to verify samples are arriving
  if (m_log_count++ < 5) {
    LOG_INFO(
        "Async callback: received " + std::to_string(samples_count) +
        " samples, total accumulated: " + std::to_string(m_accumulator.size()));
  }

  if (m_accumulator.size() < m_fft_size) {
    return;
  }
  // A full accumulator is the frame: hand it over as it is, and the next
  // delivery starts a fresh one from the pool.
  FrameHandle frame = std::move(m_accumulator);
  acc_lock.unlock();
  {
    std::lock_guard<std::mutex> queue_lock(m_queue_mutex);
    if (m_queue.size() >= MAX_SAMPLE_QUEUE_SIZE) {
      m_queue.pop(); // drop the oldest: the render loop wants the newest
      LOG_DEBUG("Sample queue full, dropping oldest sample");
    }
    m_queue.push(std::move(frame));
  }
  m_cv.notify_one();
}

auto SampleQueue::pop_newest(std::chrono::milliseconds timeout) -> FrameHandle {
  std::unique_lock<std::mutex> lock(m_queue_mutex);
  m_cv.wait_for(lock, timeout, [this] { return !m_queue.empty(); });
  // Throttle: at high sample rates the producer can queue frames faster than
  // the render loop consumes them. Keep only the newest — older frames are
  // latency the user cannot perceive on a 60 Hz display. Each move-assignment
  // releases the previously held FrameHandle back to the pool.
  FrameHandle newest;
  while (!m_queue.empty()) {
    newest = std::move(m_queue.front());
    m_queue.pop();
  }
  return newest;
}

void SampleQueue::clear() {
  std::lock_guard<std::mutex> lock(m_queue_mutex);
  m_queue = {}; // the frames go back to the pool
}

void SampleQueue::reconfigure(size_t fft_size) {
  clear();
  {
    std::lock_guard<std::mutex> lock(m_accumulator_mutex);
    m_accumulator = FrameHandle();
  }
  // The old frames are back in the old pool, so it can go.
  m_pool = std::make_shared<FramePool>(fft_size, MAIN_FRAME_POOL_FRAMES);
  m_fft_size = fft_size;
}

} // namespace openspectrum
