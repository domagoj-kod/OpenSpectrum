// SPDX-License-Identifier: MIT

#include "sdl_renderer.h"
#include <iostream>
#include <stdexcept>

namespace openspectrum {

SdlRenderer::SdlRenderer(size_t width, size_t height, const std::string &title)
    : m_width(width), m_height(height) {
  if (SDL_Init(SDL_INIT_VIDEO) < 0) {
    throw std::runtime_error("SDL_Init failed: " + std::string(SDL_GetError()));
  }

  // Use SDL_RENDERER_ACCELERATED for GPU, fallback to software
  m_window = SDL_CreateWindow(title.c_str(), SDL_WINDOWPOS_CENTERED,
                              SDL_WINDOWPOS_CENTERED, static_cast<int>(width),
                              static_cast<int>(height),
                              SDL_WINDOW_SHOWN | SDL_WINDOW_RESIZABLE);

  if (!m_window) {
    SDL_Quit();
    throw std::runtime_error("SDL_CreateWindow failed: " +
                             std::string(SDL_GetError()));
  }

  // Try for hardware acceleration, fall back to software
  m_renderer = SDL_CreateRenderer(
      m_window, -1, SDL_RENDERER_ACCELERATED | SDL_RENDERER_PRESENTVSYNC);

  if (!m_renderer) {
    m_renderer = SDL_CreateRenderer(m_window, -1, SDL_RENDERER_SOFTWARE);
    if (!m_renderer) {
      SDL_DestroyWindow(m_window);
      SDL_Quit();
      throw std::runtime_error("SDL_CreateRenderer failed: " +
                               std::string(SDL_GetError()));
    }
  }

  // Create texture for RGBA rendering
  m_texture = SDL_CreateTexture(
      m_renderer, SDL_PIXELFORMAT_RGBA32, SDL_TEXTUREACCESS_STREAMING,
      static_cast<int>(width), static_cast<int>(height));

  if (!m_texture) {
    SDL_DestroyRenderer(m_renderer);
    SDL_DestroyWindow(m_window);
    SDL_Quit();
    throw std::runtime_error("SDL_CreateTexture failed: " +
                             std::string(SDL_GetError()));
  }
}

SdlRenderer::~SdlRenderer() {
  if (m_texture)
    SDL_DestroyTexture(m_texture);
  if (m_renderer)
    SDL_DestroyRenderer(m_renderer);
  if (m_window)
    SDL_DestroyWindow(m_window);
  SDL_Quit();
}

bool SdlRenderer::render(const std::vector<uint8_t> &pixels, size_t pitch) {
  if (!m_texture || pixels.size() < m_width * m_height * 4) {
    return false;
  }

  // Lock texture for direct pixel access
  void *texture_pixels = nullptr;
  int texture_pitch = 0;
  if (SDL_LockTexture(m_texture, nullptr, &texture_pixels, &texture_pitch) !=
      0) {
    std::cerr << "SDL_LockTexture failed: " << SDL_GetError() << std::endl;
    return false;
  }

  // Copy pixels (respecting pitch if provided)
  size_t src_pitch = pitch > 0 ? pitch : m_width * 4;
  for (size_t y = 0; y < m_height; ++y) {
    const uint8_t *src_row = pixels.data() + y * src_pitch;
    uint8_t *dst_row =
        static_cast<uint8_t *>(texture_pixels) + y * texture_pitch;
    std::copy(src_row,
              src_row + std::min(src_pitch, static_cast<size_t>(texture_pitch)),
              dst_row);
  }

  SDL_UnlockTexture(m_texture);

  // Clear and render
  SDL_RenderClear(m_renderer);
  SDL_RenderCopy(m_renderer, m_texture, nullptr, nullptr);
  SDL_RenderPresent(m_renderer);

  return true;
}

bool SdlRenderer::poll_events() {
  SDL_Event event;
  while (SDL_PollEvent(&event)) {
    switch (event.type) {
    case SDL_QUIT:
      return false;

    case SDL_KEYDOWN:
      if (event.key.keysym.sym == SDLK_ESCAPE ||
          event.key.keysym.sym == SDLK_q) {
        return false;
      }
      break;

    case SDL_WINDOWEVENT:
      if (event.window.event == SDL_WINDOWEVENT_CLOSE) {
        return false;
      }
      break;
    }
  }
  return true;
}

} // namespace openspectrum