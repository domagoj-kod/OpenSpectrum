// SPDX-License-Identifier: MIT

#include "arg_parser.h"
#include "signal_processor.h"

#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <iostream>
#include <string>
#include <string_view>

namespace openspectrum {

namespace {

auto parse_float(const char *str, float &value) -> bool {
  try {
    value = std::stof(str);
    return true;
  } catch (...) {
    return false;
  }
}

auto parse_double(const char *str, double &value) -> bool {
  try {
    value = std::stod(str);
    return true;
  } catch (...) {
    return false;
  }
}

auto parse_size_t(const char *str, size_t &value) -> bool {
  try {
    value = static_cast<size_t>(std::stoul(str));
    return true;
  } catch (...) {
    return false;
  }
}

} // namespace

void print_usage(const char *argv0) {
  std::cout
      << "Usage: " << argv0 << " [OPTIONS]\n"
      << "\n"
      << "OpenSpectrum - SDR Spectrum Analyzer\n"
      << "\n"
      << "Options:\n"
      << "  -f, --freq HZ       Center frequency in Hz (default: 92600000)\n"
      << "  -r, --rate HZ       Sample rate in Hz (default: 2048000)\n"
      << "  -g, --gain DB       Gain in dB (default: 10.0)\n"
      << "  -s, --fft-size N    FFT size (power of 2, default: 4096)\n"
      << "  -w, --width N       Display width in pixels (default: 1050)\n"
      << "  -H, --height N      Display height in pixels (default: 576)\n"
      << "  -W, --window NAME   Window function: rectangle, hann, hamming,\n"
      << "                      blackman, blackman-harris, flat-top\n"
      << "                      (default: blackman-harris)\n"
      << "  --iq-log            Enable IQ data logging to file\n"
      << "  --iq-duration SEC   Capture duration in seconds (default: 0 = "
         "manual)\n"
      << "  --iq-output FILE    Output filename prefix (default: "
         "auto-generated)\n"
      << "  --help              Show this help message\n"
      << "\n"
      << "Examples:\n"
      << "  " << argv0 << " -f 100000000 -g 20\n"
      << "  " << argv0 << " --freq 144500000 --gain 15 --fft-size 8192\n"
      << "  " << argv0 << " -W hann\n"
      << "  " << argv0 << " --iq-log --iq-duration 10 --iq-output my_capture\n"
      << std::flush;
}

auto parse_arguments(int argc, char *argv[]) -> AppConfig {
  AppConfig config;

  for (int i = 1; i < argc; ++i) {
    std::string_view const arg = argv[i];

    if (arg == "-f" || arg == "--freq") {
      if (i + 1 >= argc || !parse_float(argv[i + 1], config.center_freq_hz)) {
        std::cerr << "Error: Invalid frequency value\n";
        print_usage(argv[0]);
        std::exit(1);
      }
      ++i;
    } else if (arg == "-r" || arg == "--rate") {
      if (i + 1 >= argc || !parse_float(argv[i + 1], config.sample_rate_hz)) {
        std::cerr << "Error: Invalid sample rate value\n";
        print_usage(argv[0]);
        std::exit(1);
      }
      ++i;
    } else if (arg == "-g" || arg == "--gain") {
      if (i + 1 >= argc || !parse_float(argv[i + 1], config.gain_db)) {
        std::cerr << "Error: Invalid gain value\n";
        print_usage(argv[0]);
        std::exit(1);
      }
      ++i;
    } else if (arg == "-s" || arg == "--fft-size") {
      if (i + 1 >= argc || !parse_size_t(argv[i + 1], config.fft_size)) {
        std::cerr << "Error: Invalid FFT size value\n";
        print_usage(argv[0]);
        std::exit(1);
      }
      ++i;
    } else if (arg == "-w" || arg == "--width") {
      if (i + 1 >= argc || !parse_size_t(argv[i + 1], config.display_width)) {
        std::cerr << "Error: Invalid width value\n";
        print_usage(argv[0]);
        std::exit(1);
      }
      ++i;
    } else if (arg == "-H" || arg == "--height") {
      if (i + 1 >= argc || !parse_size_t(argv[i + 1], config.display_height)) {
        std::cerr << "Error: Invalid height value\n";
        print_usage(argv[0]);
        std::exit(1);
      }
      ++i;
    } else if (arg == "-W" || arg == "--window") {
      if (i + 1 >= argc) {
        std::cerr << "Error: Window function name required\n";
        print_usage(argv[0]);
        std::exit(1);
      }
      std::string window_name = argv[++i];
      // Convert to lowercase for case-insensitive matching
      std::ranges::transform(window_name, window_name.begin(),
                             [](unsigned char c) { return std::tolower(c); });
      if (window_name == "rectangle") {
        config.window_function = WindowFunction::RECTANGLE;
      } else if (window_name == "hann") {
        config.window_function = WindowFunction::HANN;
      } else if (window_name == "hamming") {
        config.window_function = WindowFunction::HAMMING;
      } else if (window_name == "blackman") {
        config.window_function = WindowFunction::BLACKMAN;
      } else if (window_name == "blackman-harris" ||
                 window_name == "blackman_harris") {
        config.window_function = WindowFunction::BLACKMAN_HARRIS;
      } else if (window_name == "flat-top" || window_name == "flat_top") {
        config.window_function = WindowFunction::FLAT_TOP;
      } else {
        std::cerr << "Error: Unknown window function: " << window_name << "\n";
        print_usage(argv[0]);
        std::exit(1);
      }
    } else if (arg == "--iq-log") {
      config.iq_logging_enabled = true;
    } else if (arg == "--iq-duration") {
      if (i + 1 >= argc ||
          !parse_double(argv[i + 1], config.iq_capture_duration)) {
        std::cerr << "Error: Invalid IQ capture duration value\n";
        print_usage(argv[0]);
        std::exit(1);
      }
      ++i;
      config.iq_logging_enabled = true; // Enable logging if duration is set
    } else if (arg == "--iq-output") {
      if (i + 1 >= argc) {
        std::cerr << "Error: IQ output filename required\n";
        print_usage(argv[0]);
        std::exit(1);
      }
      config.iq_output_file = argv[++i];
      config.iq_logging_enabled = true; // Enable logging if output is set
    } else if (arg == "--help" || arg == "-h") {
      config.show_help = true;
      print_usage(argv[0]);
      std::exit(0);
    } else {
      std::cerr << "Error: Unknown argument: " << arg << "\n";
      print_usage(argv[0]);
      std::exit(1);
    }
  }

  return config;
}

} // namespace openspectrum
