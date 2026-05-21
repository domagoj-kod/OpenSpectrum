// SPDX-License-Identifier: MIT

#include "sdl_renderer.h"
#include "sdl_control_input.h"
#include "text_renderer.h"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <iostream>
#include <memory>
#include <stdexcept>
#include <string>
#include <vector>

#include <SDL2/SDL.h>
#include <SDL2/SDL_error.h>
#include <SDL2/SDL_events.h>
#include <SDL2/SDL_keyboard.h>
#include <SDL2/SDL_keycode.h>
#include <SDL2/SDL_pixels.h>
#include <SDL2/SDL_rect.h>
#include <SDL2/SDL_render.h>
#include <SDL2/SDL_scancode.h>
#include <SDL2/SDL_stdinc.h>
#include <SDL2/SDL_video.h>

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

  if (m_window == nullptr) {
    SDL_Quit();
    throw std::runtime_error("SDL_CreateWindow failed: " +
                             std::string(SDL_GetError()));
  }

  // Try for hardware acceleration, fall back to software
  m_renderer = SDL_CreateRenderer(
      m_window, -1, SDL_RENDERER_ACCELERATED | SDL_RENDERER_PRESENTVSYNC);

  if (m_renderer == nullptr) {
    m_renderer = SDL_CreateRenderer(m_window, -1, SDL_RENDERER_SOFTWARE);
    if (m_renderer == nullptr) {
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

  if (m_texture == nullptr) {
    SDL_DestroyRenderer(m_renderer);
    SDL_DestroyWindow(m_window);
    SDL_Quit();
    throw std::runtime_error("SDL_CreateTexture failed: " +
                             std::string(SDL_GetError()));
  }

  // Initialize text renderer for status bar
  m_text_renderer = std::make_unique<TextRenderer>(m_renderer, 16);
  if (!m_text_renderer->init()) {
    std::cerr << "Warning: Failed to initialize text renderer" << '\n';
  }
  m_status_texture = nullptr;
  m_status_dirty = true;
}

SdlRenderer::~SdlRenderer() {
  if (m_iq_texture != nullptr) {
    SDL_DestroyTexture(m_iq_texture);
  }
  if (m_peak_texture != nullptr) {
    SDL_DestroyTexture(m_peak_texture);
  }
  if (m_status_texture != nullptr) {
    SDL_DestroyTexture(m_status_texture);
  }
  if (m_texture != nullptr) {
    SDL_DestroyTexture(m_texture);
  }
  if (m_renderer != nullptr) {
    SDL_DestroyRenderer(m_renderer);
  }
  if (m_window != nullptr) {
    SDL_DestroyWindow(m_window);
  }
  SDL_Quit();
}

void SdlRenderer::render_overlays() {
  // Render status bar on top
  if (m_status_texture != nullptr) {
    int text_width = 0;
    int text_height = 0;
    m_text_renderer->get_text_size(m_current_status, &text_width, &text_height);

    SDL_Rect const dest_rect = {
        static_cast<int>(m_width - text_width - 10), // 10px margin from right
        static_cast<int>(m_height - text_height -
                         10), // 10px margin from bottom
        text_width, text_height};
    SDL_RenderCopy(m_renderer, m_status_texture, nullptr, &dest_rect);
  }

  // Render IQ logging status in top-right corner (below PEAK)
  if (m_iq_texture != nullptr) {
    int text_width = 0;
    int text_height = 0;
    m_text_renderer->get_text_size(m_current_iq_status, &text_width, &text_height);

    // Position below PEAK indicator (which is at y=6 with background padding)
    // PEAK background height: text_height + 10 (5px padding each side)
    int peak_text_height = 0;
    m_text_renderer->get_text_size("PEAK: -00.0 dB", nullptr, &peak_text_height);
    int const peak_bg_height = peak_text_height + 10;

    SDL_Rect const iq_dest_rect = {
        static_cast<int>(m_width - text_width - 16), // Match PEAK X position
        6 + peak_bg_height + 4, // Below PEAK background with 4px gap
        text_width, text_height};
    SDL_RenderCopy(m_renderer, m_iq_texture, nullptr, &iq_dest_rect);
  }

  // Render peak indicator in top-right corner with semi-transparent background
  if (m_peak_texture != nullptr) {
    int text_width = 0;
    int text_height = 0;
    m_text_renderer->get_text_size("PEAK: -00.0 dB", &text_width, &text_height);

    // Background rectangle (slightly larger than text with padding)
    SDL_Rect const bg_rect = {
        static_cast<int>(m_width - text_width - 16), // 16px from right
        6,                                           // 6px from top
        text_width + 10,                             // 5px padding on each side
        text_height + 10 // 5px padding on top/bottom
    };

    // Save current draw color
    Uint8 r;
    Uint8 g;
    Uint8 b;
    Uint8 a;
    SDL_GetRenderDrawColor(m_renderer, &r, &g, &b, &a);

    // Set background color (dark semi-transparent black)
    SDL_SetRenderDrawColor(m_renderer, 0, 0, 0, 192);
    SDL_RenderFillRect(m_renderer, &bg_rect);

    // Render text on top of background (centered in backdrop)
    SDL_Rect const text_rect = {
        static_cast<int>(m_width - text_width - 13), // Center in backdrop
        8,                                           // 8px from top
        text_width, text_height};
    SDL_RenderCopy(m_renderer, m_peak_texture, nullptr, &text_rect);

    // Restore draw color
    SDL_SetRenderDrawColor(m_renderer, r, g, b, a);
  }
}

auto SdlRenderer::render(const std::vector<uint8_t> &pixels, size_t pitch)
    -> bool {
  if ((m_texture == nullptr) || pixels.size() < m_width * m_height * 4) {
    return false;
  }

  // Full screen dirty rect (fallback to full update)
  SDL_Rect full_rect = {0, 0, static_cast<int>(m_width), static_cast<int>(m_height)};
  return render_with_dirty_regions(pixels, pitch, {full_rect});
}

auto SdlRenderer::render_with_dirty_regions(
    const std::vector<uint8_t> &pixels,
    size_t pitch,
    const std::vector<SDL_Rect> &dirty_rects) -> bool {
  if (m_texture == nullptr || pixels.size() < m_width * m_height * 4) {
    return false;
  }

  // If no dirty rects provided, do full render
  if (dirty_rects.empty()) {
    return render(pixels, pitch);
  }

  // Lock texture for direct pixel access
  void *texture_pixels = nullptr;
  int texture_pitch = 0;
  if (SDL_LockTexture(m_texture, nullptr, &texture_pixels, &texture_pitch) !=
      0) {
    std::cerr << "SDL_LockTexture failed: " << SDL_GetError() << '\n';
    return false;
  }

  const size_t src_pitch = pitch > 0 ? pitch : m_width * 4;

  // Copy only dirty regions
  for (const auto &rect : dirty_rects) {
    // Clamp rect to texture bounds
    int clamped_x = std::max(0, rect.x);
    int clamped_y = std::max(0, rect.y);
    int clamped_w = std::min(rect.w, static_cast<int>(m_width) - clamped_x);
    int clamped_h = std::min(rect.h, static_cast<int>(m_height) - clamped_y);

    if (clamped_w <= 0 || clamped_h <= 0) continue;

    for (int y = clamped_y; y < clamped_y + clamped_h; ++y) {
      const uint8_t *src_row = pixels.data() + (y * src_pitch) + (clamped_x * 4);
      uint8_t *dst_row =
          static_cast<uint8_t *>(texture_pixels) + (y * texture_pitch) + (clamped_x * 4);

      size_t copy_size = static_cast<size_t>(clamped_w) * 4;
      std::copy(src_row, src_row + copy_size, dst_row);
    }
  }

  SDL_UnlockTexture(m_texture);

  // Clear and render
  SDL_RenderClear(m_renderer);
  SDL_RenderCopy(m_renderer, m_texture, nullptr, nullptr);

  // Render overlays on top
  render_overlays();

  SDL_RenderPresent(m_renderer);

  return true;
}

auto SdlRenderer::poll_events(ControlState *state) -> bool {
  SDL_Event event;
  while (SDL_PollEvent(&event) != 0) {
    switch (event.type) {
    case SDL_QUIT:
      return false;

    case SDL_KEYDOWN:
      if (event.key.keysym.sym == SDLK_ESCAPE ||
          event.key.keysym.sym == SDLK_q) {
        return false;
      }

      // Handle control state if provided
      if (state != nullptr) {
        // Create temporary SDL control input handler
        SdlControlInput control_input(*state);

        // Get modifier state
        const Uint8 *keystate = SDL_GetKeyboardState(nullptr);
        bool const shift_held = (keystate[SDL_SCANCODE_LSHIFT] != 0U) ||
                                (keystate[SDL_SCANCODE_RSHIFT] != 0U);
        bool const ctrl_held = (keystate[SDL_SCANCODE_LCTRL] != 0U) ||
                               (keystate[SDL_SCANCODE_RCTRL] != 0U);

        // Handle control keys
        if (control_input.handle_keyboard(event.key.keysym.sym, shift_held,
                                          ctrl_held)) {
          m_status_dirty = true;
        }
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

void SdlRenderer::render_status_bar(const std::string &status_text) {
  // Only re-render if text changed
  if (status_text == m_current_status && !m_status_dirty) {
    return;
  }

  m_current_status = status_text;
  m_status_dirty = false;

  // Destroy old texture
  if (m_status_texture != nullptr) {
    SDL_DestroyTexture(m_status_texture);
    m_status_texture = nullptr;
  }

  // Render new status text
  if (!status_text.empty()) {
    SDL_Color const text_color = {255, 255, 255, 255}; // White
    m_status_texture = m_text_renderer->render_text(status_text, text_color);
  }
}

void SdlRenderer::render_peak_indicator(float peak_db) {
  if (peak_db < -140.0F) {
    // Invalid peak, destroy texture if exists
    if (m_peak_texture != nullptr) {
      SDL_DestroyTexture(m_peak_texture);
      m_peak_texture = nullptr;
    }
    return;
  }

  char buf[32];
  snprintf(buf, sizeof(buf), "PEAK: %.1f dB", peak_db);

  // Destroy old texture
  if (m_peak_texture != nullptr) {
    SDL_DestroyTexture(m_peak_texture);
  }

  // Render new text
  SDL_Color const text_color = {255, 255, 255, 255}; // White
  m_peak_texture = m_text_renderer->render_text(buf, text_color);
}

void SdlRenderer::render_iq_status(const std::string &iq_text) {
  // Only re-render if text changed
  if (iq_text == m_current_iq_status) {
    return;
  }
  m_current_iq_status = iq_text;

  // Destroy old texture
  if (m_iq_texture != nullptr) {
    SDL_DestroyTexture(m_iq_texture);
    m_iq_texture = nullptr;
  }

  // Render new text if not empty
  if (!iq_text.empty()) {
    SDL_Color const text_color = {255, 255, 255, 255}; // White
    m_iq_texture = m_text_renderer->render_text(iq_text, text_color);
  }
}

} // namespace openspectrum
