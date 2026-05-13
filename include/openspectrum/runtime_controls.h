// SPDX-License-Identifier: MIT
#pragma once

// Backward compatibility wrapper for RuntimeControls
// This header redirects to the new ControlState class
// The old RuntimeControls class has been split into:
//   - ControlState: SDL-agnostic state management (public API)
//   - SdlControlInput: SDL-specific keyboard input handling (GUI component)
//
// DEPRECATED: Please use ControlState instead. This header will be removed in a future version.

#include "control_state.h"

namespace openspectrum {

// RuntimeControls is now an alias for ControlState
// All state management functionality is preserved
// SDL-specific keyboard handling is moved to SdlControlInput
using RuntimeControls [[deprecated("Use ControlState instead. SDL input handling moved to SdlControlInput.")]] = ControlState;

} // namespace openspectrum
