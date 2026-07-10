// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include <SDL3/SDL.h>

#include "openspectrum/attributes.h"

namespace openspectrum {

class ControlState;
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

  // Full waterfall render: uploads the whole waterfall buffer into the texture
  // bottom region (used while history is filling or after a LUT/reset). The
  // spectrum is drawn on the GPU from spec_verts/spec_idx (SDL_RenderGeometry),
  // so no spectrum pixel upload happens here.
  OS_HOT bool render_displays(const std::vector<SDL_Vertex> &spec_verts,
                              const std::vector<int> &spec_idx,
                              const uint8_t *waterfall_data,
                              size_t spectrum_height,
                              const std::vector<SDL_Rect> &wf_dirty_rects);

  // Re-present the last rendered texture with updated overlays (no texture
  // upload). Use when no new pixel data is available (e.g. waiting for
  // samples).
  OS_HOT void present_frame();

  // Steady-state render: GPU-shifts the waterfall texture up by line_height,
  // uploads only new_wf_line_rgba (m_width * line_height * 4 bytes) at the
  // bottom, then composites the GPU-drawn spectrum (spec_verts/spec_idx) over
  // the waterfall in one pass.
  OS_HOT bool render_displays_scroll(const std::vector<SDL_Vertex> &spec_verts,
                                     const std::vector<int> &spec_idx,
                                     const uint8_t *new_wf_line_rgba,
                                     size_t spectrum_height, size_t wf_height,
                                     size_t line_height);

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

  // Plot region width: the window minus the right-hand instrument panel. All
  // freq<->x mapping (spectrum geometry, waterfall, freq scale, markers/cursor)
  // spans [0, plot_width); the panel occupies [plot_width, width). The plot
  // origin stays at x=0, so this is the single seam callers thread through.
  [[nodiscard]] size_t plot_width() const noexcept { return m_plot_width; }

  // Right-panel status fields (formatted by ControlState). Fed on change; the
  // panel draws them as a green-label / white-value grid. Replaces the old
  // bottom status bar.
  struct PanelStatus {
    std::string freq;
    std::string gain;
    std::string fft;
    std::string window;
    std::string palette;
    std::string trace; // "AVG" / "MAX" / "AVG MAX" / "" (empty = hidden)
  };
  void set_status_fields(PanelStatus s) { m_status = std::move(s); }

  // Check if initialized
  [[nodiscard]] bool is_valid() const noexcept {
    return m_window != nullptr && m_renderer != nullptr;
  }

  // Spectrogram export (WYSIWYG): arm a one-shot readback of the next fully
  // composited frame — base + overlays (dB/time axis strip, frequency scale,
  // HUD). present_composited() does the SDL_RenderReadPixels just before the
  // swap (the only point the backbuffer holds the finished frame; SDL3 leaves
  // it undefined after present). Drain with take_capture() once it lands.
  void request_capture() noexcept { m_capture_pending = true; }

  // Move the captured frame (window-resolution RGBA, tightly packed) into
  // `out` and report its dimensions. Returns false if none is ready. Clears
  // the ready flag so each armed capture is consumed exactly once.
  [[nodiscard]] bool take_capture(std::vector<uint8_t> &out, int &width_out,
                                  int &height_out);

  // DEBUG (frame-timing branch): wall-clock duration in milliseconds of the
  // most recent SDL_RenderPresent call. Lets the main loop split the SDL
  // present/swap/flush cost out of the total render time. One present per
  // frame, so this is overwritten (not accumulated) each render call.
  [[nodiscard]] double last_present_ms() const noexcept {
    return m_last_present_ms;
  }

  // Supply the latest per-second timing snapshot for the panel's PERF block
  // (always shown). Call once per frame before any render/present.
  void set_timing_overlay(double fps, double cpu_ms, double render_build_ms,
                          double present_ms) noexcept;

  // Mouse cursor readout over the spectrum pane. main() computes the values (it
  // owns the spectrum data + frequency mapping); the renderer captures the raw
  // pointer position in poll_events and draws the marker/dot/box every frame.
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

  // Raw pointer position from poll_events, in render (logical) coordinates.
  // cursor_active() is false while the pointer is outside the window.
  [[nodiscard]] bool cursor_active() const noexcept { return m_cursor_active; }
  [[nodiscard]] float cursor_x() const noexcept { return m_cursor_x; }
  [[nodiscard]] float cursor_y() const noexcept { return m_cursor_y; }

  // Persistent frequency markers (vertical reference lines through both panes).
  // main() owns the marker list (they're frequencies); each frame it sends the
  // on-screen positions + live levels here for drawing. The clicks that
  // create/remove them are captured in poll_events and drained by main().
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

