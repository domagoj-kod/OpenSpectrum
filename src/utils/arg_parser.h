// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "openspectrum/attributes.h"
#include "openspectrum/iq_logger.h"
#include "signal_processor.h"

#include <string>

namespace openspectrum {

// Configuration structure for command-line arguments
struct AppConfig {
  size_t fft_size = 4096;
  // double, not float: a float holds 24 bits, so 2.437 GHz would land up to
  // 128 Hz off and 5.8 GHz up to 256 Hz off before reaching the tuner.
  double center_freq_hz = 92600000.0; // 92.6 MHz
  double sample_rate_hz = 2048000.0;  // 2.048 MS/s
  float gain_db = 10.0f;              // 10 dB
  WindowFunction window_function = WindowFunction::BLACKMAN_HARRIS;

  // Frame-rate cap. Default 30 keeps the display smooth while roughly halving
  // GPU/render power vs the 60 Hz refresh — the low-end/battery default for
  // long unattended measurements. Override with --max-fps N; 0 = uncapped
  // (vsync alone paces, useful for perf profiling). Vsync stays on either way.
  int max_fps = 30;

  // IQ logging options
  bool iq_logging_enabled = false;
  double iq_capture_duration = 0.0; // seconds (0 = manual stop via keyboard)
  std::string iq_output_file;       // Output filename (without extension)
  IqSampleFormat iq_format = IqSampleFormat::CF32;

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

} // namespace openspectrum
