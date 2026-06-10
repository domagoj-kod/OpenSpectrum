// SPDX-License-Identifier: GPL-3.0-or-later

#include "sdl_control_input.h"
#include "logger.h"
#include "openspectrum/control_state.h"
#include "signal_processor.h"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <string>

#include <SDL3/SDL_keycode.h>

namespace openspectrum {

SdlControlInput::SdlControlInput(ControlState &state) : m_state(state) {}

// Handle keyboard input
auto SdlControlInput::handle_keyboard(SDL_Keycode key, bool shift_held,
                                      bool ctrl_held) -> bool {
  // Adjust step sizes based on modifiers
  uint32_t current_freq_step = freq_step;
  float current_gain_step = gain_step;

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
  auto const &constraints = m_state.get_constraints();

  switch (key) {
  // Frequency controls
  case SDLK_PLUS:
  case SDLK_EQUALS: {
    uint32_t const prev = m_state.get_frequency();
    uint32_t const new_freq =
        std::min(m_state.get_frequency() + current_freq_step,
                 constraints.max_frequency_hz);
    if (new_freq != prev) {
      m_state.set_frequency(new_freq);
      LOG_INFO("[FREQ+] " + ControlState::format_frequency(prev) + " -> " +
               ControlState::format_frequency(m_state.get_frequency()));
      changed = true;
      m_state.mark_status_dirty();
    }
    break;
  }

  case SDLK_MINUS:
  case SDLK_UNDERSCORE: {
    uint32_t const prev = m_state.get_frequency();
    uint32_t const new_freq =
        std::max(m_state.get_frequency() - current_freq_step,
                 constraints.min_frequency_hz);
    if (new_freq != prev) {
      m_state.set_frequency(new_freq);
      LOG_INFO("[FREQ-] " + ControlState::format_frequency(prev) + " -> " +
               ControlState::format_frequency(m_state.get_frequency()));
      changed = true;
      m_state.mark_status_dirty();
    }
    break;
  }

  // Gain controls
  case SDLK_R: {
    float const prev = m_state.get_gain();
    float const new_gain = std::min(m_state.get_gain() + current_gain_step,
                                    constraints.max_gain_db);
    if (new_gain != prev) {
      m_state.set_gain(new_gain);
      LOG_INFO("[GAIN+] " + ControlState::format_gain(prev) + " -> " +
               ControlState::format_gain(m_state.get_gain()));
      changed = true;
      m_state.mark_status_dirty();
    }
    break;
  }

  case SDLK_F: {
    float const prev = m_state.get_gain();
    float const new_gain = std::max(m_state.get_gain() - current_gain_step,
                                    constraints.min_gain_db);
    if (new_gain != prev) {
      m_state.set_gain(new_gain);
      LOG_INFO("[GAIN-] " + ControlState::format_gain(prev) + " -> " +
               ControlState::format_gain(m_state.get_gain()));
      changed = true;
      m_state.mark_status_dirty();
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
      if (new_size != m_state.get_fft_size()) {
        m_state.set_fft_size(new_size);
        LOG_INFO("[FFT] " + std::to_string(m_state.get_fft_size()) + " -> " +
                 std::to_string(new_size));
        changed = true;
        m_state.mark_status_dirty();
      }
    }
    break;
  }

  // Color palette: 'c' cycles forward, Shift+C cycles backward.
  case SDLK_C: {
    m_state.cycle_palette(shift_held ? -1 : 1);
    LOG_INFO(std::string("[PALETTE] ") +
             ControlState::palette_name(m_state.get_palette_index()));
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
    WindowFunction const current = m_state.get_window();
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
        m_state.set_window(new_window);
        LOG_INFO(
            "[WINDOW] " +
            std::string(SignalProcessor::window_function_to_string(current)) +
            " -> " +
            std::string(
                SignalProcessor::window_function_to_string(new_window)));
        changed = true;
        m_state.mark_status_dirty();
      }
    }
    break;
  }

  // IQ logging toggle (Ctrl+S)
  case SDLK_S:
    if (ctrl_held && !shift_held) {
      m_state.request_iq_logging_toggle();
      LOG_INFO("IQ logging toggle requested");
      changed = true;
      m_state.mark_status_dirty();
    }
    break;

  // Frame-timing overlay toggle (T key - debug)
  case SDLK_T:
    if (!ctrl_held && !shift_held) {
      m_state.toggle_timing_overlay();
      LOG_INFO(std::string("Frame-timing overlay ") +
               (m_state.timing_overlay_enabled() ? "ON" : "OFF"));
      changed = true;
    }
    break;

  // Spectrogram export (e key - instant export)
  case SDLK_E:
    if (!ctrl_held && !shift_held) {
      m_state.request_spectrogram_export();
      LOG_INFO("Spectrogram export requested");
      changed = true;
      m_state.mark_status_dirty();
    }
    break;
  }

  return changed;
}

} // namespace openspectrum
