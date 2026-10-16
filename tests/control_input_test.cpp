// SPDX-License-Identifier: GPL-3.0-or-later
// Pins the span-relative tuning steps: one span, Shift 0.1 MHz, Ctrl 10 spans.

#undef NDEBUG // these are asserts; they must not compile out
#include "control_state.h"
#include "gui/sdl_control_input.h"

#include <cassert>

using openspectrum::ControlState;
using openspectrum::DeviceConstraints;
using openspectrum::handle_control_key;

auto main() -> int {
  ControlState state;
  DeviceConstraints c;
  c.min_frequency_hz = 70'000'000;
  c.max_frequency_hz = 6'000'000'000;
  c.span_hz = 20'000'000;
  state.set_constraints(c);
  state.set_frequency(100'000'000);

  handle_control_key(state, SDLK_PLUS, false, false);
  assert(state.get_frequency() == 120'000'000); // one span
  handle_control_key(state, SDLK_PLUS, true, false);
  assert(state.get_frequency() == 120'100'000); // Shift: 0.1 MHz
  handle_control_key(state, SDLK_PLUS, false, true);
  assert(state.get_frequency() == 320'100'000); // Ctrl: ten spans

  handle_control_key(state, SDLK_MINUS, false, true);
  handle_control_key(state, SDLK_MINUS, false, true);
  assert(state.get_frequency() == 70'000'000); // clamped, no unsigned wrap

  state.set_frequency(5'990'000'000);
  handle_control_key(state, SDLK_PLUS, false, false);
  assert(state.get_frequency() == 6'000'000'000); // clamped at the top
}
