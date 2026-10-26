// SPDX-License-Identifier: GPL-3.0-or-later
// Pins the span-relative tuning steps: one span, Shift 0.1 MHz, Ctrl 10 spans;
// no capture setting changes while a frozen frame is held; the FFT-size keys
// and the window wrap; frequency, gain and FFT size hold during a capture.

#undef NDEBUG // these are asserts; they must not compile out
#include "control_state.h"
#include "gui/sdl_control_input.h"

#include <cassert>
#include <cstddef>
#include <cstdint>

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

  // Frozen: nothing the held frame was captured, shown or exported with
  // moves; trace keys still work, and all of it returns on resume.
  state.hold_controls(true);
  state.set_frequency(1'000'000'000);
  float const gain = state.get_gain();
  size_t const fft = state.get_fft_size();
  auto const window = state.get_window();
  size_t const palette = state.get_palette_index();
  for (SDL_Keycode const key : {SDLK_PLUS, SDLK_MINUS, SDLK_R, SDLK_F, SDLK_1,
                                SDLK_7, SDLK_UP, SDLK_DOWN, SDLK_C}) {
    assert(!handle_control_key(state, key, false, false));
    assert(state.get_frequency() == 1'000'000'000 && state.get_gain() == gain);
    assert(state.get_fft_size() == fft && !state.fft_size_changed());
    assert(state.get_window() == window &&
           state.get_palette_index() == palette);
  }
  handle_control_key(state, SDLK_M, false, false);
  assert(state.max_hold_enabled());
  state.hold_controls(false);
  handle_control_key(state, SDLK_PLUS, false, false);
  assert(state.get_frequency() == 1'020'000'000);
  handle_control_key(state, SDLK_R, false, false);
  assert(state.get_gain() == gain + 1.0F);

  // Keys 1-7 index the FFT sizes; a key past the list changes nothing.
  handle_control_key(state, SDLK_1, false, false);
  assert(state.get_fft_size() == c.supported_fft_sizes[0]);
  c.supported_fft_sizes = {1024, 2048};
  state.set_constraints(c);
  assert(!handle_control_key(state, SDLK_3, false, false));
  assert(state.get_fft_size() == 1024);

  // Down from the first window wraps to the last.
  state.set_window(c.supported_window_functions.front());
  handle_control_key(state, SDLK_DOWN, false, false);
  assert(state.get_window() == c.supported_window_functions.back());

  // Capturing: what the sidecar states stays put; window and palette do not.
  state.hold_receiver(true);
  state.clear_fft_change_flag(); // left set by the FFT-size keys above
  uint64_t const freq = state.get_frequency();
  float const gain_now = state.get_gain();
  size_t const fft_now = state.get_fft_size();
  for (SDL_Keycode const key :
       {SDLK_PLUS, SDLK_MINUS, SDLK_R, SDLK_F, SDLK_1, SDLK_2, SDLK_7}) {
    assert(!handle_control_key(state, key, false, false));
    assert(state.get_frequency() == freq && state.get_gain() == gain_now);
    assert(state.get_fft_size() == fft_now && !state.fft_size_changed());
  }
  size_t const palette_now = state.get_palette_index();
  assert(handle_control_key(state, SDLK_C, false, false));
  assert(state.get_palette_index() != palette_now);
  assert(handle_control_key(state, SDLK_UP, false, false));
  assert(state.get_window() == c.supported_window_functions.front());
  state.hold_receiver(false);
  handle_control_key(state, SDLK_PLUS, false, false);
  assert(state.get_frequency() > freq);
}
