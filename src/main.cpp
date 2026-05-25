// SPDX-License-Identifier: MIT

#include "fft/fft_analyzer.h"
#include "gui/sdl_renderer.h"
#include "hardware/rtl_sdr_device.h"
#include "openspectrum/control_state.h"
#include "openspectrum/iq_logger.h"
#include "openspectrum/spectrogram_exporter.h"
#include "signal/signal_processor.h"
#include "utils/arg_parser.h"
#include "utils/logger.h"
#include "visualization/spectrum_display.h"
#include "visualization/waterfall_display.h"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <complex>
#include <csignal>
#include <cstddef>
#include <cstdint>
#include <exception>
#include <memory>
#include <string>
#include <vector>

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

auto main(int argc, char *argv[]) -> int {
  // Parse command-line arguments
  AppConfig const config = parse_arguments(argc, argv);

  if (config.show_help) {
    print_usage(argv[0]);
    return 0;
  }

  // Validate FFT size is a power of two
  if (!is_power_of_two(config.fft_size)) {
    LOG_ERROR("FFT size must be a power of two (e.g., 1024, 2048, 4096, 8192)");
    return 1;
  }

  // Initialize logging
  Logger::get_instance().add_sink(std::make_unique<ConsoleSink>());
  // Logger::get_instance().add_sink(std::make_unique<FileSink>("spectrum.log"));
  Logger::get_instance().set_level(LogLevel::INFO);

  LOG_INFO("Starting SDR Spectrum Analyzer");

  // Register signal handlers
  std::signal(SIGINT, signal_handler);
  std::signal(SIGTERM, signal_handler);

  // Log configuration
  LOGS_INFO << "Configuration: freq=" << static_cast<int>(config.center_freq_hz)
            << "Hz, rate=" << static_cast<int>(config.sample_rate_hz)
            << "Hz, gain=" << config.gain_db
            << "dB, fft_size=" << config.fft_size << ", window="
            << SignalProcessor::window_function_to_string(
                   config.window_function);

  // Configuration
  const size_t FFT_SIZE = config.fft_size;
  const size_t DISPLAY_WIDTH = config.display_width;
  const size_t DISPLAY_HEIGHT = config.display_height;
  const size_t WATERFALL_LINES = DISPLAY_HEIGHT / 2;

  try {
    // 1. Initialize SDL2 renderer FIRST (before hardware)
    // VSYNC disabled by default for performance (can cause tearing but much faster)
    SdlRenderer renderer(DISPLAY_WIDTH, DISPLAY_HEIGHT, "OpenSpectrum SDR", false);
    if (!renderer.is_valid()) {
      LOG_ERROR("Failed to initialize SDL2 renderer");
      return 1;
    }
    LOG_INFO("SDL2 renderer initialized");

    // 1.5 Initialize runtime controls
    ControlState control_state;
    control_state.set_frequency(static_cast<uint32_t>(config.center_freq_hz));
    control_state.set_gain(config.gain_db);
    control_state.set_fft_size(config.fft_size);
    control_state.set_window(config.window_function);
    LOG_INFO("Runtime controls initialized");

    // 1.6 Initialize IQ logger (if enabled)
    IqLoggerConfig iq_logger_config;
    if (!config.iq_output_file.empty()) {
      iq_logger_config.filename_prefix = config.iq_output_file;
    }
    IqLogger iq_logger(iq_logger_config);
    bool iq_capturing = false;
    double iq_capture_start = 0.0;

    // Set up callback for when capture completes
    iq_logger.set_complete_callback([](const std::string &filename,
                                       const std::string &meta_filename) {
      LOG_INFO("IQ capture complete: " + filename + " (" + meta_filename + ")");
    });

    // Start IQ capture if enabled via command line
    if (config.iq_logging_enabled) {
      iq_logger.start_capture(
          static_cast<uint32_t>(config.center_freq_hz),
          static_cast<uint32_t>(config.sample_rate_hz), config.gain_db,
          config.fft_size,
          SignalProcessor::window_function_to_string(config.window_function),
          "Command-line capture");
      iq_capturing = true;
      iq_capture_start =
          std::chrono::duration<double>(
              std::chrono::system_clock::now().time_since_epoch())
              .count();
      LOG_INFO("IQ logging enabled: " + iq_logger.get_data_filename());
    }

    // 1.7 Initialize spectrogram exporter
    SpectrogramExportConfig exp_config;
    exp_config.output_directory = "spectrograms";
    exp_config.filename_prefix = "spectrogram";
    exp_config.include_metadata = true;
    exp_config.png_compression_level = 8;
    SpectrogramExporter spectrogram_exporter(exp_config);
    LOG_INFO("Spectrogram exporter initialized");

    // 2. Initialize hardware
    LOG_INFO("Initializing RTL-SDR device...");
    RtlSdrDevice dev;
    if (!dev.open()) {
      LOG_ERROR("Failed to open RTL-SDR device");
      return 1;
    }

    dev.set_sample_rate(static_cast<uint32_t>(config.sample_rate_hz));
    dev.set_frequency(control_state.get_frequency());
    dev.set_gain(control_state.get_gain());

    // CRITICAL: flush USB buffer before first read
    dev.reset_buffer();
    LOGS_INFO << "RTL-SDR initialized: freq="
              << static_cast<int>(config.center_freq_hz)
              << "Hz, rate=" << static_cast<int>(config.sample_rate_hz)
              << "Hz, gain=" << config.gain_db << "dB";

    // 3. Initialize signal processor
    SignalProcessor signal_processor(FFT_SIZE);
    signal_processor.set_window(config.window_function);

    // 4. Initialize FFT analyzer
    FftAnalyzer fft_analyzer(FFT_SIZE);
    fft_analyzer.enable_dc_center(true);
    fft_analyzer.set_window_coherent_gain(
        SignalProcessor::get_coherent_gain(config.window_function));

    // 5. Initialize displays (split vertically: spectrum on top, waterfall
    // below)
    SpectrumDisplay spectrum_display(DISPLAY_WIDTH, DISPLAY_HEIGHT / 2);
    WaterfallDisplay waterfall_display(DISPLAY_WIDTH, DISPLAY_HEIGHT / 2,
                                       WATERFALL_LINES);

    spectrum_display.set_db_range(-120.0F, 0.0F);
    waterfall_display.set_db_range(-120.0F, 0.0F);

    // Create combined display buffer (spectrum on top, waterfall on bottom)
    std::vector<uint8_t> combined_pixels(DISPLAY_WIDTH * DISPLAY_HEIGHT * 4, 0);

    LOG_INFO("Display initialized: spectrum and waterfall");

    // Main processing loop
    LOG_INFO("Starting main loop. Press ESC or Ctrl+C to stop.");
    LOG_INFO("Controls: +/- Frequency, r/f Gain, 1-4 FFT size, UP/DOWN Window, "
             "Ctrl+S Toggle IQ logging, e Export spectrogram, Shift/Ctrl for "
             "fine/coarse");

    std::vector<std::complex<float>> samples;
    std::vector<std::complex<float>> fft_output;
    size_t current_fft_size = config.fft_size;

    // Initialize buffers with current FFT size
    samples.resize(current_fft_size);
    fft_output.resize(current_fft_size);

    // Track peak amplitude for display
    float peak_db = -140.0F;

    while (g_running.load(std::memory_order_relaxed)) {
      // === 1. Process SDL2 events (must be first in loop) ===
      if (!renderer.poll_events(&control_state)) {
        g_running = false;
        break;
      }

      // === 1.1. Check for window function change ===
      if (control_state.window_changed()) {
        signal_processor.set_window(control_state.get_window());
        fft_analyzer.set_window_coherent_gain(
            SignalProcessor::get_coherent_gain(control_state.get_window()));
        control_state.clear_window_change_flag();
        LOG_INFO("Window function changed to: " +
                 std::string(SignalProcessor::window_function_to_string(
                     control_state.get_window())));
      }

      // === 1.2. Check for FFT size change and reinitialize if needed ===
      if (control_state.fft_size_changed()) {
        size_t const new_fft_size = control_state.get_fft_size();
        control_state.set_reconfiguring(true);
        control_state.clear_fft_change_flag();

        LOG_INFO("Reinitializing with FFT size: " +
                 std::to_string(new_fft_size));

        // Reinitialize FFT-dependent components
        current_fft_size = new_fft_size;
        samples.resize(current_fft_size);
        fft_output.resize(current_fft_size);

        // Recreate signal processor with new size
        signal_processor = SignalProcessor(current_fft_size);
        signal_processor.set_window(control_state.get_window());

        // Recreate FFT analyzer with new size (using move semantics)
        fft_analyzer = FftAnalyzer(current_fft_size);
        fft_analyzer.enable_dc_center(true);
        fft_analyzer.set_window_coherent_gain(
            SignalProcessor::get_coherent_gain(control_state.get_window()));

        // Clear waterfall to avoid size mismatch
        waterfall_display.reset();

        control_state.set_reconfiguring(false);
      }

      // === 1.2.5. Check for IQ logging toggle request ===
      if (control_state.iq_logging_toggle_requested()) {
        control_state.clear_iq_logging_toggle();
        if (iq_capturing) {
          iq_logger.stop_capture();
          iq_capturing = false;
          LOG_INFO("IQ logging stopped");
        } else {
          iq_logger.start_capture(static_cast<uint32_t>(config.center_freq_hz),
                                  static_cast<uint32_t>(config.sample_rate_hz),
                                  config.gain_db, current_fft_size,
                                  SignalProcessor::window_function_to_string(
                                      control_state.get_window()),
                                  "Manual capture");
          iq_capturing = true;
          iq_capture_start =
              std::chrono::duration<double>(
                  std::chrono::system_clock::now().time_since_epoch())
                  .count();
          LOG_INFO("IQ logging started: " + iq_logger.get_data_filename());
        }
      }

      // === 1.2.7. Check for spectrogram export request ===
      if (control_state.spectrogram_export_requested()) {
        control_state.clear_spectrogram_export();

        // Get current color map name from spectrum display
        std::string color_map_name = "jet"; // Default
        // Note: SpectrumDisplay doesn't expose color map getter,
        // so we use default. Could be enhanced later.

        // Export combined spectrogram (spectrum + waterfall)
        auto result = spectrogram_exporter.export_combined(
            spectrum_display.get_pixels(), waterfall_display.get_pixels(),
            DISPLAY_WIDTH, spectrum_display.height(),
            waterfall_display.height(), control_state.get_frequency(),
            static_cast<uint32_t>(config.sample_rate_hz),
            control_state.get_gain(), current_fft_size,
            SignalProcessor::window_function_to_string(
                control_state.get_window()),
            color_map_name, "");

        if (result.success) {
          LOG_INFO("Spectrogram exported: " + result.filename);
          if (!result.metadata_filename.empty()) {
            LOG_INFO("Metadata written: " + result.metadata_filename);
          }
          // Show brief status message
          renderer.render_status_bar("Exported: " + result.filename);
        } else {
          LOG_ERROR("Spectrogram export failed: " + result.error_message);
          renderer.render_status_bar("Export failed!");
        }
      }

      // === 1.3. Apply control state changes to device (batch update) ===
      control_state.apply_to_device(dev);

      // Update status bar (without PEAK - now shown separately)
      if (control_state.status_changed()) {
        renderer.render_status_bar(control_state.get_status_string());
        control_state.clear_status_dirty();
      }

      // Update IQ logging status indicator periodically
      static size_t frame_count = 0;
      if (++frame_count % 60 == 0) { // Update every ~60 frames (~1 second)
        if (iq_capturing) {
          auto stats = iq_logger.get_stats();
          std::string const iq_status =
              "LOGGING: " +
              std::to_string(static_cast<size_t>(stats.duration_seconds)) +
              "s (" + std::to_string(stats.sample_count) + " samples)";
          renderer.render_iq_status(iq_status);
        } else {
          // Clear IQ status when not capturing
          renderer.render_iq_status("");
        }
      }

      // Update peak indicator in top-right corner (updates every frame)
      if (peak_db > -140.0F) {
        renderer.render_peak_indicator(peak_db);
      }

      // === 2. Read samples from hardware ===
      try {
        samples = dev.read_samples(current_fft_size);
      } catch (const std::exception &e) {
        LOG_ERROR("Read error: " + std::string(e.what()));
        break;
      }

      // === 2.5. IQ logging (if enabled) ===
      if (iq_capturing) {
        iq_logger.write_samples(samples);
      }

      // === 3. Signal processing ===
      openspectrum::SignalProcessor::remove_dc(samples);
      signal_processor.apply_window(samples);

      // === 4. FFT execution ===
      fft_analyzer.execute(samples, fft_output);

      // === 4.1. Get peak amplitude for status display ===
      peak_db = fft_analyzer.get_max_db();

      // === 5. Get spectral data ===
      const auto &db_spectrum = fft_analyzer.get_db_spectrum();
      const auto &freq_bins = fft_analyzer.get_frequency_bins();

      // === 6. Update displays ===
      spectrum_display.update_spectrum(
          db_spectrum, freq_bins,
          static_cast<float>(control_state.get_frequency()),
          config.sample_rate_hz);

      waterfall_display.add_spectrum_line(db_spectrum);

      // === 7. Combine display buffers ===
      const auto &spec_pixels = spectrum_display.get_pixels();
      const auto &wf_pixels = waterfall_display.get_pixels();

      // Copy spectrum to top half
      size_t const spec_size = spec_pixels.size();
      size_t const wf_size = wf_pixels.size();
      size_t const half_size = DISPLAY_WIDTH * (DISPLAY_HEIGHT / 2) * 4;

      std::copy_n(spec_pixels.data(), std::min(spec_size, half_size),
                  combined_pixels.data());

      // Copy waterfall to bottom half
      std::copy_n(wf_pixels.data(), std::min(wf_size, half_size),
                  combined_pixels.data() + half_size);

      // === 8. Render to window ===
      // Collect dirty rectangles from both displays
      const auto &spec_dirty_rects = spectrum_display.get_dirty_rects();
      const auto &wf_dirty_rects = waterfall_display.get_dirty_rects();

      std::vector<SDL_Rect> all_dirty_rects;
      all_dirty_rects.reserve(spec_dirty_rects.size() + wf_dirty_rects.size());

      // Spectrum is in top half - no offset needed
      for (const auto &rect : spec_dirty_rects) {
        all_dirty_rects.push_back(rect);
      }

      // Waterfall is in bottom half - offset y by half height
      size_t wf_y_offset = DISPLAY_HEIGHT / 2;
      for (const auto &rect : wf_dirty_rects) {
        SDL_Rect offset_rect = rect;
        offset_rect.y += static_cast<int>(wf_y_offset);
        all_dirty_rects.push_back(offset_rect);
      }

      // Use incremental rendering if there are dirty rects, otherwise full render
      bool render_ok;
      if (!all_dirty_rects.empty()) {
        render_ok = renderer.render_with_dirty_regions(
            combined_pixels, 0, all_dirty_rects);
        // Clear dirty flags after rendering
        spectrum_display.clear_dirty_rects();
        waterfall_display.clear_dirty_rects();
      } else {
        render_ok = renderer.render(combined_pixels);
      }

      if (!render_ok) {
        LOG_ERROR("Render failed");
        break;
      }

      // === 9. Check for IQ capture duration expiry ===
      if (iq_capturing && config.iq_capture_duration > 0.0) {
        double const current_time =
            std::chrono::duration<double>(
                std::chrono::system_clock::now().time_since_epoch())
                .count();
        if ((current_time - iq_capture_start) >= config.iq_capture_duration) {
          iq_logger.stop_capture();
          iq_capturing = false;
          LOG_INFO("IQ capture stopped after duration: " +
                   std::to_string(config.iq_capture_duration) + " seconds");
        }
      }
    }

    // Stop IQ capture if still running
    if (iq_capturing) {
      iq_logger.stop_capture();
    }

    LOG_INFO("Main loop exited cleanly");
  } catch (const std::exception &e) {
    LOG_ERROR("Fatal error: " + std::string(e.what()));
    return 1;
  }

  LOG_INFO("Shutdown complete");
  return 0;
}
