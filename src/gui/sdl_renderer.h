// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

#include <SDL3/SDL.h>

#include "utils/attributes.h"

namespace openspectrum {

class ControlState;
class TextRenderer;

class SdlRenderer {
public:
  SdlRenderer(size_t width, size_t height,
              const std::string &title = "OpenSpectrum SDR");
  ~SdlRenderer();

  SdlRenderer(const SdlRenderer &) = delete;
  SdlRenderer &operator=(const SdlRenderer &) = delete;

  // Uploads the whole waterfall buffer (history filling, after a LUT change or
  // reset). The spectrum is drawn on the GPU from spec_verts/spec_idx.
  OS_HOT bool render_displays(const std::vector<SDL_Vertex> &spec_verts,
                              const std::vector<int> &spec_idx,
                              const uint8_t *waterfall_data,
                              size_t spectrum_height,
                              const std::vector<SDL_Rect> &wf_dirty_rects);

  // Re-presents the last frame with updated overlays, no upload.
  OS_HOT void present_frame();

  // Steady state: shifts the waterfall texture up on the GPU, uploads only
  // new_wf_line_rgba and composites the spectrum over it.
  OS_HOT bool render_displays_scroll(const std::vector<SDL_Vertex> &spec_verts,
                                     const std::vector<int> &spec_idx,
                                     const uint8_t *new_wf_line_rgba,
                                     size_t spectrum_height, size_t wf_height,
                                     size_t line_height);

  // False if quit was requested. Keys go to `state` when given.
  bool poll_events(ControlState *state = nullptr);

  void render_status_bar(const std::string &status_text);

  void render_peak_indicator(float peak_db);

  void render_iq_status(const std::string &iq_text);

  // Labels span center_hz +- sample_rate_hz/2; rebuilt only when either
  // changes.
  void render_frequency_scale(uint64_t center_hz, uint32_t sample_rate_hz,
                              size_t spectrum_height);

  [[nodiscard]] size_t width() const noexcept { return m_width; }
  [[nodiscard]] size_t height() const noexcept { return m_height; }

  // The window minus the right-hand panel. All freq<->x mapping spans
  // [0, plot_width); the panel takes [plot_width, width).
  [[nodiscard]] size_t plot_width() const noexcept { return m_plot_width; }

  // Right-panel status fields (formatted by ControlState), fed on change.
  struct PanelStatus {
    std::string freq;
    std::string gain;
    std::string fft;
    std::string window;
    std::string palette;
    std::string trace; // "AVG" / "MAX" / "AVG MAX" / "" (empty = hidden)
  };
  void set_status_fields(PanelStatus s) { m_status = std::move(s); }

  [[nodiscard]] bool is_valid() const noexcept {
    return m_window != nullptr && m_renderer != nullptr;
  }

  // Arms a one-shot readback of the next composited frame, overlays included.
  // present_composited() reads it just before the swap: SDL3 leaves the
  // backbuffer undefined after present. Drain with take_capture().
  void request_capture() noexcept { m_capture_pending = true; }

  // Moves the captured frame (window-resolution RGBA, tightly packed) into
  // `out`. False if none is ready; each capture is consumed once.
  [[nodiscard]] bool take_capture(std::vector<uint8_t> &out, int &width_out,
                                  int &height_out);

  // Milliseconds in the last SDL_RenderPresent, for the frame timing.
  [[nodiscard]] double last_present_ms() const noexcept {
    return m_last_present_ms;
  }

  // The per-second timing snapshot for the PERF block; once per frame, before
  // render/present.
  void set_timing_overlay(double fps, double cpu_ms, double render_build_ms,
                          double present_ms) noexcept;

  // CursorTracker (plot_overlays.h) computes the values; the renderer captures
  // the pointer in poll_events and draws them.
  struct CursorReadout {
    enum class Pane { None, Spectrum, Waterfall };
    Pane pane = Pane::None; // None -> nothing is drawn
    float x = 0.0F;         // render-space x of the marker (both panes)
    float dot_y = 0.0F;     // Spectrum: trace dot y. Waterfall: hovered row y.
    double freq_hz = 0.0;   // frequency at x (both panes)
    float db = 0.0F;        // Spectrum: trace amplitude at x
    double seconds_ago = 0.0; // Waterfall: capture age of the hovered line
  };
  void set_cursor_readout(const CursorReadout &readout) noexcept {
    m_cursor_readout = readout;
  }

