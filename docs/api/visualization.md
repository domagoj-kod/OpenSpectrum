# Visualization API

Defined in: `src/visualization/spectrum_display.h` and `src/visualization/waterfall_display.h`

The Visualization module provides classes for rendering spectrum and waterfall displays, along with color palette management and optimized pixel buffer handling.

---

## Namespace

```cpp
namespace openspectrum {
  // All types and classes defined here
}
```

---

## RgbColor Struct

A simple RGBA color structure for pixel data representation.

```cpp
struct RgbColor {
  uint8_t red;
  uint8_t green;
  uint8_t blue;
  uint8_t alpha;

  constexpr RgbColor() : red(0), green(0), blue(0), alpha(255) {}
  constexpr RgbColor(uint8_t r, uint8_t g, uint8_t b, uint8_t a = 255)
      : red(r), green(g), blue(b), alpha(a) {}

  inline void copy_to(uint8_t *dst) const;
};
```

### Members

| Member | Type | Default | Description |
|--------|------|---------|-------------|
| `red` | `uint8_t` | 0 | Red component (0-255) |
| `green` | `uint8_t` | 0 | Green component (0-255) |
| `blue` | `uint8_t` | 0 | Blue component (0-255) |
| `alpha` | `uint8_t` | 255 | Alpha/opacity component (0-255, 255 = opaque) |

### Constructors

| Signature | Description |
|-----------|-------------|
| `RgbColor()` | Default constructor, creates black opaque color (0, 0, 0, 255) |
| `RgbColor(uint8_t r, uint8_t g, uint8_t b, uint8_t a = 255)` | Creates a color with specified components |

### Methods

#### `copy_to`

```cpp
inline void copy_to(uint8_t *dst) const;
```

Copies the RGBA components to a destination buffer.

**Parameters:**
- `dst` - Pointer to 4-byte buffer (RGBA order)

**Behavior:**
```cpp
dst[0] = red;
dst[1] = green;
dst[2] = blue;
dst[3] = alpha;
```

**Usage:**
```cpp
RgbColor color(255, 128, 0, 255);  // Orange, opaque
uint8_t pixels[4];
color.copy_to(pixels);
// pixels now contains {255, 128, 0, 255}
```

---

## PixelBuffer Class

An optimized pixel buffer for direct pointer access, designed for high-performance rendering.

```cpp
class PixelBuffer {
public:
  PixelBuffer();
  explicit PixelBuffer(size_t size);
  ~PixelBuffer();

  // No copying (use std::move instead)
  PixelBuffer(const PixelBuffer &) = delete;
  PixelBuffer &operator=(const PixelBuffer &) = delete;

  PixelBuffer(PixelBuffer &&other) noexcept;
  PixelBuffer &operator=(PixelBuffer &&other) noexcept;

  // Access
  uint8_t *data() noexcept;
  const uint8_t *data() const noexcept;
  size_t size() const noexcept;

  // Element access
  uint8_t &operator[](size_t index) noexcept;
  const uint8_t &operator[](size_t index) const noexcept;

  // Pixel access
  uint8_t *pixel_ptr(size_t x, size_t y, size_t width) noexcept;
  const uint8_t *pixel_ptr(size_t x, size_t y, size_t width) const noexcept;

  // Operations
  void clear();
  void fill_column(size_t x, size_t y_start, size_t y_end, size_t width,
                   const RgbColor &color) noexcept;
  void memset_clear();
};
```

### Constructors and Destructor

| Signature | Description |
|-----------|-------------|
| `PixelBuffer()` | Default constructor, creates empty buffer |
| `explicit PixelBuffer(size_t size)` | Creates buffer with specified byte size |
| `~PixelBuffer()` | Destructor, frees allocated memory |

**Note:** The buffer is allocated with `new uint8_t[size]()` and initialized to zero.

### Copy and Move Semantics

