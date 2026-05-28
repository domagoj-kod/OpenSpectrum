// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstdint>
#include <string>
#include <unordered_map>

#include <SDL2/SDL.h>

namespace openspectrum {

// Simple text renderer that caches glyphs as textures
class TextRenderer {
public:
  TextRenderer(SDL_Renderer *renderer, int font_size = 16);
  ~TextRenderer();

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

  // Cleanup all cached textures
  void clear_cache();

private:
  SDL_Renderer *renderer = nullptr;
  int font_size = 16;

  // Simple bitmap font (8x8 pixels per glyph)
  // Contains ASCII 32-127
  static const uint8_t BITMAP_FONT[96][8];

  // Glyph cache: character -> texture
  std::unordered_map<char, SDL_Texture *> glyph_cache;
  int glyph_width = 8;
  int glyph_height = 8;

  // Create a texture from bitmap data
  SDL_Texture *create_texture_from_bitmap(const uint8_t *bitmap,
                                          SDL_Color color);
};

} // namespace openspectrum