  // Render (logical) coordinates; cursor_active() is false outside the window.
  [[nodiscard]] bool cursor_active() const noexcept { return m_cursor_active; }
  [[nodiscard]] float cursor_x() const noexcept { return m_cursor_x; }
  [[nodiscard]] float cursor_y() const noexcept { return m_cursor_y; }

  // Markers (plot_overlays.h) owns the list; main() sends the on-screen
  // positions and levels each frame. Clicks are captured in poll_events and
  // drained by main().
  struct Marker {
    int index = 0;          // 1-based, for the "Mn" tag
    float x = 0.0F;         // render-space x (valid when on_screen)
    bool on_screen = false; // false when tuned out of the current span
    double freq_hz = 0.0;
    float db = 0.0F; // live amplitude at the marker (when on_screen)
  };
  void set_markers(std::vector<Marker> markers) {
    m_markers = std::move(markers);
  }

  // Left-margin dB scale (spectrum) and seconds-ago scale (waterfall), fed each
  // frame. wf_top_seconds <= 0 hides the time axis.
  void set_left_axes(bool enabled, float db_min, float db_max,
                     double wf_top_seconds) noexcept {
    m_axes_enabled = enabled;
    m_axis_db_min = db_min;
    m_axis_db_max = db_max;
    m_axis_wf_top_seconds = wf_top_seconds;
  }

  struct ClickEvent {
    float x = 0.0F;
    float y = 0.0F;
    bool right = false; // true = right button (remove), false = left (drop)
  };
  // Drain the clicks accumulated since the last call.
  [[nodiscard]] std::vector<ClickEvent> take_clicks() {
    return std::exchange(m_clicks, {});
  }
  // The last Ctrl+left-click since the last call (render space), to tune to.
  [[nodiscard]] std::optional<SDL_FPoint> take_tune() noexcept {
    return std::exchange(m_tune_click, std::nullopt);
  }
  // True once if Delete was pressed since the last call.
  [[nodiscard]] bool take_clear_markers() noexcept {
    return std::exchange(m_clear_markers, false);
  }

  // Trigger (plot_overlays.h) owns the state; the renderer captures the
  // Shift+left-drag as a threshold request (render-space y, nullopt if none)
  // and draws the line and the frozen indicator.
  [[nodiscard]] std::optional<float> take_trigger_set() noexcept {
    if (!m_trigger_set_pending) {
      return std::nullopt;
    }
    m_trigger_set_pending = false;
    return m_trigger_set_y;
  }
  // Per-frame draw state: armed, the line's y, its dB, frozen.
  void set_trigger(bool armed, float line_y, float db, bool frozen) noexcept {
    m_trigger_armed = armed;
    m_trigger_line_y = line_y;
    m_trigger_db = db;
    m_frozen = frozen;
  }

private:
  // After the main texture.
  void render_overlays();

  // A temporary texture per call: overlay path only.
  void blit_text(const std::string &text, SDL_Color color, float x, float y);

  // Last in render_overlays, so it sits on top.
  void draw_cursor_readout();

  // Before the cursor readout.
  void draw_markers();

  void draw_trigger();

  // Before the markers and cursor.
  void draw_left_axes();

  // The right-hand panel: status grid, PEAK, trigger, PERF, marker list.
  void render_panel();

  // Width of the left axes strip, or 0 if disabled. Sized to the widest label,
  // not the live values, so the HUD boxes beside it do not jitter.
  [[nodiscard]] int axis_strip_width() const;

  // One SDL_RenderGeometry call, no texture.
  void render_spectrum(const std::vector<SDL_Vertex> &verts,
                       const std::vector<int> &indices);

