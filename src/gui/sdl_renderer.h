// SPDX-License-Identifier: MIT
#pragma once

#include <SDL2/SDL.h>
#include <cstddef>
#include <string>
#include <vector>

namespace openspectrum {

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
  bool poll_events();

  // Get dimensions
  size_t width() const noexcept { return m_width; }
  size_t height() const noexcept { return m_height; }

  // Check if initialized
  bool is_valid() const noexcept {
    return m_window != nullptr && m_renderer != nullptr;
  }

private:
  size_t m_width;
  size_t m_height;
  SDL_Window *m_window = nullptr;
  SDL_Renderer *m_renderer = nullptr;
  SDL_Texture *m_texture = nullptr;
};

} // namespace openspectrum