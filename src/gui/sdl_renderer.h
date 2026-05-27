// SPDX-License-Identifier: MIT
#pragma once

#include <cstddef>
#include <cstdint>
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
              const std::string &title = "OpenSpectrum SDR",
              bool enable_vsync = false);
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

  // Zero-copy render: writes spectrum (top) and waterfall (bottom) directly
  // into the SDL texture, bypassing the combined_pixels intermediate buffer.
  bool render_displays(const uint8_t *spectrum_data, const uint8_t *waterfall_data,
                       size_t spectrum_height,
                       const std::vector<SDL_Rect> &spec_dirty_rects,
                       const std::vector<SDL_Rect> &wf_dirty_rects);

  // Re-present the last rendered texture with updated overlays (no texture upload).
  // Use when no new pixel data is available (e.g. waiting for samples).
  void present_frame();

  // Steady-state render: GPU-shifts the waterfall texture up by line_height,
  // uploads only new_wf_line_rgba (m_width * line_height * 4 bytes) at the
  // bottom, then composites spectrum + waterfall in one pass.
  bool render_displays_scroll(const uint8_t *spectrum_data,
                               const uint8_t *new_wf_line_rgba,
                               size_t spectrum_height,
                               size_t wf_height,
                               size_t line_height,
                               const std::vector<SDL_Rect> &spec_dirty_rects);

  // Process events. Returns true if should continue, false if quit requested
  // If state is provided, handle keyboard input for control state
  bool poll_events(ControlState *state = nullptr);

  // Render status bar with current control values
  void render_status_bar(const std::string &status_text);

  // Render peak amplitude indicator in top-right corner
  void render_peak_indicator(float peak_db);

  // Render IQ logging status indicator in bottom-left corner
  void render_iq_status(const std::string &iq_text);

  // Render dynamic frequency scale at the bottom of the spectrum display.
  // Labels span center_hz ± sample_rate_hz/2. Caches textures; rebuilds only
  // when center_hz or sample_rate_hz changes.
  void render_frequency_scale(uint32_t center_hz, uint32_t sample_rate_hz,
                               size_t spectrum_height);

  // Get dimensions
  [[nodiscard]] size_t width() const noexcept { return m_width; }
  [[nodiscard]] size_t height() const noexcept { return m_height; }

  // Check if initialized
  [[nodiscard]] bool is_valid() const noexcept {
    return m_window != nullptr && m_renderer != nullptr;
  }

  // Get the SDL renderer (for text rendering)
  [[nodiscard]] SDL_Renderer *get_sdl_renderer() const noexcept { return m_renderer; }

private:
  // Render overlays (status bar, peak indicator, IQ status, freq scale)
  // Called after rendering the main texture
  void render_overlays();

  void rebuild_freq_scale_ticks();

  size_t m_width;
  size_t m_height;
  SDL_Window *m_window = nullptr;
  SDL_Renderer *m_renderer = nullptr;
  SDL_Texture *m_texture = nullptr;

  // VSYNC control
  bool m_enable_vsync = false;

  // Text rendering for status bar
  std::unique_ptr<TextRenderer> m_text_renderer;

  // Texture cache to avoid recreating textures every frame
  struct CachedTexture {
    SDL_Texture* texture = nullptr;
    std::string content;
    SDL_Color color;
    bool valid = false;
  };

  CachedTexture m_status_cache;
  CachedTexture m_peak_cache;
  CachedTexture m_iq_cache;
  SDL_Texture *m_status_texture = nullptr;
  std::string m_current_status;
  bool m_status_dirty = true;

  // Peak amplitude indicator for top-right corner
  SDL_Texture *m_peak_texture = nullptr;

  // IQ logging status indicator for bottom-left corner
  SDL_Texture *m_iq_texture = nullptr;
  std::string m_current_iq_status;

  // GPU ping-pong waterfall scroll textures.
  // m_wf_scroll_tex: the current display frame (TEXTUREACCESS_TARGET).
  // m_wf_scroll_aux: the scratch frame for the next shift+composite.
  // m_wf_line_tex:   narrow streaming texture for the new-line CPU upload.
  SDL_Texture *m_wf_scroll_tex  = nullptr;
  SDL_Texture *m_wf_scroll_aux  = nullptr;
  SDL_Texture *m_wf_line_tex    = nullptr;
  bool         m_wf_scroll_valid = false;
  size_t       m_wf_scroll_line_height = 0;

  bool ensure_wf_scroll_textures(size_t wf_height, size_t line_height);

  // Frequency scale overlay — baked into a single RGBA target texture.
  // Rebuilt only when center_hz or sample_rate changes; render_overlays() does
  // one SDL_RenderCopy per frame instead of N state-changing draw calls.
  SDL_Texture *m_freq_scale_texture = nullptr;
  uint32_t m_freq_scale_center_hz = 0;
  uint32_t m_freq_scale_sample_rate_hz = 0;
  size_t m_freq_scale_spectrum_height = 0;
};

} // namespace openspectrum
