// SPDX-License-Identifier: MIT
#pragma once

#include <SDL2/SDL.h>

#include "openspectrum/control_state.h"

namespace openspectrum {

// SDL-specific keyboard input handler for control state
// This class provides keyboard-based control of ControlState
class SdlControlInput {
public:
  explicit SdlControlInput(ControlState &state);
  ~SdlControlInput() = default;

  // Non-copyable, non-movable
  SdlControlInput(const SdlControlInput &) = delete;
  SdlControlInput &operator=(const SdlControlInput &) = delete;

  // Handle SDL keyboard event, returns true if state changed
  bool handle_keyboard(SDL_Keycode key, bool shift_held, bool ctrl_held);

  // Set step sizes (for testing or customization)
  void set_frequency_step(uint32_t step) noexcept { freq_step = step; }
  void set_gain_step(float step) noexcept { gain_step = step; }

private:
  ControlState &m_state;

  // Step sizes (adjustable with modifiers)
  uint32_t freq_step = 1000000; // 1 MHz default
  float gain_step = 1.0f;       // 1 dB default
};

} // namespace openspectrum
