// SPDX-License-Identifier: MIT

#include "openspectrum/control_state.h"
#include "rtl_sdr_device.h"
#include "signal_processor.h"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <iterator>
#include <string>

namespace openspectrum {

ControlState::ControlState() {
  // Initialize RTL2832U-specific constraints
  constraints.min_frequency_hz = 500000;     // 500 kHz
  constraints.max_frequency_hz = 1700000000; // 1.7 GHz
  constraints.min_gain_db = 0.0F;
  constraints.max_gain_db = 49.6F;
  constraints.supported_fft_sizes = {512, 1024, 2048, 4096};
}

// Helper to find FFT size index
auto ControlState::find_fft_index(size_t size) const -> int {
  auto it = std::ranges::find(constraints.supported_fft_sizes, size);
  if (it != constraints.supported_fft_sizes.end()) {
    return static_cast<int>(
        std::distance(constraints.supported_fft_sizes.begin(), it));
  }
  return -1;
}

// Helper to find window function index
auto ControlState::find_window_index(WindowFunction w) const -> int {
  auto it = std::ranges::find(constraints.supported_window_functions, w);
  if (it != constraints.supported_window_functions.end()) {
    return static_cast<int>(
        std::distance(constraints.supported_window_functions.begin(), it));
  }
  return -1;
}

// Format window function name for display
static auto format_window(WindowFunction w) -> std::string {
  return std::string(SignalProcessor::window_function_to_string(w));
}

// Format frequency with auto-scaling units
auto ControlState::format_frequency(uint32_t hz) -> std::string {
  if (hz >= 1000000000) {
    return std::to_string(hz / 1000000000) + "." +
           std::to_string((hz % 1000000000) / 1000000) + " GHz";
  }
  if (hz >= 1000000) {
    return std::to_string(hz / 1000000) + "." +
           std::to_string((hz % 1000000) / 1000) + " MHz";
  }
  if (hz >= 1000) {
    return std::to_string(hz / 1000) + "." + std::to_string(hz % 1000) + " kHz";
  }
  return std::to_string(hz) + " Hz";
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
  frequency_prev = frequency_hz;
}

void ControlState::set_gain(float db) {
  gain_db = std::clamp(db, constraints.min_gain_db, constraints.max_gain_db);
  gain_prev = gain_db;
}

void ControlState::set_fft_size(size_t size) {
  int const index = find_fft_index(size);
  if (index >= 0) {
    if (size != fft_size) {
      fft_size = size;
      fft_prev = size;
      fft_changed = true;
    }
  } else if (!constraints.supported_fft_sizes.empty()) {
    // Fallback to first supported size
    size_t const fallback = constraints.supported_fft_sizes[0];
    if (fallback != fft_size) {
      fft_size = fallback;
      fft_prev = fft_size;
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
  if (find_fft_index(fft_size) < 0 &&
      !constraints.supported_fft_sizes.empty()) {
    fft_size = constraints.supported_fft_sizes[0];
  }
  // Ensure window function is supported
  if (find_window_index(window_function) < 0 &&
      !constraints.supported_window_functions.empty()) {
    window_function = constraints.supported_window_functions[0];
  }
}

// Setter for window function
void ControlState::set_window(WindowFunction w) {
  int const index = find_window_index(w);
  if (index >= 0) {
    if (w != window_function) {
      window_function = w;
      window_prev = w;
      window_changed_flag = true;
    }
  } else if (!constraints.supported_window_functions.empty()) {
    WindowFunction const fallback = constraints.supported_window_functions[0];
    if (fallback != window_function) {
      window_function = fallback;
      window_prev = window_function;
      window_changed_flag = true;
    }
  }
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
                      " | WINDOW: " + format_window(window_function);
  }
  m_status_dirty = false;
  return m_cached_status;
}

// Apply all pending changes to device (batch update)
void ControlState::apply_to_device(RtlSdrDevice &dev) const {
  dev.set_frequency(frequency_hz);
  dev.set_gain(gain_db);
}

} // namespace openspectrum
