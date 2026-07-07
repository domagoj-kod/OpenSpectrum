// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "openspectrum/control_state.h"

#include <SDL3/SDL.h>

namespace openspectrum {

// Apply an SDL keyboard event to the control state. Returns true if any
// control value changed (caller repaints the status bar).
bool handle_control_key(ControlState &state, SDL_Keycode key, bool shift_held,
                        bool ctrl_held);

} // namespace openspectrum
