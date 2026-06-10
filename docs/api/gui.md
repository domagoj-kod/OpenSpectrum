# GUI API

Defined in: `src/gui/sdl_renderer.h`, `src/gui/sdl_control_input.h`, and `src/gui/text_renderer.h`

The GUI module provides SDL3-based rendering capabilities for the spectrum analyzer, including window management, texture rendering, text rendering using a bitmap font, and keyboard input handling.

---

## Dependencies

This module requires:
- SDL3 library (`libsdl2-dev` or equivalent)
- Headers: `SDL3/SDL.h`

---

## Namespace

```cpp
namespace openspectrum {
  // All classes defined here
}
```

---

## Overview

**Architecture Update:** The GUI module now integrates with `ControlState` for state management. The `SdlRenderer::poll_events()` method accepts a `ControlState*` parameter, and internally uses `SdlControlInput` to handle keyboard events and update the state.

This change separates platform-specific input handling (SDL) from platform-agnostic state management, making the state reusable in non-SDL contexts.

---

## SdlRenderer Class

The `SdlRenderer` class manages the SDL3 window, renderer, and texture for displaying the spectrum analyzer output.

```cpp
class SdlRenderer {
public:
  SdlRenderer(size_t width, size_t height,
              const std::string &title = "OpenSpectrum SDR");
  ~SdlRenderer();

  // Non-copyable, non-movable
  SdlRenderer(const SdlRenderer &) = delete;
  SdlRenderer &operator=(const SdlRenderer &) = delete;

  // Rendering
  bool render(const std::vector<uint8_t> &pixels, size_t pitch = 0);
  void render_status_bar(const std::string &status_text);

  // Event handling
  bool poll_events(ControlState *state = nullptr);

  // Accessors
  size_t width() const noexcept;
  size_t height() const noexcept;
  bool is_valid() const noexcept;
  SDL_Renderer *get_sdl_renderer() const noexcept;

private:
  // Internal SDL resources
  size_t m_width;
  size_t m_height;
  SDL_Window *m_window;
  SDL_Renderer *m_renderer;
  SDL_Texture *m_texture;
  std::unique_ptr<TextRenderer> m_text_renderer;
  SDL_Texture *m_status_texture;
  std::string m_current_status;
  bool m_status_dirty;
};
```

### Constructor

```cpp
SdlRenderer(size_t width, size_t height,
            const std::string &title = "OpenSpectrum SDR");
```

Constructs an SDL3 renderer with the specified dimensions and title.

**Parameters:**
- `width` - Window width in pixels
- `height` - Window height in pixels
- `title` - Window title string (default: "OpenSpectrum SDR")

**Behavior:**
- Initializes SDL3 video subsystem if not already initialized
- Creates SDL_Window with `SDL_WINDOW_SHOWN | SDL_WINDOW_RESIZABLE`
- Creates SDL_Renderer with `SDL_RENDERER_ACCELERATED | SDL_RENDERER_PRESENTVSYNC`
- Creates SDL_Texture for rendering pixels
- Creates TextRenderer for status bar text
- Allocates internal pixel buffer

**Initialization Flags:**
- SDL_INIT_VIDEO - Video subsystem
- SDL_WINDOW_SHOWN - Window is shown
- SDL_WINDOW_RESIZABLE - Window can be resized
- SDL_RENDERER_ACCELERATED - Hardware-accelerated rendering
- SDL_RENDERER_PRESENTVSYNC - Synchronized with display refresh rate

### Destructor

```cpp
~SdlRenderer();
```

Destructor that properly cleans up SDL3 resources.

**Behavior:**
- Destroys SDL_Texture
- Destroys TextRenderer
- Destroys SDL_Renderer
- Destroys SDL_Window
- Does NOT call SDL_Quit() (allows other SDL subsystems to continue)

### Copy and Move Semantics

| Operation | Supported | Description |
|-----------|-----------|-------------|
| Copy constructor | ❌ No | Deleted (SDL resources cannot be copied) |
| Copy assignment | ❌ No | Deleted |
| Move constructor | ❌ No | Deleted (SDL resources cannot be safely moved) |
| Move assignment | ❌ No | Deleted |

The class is non-copyable and non-movable because SDL resources (Window, Renderer, Texture) are raw pointers that cannot be safely transferred or copied.

### Rendering Methods

#### `render`

```cpp
bool render(const std::vector<uint8_t> &pixels, size_t pitch = 0);
```

Renders a pixel buffer to the window.

