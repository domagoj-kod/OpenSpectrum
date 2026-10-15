// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "utils/logger.h"

#include <chrono>
#include <cstdint>
#include <cstdio>
#include <string>

namespace openspectrum {

// Per-second frame timing. Each rendered frame splits into three wall-clock
// phases; once a second their averages go to the panel's PERF block and, with
// the maxima, to a FRAME-TIMING log line. bench.sh and play-bench.ps1 parse
// that line, so keep its format.
//   cpu          : remove_dc + apply_window + FFT + display updates
//   render_build : SDL draw calls + texture upload + render-target switches
//   present      : SDL_RenderPresent (swap/flush)
// Only full frames are measured, since those are what advance the waterfall.
class FrameTiming {
public:
  using clock = std::chrono::steady_clock;

  // The last full second; zero until one has passed.
  struct Averages {
    double fps = 0.0;
    double cpu_ms = 0.0;
    double render_build_ms = 0.0;
    double present_ms = 0.0;
  };

  static auto ms(clock::time_point from, clock::time_point to) -> double {
    return std::chrono::duration<double, std::milli>(to - from).count();
  }

  // One frame. `render_ms` is the whole render call, present included.
  void add(double cpu_ms, double render_ms, double present_ms) {
    double const build_ms = render_ms - present_ms;
    m_cpu.add(cpu_ms);
    m_build.add(build_ms < 0.0 ? 0.0 : build_ms); // clock skew guard
    m_present.add(present_ms);
    ++m_frames;

    double const window_s =
        std::chrono::duration<double>(clock::now() - m_window_start).count();
    if (window_s < 1.0) {
      return;
    }
    double const inv = 1.0 / static_cast<double>(m_frames);
    m_avg = {static_cast<double>(m_frames) / window_s, m_cpu.sum_ms * inv,
             m_build.sum_ms * inv, m_present.sum_ms * inv};
    char line[256];
    std::snprintf(
        line, sizeof(line),
        "FRAME-TIMING fps=%.1f frames=%llu | cpu avg=%.2f max=%.2f | "
        "render_build avg=%.2f max=%.2f | present avg=%.2f max=%.2f (ms)",
        m_avg.fps, static_cast<unsigned long long>(m_frames), m_avg.cpu_ms,
        m_cpu.max_ms, m_avg.render_build_ms, m_build.max_ms, m_avg.present_ms,
        m_present.max_ms);
    LOG_INFO(std::string(line));
    m_cpu = {};
    m_build = {};
    m_present = {};
    m_frames = 0;
    m_window_start = clock::now();
  }

  auto averages() const -> const Averages & { return m_avg; }

private:
  struct PhaseStat {
    double sum_ms = 0.0;
    double max_ms = 0.0;
    void add(double ms) {
      sum_ms += ms;
      if (ms > max_ms) {
        max_ms = ms;
      }
    }
  };

  PhaseStat m_cpu;
  PhaseStat m_build;
  PhaseStat m_present;
  uint64_t m_frames = 0;
  clock::time_point m_window_start = clock::now();
  Averages m_avg;
};

} // namespace openspectrum
