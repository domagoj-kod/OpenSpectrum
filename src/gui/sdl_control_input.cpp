// SPDX-License-Identifier: GPL-3.0-or-later

#include "sdl_control_input.h"
#include "format.h"
#include "logger.h"
#include "openspectrum/control_state.h"
#include "signal_processor.h"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <string>

#include <SDL3/SDL_keycode.h>

namespace openspectrum {

auto handle_control_key(ControlState &state, SDL_Keycode key, bool shift_held,
                        bool ctrl_held) -> bool {
  // Step sizes, adjusted by modifiers
  uint32_t current_freq_step = 1000000; // 1 MHz default
  float current_gain_step = 1.0F;       // 1 dB default

  if (shift_held) {
    // Fine control
    current_freq_step = 100000; // 0.1 MHz
    current_gain_step = 0.1F;   // 0.1 dB
  } else if (ctrl_held) {
    // Coarse control
    current_freq_step = 10000000; // 10 MHz
    current_gain_step = 10.0F;    // 10 dB
  }

  bool changed = false;
  auto const &constraints = state.get_constraints();

  switch (key) {
  // Frequency controls
  case SDLK_PLUS:
  case SDLK_EQUALS: {
    uint32_t const prev = state.get_frequency();
    uint32_t const new_freq =
        std::min(state.get_frequency() + current_freq_step,
                 constraints.max_frequency_hz);
    if (new_freq != prev) {
      state.set_frequency(new_freq);
      LOG_INFO("[FREQ+] " + format_frequency(prev) + " -> " +
               format_frequency(state.get_frequency()));
      changed = true;
      state.mark_status_dirty();
    }
    break;
  }

  case SDLK_MINUS:
  case SDLK_UNDERSCORE: {
    uint32_t const prev = state.get_frequency();
    uint32_t const new_freq =
        std::max(state.get_frequency() - current_freq_step,
                 constraints.min_frequency_hz);
    if (new_freq != prev) {
      state.set_frequency(new_freq);
      LOG_INFO("[FREQ-] " + format_frequency(prev) + " -> " +
               format_frequency(state.get_frequency()));
      changed = true;
      state.mark_status_dirty();
    }
    break;
  }

  // Gain controls
  case SDLK_R: {
    float const prev = state.get_gain();
    float const new_gain =
        std::min(state.get_gain() + current_gain_step, constraints.max_gain_db);
    if (new_gain != prev) {
      state.set_gain(new_gain);
      LOG_INFO("[GAIN+] " + ControlState::format_gain(prev) + " -> " +
               ControlState::format_gain(state.get_gain()));
      changed = true;
      state.mark_status_dirty();
    }
    break;
  }

  case SDLK_F: {
    float const prev = state.get_gain();
    float const new_gain =
        std::max(state.get_gain() - current_gain_step, constraints.min_gain_db);
    if (new_gain != prev) {
      state.set_gain(new_gain);
      LOG_INFO("[GAIN-] " + ControlState::format_gain(prev) + " -> " +
               ControlState::format_gain(state.get_gain()));
      changed = true;
      state.mark_status_dirty();
    }
    break;
  }

  // FFT size controls (1-5 keys select a supported size)
  case SDLK_1:
  case SDLK_2:
  case SDLK_3:
  case SDLK_4:
  case SDLK_5: {
    int const index = key - SDLK_1;
    if (index < static_cast<int>(constraints.supported_fft_sizes.size())) {
      size_t const new_size = constraints.supported_fft_sizes[index];
      size_t const old_size = state.get_fft_size();
      if (new_size != old_size) {
        state.set_fft_size(new_size);
        LOG_INFO("[FFT] " + std::to_string(old_size) + " -> " +
                 std::to_string(new_size));
        changed = true;
        state.mark_status_dirty();
      }
    }
    break;
  }

  // Color palette: 'c' cycles forward, Shift+C cycles backward.
  case SDLK_C: {
    state.cycle_palette(shift_held ? -1 : 1);
    LOG_INFO(std::string("[PALETTE] ") +
             ControlState::palette_name(state.get_palette_index()));
    changed = true;
    break;
  }

  // Window function controls (UP/DOWN arrows cycle through supported windows)
  case SDLK_UP:
  case SDLK_DOWN: {
    int const direction = (key == SDLK_UP) ? 1 : -1;
    auto const &windows = constraints.supported_window_functions;
    if (windows.empty()) {
      break;
    }

    int current_index = -1;
    WindowFunction const current = state.get_window();
    for (size_t i = 0; i < windows.size(); ++i) {
      if (windows[i] == current) {
        current_index = static_cast<int>(i);
        break;
      }
    }

    int new_index = current_index + direction;
    if (new_index < 0) {
      new_index = static_cast<int>(windows.size()) - 1;
    } else if (new_index >= static_cast<int>(windows.size())) {
      new_index = 0;
    }

    if (new_index >= 0 && new_index < static_cast<int>(windows.size())) {
      WindowFunction const new_window = windows[new_index];
      if (new_window != current) {
        state.set_window(new_window);
        LOG_INFO(
            "[WINDOW] " +
            std::string(SignalProcessor::window_function_to_string(current)) +
            " -> " +
            std::string(
                SignalProcessor::window_function_to_string(new_window)));
        changed = true;
        state.mark_status_dirty();
      }
    }
    break;
  }

  // IQ logging toggle (Ctrl+S)
  case SDLK_S:
    if (ctrl_held && !shift_held) {
      state.request_iq_logging_toggle();
      LOG_INFO("IQ logging toggle requested");
      changed = true;
      state.mark_status_dirty();
    }
    break;

  // Frame-timing overlay toggle (T key - debug)
  case SDLK_T:
    if (!ctrl_held && !shift_held) {
      state.toggle_timing_overlay();
      LOG_INFO(std::string("Frame-timing overlay ") +
               (state.timing_overlay_enabled() ? "ON" : "OFF"));
      changed = true;
    }
    break;

  // Spectrogram export (e key - instant export)
  case SDLK_E:
    if (!ctrl_held && !shift_held) {
      state.request_spectrogram_export();
      LOG_INFO("Spectrogram export requested");
      changed = true;
      state.mark_status_dirty();
    }
    break;

  // Amplitude-trigger resume (Space): unfreeze the display and re-arm. No-op
  // unless main() is currently frozen.
  case SDLK_SPACE:
    if (!ctrl_held && !shift_held) {
      state.request_unfreeze();
      changed = true;
    }
    break;

  // Max-hold trace toggle (m key)
  case SDLK_M:
    if (!ctrl_held && !shift_held) {
      state.toggle_max_hold();
      LOG_INFO(std::string("[TRACE] Max-hold ") +
               (state.max_hold_enabled() ? "ON" : "OFF"));
      changed = true;
    }
    break;

  // Video averaging toggle (a key)
  case SDLK_A:
    if (!ctrl_held && !shift_held) {
      state.toggle_averaging();
      LOG_INFO(std::string("[TRACE] Averaging ") +
               (state.averaging_enabled() ? "ON" : "OFF"));
      changed = true;
    }
    break;

  // Reset traces (x key): clears the max-hold peaks and re-seeds the average
  case SDLK_X:
    if (!ctrl_held && !shift_held) {
      state.request_trace_reset();
      LOG_INFO("[TRACE] Reset");
      changed = true;
    }
    break;
  }

  return changed;
}

} // namespace openspectrum
