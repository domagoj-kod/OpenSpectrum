// SPDX-License-Identifier: MIT
#pragma once

#include <cstddef>
#include <memory>
#include <string>
#include <vector>

#include <SDL2/SDL.h>

namespace openspectrum {

class ControlState;
class SdlControlInput;
class TextRenderer;

class SdlRenderer {
public:
  SdlRenderer(size_t width, size_t height,
              const std::string &title = "OpenSpectrum SDR");
  ~SdlRenderer();

  // Non-copyable, non-movable (SDL resources)
  SdlRenderer(const SdlRenderer &) = delete;
  SdlRenderer &operator=(const SdlRenderer &) = delete;

  // Render RGBA pixel buffer (width * height * 4 bytes)
  // Returns true on success, false on error
  bool render(const std::vector<uint8_t> &pixels, size_t pitch = 0);

  // Render with dirty regions for incremental updates
  // Only updates specified rectangles in the texture
  // Returns true on success, false on error
  bool render_with_dirty_regions(
      const std::vector<uint8_t> &pixels,
      size_t pitch,
      const std::vector<SDL_Rect> &dirty_rects);

  // Process events. Returns true if should continue, false if quit requested
  // If state is provided, handle keyboard input for control state
  bool poll_events(ControlState *state = nullptr);

  // Render status bar with current control values
  void render_status_bar(const std::string &status_text);

  // Render peak amplitude indicator in top-right corner
  void render_peak_indicator(float peak_db);

  // Render IQ logging status indicator in bottom-left corner
  void render_iq_status(const std::string &iq_text);

  // Get dimensions
  size_t width() const noexcept { return m_width; }
  size_t height() const noexcept { return m_height; }

  // Check if initialized
  bool is_valid() const noexcept {
    return m_window != nullptr && m_renderer != nullptr;
  }

  // Get the SDL renderer (for text rendering)
  SDL_Renderer *get_sdl_renderer() const noexcept { return m_renderer; }

private:
  // Render overlays (status bar, peak indicator, IQ status)
  // Called after rendering the main texture
  void render_overlays();

  size_t m_width;
  size_t m_height;
  SDL_Window *m_window = nullptr;
  SDL_Renderer *m_renderer = nullptr;
  SDL_Texture *m_texture = nullptr;

  // Text rendering for status bar
  std::unique_ptr<TextRenderer> m_text_renderer;
  SDL_Texture *m_status_texture = nullptr;
  std::string m_current_status;
  bool m_status_dirty = true;

  // Peak amplitude indicator for top-right corner
  SDL_Texture *m_peak_texture = nullptr;

  // IQ logging status indicator for bottom-left corner
  SDL_Texture *m_iq_texture = nullptr;
  std::string m_current_iq_status;
};

} // namespace openspectrum
