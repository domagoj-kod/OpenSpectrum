# Runtime Controls API

Defined in: `include/openspectrum/runtime_controls.h`

The Runtime Controls module provides real-time user-adjustable parameters for controlling the SDR spectrum analyzer during operation. This includes frequency tuning, gain adjustment, FFT size selection, and window function selection.

---

## Namespace

```cpp
namespace openspectrum {
  // All types and classes defined here
}
```

---

## DeviceConstraints Struct

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

## RuntimeControls Class

The `RuntimeControls` class manages all user-adjustable parameters and handles keyboard input for real-time control of the spectrum analyzer.

```cpp
class RuntimeControls {
public:
  RuntimeControls();
  ~RuntimeControls();

  // Input handling
  bool handle_keyboard(SDL_Keycode key, bool shift_held, bool ctrl_held);

  // Status display
  std::string get_status_string() const;
  static std::string format_frequency(uint32_t hz);
  static std::string format_gain(float db);

  // FFT configuration
  bool fft_size_changed() const;
  void clear_fft_change_flag();

  // Getters
  uint32_t get_frequency() const;
  float get_gain() const;
  size_t get_fft_size() const;
  WindowFunction get_window() const;

  // Change flags
  bool window_changed() const;
  void clear_window_change_flag();

  // Setters (initial configuration)
  void set_frequency(uint32_t hz);
  void set_gain(float db);
  void set_fft_size(size_t size);
  void set_window(WindowFunction w);

  // Constraints
  void set_constraints(const DeviceConstraints &constraints);
  const DeviceConstraints &get_constraints() const;

  // Reconfiguration state
  bool is_reconfiguring() const;
  void set_reconfiguring(bool state);

  // Status caching
  bool status_changed() const;
  void clear_status_dirty() const;

  // Device application
  void apply_to_device(RtlSdrDevice &dev) const;
};
```

### Constructor and Destructor

| Signature | Description |
|-----------|-------------|
| `RuntimeControls()` | Constructs with default values: frequency=100MHz, gain=20dB, FFT size=4096, window=BLACKMAN_HARRIS |
| `~RuntimeControls()` | Destructor |

### Input Handling

#### `handle_keyboard`

```cpp
bool handle_keyboard(SDL_Keycode key, bool shift_held, bool ctrl_held);
```

Processes a keyboard event and updates control values accordingly.

**Parameters:**
- `key` - SDL_Keycode of the pressed key
- `shift_held` - True if Shift key is held (enables fine adjustment)
- `ctrl_held` - True if Ctrl key is held (enables coarse adjustment)

**Returns:**
- `true` if any control value was changed
- `false` if the key was not handled or didn't change values

**Keyboard Controls:**

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

## Usage Example

```cpp
#include "openspectrum/runtime_controls.h"
#include "hardware/rtl_sdr_device.h"
#include "gui/sdl_renderer.h"

using namespace openspectrum;

int main() {
  // Create runtime controls
  RuntimeControls controls;
  
  // Set initial values
  controls.set_frequency(100000000);  // 100 MHz
  controls.set_gain(20.0f);            // 20 dB
  controls.set_fft_size(4096);        // 4K FFT
  controls.set_window(WindowFunction::BLACKMAN_HARRIS);
  
  // Set device constraints
  DeviceConstraints constraints;
  constraints.max_frequency_hz = 1700000000;
  constraints.max_gain_db = 49.6f;
  controls.set_constraints(constraints);
  
  // In event loop:
  SDL_Event event;
  while (SDL_PollEvent(&event)) {
    if (event.type == SDL_KEYDOWN) {
      bool shift = (event.key.keysym.mod & KMOD_SHIFT) != 0;
      bool ctrl = (event.key.keysym.mod & KMOD_CTRL) != 0;
      if (controls.handle_keyboard(event.key.keysym.sym, shift, ctrl)) {
        // Values changed, update device
        controls.apply_to_device(device);
      }
    }
  }
  
  // Check for FFT size change
  if (controls.fft_size_changed()) {
    // Reinitialize FFT-dependent components
    size_t new_size = controls.get_fft_size();
    // ... reinitialize ...
    controls.clear_fft_change_flag();
  }
  
  // Update status bar when changed
  if (controls.status_changed()) {
    renderer.render_status_bar(controls.get_status_string());
    controls.clear_status_dirty();
  }
}
```

---

## See Also

- [Types](types.md) - WindowFunction enum
- [SignalProcessor](signal_processing.md) - Uses window functions
- [FftAnalyzer](fft_analysis.md) - FFT size affects analysis
- [RtlSdrDevice](hardware.md) - Device controlled by RuntimeControls
- [SdlRenderer](gui.md#sdlrenderer) - Handles keyboard input
