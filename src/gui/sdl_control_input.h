// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "control_state.h"

#include <SDL3/SDL.h>

namespace openspectrum {

// True if any control value changed.
bool handle_control_key(ControlState &state, SDL_Keycode key, bool shift_held,
                        bool ctrl_held);

} // namespace openspectrum
