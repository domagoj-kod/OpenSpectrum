// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstdint>
#include <string>

#include <SDL3/SDL.h>

namespace openspectrum {

// Bitmap-font text. render_text() returns a fresh SDL_Texture owned by the
// caller; call sites cache it.
class TextRenderer {
public:
  TextRenderer(SDL_Renderer *renderer, int font_size = 16);
  ~TextRenderer() = default;

  TextRenderer(const TextRenderer &) = delete;
  TextRenderer &operator=(const TextRenderer &) = delete;

  bool init();

  SDL_Texture *render_text(const std::string &text,
                           SDL_Color color = {255, 255, 255, 255});

  void get_text_size(const std::string &text, int *w, int *h) const;

private:
  SDL_Renderer *renderer = nullptr;

  // The 8x16 IBM VGA font.
  static constexpr int GLYPH_SRC_W = 8;
  static constexpr int GLYPH_SRC_H = 16;

  // ASCII 32-127, index = code - 32.
  static const uint8_t BITMAP_FONT[96][16];

  int glyph_width = GLYPH_SRC_W;
  int glyph_height = GLYPH_SRC_H;
};

} // namespace openspectrum