| Operation | Supported | Description |
|-----------|-----------|-------------|
| Copy constructor | ❌ No | Deleted |
| Copy assignment | ❌ No | Deleted |
| Move constructor | ✅ Yes | Transfers ownership of data |
| Move assignment | ✅ Yes | Transfers ownership of data |

### Access Methods

#### `data`

```cpp
uint8_t *data() noexcept;
const uint8_t *data() const noexcept;
```

Returns a direct pointer to the pixel buffer data.

**Returns:**
- Pointer to the first byte of the buffer

**Format:** RGBA interleaved, 4 bytes per pixel

#### `size`

```cpp
size_t size() const noexcept;
```

Returns the total size of the buffer in bytes.

**Returns:**
- Number of bytes allocated

#### `operator[]`

```cpp
uint8_t &operator[](size_t index) noexcept;
const uint8_t &operator[](size_t index) const noexcept;
```

Subscript operator for byte-level access.

**Parameters:**
- `index` - Byte index (0 to size()-1)

**Returns:**
- Reference to the byte at the specified index

**Note:** For performance-critical code, prefer direct pointer access using `data()`.

### Pixel Access Methods

#### `pixel_ptr`

```cpp
uint8_t *pixel_ptr(size_t x, size_t y, size_t width) noexcept;
const uint8_t *pixel_ptr(size_t x, size_t y, size_t width) const noexcept;
```

Returns a pointer to the pixel at coordinates (x, y).

**Parameters:**
- `x` - X coordinate (column)
- `y` - Y coordinate (row)
- `width` - Width of the image in pixels

**Returns:**
- Pointer to the first byte of the pixel (4 bytes: R, G, B, A)

**Formula:**
```cpp
return m_data + (y * width + x) * 4;
```

#### `fill_column`

```cpp
inline void fill_column(size_t x, size_t y_start, size_t y_end, size_t width,
                        const RgbColor &color) noexcept;
```

Fills a vertical column with a color.

**Parameters:**
- `x` - X coordinate of the column
- `y_start` - Starting Y coordinate (inclusive)
- `y_end` - Ending Y coordinate (inclusive)
- `width` - Width of the image in pixels
- `color` - Color to fill with

**Behavior:**
- Efficiently fills a vertical line by iterating and using `copy_to`
- Uses stride calculation for row skipping

**Complexity:** O(y_end - y_start + 1)

### Clear Methods

#### `clear`

```cpp
void clear();
```

Clears the buffer by filling with zeros (black/transparent).

**Behavior:**
```cpp
std::fill(m_data, m_data + m_size, 0);
```

#### `memset_clear`

```cpp
void memset_clear();
```

Fast clear using memset-style fill.

**Behavior:**
- Same as `clear()` but may use optimized memset implementation
- Checks for null pointer before clearing

---

## SpectrumPalette Class

Manages color palettes for spectrum visualization with support for multiple colormap types.

```cpp
class SpectrumPalette {
public:
  static constexpr size_t PALETTE_SIZE = 256;

  SpectrumPalette();

  void set_db_range(float min_db, float max_db);
  RgbColor get_color(float db_value) const;
  RgbColor get_color(float db_value, float min_db, float max_db) const;

  enum class ColorMap { JET, VIRIDIS, HOT, GRAyscale, BLUE_RED };
  void set_color_map(ColorMap map);

private:
  ColorMap m_color_map;
  std::array<RgbColor, PALETTE_SIZE> m_palette;
  float m_scale_to_index;
  float m_db_min;
  float m_db_max;
  // ... palette generation methods
};
```

### Constants

| Constant | Value | Description |
|----------|-------|-------------|
| `PALETTE_SIZE` | 256 | Number of colors in each palette |

### Constructor

```cpp
SpectrumPalette();
```

Constructs a SpectrumPalette with default settings.

**Initial State:**
- Color map: `ColorMap::JET`
- dB range: [-120.0, 0.0] dB
- Palette is pre-generated

