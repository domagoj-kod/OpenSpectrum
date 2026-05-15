# Runtime Controls API (DEPRECATED)

> **⚠️ DEPRECATION NOTICE**
> 
> This module is **deprecated** and has been split into two components:
> 
> - **[ControlState](control_state.md)** - SDL-agnostic state management (RECOMMENDED replacement)
> - `SdlControlInput` - SDL-specific keyboard input handling (internal GUI component)
> 
> **Please use `ControlState` for new code.** The `RuntimeControls` class is now a deprecated type alias that redirects to `ControlState`. Keyboard input handling has been moved to `SdlControlInput` which is used internally by `SdlRenderer::poll_events()`.

Defined in: `include/openspectrum/runtime_controls.h`

The Runtime Controls module **was** the primary interface for real-time user-adjustable parameters. It has been architecturally split to separate platform-independent state management from platform-specific input handling.

---

## Migration Guide

### Quick Migration

| Old Code | New Code |
|----------|----------|
| `RuntimeControls controls;` | `ControlState state;` |
| `controls.handle_keyboard(key, shift, ctrl);` | Use `SdlRenderer::poll_events(&state)` |
| `controls.get_frequency()` | `state.get_frequency()` |
| `controls.set_frequency(hz)` | `state.set_frequency(hz)` |
| `controls.apply_to_device(dev)` | `state.apply_to_device(dev)` |

### Full Example

**Before:**
```cpp
#include "openspectrum/runtime_controls.h"

RuntimeControls controls;
controls.set_frequency(100000000);

// In event loop
if (controls.handle_keyboard(key, shift, ctrl)) {
    controls.apply_to_device(device);
}
```

**After:**
```cpp
#include "openspectrum/control_state.h"
#include "gui/sdl_renderer.h"

ControlState state;
state.set_frequency(100000000);

// In event loop (using SdlRenderer)
SdlRenderer renderer(width, height);
if (!renderer.poll_events(&state)) {
    // quit
}
state.apply_to_device(device);
```

Or with direct keyboard handling:
```cpp
#include "openspectrum/control_state.h"
#include "gui/sdl_control_input.h"

ControlState state;
SdlControlInput input_handler(state);

// In event loop
if (input_handler.handle_keyboard(key, shift, ctrl)) {
    state.apply_to_device(device);
}
```

---

## Namespace

```cpp
namespace openspectrum {
  // All types and classes defined here
}
```

> **Note:** `RuntimeControls` is now defined as:
> ```cpp
> using RuntimeControls [[deprecated("Use ControlState instead. SDL input handling moved to SdlControlInput.")]] = ControlState;
> ```

---

## DeviceConstraints Struct