**Parameters:**
- `pixels` - Pixel data in RGBA format (4 bytes per pixel)
- `pitch` - Number of bytes per row (0 = use width * 4)

**Returns:**
- `true` on success
- `false` on error

**Preconditions:**
- `pixels.size()` should be at least `width() * height() * 4`
- Pixel format: RGBA interleaved, 4 bytes per pixel

**Behavior:**
- Updates the SDL_Texture with pixel data
- Clears the renderer
- Copies the texture to the renderer
- Presents the renderer (swaps buffers)

**Performance:**
- Uses SDL_UpdateTexture for efficient pixel upload
- Uses SDL_RenderCopy for texture rendering
- Uses SDL_RenderPresent for display update

#### `render_status_bar`

```cpp
void render_status_bar(const std::string &status_text);
```

Renders text to the status bar texture.

**Parameters:**
- `status_text` - Text to render in the status bar

**Behavior:**
- Creates or updates the status bar texture
- The texture can be rendered as part of the main display

**Note:** This creates an SDL_Texture that must be manually rendered or incorporated into the main display.

### Event Handling Methods

#### `poll_events`

```cpp
bool poll_events(ControlState *state = nullptr);
```

Poll and process SDL3 events.

**Parameters:**
- `state` - Optional pointer to ControlState for keyboard handling (default: nullptr). The renderer internally creates an `SdlControlInput` to process keyboard events and update the state.

**Returns:**
- `true` if the application should continue running
- `false` if a quit event was received

**Behavior:**
- Polls all pending SDL events using SDL_PollEvent
- Handles QUIT events (window close, Ctrl+C)
- Handles KEYDOWN events
- If `state` is provided, passes keyboard events to an internal `SdlControlInput` which updates the ControlState
- Returns false on SDL_QUIT event

**Event Types Handled:**
- `SDL_QUIT` - Returns false to signal application exit
- `SDL_KEYDOWN` - Passes to SdlControlInput if state is provided

**Example:**
```cpp
#include "openspectrum/control_state.h"

ControlState state;

while (running) {
  if (!renderer.poll_events(&state)) {
    running = false;
    break;
  }
  // Handle other events...
}
```

### Accessor Methods

#### `width` and `height`

```cpp
size_t width() const noexcept;
size_t height() const noexcept;
```

Return the renderer dimensions.

**Returns:**
- Width or height in pixels

#### `is_valid`

```cpp
bool is_valid() const noexcept;
```

Checks if the renderer was successfully initialized.

**Returns:**
- `true` if both window and renderer are not null
- `false` if initialization failed

**Usage:**
```cpp
SdlRenderer renderer(1024, 576);
if (!renderer.is_valid()) {
  std::cerr << "Failed to initialize SDL3 renderer" << std::endl;
  return 1;
}
```

#### `get_sdl_renderer`

```cpp
SDL_Renderer *get_sdl_renderer() const noexcept;
```

Returns the underlying SDL_Renderer pointer.

**Returns:**
- Pointer to the SDL_Renderer

**Purpose:**
- Allows access to the SDL_Renderer for custom rendering operations
- Used by TextRenderer for creating textures

---

## TextRenderer Class

The `TextRenderer` class provides bitmap font text rendering, caching glyphs as SDL textures for efficiency.

```cpp
class TextRenderer {
public:
  TextRenderer(SDL_Renderer *renderer, int font_size = 16);
  ~TextRenderer();

  // Non-copyable, non-movable
  TextRenderer(const TextRenderer &) = delete;
  TextRenderer &operator=(const TextRenderer &) = delete;

  // Initialization
  bool init();

  // Rendering
  SDL_Texture *render_text(const std::string &text,
                           SDL_Color color = {255, 255, 255, 255});
  void get_text_size(const std::string &text, int *w, int *h) const;

  // Cleanup
  void clear_cache();

private:
  // Internal state
  SDL_Renderer *renderer;
  int font_size;
  static const uint8_t BITMAP_FONT[96][8];
  std::unordered_map<char, SDL_Texture *> glyph_cache;
  int glyph_width;
  int glyph_height;

  SDL_Texture *create_texture_from_bitmap(const uint8_t *bitmap,
                                         SDL_Color color);
};
```

### Constructor

```cpp
TextRenderer(SDL_Renderer *renderer, int font_size = 16);
```

Constructs a TextRenderer with the specified SDL renderer and font size.

**Parameters:**
- `renderer` - Pointer to an initialized SDL_Renderer
- `font_size` - Font size in pixels (default: 16)

