// SPDX-License-Identifier: MIT
#pragma once

#include <cstddef>
#include <memory>
#include <string>
#include <vector>

#include <SDL2/SDL.h>

namespace openspectrum {

class RuntimeControls;
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

  // Process events. Returns true if should continue, false if quit requested
  // If controls is provided, handle keyboard input for runtime controls
  bool poll_events(RuntimeControls *controls = nullptr);

  // Render status bar with current control values
  void render_status_bar(const std::string &status_text);

  // Render peak amplitude indicator in top-right corner
  void render_peak_indicator(float peak_db);

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
};

} // namespace openspectrum