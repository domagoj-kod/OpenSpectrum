// SPDX-License-Identifier: GPL-3.0-or-later

#include "control_state.h"
#include "hardware/sample_source.h"
#include "signal/signal_processor.h"
#include "utils/format.h"

#include <algorithm>
#include <cmath>
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

static auto format_window(WindowFunction w) -> std::string {
  return SignalProcessor::window_function_to_string(w);
}

auto ControlState::format_gain(float db) -> std::string {
  char buffer[32];
  snprintf(buffer, sizeof(buffer), "%.1f dB", db);
  return buffer;
}

void ControlState::set_frequency(uint64_t hz) {
  frequency_hz = std::clamp(hz, constraints.min_frequency_hz,
                            constraints.max_frequency_hz);
}

void ControlState::set_gain(float db) {
  // NaN passes std::clamp, and the command line and the settings file read
  // "nan": keep the gain there is.
  if (std::isnan(db)) {
    return;
  }
  gain_db = std::clamp(db, constraints.min_gain_db, constraints.max_gain_db);
}

void ControlState::set_fft_size(size_t size) {
  if (fft_size_supported(size)) {
    if (size != fft_size) {
      fft_size = size;
      fft_changed = true;
    }
  } else if (!constraints.supported_fft_sizes.empty()) {
    size_t const fallback = constraints.supported_fft_sizes[0];
    if (fallback != fft_size) {
      fft_size = fallback;
      fft_changed = true;
    }
  }
}

void ControlState::set_constraints(const DeviceConstraints &new_constraints) {
  constraints = new_constraints;
  frequency_hz = std::clamp(frequency_hz, constraints.min_frequency_hz,
                            constraints.max_frequency_hz);
  gain_db =
      std::clamp(gain_db, constraints.min_gain_db, constraints.max_gain_db);
  if (!fft_size_supported(fft_size) &&
      !constraints.supported_fft_sizes.empty()) {
    fft_size = constraints.supported_fft_sizes[0];
  }
  if (!window_supported(window_function) &&
      !constraints.supported_window_functions.empty()) {
    window_function = constraints.supported_window_functions[0];
  }
}

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

void ControlState::set_palette(size_t index) {
  if (index < PALETTE_COUNT && index != palette_index) {
    palette_index = index;
    palette_changed_flag = true;
    mark_status_dirty();
  }
}

// Order MUST match SpectrumPalette::ColorMap { JET, VIRIDIS, HOT, GRAYSCALE,
// BLUE_RED }.
auto ControlState::palette_name(size_t index) noexcept -> const char * {
  static constexpr const char *kNames[PALETTE_COUNT] = {"JET", "VIRIDIS", "HOT",
                                                        "GRAY", "BLU-RED"};
  return index < PALETTE_COUNT ? kNames[index] : "?";
}

auto ControlState::get_status_fields() const -> StatusFields {
  StatusFields f;
  f.freq = format_frequency(frequency_hz);
  f.gain = format_gain(gain_db);
  f.fft = std::to_string(fft_size);
  f.window = format_window(window_function);
  f.palette = palette_name(palette_index);
  if (m_averaging && m_max_hold) {
    f.trace = "AVG MAX";
  } else if (m_averaging) {
    f.trace = "AVG";
  } else if (m_max_hold) {
    f.trace = "MAX";
  }
  return f;
}

void ControlState::apply_to_device(SampleSource &dev) const {
  // Only on a change: each is a blocking USB transfer. The first call applies
  // both.
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