> [!Note]
> This struct is now defined in `include/openspectrum/control_state.h` (not runtime_controls.h). See [ControlState](control_state.md#deviceconstraints-struct) for the current documentation.

The `DeviceConstraints` struct **was** defined here but has been moved to the [ControlState](control_state.md) module. The struct remains the same and is still used to define operational limits for hardware devices.

For complete, up-to-date documentation, see: [DeviceConstraints in ControlState](control_state.md#deviceconstraints-struct)

### Members

| Member | Type | Default | Description |
|--------|------|---------|-------------|
| `min_frequency_hz` | `uint32_t` | 0 | Minimum supported frequency in Hz |
| `max_frequency_hz` | `uint32_t` | 1,700,000,000 | Maximum supported frequency in Hz (1.7 GHz for RTL2832U) |
| `min_gain_db` | `float` | 0.0 | Minimum gain in dB |
| `max_gain_db` | `float` | 49.6 | Maximum gain in dB (RTL2832U maximum) |
| `supported_fft_sizes` | `std::vector<size_t>` | {512, 1024, 2048, 4096} | Available FFT sizes (must be powers of two) |
| `supported_window_functions` | `std::vector<WindowFunction>` | All WindowFunction values | Available window functions for spectral analysis |

### Usage Example

```cpp
#include "openspectrum/runtime_controls.h"
#include "signal/signal_processor.h"

using namespace openspectrum;

// Create constraints for RTL2832U device
DeviceConstraints constraints;
constraints.max_frequency_hz = 1700000000;  // 1.7 GHz
constraints.max_gain_db = 49.6f;

// Use with RuntimeControls
RuntimeControls controls;
controls.set_constraints(constraints);
```

---

## RuntimeControls Class (DEPRECATED)

> **⚠️ This class is deprecated.** Use [ControlState](control_state.md) instead.

The `RuntimeControls` class **was** the main class for managing user-adjustable parameters and handling keyboard input. It has been **split** into:

- **[ControlState](control_state.md)** - All state management methods (getters, setters, constraints, etc.)
- `SdlControlInput` - SDL-specific keyboard input handling (internal)

### Current Definition

`RuntimeControls` is now a **deprecated type alias**:

```cpp
using RuntimeControls [[deprecated("Use ControlState instead. SDL input "
                                   "handling moved to SdlControlInput.")]] = ControlState;
```

This means:
- All **state management** methods work identically on `RuntimeControls` (because it IS `ControlState`)
- The **`handle_keyboard()`** method is **NO LONGER AVAILABLE** on `RuntimeControls`
- Keyboard handling must be done via `SdlRenderer::poll_events(&state)` or `SdlControlInput`

### Methods Still Available (via ControlState)

All state management methods are still available through the alias:

| Method | Status | Replacement |
|--------|--------|-------------|
| `get_frequency()`, `get_gain()`, `get_fft_size()`, `get_window()` | ✅ Available | Same on ControlState |
| `set_frequency()`, `set_gain()`, `set_fft_size()`, `set_window()` | ✅ Available | Same on ControlState |
| `set_constraints()`, `get_constraints()` | ✅ Available | Same on ControlState |
| `fft_size_changed()`, `clear_fft_change_flag()` | ✅ Available | Same on ControlState |
| `window_changed()`, `clear_window_change_flag()` | ✅ Available | Same on ControlState |
| `get_status_string()` | ✅ Available | Same on ControlState |
| `format_frequency()`, `format_gain()` | ✅ Available | Same on ControlState |
| `status_changed()`, `clear_status_dirty()` | ✅ Available | Same on ControlState |
| `is_reconfiguring()`, `set_reconfiguring()` | ✅ Available | Same on ControlState |
| `apply_to_device()` | ✅ Available | Same on ControlState |
| `handle_keyboard()` | ❌ REMOVED | Use `SdlRenderer::poll_events(&state)` |

### What Changed

**REMOVED:**
- `bool handle_keyboard(SDL_Keycode key, bool shift_held, bool ctrl_held)` - Moved to `SdlControlInput`

**PRESERVED:**
- All state getters/setters
- All constraint management
- All change flags
- All status display methods
- All device application methods

### Constructor and Destructor

| Signature | Description |
|-----------|-------------|
| `RuntimeControls()` | Constructs with default values: frequency=100MHz, gain=20dB, FFT size=4096, window=BLACKMAN_HARRIS |
| `~RuntimeControls()` | Destructor |

### Input Handling

> **⚠️ The `handle_keyboard()` method has been REMOVED from RuntimeControls.**

Keyboard input handling has been moved to `SdlControlInput` which is used internally by `SdlRenderer::poll_events()`. For keyboard handling, use one of these approaches:

#### Option 1: Using SdlRenderer (Recommended)

```cpp
SdlRenderer renderer(width, height);
ControlState state;

// In event loop
if (!renderer.poll_events(&state)) {
    // quit
}
```

#### Option 2: Using SdlControlInput Directly

```cpp
#include "gui/sdl_control_input.h"

ControlState state;
SdlControlInput input_handler(state);

// In event loop
SDL_Event event;
while (SDL_PollEvent(&event)) {
    if (event.type == SDL_KEYDOWN) {
        bool shift = (event.key.keysym.mod & KMOD_SHIFT) != 0;
        bool ctrl = (event.key.keysym.mod & KMOD_CTRL) != 0;
        if (input_handler.handle_keyboard(event.key.keysym.sym, shift, ctrl)) {
            // State changed, update device
            state.apply_to_device(device);
        }
    }
}
```

#### Available Keyboard Controls (via SdlControlInput)

The same keyboard controls are available through `SdlControlInput`:

| Key | Action | Shift (Fine) | Ctrl (Coarse) |
|-----|--------|--------------|---------------|
| `+` / `=` | Increase frequency | Smaller step | Larger step |
| `-` / `_` | Decrease frequency | Smaller step | Larger step |
| `r` | Increase gain | +0.1 dB | +10 dB |
| `f` | Decrease gain | -0.1 dB | -10 dB |
| `1` | FFT size: 512 | - | - |
| `2` | FFT size: 1024 | - | - |
| `3` | FFT size: 2048 | - | - |
| `4` | FFT size: 4096 | - | - |
| `UP` | Next window function | - | - |
| `DOWN` | Previous window function | - | - |
| `Ctrl+S` | Toggle IQ logging | - | - |

### Status Display

#### `get_status_string`

```cpp
std::string get_status_string() const;
```

Returns a formatted string containing all current control values for display in the status bar.

**Returns:**
- Formatted string like: `Freq: 100.000000 MHz | Gain: 20.00 dB | FFT: 4096 | Window: B-Harris`

**Caching:**
- The status string is cached for performance
- Use `status_changed()` and `clear_status_dirty()` to manage caching

#### `format_frequency` (static)

```cpp
static std::string format_frequency(uint32_t hz);
```

Formats a frequency value in Hz to a human-readable string.

**Parameters:**
- `hz` - Frequency in Hertz

**Returns:**
- Formatted string (e.g., "100.000000 MHz" for 100000000 Hz)

#### `format_gain` (static)

```cpp
static std::string format_gain(float db);
```

Formats a gain value in dB to a human-readable string.

**Parameters:**
- `db` - Gain in decibels

**Returns:**
- Formatted string with 2 decimal places (e.g., "20.00")

### FFT Configuration

#### `fft_size_changed`

```cpp
bool fft_size_changed() const;
```

**Returns:** `true` if the FFT size has been changed since the last check.

Use this to detect when components dependent on FFT size need to be reinitialized.

#### `clear_fft_change_flag`

```cpp
void clear_fft_change_flag();
```

Clears the FFT size change flag after handling the change.

### Getters

| Method | Returns | Description |
|--------|---------|-------------|
| `get_frequency()` | `uint32_t` | Current center frequency in Hz |
| `get_gain()` | `float` | Current gain in dB |
| `get_fft_size()` | `size_t` | Current FFT size |
| `get_window()` | `WindowFunction` | Current window function |
| `get_constraints()` | `const DeviceConstraints&` | Current device constraints |

### Change Flags

#### `window_changed`

```cpp
bool window_changed() const;
```

**Returns:** `true` if the window function has been changed since the last check.

#### `clear_window_change_flag`

```cpp
void clear_window_change_flag();
```

Clears the window change flag after handling the change.

### Setters

These methods are primarily used for initial configuration. Runtime changes should go through `handle_keyboard()` for consistent behavior.

| Method | Parameters | Description |
|--------|------------|-------------|
| `set_frequency(uint32_t hz)` | `hz` - Frequency in Hz | Sets the center frequency |
| `set_gain(float db)` | `db` - Gain in dB | Sets the gain value |
| `set_fft_size(size_t size)` | `size` - FFT size (must be in supported sizes) | Sets the FFT size |
| `set_window(WindowFunction w)` | `w` - Window function | Sets the window function |

### Constraints

#### `set_constraints`

```cpp
void set_constraints(const DeviceConstraints &constraints);
```

Sets the device constraints which limit the valid ranges for control parameters.

**Parameters:**
- `constraints` - DeviceConstraints struct with min/max values

#### `get_constraints`

```cpp
const DeviceConstraints &get_constraints() const;
```

**Returns:** Reference to the current device constraints.

### Reconfiguration State

#### `is_reconfiguring`

```cpp
bool is_reconfiguring() const;
```

**Returns:** `true` if the system is currently reconfiguring (e.g., during FFT size change).

This can be used to show a visual indicator during reconfiguration.

#### `set_reconfiguring`

```cpp
void set_reconfiguring(bool state);
```

Sets the reconfiguration state.

**Parameters:**
- `state` - `true` to indicate reconfiguration in progress

### Status Caching

#### `status_changed`

```cpp
bool status_changed() const;
```

**Returns:** `true` if the status string has changed since the last render.

This enables efficient status bar updates (only re-render when changed).

#### `clear_status_dirty`

```cpp
void clear_status_dirty() const;
```

Clears the status changed flag after rendering the status bar.

### Device Application

#### `apply_to_device`

```cpp
void apply_to_device(RtlSdrDevice &dev) const;
```

Applies all current control values to the hardware device in a batch update.

**Parameters:**
- `dev` - Reference to the RtlSdrDevice to update

**Behavior:**
- Updates device frequency, sample rate, and gain
- Designed for efficient batch updates to minimize device configuration changes

---

## Usage Example (Deprecated - See ControlState)

> **⚠️ This example uses the deprecated RuntimeControls.** For new code, see [ControlState](control_state.md) for updated examples.

The following example shows how RuntimeControls **was** used. For new code, replace `RuntimeControls` with `ControlState` and use `SdlRenderer::poll_events()` for keyboard handling.

```cpp
#include "openspectrum/runtime_controls.h"  // Deprecated - use control_state.h
#include "hardware/rtl_sdr_device.h"
#include "gui/sdl_renderer.h"

using namespace openspectrum;

int main() {
  // Create runtime controls (DEPRECATED)
  RuntimeControls controls;  // Now an alias for ControlState
  
  // Set initial values (these still work)
  controls.set_frequency(100000000);  // 100 MHz
  controls.set_gain(20.0f);            // 20 dB
  controls.set_fft_size(4096);        // 4K FFT
  controls.set_window(WindowFunction::BLACKMAN_HARRIS);
  
  // Set device constraints
  DeviceConstraints constraints;
  constraints.max_frequency_hz = 1700000000;
  constraints.max_gain_db = 49.6f;
  controls.set_constraints(constraints);
  
  // State management methods still work:
  if (controls.fft_size_changed()) {
    size_t new_size = controls.get_fft_size();
    // ... reinitialize ...
    controls.clear_fft_change_flag();
  }
  
  if (controls.status_changed()) {
    renderer.render_status_bar(controls.get_status_string());
    controls.clear_status_dirty();
  }
  
  // But keyboard handling must now use SdlRenderer:
  SdlRenderer renderer(width, height);
  if (!renderer.poll_events(&controls)) {  // Pass ControlState (via alias)
    // quit
  }
  controls.apply_to_device(device);
}
```

**For new code, see:** [ControlState Usage Examples](control_state.md#usage-example)

---

## See Also

- **[ControlState](control_state.md)** - RECOMMENDED replacement for RuntimeControls
- [Types](types.md) - WindowFunction enum
- [SignalProcessor](signal_processing.md) - Uses window functions
- [FftAnalyzer](fft_analysis.md) - FFT size affects analysis
- [RtlSdrDevice](hardware.md) - Device controlled by ControlState
- [SdlRenderer](gui.md#sdlrenderer) - Handles keyboard input via poll_events()
- [SdlControlInput](gui.md#sdlcontrolinput) - SDL-specific keyboard input handler