  // Left-margin axes overlay: a dB scale on the spectrum pane and an elapsed
  // "seconds-ago" scale on the waterfall pane. main() feeds the live spectrum
  // dB range and the age of the oldest visible waterfall row each frame. Drawn
  // as an overlay over the left edge — no change to the freq<->x or bin
  // mapping. wf_top_seconds <= 0 hides the time axis (not enough history
  // captured yet).
  void set_left_axes(bool enabled, float db_min, float db_max,
                     double wf_top_seconds) noexcept {
    m_axes_enabled = enabled;
    m_axis_db_min = db_min;
    m_axis_db_max = db_max;
    m_axis_wf_top_seconds = wf_top_seconds;
  }

  // A mouse click captured in poll_events, in render coordinates.
  struct ClickEvent {
    float x = 0.0F;
    float y = 0.0F;
    bool right = false; // true = right button (remove), false = left (drop)
  };
  // Drain the clicks accumulated since the last call.
  [[nodiscard]] std::vector<ClickEvent> take_clicks() {
    return std::exchange(m_clicks, {});
  }
  // True once if the clear-all-markers key (Delete) was pressed since last
  // call.
  [[nodiscard]] bool take_clear_markers() noexcept {
    return std::exchange(m_clear_markers, false);
  }

  // Amplitude trigger. main() owns the armed/threshold/frozen state and the
  // dB<->y mapping; the renderer captures Shift+left-drag/click in the spectrum
  // pane as a threshold-set request and draws the line + frozen indicator.
  //
  // Drain a Shift+left threshold-set request (render-space y); nullopt if none
  // happened since the last call.
  [[nodiscard]] std::optional<float> take_trigger_set() noexcept {
    if (!m_trigger_set_pending) {
      return std::nullopt;
    }
    m_trigger_set_pending = false;
    return m_trigger_set_y;
  }
  // Per-frame trigger draw state from main(): armed flag, the line's
  // render-space y, the dB it represents, and whether the display is frozen.
  void set_trigger(bool armed, float line_y, float db, bool frozen) noexcept {
    m_trigger_armed = armed;
    m_trigger_line_y = line_y;
    m_trigger_db = db;
    m_frozen = frozen;
  }

private:
  // Render overlays (status bar, peak indicator, IQ status, freq scale)
  // Called after rendering the main texture
  void render_overlays();

  // Draw `text` at (x, y) on the active render target via a temporary
  // blended texture (created and freed per call — overlay-path only).
  void blit_text(const std::string &text, SDL_Color color, float x, float y);

  // Draw the cursor readout: vertical marker, dot snapped on the trace, and a
  // small box with frequency + amplitude. No-op when m_cursor_readout.active is
  // false. Called at the end of render_overlays so it sits on top.
  void draw_cursor_readout();

  // Draw the persistent frequency markers: vertical lines + "Mn" tags, and the
  // bottom-left list panel (frequency + live level). Called from
  // render_overlays before the cursor readout so the live cursor sits on top.
  void draw_markers();

  // Draw the amplitude-trigger horizontal threshold line across the spectrum
  // pane (when armed) plus its dB label, and a FROZEN indicator when the
  // display is held. Called from render_overlays. No-op when disarmed/unfrozen.
  void draw_trigger();

  // Draw the left-margin dB axis (spectrum pane) and seconds-ago axis
  // (waterfall pane). Overlay only; called from render_overlays before the
  // markers/cursor so those sit on top.
  void draw_left_axes();

  // Draw the right-hand instrument panel (opaque bordered box over the gutter
  // at [plot_width, width)): status grid, PEAK, trigger row, PERF block, and
  // the marker list. Consumes data the renderer already holds; drawn from
  // render_overlays after the freq scale.
  void render_panel();

  // Width (px) of the left axes strip, or 0 when axes are disabled. Stable
  // (sized to the widest possible label, not the live values) so the left HUD
  // boxes can shift clear of it without jitter. Used as the left inset for the
  // timing overlay, cursor readout, and marker panel.
  [[nodiscard]] int axis_strip_width() const;

  // Draw the spectrum bars on the GPU (one SDL_RenderGeometry call, solid-color
  // geometry, no texture). Called into the active render target.
  void render_spectrum(const std::vector<SDL_Vertex> &verts,
                       const std::vector<int> &indices);

  // Compose the base frame into m_frame_tex: clear, blit the waterfall source
  // region to the bottom, then draw the spectrum bars over the top region.
  // wf_src may be null to copy the whole source texture.
  void compose_base(SDL_Texture *wf_tex, const SDL_FRect *wf_src,
                    const SDL_FRect &wf_dst,
                    const std::vector<SDL_Vertex> &verts,
                    const std::vector<int> &indices);

  // RenderClear + blit m_frame_tex + overlays + present. Shared by all paths.
  void present_composited();

  // DEBUG (frame-timing branch): SDL_RenderPresent wrapped in a steady-clock
  // measurement, result stored in m_last_present_ms.
  void present_timed();

