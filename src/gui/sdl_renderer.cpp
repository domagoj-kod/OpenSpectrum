// SPDX-License-Identifier: GPL-3.0-or-later

#include "sdl_renderer.h"
#include "sdl_control_input.h"
#include "text_renderer.h"

#include <algorithm>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstring>
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
#include <logger.h>

namespace openspectrum {

static uint32_t nice_tick_interval(uint32_t sample_rate_hz) {
  static const uint32_t NICE[] = {
      10'000, 20'000, 25'000, 50'000, 100'000, 200'000, 250'000, 500'000,
      1'000'000, 2'000'000, 2'500'000, 5'000'000, 10'000'000
  };
  uint32_t const target = sample_rate_hz / 6;
  for (uint32_t n : NICE) {
    if (n >= target) return n;
  }
  return NICE[sizeof(NICE) / sizeof(NICE[0]) - 1];
}

static std::string format_freq_label(int64_t hz) {
  char buf[32];
  if (hz >= 1'000'000LL) {
    if (hz % 1'000'000LL == 0)
      snprintf(buf, sizeof(buf), "%lld MHz", static_cast<long long>(hz / 1'000'000LL));
    else if (hz % 100'000LL == 0)
      snprintf(buf, sizeof(buf), "%.1f MHz", static_cast<double>(hz) / 1e6);
    else
      snprintf(buf, sizeof(buf), "%.2f MHz", static_cast<double>(hz) / 1e6);
  } else {
    if (hz % 1'000LL == 0)
      snprintf(buf, sizeof(buf), "%lld kHz", static_cast<long long>(hz / 1'000LL));
    else
      snprintf(buf, sizeof(buf), "%.1f kHz", static_cast<double>(hz) / 1e3);
  }
  return {buf};
}

SdlRenderer::SdlRenderer(size_t width, size_t height, const std::string &title,
                         bool enable_vsync)
    : m_width(width), m_height(height), m_enable_vsync(enable_vsync) {
  if (SDL_Init(SDL_INIT_VIDEO) < 0) {
    throw std::runtime_error("SDL_Init failed: " + std::string(SDL_GetError()));
  }

  // D3D9 LockTexture stalls the CPU-GPU pipeline every frame (requires sync).
  // D3D11 Map(WRITE_DISCARD) avoids this. Force it before renderer creation.
#ifdef _WIN32
  SDL_SetHint(SDL_HINT_RENDER_DRIVER, "direct3d11");
#endif

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
  // VSYNC disabled by default for performance (can be enabled via constructor flag)
  int renderer_flags = SDL_RENDERER_ACCELERATED;
  if (m_enable_vsync) {
    renderer_flags |= SDL_RENDERER_PRESENTVSYNC;
  }

  m_renderer = SDL_CreateRenderer(m_window, -1, renderer_flags);

  // SDL_GetRendererInfo with a NULL renderer is UB; info.name isn't
  // explicitly documented non-null on success either.
  SDL_RendererInfo info;
  if (m_renderer != nullptr && SDL_GetRendererInfo(m_renderer, &info) == 0 &&
      info.name != nullptr) {
    const char *renderer_name = info.name;
    bool is_software = (strstr(renderer_name, "llvmpipe") != nullptr) ||
                       (strstr(renderer_name, "swrast") != nullptr) ||
                       (info.flags & SDL_RENDERER_SOFTWARE);

    std::string vsync_str = (info.flags & SDL_RENDERER_PRESENTVSYNC) ? "ON" : "OFF";
    std::string accel_str = (info.flags & SDL_RENDERER_ACCELERATED) ? "YES" : "NO";
    LOG_INFO("Renderer: " + std::string(renderer_name) +
             ", VSYNC: " + vsync_str +
             ", Accelerated: " + accel_str);

    if (is_software) {
      LOG_ERROR("WARNING: Software rendering detected! Renderer: " +
                std::string(renderer_name) + " - NO GPU ACCELERATION");
    }
  }

  if (m_renderer == nullptr) {
    LOG_WARNING("Hardware acceleration failed: " + std::string(SDL_GetError()) +
                ". Falling back to software renderer.");
    renderer_flags = SDL_RENDERER_SOFTWARE;
    if (m_enable_vsync) {
      renderer_flags |= SDL_RENDERER_PRESENTVSYNC;
    }
    m_renderer = SDL_CreateRenderer(m_window, -1, renderer_flags);
    if (m_renderer == nullptr) {
      SDL_DestroyWindow(m_window);
      SDL_Quit();
      throw std::runtime_error("SDL_CreateRenderer failed: " +
                               std::string(SDL_GetError()));
    }
    // Re-log after fallback. Same defensive pattern as the primary query:
    // SDL_GetRendererInfo with a NULL renderer is UB, and info.name is not
    // explicitly documented as non-null on success.
    if (m_renderer != nullptr && SDL_GetRendererInfo(m_renderer, &info) == 0 &&
        info.name != nullptr) {
      LOG_WARNING("Fallback renderer: " + std::string(info.name));
    }
  }

  // Render at a fixed logical resolution. SDL then scales the whole scene to
  // the actual window and letterboxes the remainder in black on resize /
  // fullscreen, with no per-resize handling. Without this, a resized window
  // left the composited content at native size in a corner and the uncleared
  // remainder flashed arbitrary back-buffer colors every frame (a
  // photosensitivity hazard), and non-integer waterfall scaling produced a
  // crawling 1px seam. Logical size == window size in the default case, so the
  // unresized path is unchanged.
  SDL_RenderSetLogicalSize(m_renderer, static_cast<int>(width),
                           static_cast<int>(height));
  // Keep the clear color black so every SDL_RenderClear (and the letterbox
  // bars) are black. The only code that changes the draw color (peak indicator,
  // timing overlay) saves and restores it, so this stays black for all frames.
  SDL_SetRenderDrawColor(m_renderer, 0, 0, 0, 255);

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
  if (m_wf_line_tex    != nullptr) SDL_DestroyTexture(m_wf_line_tex);
  if (m_wf_scroll_aux  != nullptr) SDL_DestroyTexture(m_wf_scroll_aux);
  if (m_wf_scroll_tex  != nullptr) SDL_DestroyTexture(m_wf_scroll_tex);
  if (m_freq_scale_texture != nullptr) SDL_DestroyTexture(m_freq_scale_texture);

  // Clean up cached textures (only if they differ from current textures)
  if (m_status_cache.texture != nullptr && m_status_cache.texture != m_status_texture) {
    SDL_DestroyTexture(m_status_cache.texture);
  }
  if (m_peak_cache.texture != nullptr && m_peak_cache.texture != m_peak_texture) {
    SDL_DestroyTexture(m_peak_cache.texture);
  }
  if (m_iq_cache.texture != nullptr && m_iq_cache.texture != m_iq_texture) {
    SDL_DestroyTexture(m_iq_cache.texture);
  }

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

    // Scale the bar down to fit the window width so a long status string (e.g.
    // once the palette field is appended) can't overflow the left edge and clip
    // FREQ. When it already fits, scale == 1 and it sits bottom-right as before;
    // when too wide it shrinks proportionally (aspect preserved) to fill the
    // available width. Robust to narrow / resized windows.
    const int avail = static_cast<int>(m_width) - 20; // 10px margin each side
    double scale = 1.0;
    if (text_width > avail && text_width > 0) {
      scale = static_cast<double>(avail) / static_cast<double>(text_width);
    }
    const int draw_w = static_cast<int>(text_width * scale);
    const int draw_h = static_cast<int>(text_height * scale);

    SDL_Rect const dest_rect = {
        static_cast<int>(m_width) - draw_w - 10,  // 10px margin from right
        static_cast<int>(m_height) - draw_h - 10, // 10px margin from bottom
        draw_w, draw_h};
    SDL_RenderCopy(m_renderer, m_status_texture, nullptr, &dest_rect);
  }

  // Render IQ logging status in top-right corner (below PEAK)
  if (m_iq_texture != nullptr) {
    int text_width = 0;
    int text_height = 0;
    m_text_renderer->get_text_size(m_current_iq_status, &text_width,
                                   &text_height);

    // Position below PEAK indicator (which is at y=6 with background padding)
    // PEAK background height: text_height + 10 (5px padding each side)
    int peak_text_height = 0;
    m_text_renderer->get_text_size("PEAK: -00.0 dB", nullptr,
                                   &peak_text_height);
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

  // Frequency scale — one copy of the pre-baked target texture per frame.
  // No state changes, no per-tick draw calls, no blend-mode save/restore.
  if (m_freq_scale_texture != nullptr && m_freq_scale_spectrum_height >= 22) {
    const int scale_h = 22;
    const int scale_y = static_cast<int>(m_freq_scale_spectrum_height) - scale_h;
    SDL_Rect const dst = {0, scale_y, static_cast<int>(m_width), scale_h};
    SDL_RenderCopy(m_renderer, m_freq_scale_texture, nullptr, &dst);
  }

  // DEBUG: frame-timing overlay (toggled with 'T'), top-left, green on a
  // semi-transparent backdrop. Text textures are created/destroyed per frame —
  // acceptable because this path is off by default.
  if (m_timing_overlay_enabled && m_text_renderer) {
    char lines[4][32];
    std::snprintf(lines[0], sizeof(lines[0]), "fps  %.1f", m_timing_fps);
    std::snprintf(lines[1], sizeof(lines[1]), "cpu  %.2f ms", m_timing_cpu_ms);
    std::snprintf(lines[2], sizeof(lines[2]), "bld  %.2f ms", m_timing_build_ms);
    std::snprintf(lines[3], sizeof(lines[3]), "pres %.2f ms", m_timing_present_ms);

    int line_h = 0;
    int max_w = 0;
    for (auto &s : lines) {
      int w = 0;
      int h = 0;
      m_text_renderer->get_text_size(s, &w, &h);
      if (w > max_w) max_w = w;
      if (h > line_h) line_h = h;
    }

    const int pad = 5;
    const int x0 = 8;
    const int y0 = 8;
    SDL_Rect const bg = {x0 - pad, y0 - pad, max_w + 2 * pad,
                         4 * line_h + 2 * pad};
    Uint8 r;
    Uint8 g;
    Uint8 b;
    Uint8 a;
    SDL_GetRenderDrawColor(m_renderer, &r, &g, &b, &a);
    SDL_SetRenderDrawColor(m_renderer, 0, 0, 0, 192);
    SDL_RenderFillRect(m_renderer, &bg);
    SDL_SetRenderDrawColor(m_renderer, r, g, b, a);

    SDL_Color const col = {0, 255, 0, 255};
    for (int i = 0; i < 4; ++i) {
      SDL_Texture *t = m_text_renderer->render_text(lines[i], col);
      if (t != nullptr) {
        int w = 0;
        int h = 0;
        m_text_renderer->get_text_size(lines[i], &w, &h);
        SDL_SetTextureBlendMode(t, SDL_BLENDMODE_BLEND);
        SDL_Rect const d = {x0, y0 + i * line_h, w, h};
        SDL_RenderCopy(m_renderer, t, nullptr, &d);
        SDL_DestroyTexture(t);
      }
    }
  }
}

void SdlRenderer::set_timing_overlay(bool enabled, double fps, double cpu_ms,
                                     double render_build_ms,
                                     double present_ms) noexcept {
  m_timing_overlay_enabled = enabled;
  m_timing_fps = fps;
  m_timing_cpu_ms = cpu_ms;
  m_timing_build_ms = render_build_ms;
  m_timing_present_ms = present_ms;
}

auto SdlRenderer::render_displays(const uint8_t *spectrum_data,
                                   const uint8_t *waterfall_data,
                                   size_t spectrum_height,
                                   const std::vector<SDL_Rect> &spec_dirty_rects,
                                   const std::vector<SDL_Rect> &wf_dirty_rects)
    -> bool {
  if (m_texture == nullptr) return false;

  void *texture_pixels = nullptr;
  int texture_pitch = 0;
  if (SDL_LockTexture(m_texture, nullptr, &texture_pixels, &texture_pitch) != 0) {
    std::cerr << "SDL_LockTexture failed: " << SDL_GetError() << '\n';
    return false;
  }

  uint8_t *tex = static_cast<uint8_t *>(texture_pixels);
  const size_t src_pitch = m_width * 4;
  const int wf_height = static_cast<int>(m_height - spectrum_height);

  for (const auto &rect : spec_dirty_rects) {
    int x0 = std::max(0, rect.x);
    int y0 = std::max(0, rect.y);
    int x1 = std::min(rect.x + rect.w, static_cast<int>(m_width));
    int y1 = std::min(rect.y + rect.h, static_cast<int>(spectrum_height));
    if (x1 <= x0 || y1 <= y0) continue;
    size_t copy_bytes = static_cast<size_t>(x1 - x0) * 4;
    for (int y = y0; y < y1; ++y) {
      memcpy(tex + y * texture_pitch + x0 * 4,
             spectrum_data + y * src_pitch + x0 * 4, copy_bytes);
    }
  }

  for (const auto &rect : wf_dirty_rects) {
    int x0 = std::max(0, rect.x);
    int y0 = std::max(0, rect.y);
    int x1 = std::min(rect.x + rect.w, static_cast<int>(m_width));
    int y1 = std::min(rect.y + rect.h, wf_height);
    if (x1 <= x0 || y1 <= y0) continue;
    size_t copy_bytes = static_cast<size_t>(x1 - x0) * 4;
    for (int y = y0; y < y1; ++y) {
      memcpy(tex + (y + static_cast<int>(spectrum_height)) * texture_pitch + x0 * 4,
             waterfall_data + y * src_pitch + x0 * 4, copy_bytes);
    }
  }

  SDL_UnlockTexture(m_texture);

  // Seed the GPU scroll texture from the main texture's waterfall region so
  // the first render_displays_scroll() frame is visually continuous.
  if (m_wf_scroll_tex != nullptr) {
    int wf_h = static_cast<int>(m_height) - static_cast<int>(spectrum_height);
    if (wf_h > 0) {
      SDL_Rect const wf_src = {0, static_cast<int>(spectrum_height),
                               static_cast<int>(m_width), wf_h};
      SDL_SetRenderTarget(m_renderer, m_wf_scroll_tex);
      SDL_RenderCopy(m_renderer, m_texture, &wf_src, nullptr);
      SDL_SetRenderTarget(m_renderer, nullptr);
      m_wf_scroll_valid = true;
    }
  }

  SDL_RenderClear(m_renderer);
  SDL_RenderCopy(m_renderer, m_texture, nullptr, nullptr);
  render_overlays();
  present_timed();
  return true;
}

void SdlRenderer::present_frame() {
  SDL_RenderClear(m_renderer);
  SDL_RenderCopy(m_renderer, m_texture, nullptr, nullptr);
  render_overlays();
  present_timed();
}

// DEBUG (frame-timing branch): time the present so the main loop can subtract
// the swap/flush cost from total render time.
void SdlRenderer::present_timed() {
  auto t0 = std::chrono::steady_clock::now();
  SDL_RenderPresent(m_renderer);
  auto t1 = std::chrono::steady_clock::now();
  m_last_present_ms = std::chrono::duration<double, std::milli>(t1 - t0).count();
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

  // Use cached texture if available and content hasn't changed
  if (m_status_cache.valid && m_status_cache.content == status_text) {
    m_status_texture = m_status_cache.texture;
    return;
  }

  // Destroy old texture
  if (m_status_texture != nullptr) {
    SDL_DestroyTexture(m_status_texture);
    m_status_texture = nullptr;
  }

  // Destroy old cache texture if different
  if (m_status_cache.texture != nullptr && m_status_cache.texture != m_status_texture) {
    SDL_DestroyTexture(m_status_cache.texture);
    m_status_cache.texture = nullptr;
  }

  // Render new status text
  if (!status_text.empty()) {
    SDL_Color const text_color = {255, 255, 255, 255}; // White
    m_status_texture = m_text_renderer->render_text(status_text, text_color);

    // Update cache
    m_status_cache.texture = m_status_texture;
    m_status_cache.content = status_text;
    m_status_cache.color = text_color;
    m_status_cache.valid = true;
  }
}

void SdlRenderer::render_peak_indicator(float peak_db) {
  if (peak_db < -140.0F) {
    // Invalid peak, destroy texture if exists
    if (m_peak_texture != nullptr) {
      SDL_DestroyTexture(m_peak_texture);
      m_peak_texture = nullptr;
    }
    // Clear cache
    if (m_peak_cache.texture != nullptr) {
      SDL_DestroyTexture(m_peak_cache.texture);
      m_peak_cache.texture = nullptr;
    }
    m_peak_cache.valid = false;
    return;
  }

  char buf[32];
  snprintf(buf, sizeof(buf), "PEAK: %.1f dB", peak_db);

  // Use cached texture if content hasn't changed
  if (m_peak_cache.valid && m_peak_cache.content == buf) {
    m_peak_texture = m_peak_cache.texture;
    return;
  }

  // Destroy old texture
  if (m_peak_texture != nullptr) {
    SDL_DestroyTexture(m_peak_texture);
    m_peak_texture = nullptr;
  }

  // Destroy old cache texture if different
  if (m_peak_cache.texture != nullptr && m_peak_cache.texture != m_peak_texture) {
    SDL_DestroyTexture(m_peak_cache.texture);
    m_peak_cache.texture = nullptr;
  }

  // Render new text
  SDL_Color const text_color = {255, 255, 255, 255}; // White
  m_peak_texture = m_text_renderer->render_text(buf, text_color);

  // Update cache
  m_peak_cache.texture = m_peak_texture;
  m_peak_cache.content = buf;
  m_peak_cache.color = text_color;
  m_peak_cache.valid = true;
}

void SdlRenderer::render_iq_status(const std::string &iq_text) {
  // Only re-render if text changed
  if (iq_text == m_current_iq_status) {
    return;
  }
  m_current_iq_status = iq_text;

  // Use cached texture if available and content hasn't changed
  if (m_iq_cache.valid && m_iq_cache.content == iq_text) {
    m_iq_texture = m_iq_cache.texture;
    return;
  }

  // Destroy old texture
  if (m_iq_texture != nullptr) {
    SDL_DestroyTexture(m_iq_texture);
    m_iq_texture = nullptr;
  }

  // Destroy old cache texture if different
  if (m_iq_cache.texture != nullptr && m_iq_cache.texture != m_iq_texture) {
    SDL_DestroyTexture(m_iq_cache.texture);
    m_iq_cache.texture = nullptr;
  }

  // Render new text if not empty
  if (!iq_text.empty()) {
    SDL_Color const text_color = {255, 255, 255, 255}; // White
    m_iq_texture = m_text_renderer->render_text(iq_text, text_color);

    // Update cache
    m_iq_cache.texture = m_iq_texture;
    m_iq_cache.content = iq_text;
    m_iq_cache.color = text_color;
    m_iq_cache.valid = true;
  }
}

bool SdlRenderer::ensure_wf_scroll_textures(size_t wf_height, size_t line_height) {
  bool rebuild = (m_wf_scroll_tex == nullptr);
  // Recreate if line_height changed (FFT size switch)
  if (!rebuild && m_wf_scroll_line_height != line_height) {
    SDL_DestroyTexture(m_wf_scroll_tex);  m_wf_scroll_tex = nullptr;
    SDL_DestroyTexture(m_wf_scroll_aux);  m_wf_scroll_aux = nullptr;
    SDL_DestroyTexture(m_wf_line_tex);    m_wf_line_tex   = nullptr;
    m_wf_scroll_valid = false;
    rebuild = true;
  }
  if (!rebuild) return true;

  const int w = static_cast<int>(m_width);
  const int h = static_cast<int>(wf_height);
  const int lh = static_cast<int>(line_height);

  m_wf_scroll_tex = SDL_CreateTexture(m_renderer, SDL_PIXELFORMAT_RGBA32,
                                       SDL_TEXTUREACCESS_TARGET, w, h);
  m_wf_scroll_aux = SDL_CreateTexture(m_renderer, SDL_PIXELFORMAT_RGBA32,
                                       SDL_TEXTUREACCESS_TARGET, w, h);
  m_wf_line_tex   = SDL_CreateTexture(m_renderer, SDL_PIXELFORMAT_RGBA32,
                                       SDL_TEXTUREACCESS_STREAMING, w, lh);

  if (m_wf_scroll_tex == nullptr || m_wf_scroll_aux == nullptr || m_wf_line_tex == nullptr) {
    return false;
  }

  m_wf_scroll_line_height = line_height;
  return true;
}

bool SdlRenderer::render_displays_scroll(const uint8_t *spectrum_data,
                                          const uint8_t *new_wf_line_rgba,
                                          size_t spectrum_height,
                                          size_t wf_height,
                                          size_t line_height,
                                          const std::vector<SDL_Rect> &spec_dirty_rects) {
  if (m_texture == nullptr) return false;
  if (!ensure_wf_scroll_textures(wf_height, line_height)) return false;

  // --- Spectrum upload (dirty regions only, top half of m_texture) ---
  if (!spec_dirty_rects.empty()) {
    void *tex_pixels = nullptr; int tex_pitch = 0;
    if (SDL_LockTexture(m_texture, nullptr, &tex_pixels, &tex_pitch) == 0) {
      uint8_t *tex = static_cast<uint8_t *>(tex_pixels);
      const size_t src_pitch = m_width * 4;
      for (const auto &r : spec_dirty_rects) {
        int x0 = std::max(0, r.x);
        int y0 = std::max(0, r.y);
        int x1 = std::min(r.x + r.w, static_cast<int>(m_width));
        int y1 = std::min(r.y + r.h, static_cast<int>(spectrum_height));
        if (x1 <= x0 || y1 <= y0) continue;
        size_t bytes = static_cast<size_t>(x1 - x0) * 4;
        for (int y = y0; y < y1; ++y) {
          memcpy(tex + y * tex_pitch + x0 * 4,
                 spectrum_data + y * src_pitch + x0 * 4, bytes);
        }
      }
      SDL_UnlockTexture(m_texture);
    }
  }

  // --- GPU waterfall scroll ---
  // Upload the new line (~5 KB) to the narrow streaming texture
  {
    void *lp = nullptr; int lpitch = 0;
    if (SDL_LockTexture(m_wf_line_tex, nullptr, &lp, &lpitch) == 0) {
      const size_t src_row = m_width * 4;
      for (size_t y = 0; y < line_height; ++y) {
        memcpy(static_cast<uint8_t*>(lp) + y * lpitch,
               new_wf_line_rgba + y * src_row, src_row);
      }
      SDL_UnlockTexture(m_wf_line_tex);
    }
  }

  // Render into the aux texture:
  //   1. Blit existing waterfall shifted up by line_height (GPU-to-GPU copy)
  //   2. Place new line at the bottom
  SDL_SetRenderTarget(m_renderer, m_wf_scroll_aux);
  int const shift_h = static_cast<int>(wf_height) - static_cast<int>(line_height);
  if (m_wf_scroll_valid) {
    if (shift_h > 0) {
      SDL_Rect const src_r = {0, static_cast<int>(line_height),
                               static_cast<int>(m_width), shift_h};
      SDL_Rect const dst_r = {0, 0, static_cast<int>(m_width), shift_h};
      SDL_RenderCopy(m_renderer, m_wf_scroll_tex, &src_r, &dst_r);
    }
  } else if (shift_h > 0) {
    // First scroll frame: the scroll textures were just created so they hold
    // garbage. Seed from m_texture's waterfall region (filled by the last
    // full-render pass) shifted up by line_height — produces a continuous
    // transition instead of a black flash + bottom-up refill.
    SDL_Rect const src_r = {0,
                             static_cast<int>(spectrum_height) + static_cast<int>(line_height),
                             static_cast<int>(m_width), shift_h};
    SDL_Rect const dst_r = {0, 0, static_cast<int>(m_width), shift_h};
    SDL_RenderCopy(m_renderer, m_texture, &src_r, &dst_r);
  } else {
    // Pathological: wf_height <= line_height. Just clear.
    SDL_SetRenderDrawColor(m_renderer, 0, 0, 0, 255);
    SDL_RenderClear(m_renderer);
  }
  // Paste new line at the bottom
  SDL_Rect const line_dst = {0, static_cast<int>(wf_height - line_height),
                              static_cast<int>(m_width), static_cast<int>(line_height)};
  SDL_RenderCopy(m_renderer, m_wf_line_tex, nullptr, &line_dst);
  SDL_SetRenderTarget(m_renderer, nullptr);

  // Swap active/aux
  std::swap(m_wf_scroll_tex, m_wf_scroll_aux);
  m_wf_scroll_valid = true;

  // --- Compose final frame ---
  SDL_RenderClear(m_renderer);
  // Spectrum (top)
  SDL_Rect const spec_src = {0, 0, static_cast<int>(m_width),
                              static_cast<int>(spectrum_height)};
  SDL_Rect const spec_dst = spec_src;
  SDL_RenderCopy(m_renderer, m_texture, &spec_src, &spec_dst);
  // Waterfall (bottom) — entire scroll texture maps to the bottom half
  SDL_Rect const wf_dst = {0, static_cast<int>(spectrum_height),
                            static_cast<int>(m_width), static_cast<int>(wf_height)};
  SDL_RenderCopy(m_renderer, m_wf_scroll_tex, nullptr, &wf_dst);

  render_overlays();
  present_timed();
  return true;
}

void SdlRenderer::rebuild_freq_scale_ticks() {
  if (m_freq_scale_texture != nullptr) {
    SDL_DestroyTexture(m_freq_scale_texture);
    m_freq_scale_texture = nullptr;
  }

  if (m_freq_scale_sample_rate_hz == 0 || m_freq_scale_spectrum_height < 22
      || m_text_renderer == nullptr) return;

  constexpr int SCALE_H = 22;

  // Bake the entire scale bar into a single render-target texture.
  // render_overlays() does one SDL_RenderCopy per frame — no state churn.
  m_freq_scale_texture = SDL_CreateTexture(
      m_renderer, SDL_PIXELFORMAT_RGBA32,
      SDL_TEXTUREACCESS_TARGET,
      static_cast<int>(m_width), SCALE_H);
  if (m_freq_scale_texture == nullptr) return;

  SDL_SetTextureBlendMode(m_freq_scale_texture, SDL_BLENDMODE_BLEND);

  SDL_SetRenderTarget(m_renderer, m_freq_scale_texture);

  // Semi-transparent dark background (SDL_RenderClear writes alpha directly)
  SDL_SetRenderDrawColor(m_renderer, 0, 0, 0, 180);
  SDL_RenderClear(m_renderer);

  // Compute tick positions
  uint32_t const interval = nice_tick_interval(m_freq_scale_sample_rate_hz);
  int64_t const start_hz = static_cast<int64_t>(m_freq_scale_center_hz)
                         - static_cast<int64_t>(m_freq_scale_sample_rate_hz) / 2;
  int64_t const end_hz   = start_hz + static_cast<int64_t>(m_freq_scale_sample_rate_hz);
  int64_t const first_tick = ((start_hz + static_cast<int64_t>(interval) - 1)
                               / static_cast<int64_t>(interval))
                             * static_cast<int64_t>(interval);

  SDL_SetRenderDrawColor(m_renderer, 180, 180, 180, 255);
  SDL_Color const label_color = {200, 200, 200, 255};

  for (int64_t freq = first_tick; freq < end_hz; freq += static_cast<int64_t>(interval)) {
    if (freq < 0) continue;
    float const x_frac = static_cast<float>(freq - start_hz)
                       / static_cast<float>(m_freq_scale_sample_rate_hz);
    int const x = static_cast<int>(x_frac * static_cast<float>(m_width));

    // Tick line
    SDL_Rect const tick_line = {x, 0, 1, 4};
    SDL_RenderFillRect(m_renderer, &tick_line);

    // Label — temporary texture, composited onto scale texture then freed
    std::string const label = format_freq_label(freq);
    SDL_Texture *label_tex  = m_text_renderer->render_text(label, label_color);
    if (label_tex != nullptr) {
      SDL_SetTextureBlendMode(label_tex, SDL_BLENDMODE_BLEND);
      int lw = 0;
      m_text_renderer->get_text_size(label, &lw, nullptr);
      int lx = x - lw / 2;
      lx = std::max(0, std::min(lx, static_cast<int>(m_width) - lw));
      SDL_Rect const lrect = {lx, 5, lw, 16};
      SDL_RenderCopy(m_renderer, label_tex, nullptr, &lrect);
      SDL_DestroyTexture(label_tex);
    }
  }

  SDL_SetRenderTarget(m_renderer, nullptr);
}

void SdlRenderer::render_frequency_scale(uint32_t center_hz, uint32_t sample_rate_hz,
                                          size_t spectrum_height) {
  if (center_hz      != m_freq_scale_center_hz
   || sample_rate_hz != m_freq_scale_sample_rate_hz
   || spectrum_height!= m_freq_scale_spectrum_height) {
    m_freq_scale_center_hz      = center_hz;
    m_freq_scale_sample_rate_hz = sample_rate_hz;
    m_freq_scale_spectrum_height= spectrum_height;
    rebuild_freq_scale_ticks();
  }
}

} // namespace openspectrum
