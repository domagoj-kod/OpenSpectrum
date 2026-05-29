// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

#include <SDL2/SDL.h>

#include "openspectrum/attributes.h"

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

  // Zero-copy render: writes spectrum (top) and waterfall (bottom) directly
  // into the SDL texture, bypassing the combined_pixels intermediate buffer.
  OS_HOT bool render_displays(const uint8_t *spectrum_data, const uint8_t *waterfall_data,
                              size_t spectrum_height,
                              const std::vector<SDL_Rect> &spec_dirty_rects,
                              const std::vector<SDL_Rect> &wf_dirty_rects);

  // Re-present the last rendered texture with updated overlays (no texture upload).
  // Use when no new pixel data is available (e.g. waiting for samples).
  OS_HOT void present_frame();

  // Steady-state render: GPU-shifts the waterfall texture up by line_height,
  // uploads only new_wf_line_rgba (m_width * line_height * 4 bytes) at the
  // bottom, then composites spectrum + waterfall in one pass.
  OS_HOT bool render_displays_scroll(const uint8_t *spectrum_data,
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

  // DEBUG (frame-timing branch): wall-clock duration in milliseconds of the
  // most recent SDL_RenderPresent call. Lets the main loop split the SDL
  // present/swap/flush cost out of the total render time. One present per
  // frame, so this is overwritten (not accumulated) each render call.
  [[nodiscard]] double last_present_ms() const noexcept { return m_last_present_ms; }

  // DEBUG: supply the latest per-second timing snapshot for the on-screen
  // overlay (toggled with 'T'). When enabled, render_overlays() draws it
  // top-left. Call once per frame before any render/present.
  void set_timing_overlay(bool enabled, double fps, double cpu_ms,
                          double render_build_ms, double present_ms) noexcept;

private:
  // Render overlays (status bar, peak indicator, IQ status, freq scale)
  // Called after rendering the main texture
  void render_overlays();

  // DEBUG (frame-timing branch): SDL_RenderPresent wrapped in a steady-clock
  // measurement, result stored in m_last_present_ms.
  void present_timed();

  void rebuild_freq_scale_ticks();

  size_t m_width;
  size_t m_height;
  SDL_Window *m_window = nullptr;
  SDL_Renderer *m_renderer = nullptr;
  SDL_Texture *m_texture = nullptr;

  // VSYNC control
  bool m_enable_vsync = false;

  // DEBUG (frame-timing branch): duration of the last present, see last_present_ms().
  double m_last_present_ms = 0.0;

  // DEBUG: latest timing snapshot + visibility for the 'T' overlay.
  bool   m_timing_overlay_enabled = false;
  double m_timing_fps = 0.0;
  double m_timing_cpu_ms = 0.0;
  double m_timing_build_ms = 0.0;
  double m_timing_present_ms = 0.0;

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
