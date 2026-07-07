// SPDX-License-Identifier: GPL-3.0-or-later

#include "openspectrum/control_state.h"
#include "format.h"
#include "rtl_sdr_device.h"
#include "signal_processor.h"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <string>

namespace openspectrum {

auto ControlState::fft_size_supported(size_t size) const -> bool {
  return std::ranges::find(constraints.supported_fft_sizes, size) !=
         constraints.supported_fft_sizes.end();
}

auto ControlState::window_supported(WindowFunction w) const -> bool {
  return std::ranges::find(constraints.supported_window_functions, w) !=
         constraints.supported_window_functions.end();
}

// Format window function name for display
static auto format_window(WindowFunction w) -> std::string {
  return std::string(SignalProcessor::window_function_to_string(w));
}

// Format gain value
auto ControlState::format_gain(float db) -> std::string {
  char buffer[32];
  snprintf(buffer, sizeof(buffer), "%.1f dB", db);
  return std::string(buffer);
}

// Setters for initial configuration
void ControlState::set_frequency(uint32_t hz) {
  frequency_hz = std::clamp(hz, constraints.min_frequency_hz,
                            constraints.max_frequency_hz);
}

void ControlState::set_gain(float db) {
  gain_db = std::clamp(db, constraints.min_gain_db, constraints.max_gain_db);
}

void ControlState::set_fft_size(size_t size) {
  if (fft_size_supported(size)) {
    if (size != fft_size) {
      fft_size = size;
      fft_changed = true;
    }
  } else if (!constraints.supported_fft_sizes.empty()) {
    // Fallback to first supported size
    size_t const fallback = constraints.supported_fft_sizes[0];
    if (fallback != fft_size) {
      fft_size = fallback;
      fft_changed = true;
    }
  }
}

// Set constraints for different device types
void ControlState::set_constraints(const DeviceConstraints &new_constraints) {
  constraints = new_constraints;
  // Clamp current values to new constraints
  frequency_hz = std::clamp(frequency_hz, constraints.min_frequency_hz,
                            constraints.max_frequency_hz);
  gain_db =
      std::clamp(gain_db, constraints.min_gain_db, constraints.max_gain_db);
  // Ensure FFT size is supported
  if (!fft_size_supported(fft_size) &&
      !constraints.supported_fft_sizes.empty()) {
    fft_size = constraints.supported_fft_sizes[0];
  }
  // Ensure window function is supported
  if (!window_supported(window_function) &&
      !constraints.supported_window_functions.empty()) {
    window_function = constraints.supported_window_functions[0];
  }
}

// Setter for window function
void ControlState::set_window(WindowFunction w) {
  if (window_supported(w)) {
    if (w != window_function) {
      window_function = w;
      window_changed_flag = true;
    }
  } else if (!constraints.supported_window_functions.empty()) {
    WindowFunction const fallback = constraints.supported_window_functions[0];
    if (fallback != window_function) {
      window_function = fallback;
      window_changed_flag = true;
    }
  }
}

// Cycle the active color palette. direction: +1 forward, -1 backward (wraps).
void ControlState::cycle_palette(int direction) {
  const int n = static_cast<int>(PALETTE_COUNT);
  int idx = (static_cast<int>(palette_index) + direction) % n;
  if (idx < 0) {
    idx += n;
  }
  palette_index = static_cast<size_t>(idx);
  palette_changed_flag = true;
  mark_status_dirty();
}

// Short display name for a palette index. Order MUST match the display's
// SpectrumPalette::ColorMap enum { JET, VIRIDIS, HOT, GRAYSCALE, BLUE_RED }.
auto ControlState::palette_name(size_t index) noexcept -> const char * {
  static constexpr const char *kNames[PALETTE_COUNT] = {"JET", "VIRIDIS", "HOT",
                                                        "GRAY", "BLU-RED"};
  return index < PALETTE_COUNT ? kNames[index] : "?";
}

// Get formatted status string for display (cached)
auto ControlState::get_status_string() const -> std::string {
  if (!m_status_dirty) {
    return m_cached_status;
  }
  if (reconfiguring) {
    m_cached_status = "Reconfiguring FFT... ";
  } else {
    m_cached_status = "FREQ: " + format_frequency(frequency_hz) +
                      " | GAIN: " + format_gain(gain_db) +
                      " | FFT: " + std::to_string(fft_size) +
                      " | WINDOW: " + format_window(window_function) +
                      " | PAL: " + palette_name(palette_index);
    if (m_averaging || m_max_hold) {
      m_cached_status += " | TRC:";
      if (m_averaging) {
        m_cached_status += " AVG";
      }
      if (m_max_hold) {
        m_cached_status += " MAX";
      }
    }
  }
  m_status_dirty = false;
  return m_cached_status;
}

// Apply all pending changes to device (batch update)
void ControlState::apply_to_device(RtlSdrDevice &dev) const {
  // Only reprogram the tuner when a value actually changed. set_frequency() and
  // set_gain() each issue blocking USB control transfers (PLL retune, gain-mode
  // switch); calling them unconditionally every frame was the render-loop
  // bottleneck. The first call always applies both to program initial state.
  if (!m_device_applied || frequency_hz != m_applied_frequency_hz) {
    dev.set_frequency(frequency_hz);
    m_applied_frequency_hz = frequency_hz;
  }
  if (!m_device_applied || gain_db != m_applied_gain_db) {
    dev.set_gain(gain_db);
    m_applied_gain_db = gain_db;
  }
  m_device_applied = true;
}

} // namespace openspectrum