  void rebuild_freq_scale_ticks();

  size_t m_width;
  size_t m_height;
  size_t m_plot_width = 0; // window minus the right instrument panel
  SDL_Window *m_window = nullptr;
  SDL_Renderer *m_renderer = nullptr;
  SDL_Texture *m_texture = nullptr;

  // Persistent full-window TARGET texture holding the last composited base
  // frame (waterfall + GPU-drawn spectrum bars, no overlays). Both render paths
  // compose into it; present_frame() re-blits it so the idle/no-samples path
  // stays correct without re-running the spectrum/waterfall draw — and without
  // needing to know whether the waterfall is in fill or scroll phase.
  SDL_Texture *m_frame_tex = nullptr;

  // Read the just-composited backbuffer into m_capture_buf. Called from
  // present_composited() after render_overlays() and before the present, the
  // only window where the backbuffer holds the finished WYSIWYG frame.
  void capture_backbuffer();

  // VSYNC control
  bool m_enable_vsync = false;

  // Spectrogram capture (export): m_capture_pending armed by request_capture(),
  // serviced once in present_composited(); m_capture_ready signals the buffer
  // holds a frame for take_capture() to drain. Buffer is window-resolution
  // tightly packed RGBA.
  bool m_capture_pending = false;
  bool m_capture_ready = false;
  int m_capture_w = 0;
  int m_capture_h = 0;
  std::vector<uint8_t> m_capture_buf;

  // DEBUG (frame-timing branch): duration of the last present, see
  // last_present_ms().
  double m_last_present_ms = 0.0;

  // Latest per-second timing snapshot for the panel's PERF block.
  double m_timing_fps = 0.0;
  double m_timing_cpu_ms = 0.0;
  double m_timing_build_ms = 0.0;
  double m_timing_present_ms = 0.0;

  // Text rendering (panel + overlays). Panel text is blitted per-frame like the
  // marker/cursor overlays — cold path, ample headroom.
  std::unique_ptr<TextRenderer> m_text_renderer;

  // Right-panel status fields (set on change).
  PanelStatus m_status;

  // Latest peak amplitude (dBFS), shown in the panel. <= -140 → not yet known.
  float m_peak_db = -200.0F;

  // IQ logging status line (e.g. "LOGGING: 3s (…)"), shown in the panel when
  // capturing; empty otherwise.
  std::string m_current_iq_status;

  // Transient one-shot message (export result), drawn bottom-left over the plot
  // until its deadline. Set via render_status_bar().
  std::string m_status_msg;
  std::chrono::steady_clock::time_point m_status_until;

  // GPU ping-pong waterfall scroll textures.
  // m_wf_scroll_tex: the current display frame (TEXTUREACCESS_TARGET).
  // m_wf_scroll_aux: the scratch frame for the next shift+composite.
  // m_wf_line_tex:   narrow streaming texture for the new-line CPU upload.
  SDL_Texture *m_wf_scroll_tex = nullptr;
  SDL_Texture *m_wf_scroll_aux = nullptr;
  SDL_Texture *m_wf_line_tex = nullptr;
  bool m_wf_scroll_valid = false;
  size_t m_wf_scroll_line_height = 0;

  bool ensure_wf_scroll_textures(size_t wf_height, size_t line_height);

  // Frequency scale overlay — baked into a single RGBA target texture.
  // Rebuilt only when center_hz or sample_rate changes; render_overlays() does
  // one SDL_RenderCopy per frame instead of N state-changing draw calls.
  SDL_Texture *m_freq_scale_texture = nullptr;
  uint32_t m_freq_scale_center_hz = 0;
  uint32_t m_freq_scale_sample_rate_hz = 0;
  size_t m_freq_scale_spectrum_height = 0;

  // Mouse cursor (render-space) captured in poll_events, and the readout main()
  // asks us to draw.
  bool m_cursor_active = false;
  float m_cursor_x = 0.0F;
  float m_cursor_y = 0.0F;
  CursorReadout m_cursor_readout;

  // Frequency markers to draw, and the click/clear input drained by main().
  std::vector<Marker> m_markers;
  std::vector<ClickEvent> m_clicks;
  bool m_clear_markers = false;

  // Left-margin axes overlay state (fed per frame by main()).
  bool m_axes_enabled = false;
  float m_axis_db_min = -120.0F;
  float m_axis_db_max = 0.0F;
  double m_axis_wf_top_seconds = 0.0;

  // Amplitude-trigger input (Shift+left-drag) and per-frame draw state.
  bool m_trigger_set_pending = false;
  float m_trigger_set_y = 0.0F;
  bool m_trigger_armed = false;
  float m_trigger_line_y = 0.0F;
  float m_trigger_db = 0.0F;
  bool m_frozen = false;
};

} // namespace openspectrum