  // Composes m_frame_tex: the waterfall below, the spectrum above. wf_src may
  // be null to copy the whole texture.
  void compose_base(SDL_Texture *wf_tex, const SDL_FRect *wf_src,
                    const SDL_FRect &wf_dst,
                    const std::vector<SDL_Vertex> &verts,
                    const std::vector<int> &indices);

  // Clear, blit m_frame_tex, overlays, present: shared by all paths.
  void present_composited();

  // SDL_RenderPresent timed into m_last_present_ms.
  void present_timed();

  void rebuild_freq_scale_ticks();

  size_t m_width;
  size_t m_height;
  size_t m_plot_width = 0; // window minus the right instrument panel
  SDL_Window *m_window = nullptr;
  SDL_Renderer *m_renderer = nullptr;
  SDL_Texture *m_texture = nullptr;

  // The last composited base frame, no overlays. Both render paths compose
  // into it; present_frame() re-blits it on the idle path.
  SDL_Texture *m_frame_tex = nullptr;

  // After render_overlays() and before the present: the only time the
  // backbuffer holds the finished frame.
  void capture_backbuffer();

  // m_capture_pending is armed by request_capture() and serviced in
  // present_composited(); m_capture_ready: the buffer holds a frame.
  bool m_capture_pending = false;
  bool m_capture_ready = false;
  int m_capture_w = 0;
  int m_capture_h = 0;
  std::vector<uint8_t> m_capture_buf;

  double m_last_present_ms = 0.0;

  double m_timing_fps = 0.0;
  double m_timing_cpu_ms = 0.0;
  double m_timing_build_ms = 0.0;
  double m_timing_present_ms = 0.0;

  // Every label is rasterized once and cached (m_text_cache) until it goes a
  // frame undrawn.
  std::unique_ptr<TextRenderer> m_text_renderer;

  // Key = 4 colour bytes + the string. sweep_text_cache() evicts what was not
  // drawn last frame, so changing values cannot leak textures.
  struct CachedText {
    SDL_Texture *tex = nullptr;
    int w = 0;
    int h = 0;
    bool used = false;
  };
  std::unordered_map<std::string, CachedText> m_text_cache;
  void sweep_text_cache();

  PanelStatus m_status;

  // dBFS; <= -140 means not yet known.
  float m_peak_db = -200.0F;

  // Empty unless capturing.
  std::string m_current_iq_status;

  // One-shot message (export result) until its deadline.
  std::string m_status_msg;
  std::chrono::steady_clock::time_point m_status_until;

  // Ping-pong scroll textures: the current frame, the scratch for the next
  // shift, and a narrow streaming texture for the new line.
  SDL_Texture *m_wf_scroll_tex = nullptr;
  SDL_Texture *m_wf_scroll_aux = nullptr;
  SDL_Texture *m_wf_line_tex = nullptr;
  bool m_wf_scroll_valid = false;
  size_t m_wf_scroll_line_height = 0;

  bool ensure_wf_scroll_textures(size_t wf_height, size_t line_height);

  // Baked into one texture, rebuilt only when center_hz or sample_rate changes.
  SDL_Texture *m_freq_scale_texture = nullptr;
  uint64_t m_freq_scale_center_hz = 0;
  uint32_t m_freq_scale_sample_rate_hz = 0;
  size_t m_freq_scale_spectrum_height = 0;

  bool m_cursor_active = false;
  float m_cursor_x = 0.0F;
  float m_cursor_y = 0.0F;
  CursorReadout m_cursor_readout;

  std::vector<Marker> m_markers;
  std::vector<ClickEvent> m_clicks;
  bool m_clear_markers = false;
  std::optional<SDL_FPoint> m_tune_click;

  bool m_axes_enabled = false;
  float m_axis_db_min = -120.0F;
  float m_axis_db_max = 0.0F;
  double m_axis_wf_top_seconds = 0.0;

  bool m_trigger_set_pending = false;
  float m_trigger_set_y = 0.0F;
  bool m_trigger_armed = false;
  float m_trigger_line_y = 0.0F;
  float m_trigger_db = 0.0F;
  bool m_frozen = false;
};

} // namespace openspectrum