**Initial State:**
- renderer: Stored for later use
- font_size: Stored
- glyph_cache: Empty
- glyph_width: 8
- glyph_height: 8

### Destructor

```cpp
~TextRenderer();
```

Destructor that cleans up cached textures.

**Behavior:**
- Calls `clear_cache()` to destroy all cached glyph textures

### Initialization

#### `init`

```cpp
bool init();
```

Initializes the text renderer.

**Returns:**
- `true` on success
- `false` on failure

**Behavior:**
- Currently always returns true (bitmap font is static)
- Glyph textures are created on-demand in `render_text()`

### Rendering Methods

#### `render_text`

```cpp
SDL_Texture *render_text(const std::string &text,
                         SDL_Color color = {255, 255, 255, 255});
```

Renders text to a new SDL_Texture.

**Parameters:**
- `text` - String to render
- `color` - Text color in RGBA format (default: white)

**Returns:**
- Pointer to a new SDL_Texture containing the rendered text
- The caller owns the returned texture and must destroy it with SDL_DestroyTexture

**Behavior:**
- For each character in the text:
  - If not in cache, creates a texture for that glyph
  - Renders the glyph to a temporary surface
  - Creates a texture from the surface
- Combines glyph textures horizontally to form the complete text
- Returns a new texture with the combined text

**Note:** The returned texture is not cached; each call creates a new texture.

#### `get_text_size`

```cpp
void get_text_size(const std::string &text, int *w, int *h) const;
```

Gets the dimensions of text without rendering it.

**Parameters:**
- `text` - String to measure
- `w` - Output parameter for width in pixels
- `h` - Output parameter for height in pixels

**Behavior:**
- Calculates width based on number of characters and glyph width
- Height is always glyph_height

**Formula:**
```cpp
*w = text.length() * glyph_width;
*h = glyph_height;
```

### Cleanup Methods

#### `clear_cache`

```cpp
void clear_cache();
```

Clears all cached glyph textures.

**Behavior:**
- Iterates through glyph_cache
- Destroys each SDL_Texture with SDL_DestroyTexture
- Clears the cache map

**Use Case:** Call this when the renderer is being destroyed or when you need to free memory.

### Bitmap Font

The class includes a static 8x8 pixel bitmap font for ASCII characters 32-127:

```cpp
static const uint8_t BITMAP_FONT[96][8];
```

**Format:**
- 96 glyphs (ASCII 32 to 127)
- Each glyph is 8x8 pixels
- Each pixel is 1 bit (0 = transparent, 1 = opaque)
- Stored as 8 bytes per glyph (one byte per row)

---

## Usage Example

```cpp
#include "gui/sdl_renderer.h"
#include "gui/text_renderer.h"
#include "openspectrum/runtime_controls.h"

using namespace openspectrum;

int main() {
  const size_t WIDTH = 1024;
  const size_t HEIGHT = 576;

  // Create renderer
  SdlRenderer renderer(WIDTH, HEIGHT, "My Spectrum Analyzer");
  
  if (!renderer.is_valid()) {
    std::cerr << "Failed to initialize SDL3" << std::endl;
    return 1;
  }

  // Create control state
  ControlState state;

  // Main loop
  bool running = true;
  std::vector<uint8_t> pixels(WIDTH * HEIGHT * 4, 0);

  while (running) {
    // Process events (pass ControlState for keyboard handling)
    if (!renderer.poll_events(&state)) {
      running = false;
      break;
    }

    // Update status bar when changed
    if (state.status_changed()) {
      renderer.render_status_bar(state.get_status_string());
      state.clear_status_dirty();
    }

    // Render spectrum (pseudo-code)
    // ... fill pixels with spectrum data ...

    // Render to window
    if (!renderer.render(pixels)) {
      std::cerr << "Render failed" << std::endl;
      running = false;
    }
  }

  return 0;
}
```

---

## Advanced Usage: Text Rendering

```cpp
#include "gui/sdl_renderer.h"
#include "gui/text_renderer.h"

using namespace openspectrum;

int main() {
  SdlRenderer renderer(1024, 576);
  
  // Get the SDL renderer for text rendering
  SDL_Renderer *sdl_renderer = renderer.get_sdl_renderer();
  
  // Create text renderer
  TextRenderer text_renderer(sdl_renderer, 16);
  text_renderer.init();
  
  // Render some text
  SDL_Color white = {255, 255, 255, 255};
  SDL_Texture *text_texture = text_renderer.render_text("Frequency: 100 MHz", white);
  
  // Get text dimensions
  int text_width, text_height;
  text_renderer.get_text_size("Frequency: 100 MHz", &text_width, &text_height);
  
  // Render the texture
  SDL_Rect dest_rect = {10, 10, text_width, text_height};
  SDL_RenderCopy(sdl_renderer, text_texture, nullptr, &dest_rect);
  
  // Clean up
  SDL_DestroyTexture(text_texture);
  
  return 0;
}
```

