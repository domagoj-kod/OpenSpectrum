// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "openspectrum/attributes.h"
#include "signal_processor.h"

#include <cstdint>
#include <string>

namespace openspectrum {

// Configuration structure for command-line arguments
struct AppConfig {
  size_t fft_size = 4096;
  size_t display_width = 1050;
  size_t display_height = 576;        // 576i screen resolution config
  float center_freq_hz = 92600000.0f; // 92.6 MHz
  float sample_rate_hz = 2048000.0f;  // 2.048 MS/s
  float gain_db = 10.0f;              // 10 dB
  WindowFunction window_function = WindowFunction::BLACKMAN_HARRIS;
  bool show_help = false;

  // IQ logging options
  bool iq_logging_enabled = false;
  double iq_capture_duration = 0.0; // seconds (0 = manual stop via keyboard)
  std::string iq_output_file;       // Output filename (without extension)

  // Device options (applied once after open, before streaming)
  int ppm_correction = 0;       // crystal frequency correction
  bool bias_tee = false;        // 4.5 V antenna-port power
  bool direct_sampling = false; // Q-branch direct sampling (HF, 0-14.4 MHz)

  // IQ playback: replay a .iq capture instead of opening hardware
  std::string play_file;
};

// Parse command-line arguments
OS_COLD AppConfig parse_arguments(int argc, char *argv[]);

// Print usage information
OS_COLD void print_usage(const char *argv0);

// Validate FFT size is a power of two
inline bool is_power_of_two(size_t n) { return n > 0 && (n & (n - 1)) == 0; }

} // namespace openspectrum
