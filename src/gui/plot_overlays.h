// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "gui/sdl_renderer.h"
#include "visualization/spectrum_display.h"
#include "visualization/waterfall_display.h"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <vector>

namespace openspectrum {

// Frequency markers, cursor readout and amplitude trigger: each keeps its own
// state and hands the renderer what to draw; the renderer only captures input.

// The tuned span: frequency <-> render-space x.
struct PlotSpan {
  double center_hz = 0.0;
  double rate_hz = 0.0;
  float width = 0.0F; // plot width in render pixels

  [[nodiscard]] double left_hz() const { return center_hz - rate_hz / 2.0; }
  [[nodiscard]] double freq_at(float x) const {
    return left_hz() +
           (static_cast<double>(x) / static_cast<double>(width)) * rate_hz;
  }
  // 0 at the left edge, 1 at the right; outside [0, 1) is off screen.
  [[nodiscard]] double xrel(double hz) const {
    return (hz - left_hz()) / rate_hz;
  }
  // Ctrl+left-click target: to 1 kHz (a pixel is wider), never below 0 Hz.
  [[nodiscard]] uint64_t tune_at(float x) const {
    return static_cast<uint64_t>(std::max(0.0, std::round(freq_at(x) / 1e3))) *
           1000U;
  }
};

// Markers anchored to absolute frequency: they slide with retuning and hide
// when tuned out. Left-click drops, right-click removes the nearest, Delete
// clears all.
class Markers {
public:
  // Fits the panel list in every state.
  static constexpr size_t kMaxMarkers = 14;
  static constexpr double kRemoveRadiusPx = 12.0;

  // Applies this frame's clicks; ones outside the plot are ignored.
  void apply(const std::vector<SdlRenderer::ClickEvent> &clicks, bool clear,
             const PlotSpan &span, float plot_h);

  // The markers as drawn, each on-screen one with the spectrum's level there.
  [[nodiscard]] std::vector<SdlRenderer::Marker>
  draw_list(const PlotSpan &span, const SpectrumDisplay &spectrum) const;

  [[nodiscard]] const std::vector<double> &freqs() const { return m_hz; }

private:
  std::vector<double> m_hz;
};

// Cursor readout: over the spectrum the trace's dB at the cursor's frequency
// (mouse y ignored); over the waterfall the age of the hovered line.
class CursorTracker {
public:
  [[nodiscard]] SdlRenderer::CursorReadout
  update(bool active, float cx, float cy, const PlotSpan &span,
         const SpectrumDisplay &spectrum, const WaterfallDisplay &waterfall);

private:
  // The displayed dB is EMA'd so the digits do not churn; it snaps on a new
  // bin or re-entry.
  static constexpr double kEmaAlpha = 0.2;
  double m_db_ema = 0.0;
  size_t m_db_bin = 0;
  bool m_db_valid = false;
};

// Shift+left-drag in the spectrum pane sets a dB threshold and arms it; a peak
// at or above it freezes the display until Space resumes. The line's y follows
// the autoscaled range.
struct Trigger {
  static constexpr float kDisarmPx = 4.0F; // a drag this close to the bottom

  float threshold_db = -40.0F;
  bool armed = false;
  bool frozen = false;

  // A threshold from a pointer y in the spectrum pane (0 = top = db_max).
  void set_from_y(float y, float pane_h, float db_min, float db_max);
  // The threshold's y in the pane for the current range.
  [[nodiscard]] float line_y(float pane_h, float db_min, float db_max) const;
  // Freezing halts the stream, so a level test is enough.
  void check(float peak_db) {
    if (armed && peak_db >= threshold_db) {
      frozen = true;
    }
  }
};

} // namespace openspectrum
