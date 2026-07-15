// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "signal_processor.h"

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

// Forward declaration for RtlSdrDevice (defined in global namespace)
class RtlSdrDevice;

namespace openspectrum {

// Device constraints structure. Defaults are the RTL2832U/R820T tuning range;
// playback and direct-sampling modes override via set_constraints().
struct DeviceConstraints {
  uint32_t min_frequency_hz = 500000;     // 500 kHz
  uint32_t max_frequency_hz = 1700000000; // 1.7 GHz
  float min_gain_db = 0.0f;
  float max_gain_db = 49.6f; // RTL2832U max
  // 32768/65536 are opt-in, not a better default: at 2.048 MS/s across a ~1400
  // px pane the display shows ~1.4 kHz/px, so 16384's 125 Hz RBW is already
  // ~11x finer than anything visible — the extra bins buy resolution you cannot
  // see, and cost +81%/+189% cycles over the 4096 default (T470; every size
  // still holds 30 fps, so the cost is power, not frame rate). They earn
  // their place for narrow-signal work, not for the default view. 131072 is
  // deliberately absent: it needs 4 device callbacks per frame, which caps the
  // line rate at ~15.6/s — the exact thing rtl_sdr_device.cpp's buf_len=65536
  // was chosen to escape. Still reachable via --fft-size for experiments.
  std::vector<size_t> supported_fft_sizes = {1024,  2048,  4096, 8192,
                                             16384, 32768, 65536};
  std::vector<WindowFunction> supported_window_functions = {
      WindowFunction::RECTANGLE,       WindowFunction::HANN,
      WindowFunction::HAMMING,         WindowFunction::BLACKMAN,
      WindowFunction::BLACKMAN_HARRIS, WindowFunction::FLAT_TOP};
};

// SDL-agnostic state management for runtime controls
class ControlState {
public:
  ControlState() = default;
  ~ControlState() = default;

  // Non-copyable, non-movable
  ControlState(const ControlState &) = delete;
  ControlState &operator=(const ControlState &) = delete;

  // Getters for current values
  uint32_t get_frequency() const noexcept { return frequency_hz; }
  float get_gain() const noexcept { return gain_db; }
  size_t get_fft_size() const noexcept { return fft_size; }
  WindowFunction get_window() const noexcept { return window_function; }

  // Setters (for initial configuration)
  void set_frequency(uint32_t hz);
  void set_gain(float db);
  void set_fft_size(size_t size);
  void set_window(WindowFunction w);

  // Set constraints (for different device types)
  void set_constraints(const DeviceConstraints &new_constraints);
  const DeviceConstraints &get_constraints() const noexcept {
    return constraints;
  }

  // Check if FFT needs reinitialization
  bool fft_size_changed() const noexcept { return fft_changed; }
  void clear_fft_change_flag() noexcept { fft_changed = false; }

  // Check if window function changed
  bool window_changed() const noexcept { return window_changed_flag; }
  void clear_window_change_flag() noexcept { window_changed_flag = false; }

  // Color palette cycling (runtime, 'c' / Shift+C). Stored as an index into the
  // display's ColorMap enum; main.cpp maps it to SpectrumPalette::ColorMap so
  // this SDL/display-agnostic class stays decoupled from the visualization layer.
  static constexpr size_t PALETTE_COUNT = 5; // == SpectrumPalette::ColorMap size
  size_t get_palette_index() const noexcept { return palette_index; }
  void cycle_palette(int direction); // +1 forward, -1 backward; wraps
  bool palette_changed() const noexcept { return palette_changed_flag; }
  void clear_palette_change_flag() noexcept { palette_changed_flag = false; }
  static const char *palette_name(size_t index) noexcept;

  // Get formatted status string for display
  std::string get_status_string() const;

  // Individual formatted status fields for the instrument panel (green-label /
  // white-value grid). trace is "AVG" / "MAX" / "AVG MAX" or empty.
  struct StatusFields {
    std::string freq;
    std::string gain;
    std::string fft;
    std::string window;
    std::string palette;
    std::string trace;
  };
  StatusFields get_status_fields() const;

