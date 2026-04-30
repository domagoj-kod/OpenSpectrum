// SPDX-License-Identifier: MIT

#include "hardware/rtl_sdr_device.h"
#include "signal/signal_processor.h"
#include "fft/fft_analyzer.h"
#include "visualization/spectrum_display.h"
#include "visualization/waterfall_display.h"
#include "utils/logger.h"

#include <csignal>
#include <atomic>
#include <iostream>
#include <cmath>

// Security: Use hardened compiler flags (defined in Makefile)
// -fstack-protector-strong, -D_FORTIFY_SOURCE=2, -O2, -Wall, -Wextra

using namespace openspectrum;

static std::atomic<bool> g_running{true};

static void signal_handler(int signum)
{
    if (signum == SIGINT || signum == SIGTERM)
    {
        LOG_INFO("Shutdown signal received, stopping gracefully...");
        g_running = false;
    }
}

int main()
{
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
    constexpr size_t WATERFALL_LINES = 256;

    try
    {
        // 1. Initialize hardware
        LOG_INFO("Initializing RTL-SDR device...");
        RtlSdrDevice dev;
        if (!dev.open())
        {
            LOG_ERROR("Failed to open RTL-SDR device");
            return 1;
        }

        dev.set_sample_rate(2048000); // 2.048 MS/s
        dev.set_frequency(100000000); // 100 MHz
        dev.set_gain(29.0f);          // ~29 dB

        // CRITICAL: flush USB buffer before first read
        dev.reset_buffer();
        LOG_INFO("RTL-SDR initialized: freq=100MHz, rate=2.048MS/s, gain=29dB");

        // 2. Initialize signal processor
        SignalProcessor signal_processor(FFT_SIZE);
        signal_processor.set_window(WindowFunction::BLACKMAN_HARRIS);

        // 3. Initialize FFT analyzer
        FftAnalyzer fft_analyzer(FFT_SIZE);
        fft_analyzer.enable_dc_center(true);

        // 4. Initialize displays
        SpectrumDisplay spectrum_display(DISPLAY_WIDTH, DISPLAY_HEIGHT);
        WaterfallDisplay waterfall_display(DISPLAY_WIDTH, DISPLAY_HEIGHT, WATERFALL_LINES);

        spectrum_display.set_db_range(-120.0f, 0.0f);
        waterfall_display.set_db_range(-120.0f, 0.0f);

        LOG_INFO("Display initialized: spectrum and waterfall");

        // Main processing loop
        LOG_INFO("Starting main loop. Press Ctrl+C to stop.");
        std::cout << "\n";

        size_t frame_count = 0;
        const size_t stats_interval = 10;

        std::vector<std::complex<float>> samples(FFT_SIZE);
        std::vector<std::complex<float>> fft_output(FFT_SIZE);

        while (g_running.load(std::memory_order_relaxed))
        {
            // Read samples (blocking)
            try
            {
                samples = dev.read_samples(FFT_SIZE);
            }
            catch (const std::exception &e)
            {
                LOG_ERROR("Read error: " + std::string(e.what()));
                break;
            }

            // Pre-processing
            signal_processor.remove_dc(samples);
            signal_processor.apply_window(samples);

            // FFT execution
            fft_analyzer.execute(samples, fft_output);

            // Get spectral data
            const auto &db_spectrum = fft_analyzer.get_db_spectrum();
            const auto &freq_bins = fft_analyzer.get_frequency_bins();

            // Update displays
            spectrum_display.update_spectrum(
                db_spectrum, freq_bins,
                dev.is_open() ? static_cast<float>(100000000) : 0.0f,
                2048000.0f);

            waterfall_display.add_spectrum_line(db_spectrum);

            // Periodic statistics
            frame_count++;
            if (frame_count % stats_interval == 0)
            {
                // Could save frames, compute FPS, etc.
                // For now, just show the heartbeat
                std::cout << "<3\n"
                          << std::flush;
            }
        }

        LOG_INFO("Main loop exited cleanly");
    }
    catch (const std::exception &e)
    {
        LOG_ERROR("Fatal error: " + std::string(e.what()));
        return 1;
    }

    LOG_INFO("Shutdown complete");
    return 0;
}