// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "signal/signal_processor.h"

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace openspectrum {

class SampleSource;

// The defaults are the RTL2832U/R820T range; main() replaces them with the
// source's limits() via set_constraints().
struct DeviceConstraints {
  uint64_t min_frequency_hz = 500000;     // 500 kHz
  uint64_t max_frequency_hz = 1700000000; // 1.7 GHz
  float min_gain_db = 0.0f;
  float max_gain_db = 49.6f; // RTL2832U max
  // The displayed span (the sample rate): one frequency key press moves one
  // span, so stepping across a band leaves no gap and no overlap.
  uint64_t span_hz = 2048000;
  // 32768/65536 are for narrow-signal work, not the default view: the extra
  // bins are finer than the pane shows and cost power, not frame rate
  // (docs/MEASUREMENTS.md). 131072 is absent: it needs 4 device callbacks per
  // frame, which caps the line rate (see buf_len in rtl_sdr_device.cpp).
  std::vector<size_t> supported_fft_sizes = {1024,  2048,  4096, 8192,
                                             16384, 32768, 65536};
  std::vector<WindowFunction> supported_window_functions = {
      WindowFunction::RECTANGLE,       WindowFunction::HANN,
      WindowFunction::HAMMING,         WindowFunction::BLACKMAN,
      WindowFunction::BLACKMAN_HARRIS, WindowFunction::FLAT_TOP};
};

// What the keys change. SDL-agnostic.
class ControlState {
public:
  ControlState() = default;
  ~ControlState() = default;

  ControlState(const ControlState &) = delete;
  ControlState &operator=(const ControlState &) = delete;

  uint64_t get_frequency() const noexcept { return frequency_hz; }
  float get_gain() const noexcept { return gain_db; }
  size_t get_fft_size() const noexcept { return fft_size; }
  WindowFunction get_window() const noexcept { return window_function; }

  void set_frequency(uint64_t hz);
  void set_gain(float db);
  void set_fft_size(size_t size);
  void set_window(WindowFunction w);
  // While a trigger freeze holds the frame, the keys for frequency, gain, FFT
  // size, window and palette do nothing: the panel and an export must keep
  // describing the held frame. main() sets it each frame.
  void hold_controls(bool held) noexcept { m_controls_held = held; }
  bool controls_held() const noexcept { return m_controls_held; }

  void set_constraints(const DeviceConstraints &new_constraints);
  const DeviceConstraints &get_constraints() const noexcept {
    return constraints;
  }

  bool fft_size_changed() const noexcept { return fft_changed; }
  void clear_fft_change_flag() noexcept { fft_changed = false; }

  bool window_changed() const noexcept { return window_changed_flag; }
  void clear_window_change_flag() noexcept { window_changed_flag = false; }

  // 'c' / Shift+C. An index into SpectrumPalette::ColorMap, which main.cpp
  // maps, so this class stays independent of the display.
  // == SpectrumPalette::ColorMap size
  static constexpr size_t PALETTE_COUNT = 5;
  size_t get_palette_index() const noexcept { return palette_index; }
  void cycle_palette(int direction); // +1 or -1; wraps
  void set_palette(size_t index);    // ignored past PALETTE_COUNT
  bool palette_changed() const noexcept { return palette_changed_flag; }
  void clear_palette_change_flag() noexcept { palette_changed_flag = false; }
  static const char *palette_name(size_t index) noexcept;

  // Formatted fields for the panel. trace is "AVG", "MAX", "AVG MAX" or empty.
  struct StatusFields {
    std::string freq;
    std::string gain;
    std::string fft;
    std::string window;
    std::string palette;
    std::string trace;
  };
  StatusFields get_status_fields() const;

  static std::string format_gain(float db);

  bool status_changed() const noexcept { return m_status_dirty; }
  void clear_status_dirty() const noexcept { m_status_dirty = false; }
  void mark_status_dirty() const noexcept { m_status_dirty = true; }

  bool iq_logging_toggle_requested() const noexcept {
    return m_iq_logging_toggle;
  }
  void clear_iq_logging_toggle() noexcept { m_iq_logging_toggle = false; }
  void request_iq_logging_toggle() noexcept { m_iq_logging_toggle = true; }

  bool spectrogram_export_requested() const noexcept {
    return m_spectrogram_export_requested;
  }
  void clear_spectrogram_export() noexcept {
    m_spectrogram_export_requested = false;
  }
  void request_spectrogram_export() noexcept {
    m_spectrogram_export_requested = true;
  }

  // Space: a one-shot request main() drains to unfreeze and re-arm.
  bool unfreeze_requested() const noexcept { return m_unfreeze_requested; }
  void clear_unfreeze() noexcept { m_unfreeze_requested = false; }
  void request_unfreeze() noexcept { m_unfreeze_requested = true; }

  // 'm' max-hold, 'a' averaging, 'x' reset; main.cpp pushes them into
  // SpectrumDisplay.
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

  void apply_to_device(SampleSource &dev) const;

private:
  bool fft_size_supported(size_t size) const;
  bool window_supported(WindowFunction w) const;

  DeviceConstraints constraints;

  uint64_t frequency_hz = 100000000;
  float gain_db = 20.0f;
  size_t fft_size = 4096;
  WindowFunction window_function = WindowFunction::BLACKMAN_HARRIS;

  bool fft_changed = false;
  bool window_changed_flag = false;
  // 0 = first ColorMap (display default); see main.cpp
  size_t palette_index = 0;
  bool palette_changed_flag = false;
  bool m_iq_logging_toggle = false;
  bool m_spectrogram_export_requested = false;
  bool m_unfreeze_requested = false;
  bool m_max_hold = false;
  // Averaging is on by default: a single periodogram reads as grass. The
  // panel's TRACE field shows it.
  bool m_averaging = true;
  bool m_trace_reset = false;
  bool m_controls_held = false;

  // Set when a panel field changes; main re-reads get_status_fields().
  mutable bool m_status_dirty = true;

  // Last values programmed into the tuner: apply_to_device() runs every frame
  // and each write is a blocking USB transfer.
  mutable bool m_device_applied = false;
  mutable uint64_t m_applied_frequency_hz = 0;
  mutable float m_applied_gain_db = 0.0f;
};

} // namespace openspectrum
