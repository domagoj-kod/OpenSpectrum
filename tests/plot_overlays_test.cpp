// SPDX-License-Identifier: GPL-3.0-or-later
// Markers from clicks, and the trigger's y <-> dB mapping.

#undef NDEBUG // these are asserts; they must not compile out
#include "gui/plot_overlays.h"

#include <cassert>
#include <cmath>
#include <vector>

using namespace openspectrum;
using Click = SdlRenderer::ClickEvent;

auto main() -> int {
  // 100 MHz centre, 2 MHz span over 1000 px: 2 kHz per pixel.
  const PlotSpan span{100e6, 2e6, 1000.0F};
  assert(span.freq_at(0.0F) == 99e6 && span.freq_at(500.0F) == 100e6);
  assert(span.xrel(span.freq_at(250.0F)) == 0.25);

  Markers markers;
  const float plot_h = 600.0F;
  // Left clicks drop; ones outside the plot are ignored.
  markers.apply({{100.0F, 10.0F, false},
                 {700.0F, 590.0F, false},
                 {-1.0F, 10.0F, false},
                 {500.0F, 600.0F, false}},
                false, span, plot_h);
  assert(markers.freqs().size() == 2 && markers.freqs()[0] == 99.2e6);
  // Right click removes the nearest within 12 px, nothing farther away.
  markers.apply({{690.0F, 10.0F, true}}, false, span, plot_h);
  assert(markers.freqs().size() == 1 && markers.freqs()[0] == 99.2e6);
  markers.apply({{120.0F, 10.0F, true}}, false, span, plot_h);
  assert(markers.freqs().size() == 1);
  // A marker tuned out of the span cannot be removed by a click.
  const PlotSpan away{200e6, 2e6, 1000.0F};
  markers.apply({{0.0F, 10.0F, true}}, false, away, plot_h);
  assert(markers.freqs().size() == 1);
  // At most kMaxMarkers; Delete clears before the frame's clicks.
  std::vector<Click> many(Markers::kMaxMarkers + 3, Click{10.0F, 10.0F, false});
  markers.apply(many, false, span, plot_h);
  assert(markers.freqs().size() == Markers::kMaxMarkers);
  markers.apply({{10.0F, 10.0F, false}}, true, span, plot_h);
  assert(markers.freqs().size() == 1);

  // Trigger: a y in the pane maps to a dB and back to the same y.
  Trigger trigger;
  const float pane_h = 300.0F;
  trigger.set_from_y(75.0F, pane_h, -100.0F, 0.0F);
  assert(trigger.armed && trigger.threshold_db == -25.0F);
  assert(std::fabs(trigger.line_y(pane_h, -100.0F, 0.0F) - 75.0F) < 1e-3F);
  // The line follows an autoscaled range, clamped to the pane.
  assert(trigger.line_y(pane_h, -50.0F, -30.0F) == 0.0F);
  // Fires at or above the threshold, and stays frozen until resumed.
  trigger.check(-26.0F);
  assert(!trigger.frozen);
  trigger.check(-25.0F);
  assert(trigger.frozen);
  trigger.check(-90.0F);
  assert(trigger.frozen);
  // Dragged onto the bottom edge: disarmed, threshold kept.
  trigger.frozen = false;
  trigger.set_from_y(pane_h - 1.0F, pane_h, -100.0F, 0.0F);
  assert(!trigger.armed && trigger.threshold_db == -25.0F);
  trigger.check(0.0F);
  assert(!trigger.frozen);
}
