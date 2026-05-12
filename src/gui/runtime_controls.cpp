// SPDX-License-Identifier: MIT

#include "runtime_controls.h"
#include "../hardware/rtl_sdr_device.h"
#include "../utils/logger.h"
#include "SDL_keycode.h"
#include "signal_processor.h"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <iterator>
#include <string>

namespace openspectrum {

RuntimeControls::RuntimeControls() {
  // Initialize RTL2832U-specific constraints
  constraints.min_frequency_hz = 500000;     // 500 kHz
  constraints.max_frequency_hz = 1700000000; // 1.7 GHz
  constraints.min_gain_db = 0.0F;
  constraints.max_gain_db = 49.6F;
  constraints.supported_fft_sizes = {512, 1024, 2048, 4096};
}

RuntimeControls::~RuntimeControls() = default;

// Helper to find FFT size index
auto RuntimeControls::find_fft_index(size_t size) const -> int {
  auto it = std::ranges::find(constraints.supported_fft_sizes, size);
  if (it != constraints.supported_fft_sizes.end()) {
    return static_cast<int>(
        std::distance(constraints.supported_fft_sizes.begin(), it));
  }
  return -1;
}

// Helper to find window function index
auto RuntimeControls::find_window_index(WindowFunction w) const -> int {
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
auto RuntimeControls::format_frequency(uint32_t hz) -> std::string {
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
auto RuntimeControls::format_gain(float db) -> std::string {
  char buffer[32];
  snprintf(buffer, sizeof(buffer), "%.1f dB", db);
  return std::string(buffer);
}

// Handle keyboard input
auto RuntimeControls::handle_keyboard(SDL_Keycode key, bool shift_held,
                                      bool ctrl_held) -> bool {
  // Adjust step sizes based on modifiers
  uint32_t current_freq_step = freq_step;
  float current_gain_step = gain_step;

  if (shift_held) {
    // Fine control
    current_freq_step = 100000; // 0.1 MHz
    current_gain_step = 0.1F;   // 0.1 dB
  } else if (ctrl_held) {
    // Coarse control
    current_freq_step = 10000000; // 10 MHz
    current_gain_step = 10.0F;    // 10 dB
  }

  bool changed = false;

  switch (key) {
  // Frequency controls
  case SDLK_PLUS:
  case SDLK_EQUALS:
    frequency_prev = frequency_hz;
    frequency_hz = std::min(frequency_hz + current_freq_step,
                            constraints.max_frequency_hz);
    LOG_INFO("[FREQ+] " + format_frequency(frequency_prev) + " -> " +
             format_frequency(frequency_hz));
    changed = true;
    m_status_dirty = true;
    break;

  case SDLK_MINUS:
  case SDLK_UNDERSCORE:
    frequency_prev = frequency_hz;
    frequency_hz = std::max(frequency_hz - current_freq_step,
                            constraints.min_frequency_hz);
    LOG_INFO("[FREQ-] " + format_frequency(frequency_prev) + " -> " +
             format_frequency(frequency_hz));
    changed = true;
    m_status_dirty = true;
    break;

  // Gain controls
  case SDLK_r:
    gain_prev = gain_db;
    gain_db = std::min(gain_db + current_gain_step, constraints.max_gain_db);
    LOG_INFO("[GAIN+] " + format_gain(gain_prev) + " -> " +
             format_gain(gain_db));
    changed = true;
    m_status_dirty = true;
    break;

  case SDLK_f:
    gain_prev = gain_db;
    gain_db = std::max(gain_db - current_gain_step, constraints.min_gain_db);
    LOG_INFO("[GAIN-] " + format_gain(gain_prev) + " -> " +
             format_gain(gain_db));
    changed = true;
    m_status_dirty = true;
    break;

  // FFT size controls (1-4 keys cycle through supported sizes)
  case SDLK_1:
  case SDLK_2:
  case SDLK_3:
  case SDLK_4: {
    int const index = key - SDLK_1;
    if (index < static_cast<int>(constraints.supported_fft_sizes.size())) {
      fft_prev = fft_size;
      fft_size = constraints.supported_fft_sizes[index];
      fft_changed = true;
      m_status_dirty = true;
      LOG_INFO("[FFT] " + std::to_string(fft_prev) + " -> " +
               std::to_string(fft_size));
      changed = true;
    }
    break;
  }

  // Window function controls (UP/DOWN arrows cycle through supported windows)
  case SDLK_UP:
  case SDLK_DOWN: {
    int const direction = (key == SDLK_UP) ? 1 : -1;
    int index = find_window_index(window_function) + direction;
    if (index < 0) {
      index =
          static_cast<int>(constraints.supported_window_functions.size()) - 1;
    } else if (index >= static_cast<int>(
                            constraints.supported_window_functions.size())) {
      index = 0;
    }
    window_prev = window_function;
    window_function = constraints.supported_window_functions[index];
    window_changed_flag = true;
    m_status_dirty = true;
    LOG_INFO("[WINDOW] " + format_window(window_prev) + " -> " +
             format_window(window_function));
    changed = true;
    break;
  }
  }

  return changed;
}

// Check if FFT size changed
auto RuntimeControls::fft_size_changed() const -> bool { return fft_changed; }

void RuntimeControls::clear_fft_change_flag() { fft_changed = false; }

// Setters for initial configuration
void RuntimeControls::set_frequency(uint32_t hz) {
  frequency_hz = std::clamp(hz, constraints.min_frequency_hz,
                            constraints.max_frequency_hz);
  frequency_prev = frequency_hz;
}

void RuntimeControls::set_gain(float db) {
  gain_db = std::clamp(db, constraints.min_gain_db, constraints.max_gain_db);
  gain_prev = gain_db;
}

void RuntimeControls::set_fft_size(size_t size) {
  int const index = find_fft_index(size);
  if (index >= 0) {
    fft_size = size;
    fft_prev = size;
  } else if (!constraints.supported_fft_sizes.empty()) {
    // Fallback to first supported size
    fft_size = constraints.supported_fft_sizes[0];
    fft_prev = fft_size;
  }
}

// Set constraints for different device types
void RuntimeControls::set_constraints(
    const DeviceConstraints &new_constraints) {
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
void RuntimeControls::set_window(WindowFunction w) {
  int const index = find_window_index(w);
  if (index >= 0) {
    window_function = w;
    window_prev = w;
  } else if (!constraints.supported_window_functions.empty()) {
    window_function = constraints.supported_window_functions[0];
    window_prev = window_function;
  }
}

// Get formatted status string for display (cached)
auto RuntimeControls::get_status_string() const -> std::string {
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
void RuntimeControls::apply_to_device(RtlSdrDevice &dev) const {
  dev.set_frequency(frequency_hz);
  dev.set_gain(gain_db);
}

} // namespace openspectrum