### Configuration Methods

#### `set_db_range`

```cpp
void set_db_range(float min_db, float max_db);
```

Sets the dB range for color mapping.

**Parameters:**
- `min_db` - Minimum dB value (maps to first palette color)
- `max_db` - Maximum dB value (maps to last palette color)

**Behavior:**
- Pre-computes `m_scale_to_index` for fast color lookup
- Stores min/max for clamping

**Note:** Call this when the display range changes for optimal performance.

#### `set_color_map`

```cpp
void set_color_map(ColorMap map);
```

Sets the color map to use.

**Parameters:**
- `map` - The color map to use

**Behavior:**
- Generates a new palette of 256 colors
- Resets internal state

### Color Lookup Methods

#### `get_color` (with cached range)

```cpp
RgbColor get_color(float db_value) const;
```

Gets the color for a dB value using the pre-configured range.

**Parameters:**
- `db_value` - The dB value to map to a color

**Returns:**
- RGBA color corresponding to the dB value

**Behavior:**
- Clamps `db_value` to [m_db_min, m_db_max]
- Uses pre-computed scale for integer quantization
- Fast O(1) lookup

**Algorithm:**
```cpp
index = static_cast<int>((db_value - m_db_min) * m_scale_to_index + 0.5f);
index = std::clamp(index, 0, PALETTE_SIZE - 1);
return m_palette[index];
```

#### `get_color` (with inline range)

```cpp
RgbColor get_color(float db_value, float min_db, float max_db) const;
```

Gets the color for a dB value with inline range specification.

**Parameters:**
- `db_value` - The dB value to map to a color
- `min_db` - Minimum dB value for this lookup
- `max_db` - Maximum dB value for this lookup

**Returns:**
- RGBA color corresponding to the dB value

**Behavior:**
- Same as the cached version but computes scale on-the-fly
- Slightly slower due to division operation
- Useful when the range varies per-call

### ColorMap Enum

