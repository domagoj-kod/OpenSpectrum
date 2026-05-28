// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstdint>
#include <string>

#include <SDL2/SDL.h>

namespace openspectrum {

// Simple bitmap-font text renderer. Each render_text() call returns a fresh
// SDL_Texture owned by the caller — there is no glyph cache (call sites cache
// the rendered string-texture themselves).
class TextRenderer {
public:
  TextRenderer(SDL_Renderer *renderer, int font_size = 16);
  ~TextRenderer() = default;

  // Non-copyable, non-movable
  TextRenderer(const TextRenderer &) = delete;
  TextRenderer &operator=(const TextRenderer &) = delete;

  // Initialize the renderer
  bool init();

  // Render text to a new texture (caller owns the texture)
  SDL_Texture *render_text(const std::string &text,
                           SDL_Color color = {255, 255, 255, 255});

  // Get dimensions of text without rendering
  void get_text_size(const std::string &text, int *w, int *h) const;

private:
  SDL_Renderer *renderer = nullptr;
  int font_size = 16;

  // Simple bitmap font (8x8 pixels per glyph)
  // Contains ASCII 32-127
  static const uint8_t BITMAP_FONT[96][8];

  int glyph_width = 8;
  int glyph_height = 8;
};

} // namespace openspectrum
