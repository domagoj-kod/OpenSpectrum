// SPDX-License-Identifier: MIT

#include "fft/fft_analyzer.h"
#include "gui/sdl_renderer.h"
#include "hardware/rtl_sdr_device.h"
#include "signal/signal_processor.h"
#include "utils/logger.h"
#include "visualization/spectrum_display.h"
#include "visualization/waterfall_display.h"

#include <atomic>
#include <cmath>
#include <csignal>
#include <iostream>

// Security: Use hardened compiler flags (defined in Makefile)
// -fstack-protector-strong, -D_FORTIFY_SOURCE=2, -O2, -Wall, -Wextra

using namespace openspectrum;

static std::atomic<bool> g_running{true};

static void signal_handler(int signum) {
  if (signum == SIGINT || signum == SIGTERM) {
    LOG_INFO("Shutdown signal received, stopping gracefully...");
    g_running = false;
  }
}

int main() {
  // Initialize logging
  Logger::get_instance().add_sink(std::make_unique<ConsoleSink>());
  // Logger::get_instance().add_sink(std::make_unique<FileSink>("spectrum.log"));
  Logger::get_instance().set_level(LogLevel::INFO);

  LOG_INFO("Starting SDR Spectrum Analyzer");

  // Register signal handlers
  std::signal(SIGINT, signal_handler);
  std::signal(SIGTERM, signal_handler);

  // Configuration
  constexpr size_t FFT_SIZE = 4096;
  constexpr size_t DISPLAY_WIDTH = 800;
  constexpr size_t DISPLAY_HEIGHT = 480;
  constexpr size_t WATERFALL_LINES = DISPLAY_HEIGHT / 2;

  try {
    // 1. Initialize SDL2 renderer FIRST (before hardware)
    SdlRenderer renderer(DISPLAY_WIDTH, DISPLAY_HEIGHT, "OpenSpectrum SDR");
    if (!renderer.is_valid()) {
      LOG_ERROR("Failed to initialize SDL2 renderer");
      return 1;
    }
    LOG_INFO("SDL2 renderer initialized");

    // 2. Initialize hardware
    LOG_INFO("Initializing RTL-SDR device...");
    RtlSdrDevice dev;
    if (!dev.open()) {
      LOG_ERROR("Failed to open RTL-SDR device");
      return 1;
    }

    dev.set_sample_rate(2048000); // 2.048 MS/s
    dev.set_frequency(92600000);  // 92.6 MHz
    dev.set_gain(10.0f);          // ~10 dB

    // CRITICAL: flush USB buffer before first read
    dev.reset_buffer();
    LOG_INFO("RTL-SDR initialized: freq=100MHz, rate=2.048MS/s, gain=29dB");

    // 3. Initialize signal processor
    SignalProcessor signal_processor(FFT_SIZE);
    signal_processor.set_window(WindowFunction::BLACKMAN_HARRIS);

    // 4. Initialize FFT analyzer
    FftAnalyzer fft_analyzer(FFT_SIZE);
    fft_analyzer.enable_dc_center(true);
    fft_analyzer.set_window_coherent_gain(
        SignalProcessor::get_coherent_gain(WindowFunction::BLACKMAN_HARRIS));

    // 5. Initialize displays (split vertically: spectrum on top, waterfall
    // below)
    SpectrumDisplay spectrum_display(DISPLAY_WIDTH, DISPLAY_HEIGHT / 2);
    WaterfallDisplay waterfall_display(DISPLAY_WIDTH, DISPLAY_HEIGHT / 2,
                                       WATERFALL_LINES);

    spectrum_display.set_db_range(-120.0f, 0.0f);
    waterfall_display.set_db_range(-120.0f, 0.0f);

    // Create combined display buffer (spectrum on top, waterfall on bottom)
    std::vector<uint8_t> combined_pixels(DISPLAY_WIDTH * DISPLAY_HEIGHT * 4, 0);

    LOG_INFO("Display initialized: spectrum and waterfall");

    // Main processing loop
    LOG_INFO("Starting main loop. Press ESC or Ctrl+C to stop.");

    std::vector<std::complex<float>> samples(FFT_SIZE);
    std::vector<std::complex<float>> fft_output(FFT_SIZE);

    while (g_running.load(std::memory_order_relaxed)) {
      // === 1. Process SDL2 events (must be first in loop) ===
      if (!renderer.poll_events()) {
        g_running = false;
        break;
      }

      // === 2. Read samples from hardware ===
      try {
        samples = dev.read_samples(FFT_SIZE);
      } catch (const std::exception &e) {
        LOG_ERROR("Read error: " + std::string(e.what()));
        break;
      }

      // === 3. Signal processing ===
      signal_processor.remove_dc(samples);
      signal_processor.apply_window(samples);

      // === 4. FFT execution ===
      fft_analyzer.execute(samples, fft_output);

      // === 5. Get spectral data ===
      const auto &db_spectrum = fft_analyzer.get_db_spectrum();
      const auto &freq_bins = fft_analyzer.get_frequency_bins();

      // === 6. Update displays ===
      spectrum_display.update_spectrum(
          db_spectrum, freq_bins,
          dev.is_open() ? static_cast<float>(100000000) : 0.0f, 2048000.0f);

      waterfall_display.add_spectrum_line(db_spectrum);

      // === 7. Combine display buffers ===
      const auto &spec_pixels = spectrum_display.get_pixels();
      const auto &wf_pixels = waterfall_display.get_pixels();

      // Copy spectrum to top half
      size_t spec_size = spec_pixels.size();
      size_t wf_size = wf_pixels.size();
      size_t half_size = DISPLAY_WIDTH * (DISPLAY_HEIGHT / 2) * 4;

      std::copy_n(spec_pixels.data(), std::min(spec_size, half_size),
                  combined_pixels.data());

      // Copy waterfall to bottom half
      std::copy_n(wf_pixels.data(), std::min(wf_size, half_size),
                  combined_pixels.data() + half_size);

      // === 8. Render to window ===
      if (!renderer.render(combined_pixels)) {
        LOG_ERROR("Render failed");
        break;
      }
    }

    LOG_INFO("Main loop exited cleanly");
  } catch (const std::exception &e) {
    LOG_ERROR("Fatal error: " + std::string(e.what()));
    return 1;
  }

  LOG_INFO("Shutdown complete");
  return 0;
}