  // Get individual formatted values
  static std::string format_gain(float db);

  // Status string caching
  bool status_changed() const noexcept { return m_status_dirty; }
  void clear_status_dirty() const noexcept { m_status_dirty = false; }
  void mark_status_dirty() const noexcept { m_status_dirty = true; }

  // Reconfiguration flag (shown in the status string while FFT reinit runs)
  void set_reconfiguring(bool state) noexcept { reconfiguring = state; }

  // IQ logging control
  bool iq_logging_toggle_requested() const noexcept {
    return m_iq_logging_toggle;
  }
  void clear_iq_logging_toggle() noexcept { m_iq_logging_toggle = false; }
  void request_iq_logging_toggle() noexcept { m_iq_logging_toggle = true; }

  // Spectrogram export control
  bool spectrogram_export_requested() const noexcept {
    return m_spectrogram_export_requested;
  }
  void clear_spectrogram_export() noexcept {
    m_spectrogram_export_requested = false;
  }
  void request_spectrogram_export() noexcept {
    m_spectrogram_export_requested = true;
  }

  // Amplitude-trigger resume (Space). One-shot request main() drains to
  // unfreeze the display and re-arm the trigger.
  bool unfreeze_requested() const noexcept { return m_unfreeze_requested; }
  void clear_unfreeze() noexcept { m_unfreeze_requested = false; }
  void request_unfreeze() noexcept { m_unfreeze_requested = true; }

  // Spectrum trace modes ('m' max-hold, 'a' video averaging, 'x' reset).
  // SDL/display-agnostic flags; main.cpp pushes them into SpectrumDisplay.
  bool max_hold_enabled() const noexcept { return m_max_hold; }
  void toggle_max_hold() noexcept {
    m_max_hold = !m_max_hold;
    mark_status_dirty();
  }
  bool averaging_enabled() const noexcept { return m_averaging; }
  void toggle_averaging() noexcept {
    m_averaging = !m_averaging;
    mark_status_dirty();
  }
  bool trace_reset_requested() const noexcept { return m_trace_reset; }
  void request_trace_reset() noexcept { m_trace_reset = true; }
  void clear_trace_reset() noexcept { m_trace_reset = false; }

  // Apply all pending changes to device (batch update)
  void apply_to_device(RtlSdrDevice &dev) const;

private:
  bool fft_size_supported(size_t size) const;
  bool window_supported(WindowFunction w) const;

  DeviceConstraints constraints;

  // Current values
  uint32_t frequency_hz = 100000000;
  float gain_db = 20.0f;
  size_t fft_size = 4096;
  WindowFunction window_function = WindowFunction::BLACKMAN_HARRIS;

  // Flags
  bool fft_changed = false;
  bool reconfiguring = false;
  bool window_changed_flag = false;
  size_t palette_index = 0; // 0 = first ColorMap (display default); see main.cpp
  bool palette_changed_flag = false;
  bool m_iq_logging_toggle = false;
  bool m_spectrogram_export_requested = false;
  bool m_unfreeze_requested = false;
  bool m_max_hold = false;
  // Averaging on by default: the raw per-frame trace is a single periodogram
  // (5.57 dB of noise-floor spread), which reads as grass. 'a' toggles it off;
  // the panel's TRACE field shows AVG so the default is visible, not silent.
  bool m_averaging = true;
  bool m_trace_reset = false;

  // Status string caching for performance
  mutable std::string m_cached_status;
  mutable bool m_status_dirty = true;

  // Last values actually programmed into the tuner. apply_to_device() is called
  // every render-loop iteration; without these guards it re-issued blocking USB
  // control transfers (rtlsdr_set_center_freq + tuner gain-mode/gain writes,
  // i.e. a full PLL retune) every frame, which dominated the loop wall-time and
  // throttled the displayed frame rate (worst over high-latency USB paths such
  // as WSL2 usbipd). Now writes fire only when freq/gain actually change.
  mutable bool m_device_applied = false;
  mutable uint32_t m_applied_frequency_hz = 0;
  mutable float m_applied_gain_db = 0.0f;
};

} // namespace openspectrum
