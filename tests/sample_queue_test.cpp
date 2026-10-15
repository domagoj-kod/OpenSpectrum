// SPDX-License-Identifier: GPL-3.0-or-later
// The accumulator: deliveries of any length come out as full frames of
// consecutive samples, and the render loop gets only the newest.

#undef NDEBUG // these are asserts; they must not compile out
#include "frame_pool.h"
#include "logger.h"
#include "sample_queue.h"

#include <cassert>
#include <chrono>
#include <cstddef>
#include <initializer_list>
#include <memory>
#include <utility>

using namespace openspectrum;

constexpr std::chrono::milliseconds kNoWait{0};

// Feeds deliveries numbered 0, 1, 2, ... and returns how many frames came out,
// checking that each is a full frame of consecutive samples. `whole` says
// nothing may be dropped, so the frames must also follow one another.
static auto run(size_t fft_size, std::initializer_list<size_t> deliveries,
                bool whole) -> size_t {
  SampleQueue queue(fft_size);
  auto const source_pool = std::make_shared<FramePool>(fft_size, 1);
  size_t frames = 0;
  float next_in = 0.0F;
  float next_out = 0.0F;
  for (size_t const n : deliveries) {
    FrameHandle in = source_pool->acquire();
    in.resize(n);
    for (size_t k = 0; k < n; ++k) {
      in[k] = {next_in, 0.0F};
      next_in += 1.0F;
    }
    queue.push(std::move(in));
    FrameHandle const out = queue.pop_newest(kNoWait);
    if (!out) {
      continue;
    }
    ++frames;
    assert(out.size() == fft_size);
    assert(!whole || out[0].real() == next_out);
    for (size_t k = 1; k < fft_size; ++k) {
      assert(out[k].real() == out[k - 1].real() + 1.0F);
    }
    next_out = out[fft_size - 1].real() + 1.0F;
  }
  return frames;
}

auto main() -> int {
  Logger::get_instance().set_level(LogLevel::ERROR);
  // Whole frames, and halves (an RTL-SDR at FFT 65536): nothing is dropped.
  assert(run(16, {16, 16, 16}, true) == 3);
  assert(run(16, {8, 8, 8, 8, 8, 8}, true) == 3);
  // One short delivery (a USRP after an overflow): every later one still
  // completes a frame.
  assert(run(16, {11, 16, 16, 16, 16}, false) == 4);
  // Every delivery short (a sustained overflow): frames still come out.
  assert(run(16, {11, 11, 11, 11, 11, 11}, false) == 3);

  // A backlog: only the newest frame comes back, then nothing.
  SampleQueue queue(4);
  auto const source_pool = std::make_shared<FramePool>(4, 1);
  for (float first : {0.0F, 4.0F, 8.0F}) {
    FrameHandle in = source_pool->acquire();
    in.resize(4);
    for (size_t k = 0; k < 4; ++k) {
      in[k] = {first + static_cast<float>(k), 0.0F};
    }
    queue.push(std::move(in));
  }
  FrameHandle const newest = queue.pop_newest(kNoWait);
  assert(newest && newest[0].real() == 8.0F);
  assert(!queue.pop_newest(kNoWait));
}
