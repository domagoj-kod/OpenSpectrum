// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "signal/signal_processor.h"
#include "utils/attributes.h"
#include "utils/iq_logger.h"

#include <optional>
#include <string>

namespace openspectrum {

struct AppConfig {
  size_t fft_size = 4096;
  // double, not float: a float puts 5.8 GHz up to 256 Hz off.
  double center_freq_hz = 92600000.0; // 92.6 MHz
  double sample_rate_hz = 2048000.0;  // 2.048 MS/s
  float gain_db = 10.0f;              // 10 dB
  WindowFunction window_function = WindowFunction::BLACKMAN_HARRIS;
  // No flag: only the saved settings set it (ControlState::palette_name).
  size_t palette_index = 0;

  // Frame-rate cap; 30 halves render power for long runs. 0 = uncapped (vsync
  // still paces).
  int max_fps = 30;

  bool iq_logging_enabled = false;
  double iq_capture_duration = 0.0; // seconds (0 = manual stop via keyboard)
  std::string iq_output_file;       // filename prefix
  IqSampleFormat iq_format = IqSampleFormat::CF32;

  int ppm_correction = 0;       // crystal frequency correction
  bool bias_tee = false;        // 4.5 V antenna-port power
  bool direct_sampling = false; // Q-branch direct sampling (HF, 0-14.4 MHz)

  // Receiver: the RTL2832U, or a USRP through UHD. A UHD=1 build is the
  // USRP download, so it defaults to the USRP.
#ifdef OPENSPECTRUM_UHD
  bool use_uhd = true;
#else
  bool use_uhd = false;
#endif

  // IQ playback: replay a .iq capture instead of opening hardware
  std::string play_file;
};

// The command line over config: main() passes the saved settings in.
OS_COLD AppConfig parse_arguments(int argc, char *argv[],
                                  AppConfig config = {});

// The --window names, also the saved setting's values. Any case; '_' for '-'.
OS_COLD std::optional<WindowFunction> parse_window_name(std::string name);
OS_COLD const char *window_name(WindowFunction window);

OS_COLD void print_usage(const char *argv0);

} // namespace openspectrum
