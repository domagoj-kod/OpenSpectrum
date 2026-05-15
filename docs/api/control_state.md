# Control State API

Defined in: `include/openspectrum/control_state.h`

The Control State module provides SDL-agnostic state management for runtime controls. This class replaces the former `RuntimeControls` class for state management, separating concerns between platform-independent state and platform-specific input handling.

> [!Note]
> For keyboard input handling, see [SdlControlInput](#sdlcontrolinput) in the GUI module or use the integrated `SdlRenderer::poll_events()` method which can accept a `ControlState*` parameter.

---

## Namespace

```cpp
namespace openspectrum {
  // All types and classes defined here
}
```

---

## Overview

`ControlState` manages all user-adjustable parameters for the SDR spectrum analyzer in a platform-agnostic way. It handles:

- Center frequency tuning
- Gain adjustment
- FFT size selection
- Window function selection
- Device constraints validation
- IQ logging control

**Architecture Change:**
Previously, `RuntimeControls` combined both state management and SDL-specific keyboard input handling. The new architecture splits these concerns:
- `ControlState` (public API) - Manages state values and constraints
- `SdlControlInput` (GUI component) - Handles SDL keyboard events and updates ControlState

This separation makes the state management reusable in non-SDL contexts (e.g., headless operation, different UI frameworks).

---

## DeviceConstraints Struct

Defined in: `include/openspectrum/control_state.h`

The `DeviceConstraints` struct defines the operational limits for a hardware device. These constraints are used to validate user input and ensure parameters stay within valid ranges.

```cpp
struct DeviceConstraints {
  uint32_t min_frequency_hz = 0;
  uint32_t max_frequency_hz = 1700000000;      // 1.7 GHz (RTL2832U)
  float min_gain_db = 0.0f;
  float max_gain_db = 49.6f;                   // RTL2832U max
  std::vector<size_t> supported_fft_sizes = {512, 1024, 2048, 4096};
  std::vector<WindowFunction> supported_window_functions = {
      WindowFunction::RECTANGLE,       WindowFunction::HANN,
      WindowFunction::HAMMING,         WindowFunction::BLACKMAN,
      WindowFunction::BLACKMAN_HARRIS, WindowFunction::FLAT_TOP};
};
```

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
#include "openspectrum/control_state.h"

using namespace openspectrum;

// Create constraints for RTL2832U device
DeviceConstraints constraints;
constraints.max_frequency_hz = 1700000000;  // 1.7 GHz
constraints.max_gain_db = 49.6f;

// Use with ControlState
ControlState state;
state.set_constraints(constraints);
```

---

## ControlState Class

The `ControlState` class manages all user-adjustable parameters for the SDR spectrum analyzer.

```cpp
class ControlState {
public:
  ControlState();
  ~ControlState() = default;

  // Getters
  uint32_t get_frequency() const noexcept;
  float get_gain() const noexcept;
  size_t get_fft_size() const noexcept;
  WindowFunction get_window() const noexcept;

  // Setters
  void set_frequency(uint32_t hz);
  void set_gain(float db);
  void set_fft_size(size_t size);
  void set_window(WindowFunction w);

  // Constraints
  void set_constraints(const DeviceConstraints &new_constraints);
  const DeviceConstraints &get_constraints() const noexcept;

  // Change flags
  bool fft_size_changed() const noexcept;
  void clear_fft_change_flag() noexcept;
  bool window_changed() const noexcept;
  void clear_window_change_flag() noexcept;

  // Status display
  std::string get_status_string() const;
  static std::string format_frequency(uint32_t hz);
  static std::string format_gain(float db);

  // Status caching
  bool status_changed() const noexcept;
  void clear_status_dirty() const noexcept;
  void mark_status_dirty() const noexcept;

  // Reconfiguration state
  bool is_reconfiguring() const noexcept;
  void set_reconfiguring(bool state) noexcept;

  // IQ logging control
  bool iq_logging_toggle_requested() const noexcept;
  void clear_iq_logging_toggle() noexcept;
  void request_iq_logging_toggle() noexcept;

  // Device application
  void apply_to_device(RtlSdrDevice &dev) const;
};
```

### Constructor and Destructor

| Signature | Description |
|-----------|-------------|
| `ControlState()` | Constructs with default values: frequency=100MHz, gain=20dB, FFT size=4096, window=BLACKMAN_HARRIS |
| `~ControlState()` | Destructor (default) |

**Non-copyable, Non-movable:**
The class cannot be copied or moved to prevent accidental state duplication.

```cpp
ControlState(const ControlState &) = delete;
ControlState &operator=(const ControlState &) = delete;
```

---

### Getters

| Method | Returns | Description |
|--------|---------|-------------|
| `get_frequency()` | `uint32_t` | Current center frequency in Hz |
| `get_gain()` | `float` | Current gain in dB |
| `get_fft_size()` | `size_t` | Current FFT size |
| `get_window()` | `WindowFunction` | Current window function |
| `get_constraints()` | `const DeviceConstraints&` | Current device constraints |

---

### Setters

These methods set the control values and mark the status as dirty for display updates.

| Method | Parameters | Description |
|--------|------------|-------------|
| `set_frequency(uint32_t hz)` | `hz` - Frequency in Hz | Sets the center frequency (clamped to constraints) |
| `set_gain(float db)` | `db` - Gain in dB | Sets the gain value (clamped to constraints) |
| `set_fft_size(size_t size)` | `size` - FFT size | Sets the FFT size (must be in supported sizes) |
| `set_window(WindowFunction w)` | `w` - Window function | Sets the window function |

---

### Constraints Management

#### `set_constraints`

```cpp
void set_constraints(const DeviceConstraints &new_constraints);
```

Sets the device constraints which limit the valid ranges for control parameters.

**Parameters:**
- `new_constraints` - DeviceConstraints struct with min/max values

**Behavior:**
- Stores the new constraints
- Re-validates current values against new constraints

---

### Change Flags

#### `fft_size_changed`

```cpp
bool fft_size_changed() const noexcept;
```

**Returns:** `true` if the FFT size has been changed since the last check.

Use this to detect when components dependent on FFT size need to be reinitialized.

#### `clear_fft_change_flag`

```cpp
void clear_fft_change_flag() noexcept;
```

Clears the FFT size change flag after handling the change.

---

#### `window_changed`

```cpp
bool window_changed() const noexcept;
```

**Returns:** `true` if the window function has been changed since the last check.

#### `clear_window_change_flag`

```cpp
void clear_window_change_flag() noexcept;
```

Clears the window change flag after handling the change.

---

### Status Display

#### `get_status_string`

```cpp
std::string get_status_string() const;
```

Returns a formatted string containing all current control values for display in the status bar.

**Returns:**
- Formatted string like: `Freq: 100.000000 MHz | Gain: 20.00 dB | FFT: 4096 | Window: B-Harris | IQ: Recording`

**Caching:**
- The status string is cached for performance
- Use `status_changed()` and `clear_status_dirty()` to manage caching

---

#### `format_frequency` (static)

```cpp
static std::string format_frequency(uint32_t hz);
```

Formats a frequency value in Hz to a human-readable string.

**Parameters:**
- `hz` - Frequency in Hertz

**Returns:**
- Formatted string with appropriate unit (Hz, kHz, MHz, GHz) and 6 decimal places
- Example: "100.000000 MHz" for 100000000 Hz

---

#### `format_gain` (static)

```cpp
static std::string format_gain(float db);
```

Formats a gain value in dB to a human-readable string.

**Parameters:**
- `db` - Gain in decibels

**Returns:**
- Formatted string with 2 decimal places (e.g., "20.00")

---

### Status Caching

The status string caching system enables efficient status bar updates.

| Method | Description |
|--------|-------------|
| `status_changed()` | Returns `true` if the status string has changed since the last render |
| `clear_status_dirty()` | Clears the status changed flag after rendering the status bar |
| `mark_status_dirty()` | Marks the status as dirty (called internally by setters) |

**Usage:**
```cpp
if (state.status_changed()) {
  renderer.render_status_bar(state.get_status_string());
  state.clear_status_dirty();
}
```

---

### Reconfiguration State

#### `is_reconfiguring`

```cpp
bool is_reconfiguring() const noexcept;
```

**Returns:** `true` if the system is currently reconfiguring (e.g., during FFT size change).

This can be used to show a visual indicator during reconfiguration.

#### `set_reconfiguring`

```cpp
void set_reconfiguring(bool state) noexcept;
```

Sets the reconfiguration state.

**Parameters:**
- `state` - `true` to indicate reconfiguration in progress

---

### IQ Logging Control

These methods support the IQ logging feature integration.

| Method | Description |
|--------|-------------|
| `iq_logging_toggle_requested()` | Returns `true` if IQ logging toggle was requested (via keyboard shortcut) |
| `clear_iq_logging_toggle()` | Clears the toggle request flag |
| `request_iq_logging_toggle()` | Requests IQ logging toggle (called by input handler) |

---

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

**Note:** This method is `const` because it doesn't modify the ControlState's internal state; it only reads from it to update the device.

---

## Usage Example

### Basic Usage

```cpp
#include "openspectrum/control_state.h"
#include "hardware/rtl_sdr_device.h"

using namespace openspectrum;

int main() {
  // Create control state
  ControlState state;
  
  // Set initial values
  state.set_frequency(100000000);  // 100 MHz
  state.set_gain(20.0f);            // 20 dB
  state.set_fft_size(4096);        // 4K FFT
  state.set_window(WindowFunction::BLACKMAN_HARRIS);
  
  // Set device constraints
  DeviceConstraints constraints;
  constraints.max_frequency_hz = 1700000000;
  constraints.max_gain_db = 49.6f;
  state.set_constraints(constraints);
  
  // Apply to device
  RtlSdrDevice device;
  device.open();
  state.apply_to_device(device);
  
  return 0;
}
```

---

### With Keyboard Input (via SdlRenderer)

```cpp
#include "openspectrum/control_state.h"
#include "gui/sdl_renderer.h"

using namespace openspectrum;

int main() {
  ControlState state;
  state.set_frequency(92600000);  // 92.6 MHz (FM radio)
  state.set_gain(20.0f);
  state.set_fft_size(4096);
  state.set_window(WindowFunction::BLACKMAN_HARRIS);
  
  SdlRenderer renderer(1024, 576, "OpenSpectrum");
  
  // In main loop
  while (running) {
    // Pass ControlState to poll_events for keyboard handling
    if (!renderer.poll_events(&state)) {
      running = false;
    }
    
    // Check for changes
    if (state.fft_size_changed()) {
      // Reinitialize FFT-dependent components
      size_t new_size = state.get_fft_size();
      // ... reinitialize ...
      state.clear_fft_change_flag();
    }
    
    // Update status bar
    if (state.status_changed()) {
      renderer.render_status_bar(state.get_status_string());
      state.clear_status_dirty();
    }
  }
  
  return 0;
}
```

---

### Migration from RuntimeControls

**Before (with RuntimeControls):**
```cpp
#include "openspectrum/runtime_controls.h"

RuntimeControls controls;
controls.set_frequency(100000000);
controls.handle_keyboard(key, shift, ctrl);
controls.apply_to_device(device);
```

**After (with ControlState):**
```cpp
#include "openspectrum/control_state.h"

ControlState state;
state.set_frequency(100000000);
// Keyboard handling is now done via SdlRenderer::poll_events(&state)
// or directly via SdlControlInput if needed
state.apply_to_device(device);
```

For keyboard input handling, use `SdlRenderer::poll_events(&state)` which internally creates an `SdlControlInput` to handle SDL keyboard events and update the state.

---

## See Also

- [Types - WindowFunction](types.md#windowfunction-enum) - Window function enumeration
- [RuntimeControls (Deprecated)](runtime_controls.md) - Deprecated class, use ControlState instead
- [RtlSdrDevice](hardware.md) - Device controlled by ControlState
- [SdlRenderer](gui.md#sdlrenderer) - Handles rendering and can process keyboard input for ControlState
- [IqLogger](iq_logging.md) - IQ logging control integration
