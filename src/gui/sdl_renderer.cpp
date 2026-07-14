// SPDX-License-Identifier: GPL-3.0-or-later

#include "sdl_renderer.h"
#include "sdl_control_input.h"
#include "text_renderer.h"

#include <algorithm>
#include <cassert>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <iostream>
#include <memory>
#include <stdexcept>
#include <string>
#include <unordered_map>
#include <vector>

#include <SDL3/SDL.h>
#include <SDL3/SDL_error.h>
#include <SDL3/SDL_events.h>
#include <SDL3/SDL_keyboard.h>
#include <SDL3/SDL_keycode.h>
#include <SDL3/SDL_pixels.h>
#include <SDL3/SDL_rect.h>
#include <SDL3/SDL_render.h>
#include <SDL3/SDL_scancode.h>
#include <SDL3/SDL_stdinc.h>
#include <SDL3/SDL_video.h>
#include <logger.h>

namespace openspectrum {

static uint32_t nice_tick_interval(uint32_t sample_rate_hz) {
  static const uint32_t NICE[] = {
      10'000,  20'000,    25'000,    50'000,    100'000,   200'000,   250'000,
      500'000, 1'000'000, 2'000'000, 2'500'000, 5'000'000, 10'000'000};
  uint32_t const target = sample_rate_hz / 6;
  for (uint32_t n : NICE) {
    if (n >= target)
      return n;
  }
  return NICE[sizeof(NICE) / sizeof(NICE[0]) - 1];
}

static std::string format_freq_label(int64_t hz) {
  char buf[32];
  if (hz >= 1'000'000LL) {
    if (hz % 1'000'000LL == 0)
      snprintf(buf, sizeof(buf), "%lld MHz",
               static_cast<long long>(hz / 1'000'000LL));
    else if (hz % 100'000LL == 0)
      snprintf(buf, sizeof(buf), "%.1f MHz", static_cast<double>(hz) / 1e6);
    else
      snprintf(buf, sizeof(buf), "%.2f MHz", static_cast<double>(hz) / 1e6);
  } else {
    if (hz % 1'000LL == 0)
      snprintf(buf, sizeof(buf), "%lld kHz",
               static_cast<long long>(hz / 1'000LL));
    else
      snprintf(buf, sizeof(buf), "%.1f kHz", static_cast<double>(hz) / 1e3);
  }
  return {buf};
}

// Width (px) of the right-hand instrument panel. The plot region is the window
// width minus this. Fixed: the window uses logical-presentation scaling, so a
// resize scales the whole composited scene rather than reflowing the panel.
static constexpr size_t kPanelWidth = 240;

SdlRenderer::SdlRenderer(size_t width, size_t height, const std::string &title)
    : m_width(width), m_height(height) {
  m_plot_width = (m_width > kPanelWidth) ? m_width - kPanelWidth : m_width;
  if (!SDL_Init(SDL_INIT_VIDEO)) {
    throw std::runtime_error("SDL_Init failed: " + std::string(SDL_GetError()));
  }

  // D3D9 LockTexture stalls the CPU-GPU pipeline every frame (requires sync).
  // D3D11 Map(WRITE_DISCARD) avoids this. Force it before renderer creation.
#ifdef _WIN32
  SDL_SetHint(SDL_HINT_RENDER_DRIVER, "direct3d11");
#endif

  // SDL3: Use SDL_CreateWindowWithProperties for window creation. Position is
  // set explicitly: SDL3's plain SDL_CreateWindow no longer centers windows
  // (SDL2 passed SDL_WINDOWPOS_CENTERED), so without these props placement is
  // left to the window manager.
  SDL_PropertiesID window_props = SDL_CreateProperties();
  SDL_SetStringProperty(window_props, SDL_PROP_WINDOW_CREATE_TITLE_STRING,
                        title.c_str());
  SDL_SetNumberProperty(window_props, SDL_PROP_WINDOW_CREATE_X_NUMBER,
                        SDL_WINDOWPOS_CENTERED);
  SDL_SetNumberProperty(window_props, SDL_PROP_WINDOW_CREATE_Y_NUMBER,
                        SDL_WINDOWPOS_CENTERED);
  SDL_SetNumberProperty(window_props, SDL_PROP_WINDOW_CREATE_WIDTH_NUMBER,
                        static_cast<int>(width));
  SDL_SetNumberProperty(window_props, SDL_PROP_WINDOW_CREATE_HEIGHT_NUMBER,
                        static_cast<int>(height));
  SDL_SetNumberProperty(window_props, SDL_PROP_WINDOW_CREATE_FLAGS_NUMBER,
                        SDL_WINDOW_RESIZABLE);
  m_window = SDL_CreateWindowWithProperties(window_props);
  SDL_DestroyProperties(window_props);

  if (m_window == nullptr) {
    SDL_Quit();
    throw std::runtime_error("SDL_CreateWindow failed: " +
                             std::string(SDL_GetError()));
  }

  // SDL3: a null driver name makes SDL try every renderer in priority order,
  // ending with software — no manual software fallback needed (SDL2 required
  // an explicit second SDL_CreateRenderer call with SDL_RENDERER_SOFTWARE).
  m_renderer = SDL_CreateRenderer(m_window, nullptr);
  if (m_renderer == nullptr) {
    SDL_DestroyWindow(m_window);
    SDL_Quit();
    throw std::runtime_error("SDL_CreateRenderer failed: " +
                             std::string(SDL_GetError()));
  }
  // VSYNC is always on: paces the loop to the display refresh (tear-free) at
  // negligible cost since per-frame work is only a few ms. No flag exposes it.
  SDL_SetRenderVSync(m_renderer, 1);

  const char *renderer_name = SDL_GetRendererName(m_renderer);
  if (renderer_name != nullptr) {
    bool is_software = (strcmp(renderer_name, SDL_SOFTWARE_RENDERER) == 0);
    int vsync_value = 0;
    SDL_GetRenderVSync(m_renderer, &vsync_value);
    std::string vsync_str = (vsync_value != 0) ? "ON" : "OFF";
    std::string accel_str = is_software ? "NO" : "YES";
    LOG_INFO("Renderer: " + std::string(renderer_name) +
             ", VSYNC: " + vsync_str + ", Accelerated: " + accel_str);

    if (is_software) {
      LOG_WARNING(
          "Software rendering (no GPU acceleration) — higher CPU/power, "
          "reduced FPS possible");
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
  SDL_SetRenderLogicalPresentation(m_renderer, static_cast<int>(width),
                                   static_cast<int>(height),
                                   SDL_LOGICAL_PRESENTATION_LETTERBOX);
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
  SDL_SetTextureScaleMode(m_texture, SDL_SCALEMODE_NEAREST);

  // Persistent base-frame compose target (see m_frame_tex docs). Full window
  // size so its blit to screen goes through the same logical-size scaling as
  // the old whole-texture copy did.
  m_frame_tex = SDL_CreateTexture(
      m_renderer, SDL_PIXELFORMAT_RGBA32, SDL_TEXTUREACCESS_TARGET,
      static_cast<int>(width), static_cast<int>(height));
  if (m_frame_tex == nullptr) {
    SDL_DestroyTexture(m_texture);
    SDL_DestroyRenderer(m_renderer);
    SDL_DestroyWindow(m_window);
    SDL_Quit();
    throw std::runtime_error("SDL_CreateTexture (frame) failed: " +
                             std::string(SDL_GetError()));
  }
  SDL_SetTextureBlendMode(m_frame_tex, SDL_BLENDMODE_NONE);
  // SDL3 defaults textures to SDL_SCALEMODE_LINEAR (SDL2 was nearest).
  // m_frame_tex is what the logical-presentation scaling stretches on window
  // resize; nearest keeps the waterfall pixel-crisp like the SDL2 build.
  SDL_SetTextureScaleMode(m_frame_tex, SDL_SCALEMODE_NEAREST);

  // Clear it once so present_frame() shows black (not uninitialized GPU memory)
  // if it runs before the first render_displays() while awaiting samples.
  SDL_SetRenderTarget(m_renderer, m_frame_tex);
  SDL_SetRenderDrawColor(m_renderer, 0, 0, 0, 255);
  SDL_RenderClear(m_renderer);
  SDL_SetRenderTarget(m_renderer, nullptr);

  // Initialize text renderer for status bar
  m_text_renderer = std::make_unique<TextRenderer>(m_renderer, 16);
  if (!m_text_renderer->init()) {
    std::cerr << "Warning: Failed to initialize text renderer" << '\n';
  }
}

SdlRenderer::~SdlRenderer() {
  if (m_wf_line_tex != nullptr)
    SDL_DestroyTexture(m_wf_line_tex);
  if (m_wf_scroll_aux != nullptr)
    SDL_DestroyTexture(m_wf_scroll_aux);
  if (m_wf_scroll_tex != nullptr)
    SDL_DestroyTexture(m_wf_scroll_tex);
  if (m_freq_scale_texture != nullptr)
    SDL_DestroyTexture(m_freq_scale_texture);

  if (m_frame_tex != nullptr) {
    SDL_DestroyTexture(m_frame_tex);
  }
  if (m_texture != nullptr) {
    SDL_DestroyTexture(m_texture);
  }
  for (auto &e : m_text_cache) {
    SDL_DestroyTexture(e.second.tex);
  }
  m_text_cache.clear();
  if (m_renderer != nullptr) {
    SDL_DestroyRenderer(m_renderer);
  }
  if (m_window != nullptr) {
    SDL_DestroyWindow(m_window);
  }
  SDL_Quit();
}

void SdlRenderer::blit_text(const std::string &text, SDL_Color color, float x,
                            float y) {
  if (text.empty()) {
    return;
  }
  // Cache key = 4 colour bytes + the string, so the same text in two colours
  // (e.g. a ghosted "---" marker vs a lit value) caches as two entries.
  std::string key;
  key.reserve(4 + text.size());
  key.push_back(static_cast<char>(color.r));
  key.push_back(static_cast<char>(color.g));
  key.push_back(static_cast<char>(color.b));
  key.push_back(static_cast<char>(color.a));
  key.append(text);

  auto it = m_text_cache.find(key);
  if (it == m_text_cache.end()) {
    // Miss: rasterize + upload once. We own the texture until
    // sweep_text_cache() evicts it (the frame after it goes undrawn).
    SDL_Texture *t = m_text_renderer->render_text(text, color);
    if (t == nullptr) {
      return;
    }
    SDL_SetTextureBlendMode(t, SDL_BLENDMODE_BLEND);
    int w = 0;
    int h = 0;
    m_text_renderer->get_text_size(text, &w, &h);
    it = m_text_cache.emplace(std::move(key), CachedText{t, w, h, true}).first;
  }
  it->second.used = true;
  SDL_FRect const d = {x, y, static_cast<float>(it->second.w),
                       static_cast<float>(it->second.h)};
  SDL_RenderTexture(m_renderer, it->second.tex, nullptr, &d);
}

void SdlRenderer::sweep_text_cache() {
  // Mark-and-sweep, once per frame at the top of render_overlays(): evict any
  // string not drawn last frame and clear the flag on survivors for this
  // frame's blits to re-set. Bounds the cache to the strings actually on
  // screen.
  for (auto it = m_text_cache.begin(); it != m_text_cache.end();) {
    if (it->second.used) {
      it->second.used = false;
      ++it;
    } else {
      SDL_DestroyTexture(it->second.tex);
      it = m_text_cache.erase(it);
    }
  }
#ifndef NDEBUG
  // If the bookkeeping ever drifts (used never cleared, or nothing evicted) the
  // cache grows without bound; a live screen draws well under this ceiling.
  assert(m_text_cache.size() <= 256 &&
         "text cache unbounded — mark-sweep drift");
#endif
}

void SdlRenderer::render_overlays() {
  // Evict last frame's unused string-textures before this frame's blits
  // repopulate the cache (see blit_text / sweep_text_cache).
  sweep_text_cache();

  // Transient one-shot message (export result) — bottom-left over the plot,
  // auto-clearing at its deadline. Status / PEAK / timing all live in the panel
  // now; this is the only text left floating on the plot besides the axes.
  if (!m_status_msg.empty() &&
      std::chrono::steady_clock::now() < m_status_until) {
    blit_text(m_status_msg, {200, 200, 200, 255},
              static_cast<float>(axis_strip_width()) + 8.0F,
              static_cast<float>(m_height) - 24.0F);
  }

  // Frequency scale — one copy of the pre-baked target texture per frame, over
  // the plot region only (the panel owns [plot_width, width)). No state
  // changes, no per-tick draw calls, no blend-mode save/restore.
  if (m_freq_scale_texture != nullptr && m_freq_scale_spectrum_height >= 22) {
    const int scale_h = 22;
    const int scale_y =
        static_cast<int>(m_freq_scale_spectrum_height) - scale_h;
    SDL_FRect const dst = {0.0f, static_cast<float>(scale_y),
                           static_cast<float>(m_plot_width),
                           static_cast<float>(scale_h)};
    SDL_RenderTexture(m_renderer, m_freq_scale_texture, nullptr, &dst);
  }

  draw_left_axes();
  draw_markers();
  draw_trigger();
  draw_cursor_readout();
  render_panel(); // opaque; drawn last so nothing bleeds into the gutter
}

int SdlRenderer::axis_strip_width() const {
  if (!m_axes_enabled || m_text_renderer == nullptr) {
    return 0;
  }
  int w = 0;
  // "-120dB" is the widest label either axis can produce (space dropped).
  m_text_renderer->get_text_size("-120dB", &w, nullptr);
  constexpr int kPad = 2;
  constexpr int kTick = 3;
  return 2 * kPad + w + kTick;
}

void SdlRenderer::draw_left_axes() {
  if (!m_axes_enabled || !m_text_renderer) {
    return;
  }
  const float spec_h = static_cast<float>(m_freq_scale_spectrum_height);
  if (spec_h <= 0.0F) {
    return;
  }
  const float wf_h = static_cast<float>(m_height) - spec_h;

  // Tick positions are FIXED (evenly divided per pane); only the printed values
  // change as the dB range / waterfall age drift. This makes the labels behave
  // like a rolling coarse meter instead of jumping around as the scale
  // oscillates. Style mirrors the bottom frequency bar (dark strip, gray ticks
  // + labels) so the two scales read as one set.
  constexpr int kDiv = 4; // 5 labels per pane (0..kDiv)
  struct Label {
    float y;
    std::string text;
  };
  std::vector<Label> labels;

  // dB scale over the spectrum pane (bottom 22px reserved for the freq bar).
  const float db_usable = spec_h - 22.0F;
  if (db_usable > 20.0F && m_axis_db_max > m_axis_db_min) {
    const double range = m_axis_db_max - m_axis_db_min;
    for (int i = 0; i <= kDiv; ++i) {
      const double frac = static_cast<double>(i) / kDiv; // 0 top .. 1 bottom
      const double v = m_axis_db_max - frac * range;     // top = max dB
      char s[16];
      // Keep the sign: these are dBFS, and a level below full scale is
      // negative. Printing "83dB" for -83 dBFS reads as an error to anyone who
      // works in dB, which is everyone this axis is for.
      std::snprintf(s, sizeof(s), "%.0fdB", v);
      labels.push_back({static_cast<float>(frac) * db_usable, s});
    }
  }

  // Seconds-ago scale over the waterfall pane (top = oldest, bottom = now).
  if (wf_h > 20.0F && m_axis_wf_top_seconds > 0.05) {
    const double top = m_axis_wf_top_seconds;
    for (int i = 0; i <= kDiv; ++i) {
      const double frac = static_cast<double>(i) / kDiv; // 0 top .. 1 bottom
      const double age = top * (1.0 - frac);             // top = oldest
      char s[16];
      if (age < 0.05) {
        std::snprintf(s, sizeof(s), "now");
      } else {
        std::snprintf(s, sizeof(s), "%.1fs", age);
      }
      labels.push_back({spec_h + static_cast<float>(frac) * wf_h, s});
    }
  }

  if (labels.empty()) {
    return;
  }

  constexpr int kPad = 2;
  constexpr int kTick = 3;
  const float strip_w = static_cast<float>(axis_strip_width());

  Uint8 r;
  Uint8 g;
  Uint8 b;
  Uint8 a;
  SDL_GetRenderDrawColor(m_renderer, &r, &g, &b, &a);
  SDL_BlendMode prev_blend = SDL_BLENDMODE_NONE;
  SDL_GetRenderDrawBlendMode(m_renderer, &prev_blend);

  // Translucent strip matching the bottom frequency bar (alpha 180 over the
  // plot). SDL_RenderFillRect honors the *draw* blend mode, which defaults to
  // NONE (alpha ignored → solid black); switch to BLEND so the data shows
  // through like the freq bar does. Drawn as two segments — spectrum pane
  // (above the freq bar) + waterfall pane — leaving the 22px freq-bar band
  // clear so the vertical strip doesn't double-darken / collide with the
  // horizontal bar at the bottom-left corner.
  constexpr float kFreqBar = 22.0F;
  SDL_SetRenderDrawBlendMode(m_renderer, SDL_BLENDMODE_BLEND);
  SDL_SetRenderDrawColor(m_renderer, 0, 0, 0, 180);
  SDL_FRect const strip_top = {0.0F, 0.0F, strip_w,
                               std::max(0.0F, spec_h - kFreqBar)};
  SDL_FRect const strip_bot = {0.0F, spec_h, strip_w,
                               static_cast<float>(m_height) - spec_h};
  SDL_RenderFillRect(m_renderer, &strip_top);
  SDL_RenderFillRect(m_renderer, &strip_bot);
  SDL_SetRenderDrawBlendMode(m_renderer, prev_blend);

  constexpr SDL_Color kLabel = {200, 200, 200, 255};
  for (const auto &l : labels) {
    // Tick poking out of the strip's right edge (toward the plot).
    SDL_SetRenderDrawColor(m_renderer, 180, 180, 180, 255);
    SDL_RenderLine(m_renderer, strip_w - kTick, l.y, strip_w, l.y);

    int h = 0;
    m_text_renderer->get_text_size(l.text, nullptr, &h);
    float ty = l.y - static_cast<float>(h) / 2.0F; // center on the tick
    ty = std::clamp(ty, 0.0F,
                    static_cast<float>(m_height) - static_cast<float>(h));
    blit_text(l.text, kLabel, static_cast<float>(kPad), ty);
  }

  SDL_SetRenderDrawColor(m_renderer, r, g, b, a);
}

void SdlRenderer::draw_markers() {
  if (m_markers.empty() || !m_text_renderer) {
    return;
  }
  // Magenta reads against every palette (JET/VIRIDIS/HOT/GRAY/BLU-RED) and is
  // distinct from the gray hover cursor and the white max-hold trace.
  constexpr SDL_Color kClr = {255, 80, 255, 255};

  Uint8 r;
  Uint8 g;
  Uint8 b;
  Uint8 a;
  SDL_GetRenderDrawColor(m_renderer, &r, &g, &b, &a);

  // Vertical reference lines spanning both panes, with an "Mn" tag at the top.
  SDL_SetRenderDrawColor(m_renderer, kClr.r, kClr.g, kClr.b, 220);
  for (const auto &m : m_markers) {
    if (m.on_screen) {
      SDL_RenderLine(m_renderer, m.x, 0.0F, m.x,
                     static_cast<float>(m_height) - 1.0F);
    }
  }
  for (const auto &m : m_markers) {
    if (!m.on_screen) {
      continue;
    }
    char tag[8];
    std::snprintf(tag, sizeof(tag), "M%d", m.index);
    blit_text(tag, kClr, m.x + 2.0F, 2.0F);
  }

  // The per-marker frequency/level list is drawn by render_panel() in the
  // right gutter; here we only draw the on-plot lines + "Mn" tags.
  SDL_SetRenderDrawColor(m_renderer, r, g, b, a);
}

void SdlRenderer::draw_trigger() {
  // Only the red threshold line lives on the plot now. The "TRIG @ x dB" value
  // and the frozen "SPACE to resume" hint moved to the panel — the old on-plot
  // tag collided with the cursor readout and the centered banner covered the
  // marker "Mn" tags. armed stays true through a freeze, so this also draws the
  // line on the held spectrum.
  if (!m_trigger_armed) {
    return;
  }
  Uint8 r = 0;
  Uint8 g = 0;
  Uint8 b = 0;
  Uint8 a = 0;
  SDL_GetRenderDrawColor(m_renderer, &r, &g, &b, &a);
  SDL_SetRenderDrawColor(m_renderer, 255, 80, 80, 255);
  SDL_RenderLine(m_renderer, 0.0F, m_trigger_line_y,
                 static_cast<float>(m_plot_width) - 1.0F, m_trigger_line_y);
  SDL_SetRenderDrawColor(m_renderer, r, g, b, a);
}

void SdlRenderer::draw_cursor_readout() {
  using Pane = CursorReadout::Pane;
  if (m_cursor_readout.pane == Pane::None || !m_text_renderer) {
    return;
  }
  const float spec_h = static_cast<float>(m_freq_scale_spectrum_height);
  if (spec_h <= 0.0F) {
    return;
  }
  const float x = m_cursor_readout.x;

  Uint8 r;
  Uint8 g;
  Uint8 b;
  Uint8 a;
  SDL_GetRenderDrawColor(m_renderer, &r, &g, &b, &a);

  // Markers + box text differ per pane; the box itself is shared (below).
  char l0[40];
  char l1[40];
  SDL_SetRenderDrawColor(m_renderer, 200, 200, 200, 180);
  if (m_cursor_readout.pane == Pane::Spectrum) {
    // Vertical marker over the spectrum pane + a dot snapped onto the trace.
    SDL_RenderLine(m_renderer, x, 0.0F, x, spec_h - 1.0F);
    constexpr float kDot = 3.0F;
    SDL_FRect const dot_rect = {x - kDot, m_cursor_readout.dot_y - kDot,
                                kDot * 2.0F, kDot * 2.0F};
    SDL_SetRenderDrawColor(m_renderer, 255, 255, 255, 255);
    SDL_RenderFillRect(m_renderer, &dot_rect);
    std::snprintf(l0, sizeof(l0), "%.3f MHz", m_cursor_readout.freq_hz / 1.0e6);
    std::snprintf(l1, sizeof(l1), "%.1f dB",
                  static_cast<double>(m_cursor_readout.db));
  } else {
    // Waterfall: crosshair pinpointing the frequency (vertical) and the
    // hovered time-row (horizontal) across the waterfall pane.
    SDL_RenderLine(m_renderer, x, spec_h, x, static_cast<float>(m_height) - 1.0F);
    SDL_RenderLine(m_renderer, 0.0F, m_cursor_readout.dot_y,
                   static_cast<float>(m_plot_width) - 1.0F,
                   m_cursor_readout.dot_y);
    std::snprintf(l0, sizeof(l0), "%.3f MHz", m_cursor_readout.freq_hz / 1.0e6);
    if (m_cursor_readout.seconds_ago < 0.05) {
      std::snprintf(l1, sizeof(l1), "now");
    } else {
      std::snprintf(l1, sizeof(l1), "%.2f s ago", m_cursor_readout.seconds_ago);
    }
  }

  int w0 = 0;
  int h0 = 0;
  int w1 = 0;
  int h1 = 0;
  m_text_renderer->get_text_size(l0, &w0, &h0);
  m_text_renderer->get_text_size(l1, &w1, &h1);
  const int line_h = std::max(h0, h1);
  const int text_w = std::max(w0, w1);
  const int pad = 5;
  const auto box_w = static_cast<float>(text_w + 2 * pad);
  const auto box_h = static_cast<float>(2 * line_h + 2 * pad);

  // Fixed top-left panel. Anchoring it to the cursor/trace made it ride the
  // fast spectrum refresh — vibrating and ghosting. A static position keeps the
  // text readable; the marker line + dot still convey the measured location.
  // The top-left is otherwise clear now (the timing HUD moved to the panel).
  const float bx = 8.0F + static_cast<float>(axis_strip_width());
  const float by = 8.0F;

  SDL_FRect const bg = {bx, by, box_w, box_h};
  SDL_SetRenderDrawColor(m_renderer, 0, 0, 0, 200);
  SDL_RenderFillRect(m_renderer, &bg);

  SDL_Color const col = {255, 255, 255, 255};
  const char *lines[2] = {l0, l1};
  for (int i = 0; i < 2; ++i) {
    blit_text(lines[i], col, bx + static_cast<float>(pad),
              by + static_cast<float>(pad + i * line_h));
  }

  SDL_SetRenderDrawColor(m_renderer, r, g, b, a);
}

void SdlRenderer::render_panel() {
  const int panel_w =
      static_cast<int>(m_width) - static_cast<int>(m_plot_width);
  if (panel_w < 120 || m_text_renderer == nullptr) {
    return; // window too narrow for a panel — the plots already span full width
  }

  // Flat palette (no glow): green labels, white values, amber trigger, magenta
  // markers. Hard 90-degree corners — DOS panel meets 4KB-intro.
  constexpr SDL_Color kLabel = {110, 231, 135, 255};
  constexpr SDL_Color kValue = {235, 240, 236, 255};
  constexpr SDL_Color kDim = {127, 147, 138, 255};
  constexpr SDL_Color kAmber = {255, 178, 63, 255};
  constexpr SDL_Color kMag = {255, 80, 255, 255};
  constexpr SDL_Color kGhost = {120, 106, 134, 255};

  Uint8 r;
  Uint8 g;
  Uint8 b;
  Uint8 a;
  SDL_GetRenderDrawColor(m_renderer, &r, &g, &b, &a);

  // Bordered inset box floating on black over the gutter.
  constexpr float kGap = 7.0F;
  const float bx = static_cast<float>(m_plot_width) + kGap;
  const float by = kGap;
  const float bw = static_cast<float>(panel_w) - 2.0F * kGap;
  const float bh = static_cast<float>(m_height) - 2.0F * kGap;
  SDL_FRect const box = {bx, by, bw, bh};
  SDL_SetRenderDrawColor(m_renderer, 10, 14, 12, 255);
  SDL_RenderFillRect(m_renderer, &box);
  SDL_SetRenderDrawColor(m_renderer, 92, 111, 100, 255);
  SDL_RenderRect(m_renderer, &box);

  int cw = 0;
  int lh = 0;
  m_text_renderer->get_text_size("M", &cw, &lh); // fixed 8x16 cell
  const float pad = 9.0F;
  const float cx = bx + pad;
  const float val_x = cx + 7.0F * static_cast<float>(cw); // value column
  const float step = static_cast<float>(lh) + 2.0F;
  float y = by + pad;

  auto field = [&](const char *label, const std::string &value) {
    blit_text(label, kLabel, cx, y);
    blit_text(value, kValue, val_x, y);
    y += step;
  };

  // --- STATUS ---
  blit_text("STATUS", kDim, cx, y);
  y += step + 2.0F;
  field("FREQ", m_status.freq);
  field("GAIN", m_status.gain);
  field("FFT", m_status.fft);
  field("WINDOW", m_status.window);
  field("PAL", m_status.palette);
  if (m_peak_db > -140.0F) {
    char pk[16];
    std::snprintf(pk, sizeof(pk), "%.1f dB", static_cast<double>(m_peak_db));
    field("PEAK", pk);
  }
  if (!m_status.trace.empty()) {
    field("TRACE", m_status.trace);
  }
  if (!m_current_iq_status.empty()) {
    blit_text(m_current_iq_status, kLabel, cx, y);
    y += step;
  }
  if (m_trigger_armed || m_frozen) {
    y += 4.0F;
    char tg[24];
    std::snprintf(tg, sizeof(tg), "TRIG @ %.0f dB",
                  static_cast<double>(m_trigger_db));
    blit_text(tg, kAmber, cx, y);
    y += step;
    if (m_frozen) { // resume hint (replaces the removed centered banner)
      blit_text("SPACE to resume", kAmber, cx, y);
      y += step;
    }
  }

  // --- PERF (always shown — processing headroom at a glance) ---
  {
    y += 6.0F;
    blit_text("PERF", kDim, cx, y);
    y += step;
    char v[24];
    std::snprintf(v, sizeof(v), "%.1f", m_timing_fps);
    field("FPS", v);
    std::snprintf(v, sizeof(v), "%.2f ms", m_timing_cpu_ms);
    field("CPU", v);
    std::snprintf(v, sizeof(v), "%.2f ms", m_timing_build_ms);
    field("BLD", v);
    std::snprintf(v, sizeof(v), "%.2f ms", m_timing_present_ms);
    field("PRES", v);
  }

  // --- MARKERS (lower section) ---
  // The divider hugs the STATUS content (not a fixed fraction) so the marker
  // list gets all the remaining height — enough for the full 16 at 576px.
  const float split_y = y + 6.0F;
  SDL_SetRenderDrawColor(m_renderer, 36, 49, 43, 255);
  SDL_RenderLine(m_renderer, bx, split_y, bx + bw, split_y);
  float my = split_y + pad;
  {
    char mh[24];
    // "/14" mirrors main.cpp's kMaxMarkers so hitting the cap is self-evident.
    std::snprintf(mh, sizeof(mh), "MARKERS  %zu/14", m_markers.size());
    blit_text(mh, kDim, cx, my);
    my += step + 2.0F;
  }
  // At least 3 slots (arcade high-score look), growing to the live count, but
  // clamped to what fits and to the 16-marker cap.
  const float avail = by + bh - my;
  const auto max_rows =
      avail > 0.0F ? static_cast<size_t>(avail / step) : size_t{0};
  const size_t shown =
      std::min(std::max<size_t>(3, m_markers.size()), max_rows);
  const float rest_x = cx + 4.0F * static_cast<float>(cw);
  for (size_t i = 0; i < shown; ++i) {
    if (i < m_markers.size()) {
      const Marker &m = m_markers[i];
      char id[8];
      std::snprintf(id, sizeof(id), "M%d", m.index);
      char rest[40];
      if (m.on_screen) {
        std::snprintf(rest, sizeof(rest), "%.3f MHz  %.1f dB", m.freq_hz / 1e6,
                      static_cast<double>(m.db));
      } else {
        std::snprintf(rest, sizeof(rest), "%.3f MHz  (off)", m.freq_hz / 1e6);
      }
      blit_text(id, kMag, cx, my);
      blit_text(rest, kValue, rest_x, my);
    } else {
      char id[8];
      std::snprintf(id, sizeof(id), "M%d", static_cast<int>(i) + 1);
      blit_text(id, kGhost, cx, my);
      blit_text("---", kGhost, rest_x, my);
    }
    my += step;
  }

  SDL_SetRenderDrawColor(m_renderer, r, g, b, a);
}

void SdlRenderer::set_timing_overlay(double fps, double cpu_ms,
                                     double render_build_ms,
                                     double present_ms) noexcept {
  m_timing_fps = fps;
  m_timing_cpu_ms = cpu_ms;
  m_timing_build_ms = render_build_ms;
  m_timing_present_ms = present_ms;
}

// Draw the spectrum bars into the active render target — one SDL_RenderGeometry
// call, solid-color geometry (no texture).
void SdlRenderer::render_spectrum(const std::vector<SDL_Vertex> &verts,
                                  const std::vector<int> &indices) {
  if (verts.empty())
    return;
  SDL_RenderGeometry(m_renderer, nullptr, verts.data(),
                     static_cast<int>(verts.size()),
                     indices.empty() ? nullptr : indices.data(),
                     static_cast<int>(indices.size()));
}

void SdlRenderer::compose_base(SDL_Texture *wf_tex, const SDL_FRect *wf_src,
                               const SDL_FRect &wf_dst,
                               const std::vector<SDL_Vertex> &verts,
                               const std::vector<int> &indices) {
  SDL_SetRenderTarget(m_renderer, m_frame_tex);
  // Force black before clearing: the spectrum background (the area above the
  // bars) is the cleared color, and rebuild_freq_scale_ticks() can leave the
  // renderer's draw color non-black. Leaving it black also re-establishes the
  // app-wide invariant that overlay code saves/restores around.
  SDL_SetRenderDrawColor(m_renderer, 0, 0, 0, 255);
  SDL_RenderClear(m_renderer);
  if (wf_tex != nullptr && wf_dst.w > 0 && wf_dst.h > 0) {
    SDL_RenderTexture(m_renderer, wf_tex, wf_src, &wf_dst);
  }
  render_spectrum(verts, indices); // top region, coords in frame-texture space
  SDL_SetRenderTarget(m_renderer, nullptr);
}

void SdlRenderer::present_composited() {
  SDL_RenderClear(m_renderer);
  SDL_RenderTexture(m_renderer, m_frame_tex, nullptr, nullptr);
  render_overlays();
  // Read back the finished frame (base + overlays) before the swap — SDL3
  // leaves the backbuffer undefined once present runs, so this is the only
  // valid point for a WYSIWYG export readback.
  if (m_capture_pending)
    capture_backbuffer();
  present_timed();
}

// Reads the composited backbuffer into m_capture_buf as tightly packed RGBA at
// the renderer's output resolution. The readback surface format follows the
// swapchain, so convert to RGBA32 to give the exporter a stable layout.
void SdlRenderer::capture_backbuffer() {
  m_capture_pending = false;
  SDL_Surface *raw = SDL_RenderReadPixels(m_renderer, nullptr);
  if (raw == nullptr) {
    std::cerr << "SDL_RenderReadPixels failed: " << SDL_GetError() << '\n';
    return;
  }
  SDL_Surface *rgba = raw;
  if (raw->format != SDL_PIXELFORMAT_RGBA32) {
    rgba = SDL_ConvertSurface(raw, SDL_PIXELFORMAT_RGBA32);
    SDL_DestroySurface(raw);
    if (rgba == nullptr) {
      std::cerr << "SDL_ConvertSurface failed: " << SDL_GetError() << '\n';
      return;
    }
  }

  m_capture_w = rgba->w;
  m_capture_h = rgba->h;
  const size_t row_bytes = static_cast<size_t>(rgba->w) * 4;
  m_capture_buf.resize(row_bytes * static_cast<size_t>(rgba->h));
  // SDL surface pitch may exceed row_bytes (padding); pack row by row.
  const auto *src = static_cast<const uint8_t *>(rgba->pixels);
  for (int y = 0; y < rgba->h; ++y) {
    memcpy(m_capture_buf.data() + static_cast<size_t>(y) * row_bytes,
           src + static_cast<size_t>(y) * rgba->pitch, row_bytes);
  }
  SDL_DestroySurface(rgba);
  m_capture_ready = true;
}

bool SdlRenderer::take_capture(std::vector<uint8_t> &out, int &width_out,
                               int &height_out) {
  if (!m_capture_ready)
    return false;
  out = std::move(m_capture_buf);
  m_capture_buf.clear();
  width_out = m_capture_w;
  height_out = m_capture_h;
  m_capture_ready = false;
  return true;
}

auto SdlRenderer::render_displays(const std::vector<SDL_Vertex> &spec_verts,
                                  const std::vector<int> &spec_idx,
                                  const uint8_t *waterfall_data,
                                  size_t spectrum_height,
                                  const std::vector<SDL_Rect> &wf_dirty_rects)
    -> bool {
  if (m_texture == nullptr)
    return false;

  void *texture_pixels = nullptr;
  int texture_pitch = 0;
  if (!SDL_LockTexture(m_texture, nullptr, &texture_pixels, &texture_pitch)) {
    std::cerr << "SDL_LockTexture failed: " << SDL_GetError() << '\n';
    return false;
  }

  uint8_t *tex = static_cast<uint8_t *>(texture_pixels);
  const size_t src_pitch = m_plot_width * 4;
  const int wf_height = static_cast<int>(m_height - spectrum_height);

  // Upload only the waterfall into m_texture's bottom region. The spectrum is
  // now GPU-drawn from spec_verts, so no spectrum pixel upload happens here.
  for (const auto &rect : wf_dirty_rects) {
    int x0 = std::max(0, rect.x);
    int y0 = std::max(0, rect.y);
    int x1 = std::min(rect.x + rect.w, static_cast<int>(m_plot_width));
    int y1 = std::min(rect.y + rect.h, wf_height);
    if (x1 <= x0 || y1 <= y0)
      continue;
    size_t copy_bytes = static_cast<size_t>(x1 - x0) * 4;
    for (int y = y0; y < y1; ++y) {
      memcpy(tex + (y + static_cast<int>(spectrum_height)) * texture_pitch +
                 x0 * 4,
             waterfall_data + y * src_pitch + x0 * 4, copy_bytes);
    }
  }

  SDL_UnlockTexture(m_texture);

  // Seed the GPU scroll texture from the main texture's waterfall region so
  // the first render_displays_scroll() frame is visually continuous.
  if (m_wf_scroll_tex != nullptr && wf_height > 0) {
    SDL_FRect const wf_src = {0.0f, static_cast<float>(spectrum_height),
                              static_cast<float>(m_plot_width),
                              static_cast<float>(wf_height)};
    SDL_SetRenderTarget(m_renderer, m_wf_scroll_tex);
    SDL_RenderTexture(m_renderer, m_texture, &wf_src, nullptr);
    SDL_SetRenderTarget(m_renderer, nullptr);
    m_wf_scroll_valid = true;
  }

  // Compose base = waterfall region of m_texture (bottom) + spectrum bars
  // (top).
  SDL_FRect const wf_src = {0.0f, static_cast<float>(spectrum_height),
                            static_cast<float>(m_plot_width),
                            static_cast<float>(wf_height)};
  SDL_FRect const wf_dst = wf_src;
  compose_base(m_texture, &wf_src, wf_dst, spec_verts, spec_idx);

  present_composited();
  return true;
}

void SdlRenderer::present_frame() {
  // No new data: re-blit the last composited base frame with fresh overlays.
  present_composited();
}

// DEBUG (frame-timing branch): time the present so the main loop can subtract
// the swap/flush cost from total render time.
void SdlRenderer::present_timed() {
  auto t0 = std::chrono::steady_clock::now();
  SDL_RenderPresent(m_renderer);
  auto t1 = std::chrono::steady_clock::now();
  m_last_present_ms =
      std::chrono::duration<double, std::milli>(t1 - t0).count();
}

auto SdlRenderer::poll_events(ControlState *state) -> bool {
  SDL_Event event;
  while (SDL_PollEvent(&event)) {
    switch (event.type) {
    case SDL_EVENT_QUIT:
      return false;

    case SDL_EVENT_KEY_DOWN:
      if (event.key.key == SDLK_ESCAPE || event.key.key == SDLK_Q) {
        return false;
      }
      if (event.key.key == SDLK_DELETE) {
        m_clear_markers = true; // clear all frequency markers
      }

      // Handle control state if provided
      if (state != nullptr) {
        // Get modifier state - SDL3 returns const bool* instead of const Uint8*
        const bool *keystate = SDL_GetKeyboardState(nullptr);
        bool const shift_held =
            keystate[SDL_SCANCODE_LSHIFT] || keystate[SDL_SCANCODE_RSHIFT];
        bool const ctrl_held =
            keystate[SDL_SCANCODE_LCTRL] || keystate[SDL_SCANCODE_RCTRL];

        // SDL3: event.key.key instead of event.key.keysym.sym. The key updates
        // ControlState, which tracks its own status-dirty flag; main() feeds
        // the panel via status_changed(), so no renderer-side flag is needed.
        handle_control_key(*state, event.key.key, shift_held, ctrl_held);
      }
      break;

    case SDL_EVENT_MOUSE_MOTION:
      // Convert window coords -> render (logical) coords so the marker lines up
      // with the spectrum under logical-presentation scaling (window resize).
      SDL_ConvertEventToRenderCoordinates(m_renderer, &event);
      m_cursor_x = event.motion.x;
      m_cursor_y = event.motion.y;
      m_cursor_active = true;
      // Shift + left button held = dragging the amplitude-trigger threshold
      // line. Captured as a set-request (render-space y) that main() maps to
      // dB.
      if ((SDL_GetModState() & SDL_KMOD_SHIFT) != 0 &&
          (event.motion.state & SDL_BUTTON_LMASK) != 0) {
        m_trigger_set_y = event.motion.y;
        m_trigger_set_pending = true;
      }
      break;

    case SDL_EVENT_MOUSE_BUTTON_DOWN:
      // Left = drop a frequency marker, right = remove the nearest. main()
      // owns the marker list and processes these via take_clicks(). Shift+left
      // instead sets the amplitude-trigger threshold (no marker dropped).
      SDL_ConvertEventToRenderCoordinates(m_renderer, &event);
      m_cursor_x = event.button.x;
      m_cursor_y = event.button.y;
      m_cursor_active = true;
      if ((SDL_GetModState() & SDL_KMOD_SHIFT) != 0 &&
          event.button.button == SDL_BUTTON_LEFT) {
        m_trigger_set_y = event.button.y;
        m_trigger_set_pending = true;
      } else {
        m_clicks.push_back({event.button.x, event.button.y,
                            event.button.button == SDL_BUTTON_RIGHT});
      }
      break;

    case SDL_EVENT_WINDOW_MOUSE_LEAVE:
      m_cursor_active = false;
      break;

    case SDL_EVENT_WINDOW_CLOSE_REQUESTED:
      return false;
    }
  }
  return true;
}

void SdlRenderer::render_status_bar(const std::string &status_text) {
  // Repurposed as a transient one-shot notice (e.g. the export result). Drawn
  // bottom-left over the plot by render_overlays until the deadline; empty text
  // clears it immediately. The steady per-frame status lives in the panel now.
  m_status_msg = status_text;
  m_status_until = status_text.empty() ? std::chrono::steady_clock::time_point{}
                                       : std::chrono::steady_clock::now() +
                                             std::chrono::seconds(4);
}

void SdlRenderer::render_peak_indicator(float peak_db) {
  m_peak_db = peak_db; // the panel prints it when > -140 dBFS
}

void SdlRenderer::render_iq_status(const std::string &iq_text) {
  m_current_iq_status = iq_text; // shown in the panel while capturing
}

bool SdlRenderer::ensure_wf_scroll_textures(size_t wf_height,
                                            size_t line_height) {
  bool rebuild = (m_wf_scroll_tex == nullptr);
  // Recreate if line_height changed (FFT size switch)
  if (!rebuild && m_wf_scroll_line_height != line_height) {
    SDL_DestroyTexture(m_wf_scroll_tex);
    m_wf_scroll_tex = nullptr;
    SDL_DestroyTexture(m_wf_scroll_aux);
    m_wf_scroll_aux = nullptr;
    SDL_DestroyTexture(m_wf_line_tex);
    m_wf_line_tex = nullptr;
    m_wf_scroll_valid = false;
    rebuild = true;
  }
  if (!rebuild)
    return true;

  const int w = static_cast<int>(m_plot_width);
  const int h = static_cast<int>(wf_height);
  const int lh = static_cast<int>(line_height);

  m_wf_scroll_tex = SDL_CreateTexture(m_renderer, SDL_PIXELFORMAT_RGBA32,
                                      SDL_TEXTUREACCESS_TARGET, w, h);
  m_wf_scroll_aux = SDL_CreateTexture(m_renderer, SDL_PIXELFORMAT_RGBA32,
                                      SDL_TEXTUREACCESS_TARGET, w, h);
  m_wf_line_tex = SDL_CreateTexture(m_renderer, SDL_PIXELFORMAT_RGBA32,
                                    SDL_TEXTUREACCESS_STREAMING, w, lh);

  if (m_wf_scroll_tex == nullptr || m_wf_scroll_aux == nullptr ||
      m_wf_line_tex == nullptr) {
    return false;
  }
  // Nearest to match SDL2's default (SDL3 defaults to linear); these are
  // blitted 1:1 but keep them consistent with m_frame_tex.
  SDL_SetTextureScaleMode(m_wf_scroll_tex, SDL_SCALEMODE_NEAREST);
  SDL_SetTextureScaleMode(m_wf_scroll_aux, SDL_SCALEMODE_NEAREST);
  SDL_SetTextureScaleMode(m_wf_line_tex, SDL_SCALEMODE_NEAREST);

  m_wf_scroll_line_height = line_height;
  return true;
}

bool SdlRenderer::render_displays_scroll(
    const std::vector<SDL_Vertex> &spec_verts, const std::vector<int> &spec_idx,
    const uint8_t *new_wf_line_rgba, size_t spectrum_height, size_t wf_height,
    size_t line_height) {
  if (m_texture == nullptr)
    return false;
  if (!ensure_wf_scroll_textures(wf_height, line_height))
    return false;

  // --- GPU waterfall scroll ---
  // Upload the new line (~5 KB) to the narrow streaming texture
  {
    void *lp = nullptr;
    int lpitch = 0;
    if (SDL_LockTexture(m_wf_line_tex, nullptr, &lp, &lpitch)) {
      const size_t src_row = m_plot_width * 4;
      for (size_t y = 0; y < line_height; ++y) {
        memcpy(static_cast<uint8_t *>(lp) + y * lpitch,
               new_wf_line_rgba + y * src_row, src_row);
      }
      SDL_UnlockTexture(m_wf_line_tex);
    }
  }

  // Render into the aux texture:
  //   1. Blit existing waterfall shifted up by line_height (GPU-to-GPU copy)
  //   2. Place new line at the bottom
  SDL_SetRenderTarget(m_renderer, m_wf_scroll_aux);
  int const shift_h =
      static_cast<int>(wf_height) - static_cast<int>(line_height);
  if (m_wf_scroll_valid) {
    if (shift_h > 0) {
      SDL_FRect const src_r = {0.0f, static_cast<float>(line_height),
                               static_cast<float>(m_plot_width),
                               static_cast<float>(shift_h)};
      SDL_FRect const dst_r = {0.0f, 0.0f, static_cast<float>(m_plot_width),
                               static_cast<float>(shift_h)};
      SDL_RenderTexture(m_renderer, m_wf_scroll_tex, &src_r, &dst_r);
    }
  } else if (shift_h > 0) {
    // First scroll frame: the scroll textures were just created so they hold
    // garbage. Seed from m_texture's waterfall region (filled by the last
    // full-render pass) shifted up by line_height — produces a continuous
    // transition instead of a black flash + bottom-up refill.
    SDL_FRect const src_r = {
        0.0f,
        static_cast<float>(spectrum_height) + static_cast<float>(line_height),
        static_cast<float>(m_plot_width), static_cast<float>(shift_h)};
    SDL_FRect const dst_r = {0.0f, 0.0f, static_cast<float>(m_plot_width),
                             static_cast<float>(shift_h)};
    SDL_RenderTexture(m_renderer, m_texture, &src_r, &dst_r);
  } else {
    // Pathological: wf_height <= line_height. Just clear.
    SDL_SetRenderDrawColor(m_renderer, 0, 0, 0, 255);
    SDL_RenderClear(m_renderer);
  }
  // Paste new line at the bottom
  SDL_FRect const line_dst = {0.0f, static_cast<float>(wf_height - line_height),
                              static_cast<float>(m_plot_width),
                              static_cast<float>(line_height)};
  SDL_RenderTexture(m_renderer, m_wf_line_tex, nullptr, &line_dst);
  SDL_SetRenderTarget(m_renderer, nullptr);

  // Swap active/aux
  std::swap(m_wf_scroll_tex, m_wf_scroll_aux);
  m_wf_scroll_valid = true;

  // --- Compose final frame ---
  // Waterfall (bottom) = the whole scroll texture mapped to the bottom region;
  // spectrum (top) = GPU geometry. Composited into m_frame_tex for present.
  SDL_FRect const wf_dst = {0.0f, static_cast<float>(spectrum_height),
                            static_cast<float>(m_plot_width),
                            static_cast<float>(wf_height)};
  compose_base(m_wf_scroll_tex, nullptr, wf_dst, spec_verts, spec_idx);

  present_composited();
  return true;
}

void SdlRenderer::rebuild_freq_scale_ticks() {
  if (m_freq_scale_texture != nullptr) {
    SDL_DestroyTexture(m_freq_scale_texture);
    m_freq_scale_texture = nullptr;
  }

  if (m_freq_scale_sample_rate_hz == 0 || m_freq_scale_spectrum_height < 22 ||
      m_text_renderer == nullptr)
    return;

  constexpr int SCALE_H = 22;

  // Bake the entire scale bar into a single render-target texture.
  // render_overlays() does one SDL_RenderCopy per frame — no state churn.
  m_freq_scale_texture = SDL_CreateTexture(
      m_renderer, SDL_PIXELFORMAT_RGBA32, SDL_TEXTUREACCESS_TARGET,
      static_cast<int>(m_plot_width), SCALE_H);
  if (m_freq_scale_texture == nullptr)
    return;

  SDL_SetTextureBlendMode(m_freq_scale_texture, SDL_BLENDMODE_BLEND);
  SDL_SetTextureScaleMode(m_freq_scale_texture, SDL_SCALEMODE_NEAREST);

  SDL_SetRenderTarget(m_renderer, m_freq_scale_texture);

  // Semi-transparent dark background (SDL_RenderClear writes alpha directly)
  SDL_SetRenderDrawColor(m_renderer, 0, 0, 0, 180);
  SDL_RenderClear(m_renderer);

  // Compute tick positions
  uint32_t const interval = nice_tick_interval(m_freq_scale_sample_rate_hz);
  int64_t const start_hz =
      static_cast<int64_t>(m_freq_scale_center_hz) -
      static_cast<int64_t>(m_freq_scale_sample_rate_hz) / 2;
  int64_t const end_hz =
      start_hz + static_cast<int64_t>(m_freq_scale_sample_rate_hz);
  int64_t const first_tick = ((start_hz + static_cast<int64_t>(interval) - 1) /
                              static_cast<int64_t>(interval)) *
                             static_cast<int64_t>(interval);

  SDL_SetRenderDrawColor(m_renderer, 180, 180, 180, 255);
  SDL_Color const label_color = {200, 200, 200, 255};

  for (int64_t freq = first_tick; freq < end_hz;
       freq += static_cast<int64_t>(interval)) {
    if (freq < 0)
      continue;
    float const x_frac = static_cast<float>(freq - start_hz) /
                         static_cast<float>(m_freq_scale_sample_rate_hz);
    int const x = static_cast<int>(x_frac * static_cast<float>(m_plot_width));

    // Tick line
    SDL_FRect const tick_line = {static_cast<float>(x), 0.0f, 1.0f, 4.0f};
    SDL_RenderFillRect(m_renderer, &tick_line);

    // Label — temporary texture, composited onto the scale texture then freed
    std::string const label = format_freq_label(freq);
    int lw = 0;
    m_text_renderer->get_text_size(label, &lw, nullptr);
    int lx = x - lw / 2;
    lx = std::max(0, std::min(lx, static_cast<int>(m_plot_width) - lw));
    blit_text(label, label_color, static_cast<float>(lx), 5.0f);
  }

  SDL_SetRenderTarget(m_renderer, nullptr);
}

void SdlRenderer::render_frequency_scale(uint32_t center_hz,
                                         uint32_t sample_rate_hz,
                                         size_t spectrum_height) {
  if (center_hz != m_freq_scale_center_hz ||
      sample_rate_hz != m_freq_scale_sample_rate_hz ||
      spectrum_height != m_freq_scale_spectrum_height) {
    m_freq_scale_center_hz = center_hz;
    m_freq_scale_sample_rate_hz = sample_rate_hz;
    m_freq_scale_spectrum_height = spectrum_height;
    rebuild_freq_scale_ticks();
  }
}

} // namespace openspectrum