---

## Error Handling

The SdlRenderer and TextRenderer classes use SDL3's error reporting system. After any operation that returns false or nullptr, you can get the error with:

```cpp
const char *error = SDL_GetError();
std::cerr << "SDL Error: " << error << std::endl;
```

Common SDL3 errors:
- "Could not initialize SDL" - SDL_Init failed
- "No available video device" - No display available
- "Failed to create window" - Window creation failed
- "Failed to create renderer" - Renderer creation failed

---

## Performance Considerations

1. **Double Buffering:** SDL3 uses double buffering by default with `SDL_RENDERER_PRESENTVSYNC`
2. **Hardware Acceleration:** Rendering is hardware-accelerated when available
3. **Texture Caching:** TextRenderer caches glyph textures to avoid re-rendering
4. **Batched Rendering:** All rendering happens in a single pass before presentation
5. **No VSync Overhead:** VSync is enabled for smooth rendering

---

## SdlControlInput Class

Defined in: `src/gui/sdl_control_input.h`

The `SdlControlInput` class provides SDL-specific keyboard input handling for `ControlState`. This class is used internally by `SdlRenderer::poll_events()` but can also be used directly for custom event handling.

```cpp
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
  void set_frequency_step(uint32_t step) noexcept;
  void set_gain_step(float step) noexcept;

private:
  ControlState &m_state;
  uint32_t freq_step = 1000000; // 1 MHz default
  float gain_step = 1.0f;       // 1 dB default
};
```

### Constructor

```cpp
explicit SdlControlInput(ControlState &state);
```

Constructs an SdlControlInput that updates the specified ControlState.

**Parameters:**
- `state` - Reference to the ControlState to update

**Behavior:**
- Stores a reference to the ControlState
- Initializes default step sizes (1 MHz for frequency, 1 dB for gain)

### Destructor

```cpp
~SdlControlInput() = default;
```

Standard destructor.

### Methods

#### `handle_keyboard`

```cpp
bool handle_keyboard(SDL_Keycode key, bool shift_held, bool ctrl_held);
```

Processes a keyboard event and updates the ControlState accordingly.

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
| `+` / `=` | Increase frequency | +100kHz | +10MHz |
| `-` / `_` | Decrease frequency | -100kHz | -10MHz |
| `r` | Increase gain | +0.1 dB | +10 dB |
| `f` | Decrease gain | -0.1 dB | -10 dB |
| `1` | FFT size: 512 | - | - |
| `2` | FFT size: 1024 | - | - |
| `3` | FFT size: 2048 | - | - |
| `4` | FFT size: 4096 | - | - |
| `UP` | Next window function | - | - |
| `DOWN` | Previous window function | - | - |
| `Ctrl+S` | Toggle IQ logging request | - | - |

#### `set_frequency_step`

```cpp
void set_frequency_step(uint32_t step) noexcept;
```

Sets the frequency adjustment step size.

**Parameters:**
- `step` - Step size in Hz

**Default:** 1,000,000 (1 MHz)

#### `set_gain_step`

```cpp
void set_gain_step(float step) noexcept;
```

Sets the gain adjustment step size.

**Parameters:**
- `step` - Step size in dB

**Default:** 1.0 dB

### Usage Example

```cpp
#include "openspectrum/control_state.h"
#include "gui/sdl_control_input.h"

using namespace openspectrum;

int main() {
    ControlState state;
    SdlControlInput input_handler(state);
    
    // Customize step sizes
    input_handler.set_frequency_step(500000); // 500 kHz
    input_handler.set_gain_step(0.5f);       // 0.5 dB
    
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
    
    return 0;
}
```

---

## See Also

- [ControlState](control_state.md) - SDL-agnostic state management
- [RuntimeControls (Deprecated)](runtime_controls.md) - Deprecated class, use ControlState
- [Visualization](visualization.md) - Provides pixel data for rendering
- [SDL3 Documentation](https://wiki.libsdl.org/) - SDL3 library documentation