See [Types - ColorMap](types.md#colormap-enum) for complete documentation.

Available color maps:
- `JET` - Rainbow colormap (default)
- `VIRIDIS` - Perceptually uniform, colorblind-friendly
- `HOT` - Heat map (black-red-yellow-white)
- `GRAyscale` - Grayscale
- `BLUE_RED` - Blue to red gradient

---

## SpectrumDisplay Class

2D spectrum display for visualizing amplitude vs frequency.

```cpp
class SpectrumDisplay {
public:
  SpectrumDisplay(size_t width, size_t height);
  ~SpectrumDisplay() = default;

  void update_spectrum(const std::vector<float> &db_values,
                       const std::vector<float> &freq_bins,
                       float center_freq_hz, float sample_rate_hz);

  const PixelBuffer &get_pixels() const;
  uint8_t *pixel_data();
  const uint8_t *pixel_data() const;

  size_t width() const noexcept;
  size_t height() const noexcept;

  void set_color_map(SpectrumPalette::ColorMap map);
  void set_db_range(float min_db, float max_db);
  void set_autoscale(bool enabled);

  float min_db() const noexcept;
  float max_db() const noexcept;

private:
  // Internal state and rendering methods
};
```

### Constructor

```cpp
SpectrumDisplay(size_t width, size_t height);
```

Constructs a spectrum display with specified dimensions.

**Parameters:**
- `width` - Display width in pixels
- `height` - Display height in pixels

**Initial State:**
- Pixel buffer allocated (width × height × 4 bytes)
- Default dB range: [-120.0, 0.0] dB
- Autoscale: enabled
- Color map: `ColorMap::JET`

### Destructor

```cpp
~SpectrumDisplay() = default;
```

Default destructor.

### Rendering Methods

#### `update_spectrum`

```cpp
void update_spectrum(const std::vector<float> &db_values,
                     const std::vector<float> &freq_bins,
                     float center_freq_hz, float sample_rate_hz);
```

Updates the display with new spectrum data.

**Parameters:**
- `db_values` - dB values for each frequency bin
- `freq_bins` - Normalized frequency bin values (0.0 to 1.0)
- `center_freq_hz` - Current center frequency in Hz
- `sample_rate_hz` - Sample rate in Hz

**Behavior:**
- Maps spectrum data to pixel colors using the palette
- Renders the spectrum horizontally across the display
- If autoscale is enabled, adjusts the effective dB range
- Updates the internal pixel buffer

**Note:** The actual frequency display (labels, etc.) is handled separately by the renderer.

### Access Methods

#### `get_pixels`

```cpp
const PixelBuffer &get_pixels() const;
```

Returns a reference to the internal pixel buffer.

**Returns:**
- Const reference to the PixelBuffer containing rendered spectrum

#### `pixel_data`

```cpp
uint8_t *pixel_data();
const uint8_t *pixel_data() const;
```

Returns a direct pointer to the pixel data.

**Returns:**
- Pointer to the first byte of the pixel buffer

**Format:** RGBA interleaved, 4 bytes per pixel, row-major order

### Dimension Methods

#### `width` and `height`

```cpp
size_t width() const noexcept;
size_t height() const noexcept;
```

Return the display dimensions.

**Returns:**
- Width or height in pixels

### Configuration Methods

#### `set_color_map`

```cpp
void set_color_map(SpectrumPalette::ColorMap map);
```

Sets the color map for the spectrum display.

**Parameters:**
- `map` - The color map to use

#### `set_db_range`

```cpp
void set_db_range(float min_db, float max_db);
```

Sets the dB range for the display.

**Parameters:**
- `min_db` - Minimum dB value (bottom of display)
- `max_db` - Maximum dB value (top of display)

#### `set_autoscale`

```cpp
void set_autoscale(bool enabled);
```

Enables or disables automatic scaling.

**Parameters:**
- `enabled` - `true` to enable autoscale

**Behavior:**
- When enabled, the display automatically adjusts to show the full range of received signals
- When disabled, uses the fixed dB range set by `set_db_range()`

#### `min_db` and `max_db`

```cpp
float min_db() const noexcept;
float max_db() const noexcept;
```

Return the current dB range.

**Returns:**
- Current minimum or maximum dB value

---

## WaterfallDisplay Class

Time-frequency waterfall display showing spectral history.

```cpp
class WaterfallDisplay {
public:
  WaterfallDisplay(size_t width, size_t height, size_t history_lines);
  ~WaterfallDisplay() = default;

  void add_spectrum_line(const std::vector<float> &db_values);

  const PixelBuffer &get_pixels() const;

  size_t width() const noexcept;
  size_t height() const noexcept;

  void set_color_map(SpectrumPalette::ColorMap map);
  void set_db_range(float min_db, float max_db);
  void set_autoscale(bool enabled);

  float min_db() const noexcept;
  float max_db() const noexcept;

  void reset();

private:
  // Internal state and rendering methods
};
```

### Constructor

```cpp
WaterfallDisplay(size_t width, size_t height, size_t history_lines);
```

Constructs a waterfall display with specified dimensions and history depth.

**Parameters:**
- `width` - Display width in pixels (matches spectrum width)
- `height` - Display height in pixels
- `history_lines` - Number of history lines to maintain

**Initial State:**
- Pixel buffer allocated (width × height × 4 bytes)
- Circular buffer for history lines
- Default dB range: [-120.0, 0.0] dB
- Autoscale: enabled
- Color map: `ColorMap::JET`

### Destructor

```cpp
~WaterfallDisplay() = default;
```

Default destructor.

### Data Update Methods

#### `add_spectrum_line`

```cpp
void add_spectrum_line(const std::vector<float> &db_values);
```

Adds a new spectrum line to the waterfall history.

**Parameters:**
- `db_values` - dB values for each frequency bin (should match width)

**Behavior:**
- Adds the new line to the history buffer
- If the buffer is full, the oldest line is removed
- Re-renders the entire waterfall display
- Updates global min/max for autoscale

**Note:** This should be called once per FFT execution to build up the waterfall.

### Access Methods

#### `get_pixels`

```cpp
const PixelBuffer &get_pixels() const;
```

Returns a reference to the internal pixel buffer.

**Returns:**
- Const reference to the PixelBuffer containing rendered waterfall

### Dimension Methods

#### `width` and `height`

```cpp
size_t width() const noexcept;
size_t height() const noexcept;
```

Return the display dimensions.

**Returns:**
- Width or height in pixels

### Configuration Methods

#### `set_color_map`, `set_db_range`, `set_autoscale`, `min_db`, `max_db`

Same as [SpectrumDisplay](visualization.md#spectrumdisplay) methods.

### Control Methods

#### `reset`

```cpp
void reset();
```

Resets the waterfall display.

**Behavior:**
- Clears all history lines
- Resets global min/max values
- Clears the pixel buffer to black

**Use Case:** Call this when changing FFT size or when starting fresh visualization.

---

## Usage Example

```cpp
#include "visualization/spectrum_display.h"
#include "visualization/waterfall_display.h"
#include "fft/fft_analyzer.h"
#include "signal/signal_processor.h"

using namespace openspectrum;

int main() {
  const size_t FFT_SIZE = 4096;
  const size_t DISPLAY_WIDTH = 1024;
  const size_t DISPLAY_HEIGHT = 512;
  const size_t WATERFALL_LINES = 256;
  const float SAMPLE_RATE = 2048000.0f;

  // Create displays
  SpectrumDisplay spectrum(DISPLAY_WIDTH, DISPLAY_HEIGHT / 2);
  WaterfallDisplay waterfall(DISPLAY_WIDTH, DISPLAY_HEIGHT / 2, WATERFALL_LINES);

  // Configure
  spectrum.set_db_range(-120.0f, 0.0f);
  waterfall.set_db_range(-120.0f, 0.0f);
  spectrum.set_color_map(SpectrumPalette::ColorMap::VIRIDIS);
  waterfall.set_color_map(SpectrumPalette::ColorMap::JET);

  // In main loop:
  std::vector<std::complex<float>> samples = device.read_samples(FFT_SIZE);
  SignalProcessor::remove_dc(samples);
  processor.apply_window(samples);
  
  fft.execute(samples);
  const auto& db_spectrum = fft.get_db_spectrum();
  const auto& freq_bins = fft.get_frequency_bins();

  // Update displays
  spectrum.update_spectrum(db_spectrum, freq_bins, 100000000.0f, SAMPLE_RATE);
  waterfall.add_spectrum_line(db_spectrum);

  // Get pixel data for rendering
  const auto& spec_pixels = spectrum.get_pixels();
  const auto& wf_pixels = waterfall.get_pixels();

  // Combine and render (pseudo-code)
  // renderer.render(spec_pixels, top_half);
  // renderer.render(wf_pixels, bottom_half);

  return 0;
}
```

---

## Performance Considerations

1. **PixelBuffer:** Uses raw pointers and direct memory access for maximum performance
2. **Circular Buffer:** WaterfallDisplay uses a deque for efficient history management
3. **Caching:** Color lookups use pre-computed palettes and scaling factors
4. **Autoscale:** Global min/max tracking enables efficient auto-ranging
5. **Move Semantics:** All classes support move semantics for efficient resource transfer

---

## See Also

- [Types - ColorMap](types.md#colormap-enum) - Color map enumeration
- [FFT Analysis](fft_analysis.md) - Provides spectrum data for visualization
- [GUI](gui.md) - Handles rendering of visualization output
- [Runtime Controls](runtime_controls.md) - Manages user-adjustable parameters
