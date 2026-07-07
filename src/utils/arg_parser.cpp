// SPDX-License-Identifier: GPL-3.0-or-later

#include "arg_parser.h"
#include "signal_processor.h"

#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <iostream>
#include <string>
#include <string_view>
#include <type_traits>

namespace openspectrum {

namespace {

// One numeric parser for every option type; stoX throws on garbage.
template <typename T> auto parse_num(const char *str, T &value) -> bool {
  try {
    if constexpr (std::is_same_v<T, float>) {
      value = std::stof(str);
    } else if constexpr (std::is_same_v<T, double>) {
      value = std::stod(str);
    } else if constexpr (std::is_same_v<T, int>) {
      value = std::stoi(str);
    } else {
      value = static_cast<T>(std::stoul(str));
    }
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
      << "  --ppm N             Crystal frequency correction in ppm "
         "(default: 0)\n"
      << "  --bias-t            Power the antenna port (4.5 V bias tee)\n"
      << "  --direct-sampling   Q-branch direct sampling for HF "
         "(tunes 0-14.4 MHz)\n"
      << "  --iq-log            Enable IQ data logging to file\n"
      << "  --iq-duration SEC   Capture duration in seconds (default: 0 = "
         "manual)\n"
      << "  --iq-output FILE    Output filename prefix (default: "
         "auto-generated)\n"
      << "  --play FILE.iq      Replay a recorded IQ capture instead of "
         "opening\n"
      << "                      hardware (loops; reads freq/rate from the\n"
      << "                      .meta.json sidecar when present)\n"
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
      if (i + 1 >= argc || !parse_num(argv[i + 1], config.center_freq_hz)) {
        std::cerr << "Error: Invalid frequency value\n";
        print_usage(argv[0]);
        std::exit(1);
      }
      ++i;
    } else if (arg == "-r" || arg == "--rate") {
      if (i + 1 >= argc || !parse_num(argv[i + 1], config.sample_rate_hz)) {
        std::cerr << "Error: Invalid sample rate value\n";
        print_usage(argv[0]);
        std::exit(1);
      }
      ++i;
    } else if (arg == "-g" || arg == "--gain") {
      if (i + 1 >= argc || !parse_num(argv[i + 1], config.gain_db)) {
        std::cerr << "Error: Invalid gain value\n";
        print_usage(argv[0]);
        std::exit(1);
      }
      ++i;
    } else if (arg == "-s" || arg == "--fft-size") {
      if (i + 1 >= argc || !parse_num(argv[i + 1], config.fft_size)) {
        std::cerr << "Error: Invalid FFT size value\n";
        print_usage(argv[0]);
        std::exit(1);
      }
      ++i;
    } else if (arg == "-w" || arg == "--width") {
      if (i + 1 >= argc || !parse_num(argv[i + 1], config.display_width)) {
        std::cerr << "Error: Invalid width value\n";
        print_usage(argv[0]);
        std::exit(1);
      }
      ++i;
    } else if (arg == "-H" || arg == "--height") {
      if (i + 1 >= argc || !parse_num(argv[i + 1], config.display_height)) {
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
    } else if (arg == "--ppm") {
      if (i + 1 >= argc || !parse_num(argv[i + 1], config.ppm_correction)) {
        std::cerr << "Error: Invalid ppm value\n";
        print_usage(argv[0]);
        std::exit(1);
      }
      ++i;
    } else if (arg == "--bias-t") {
      config.bias_tee = true;
    } else if (arg == "--direct-sampling") {
      config.direct_sampling = true;
    } else if (arg == "--iq-log") {
      config.iq_logging_enabled = true;
    } else if (arg == "--iq-duration") {
      if (i + 1 >= argc ||
          !parse_num(argv[i + 1], config.iq_capture_duration)) {
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
    } else if (arg == "--play") {
      if (i + 1 >= argc) {
        std::cerr << "Error: IQ playback filename required\n";
        print_usage(argv[0]);
        std::exit(1);
      }
      config.play_file = argv[++i];
    } else if (arg == "--help" || arg == "-h") {
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
