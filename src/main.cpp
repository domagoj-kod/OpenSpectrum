// SPDX-License-Identifier: GPL-3.0-or-later

#include "fft/fft_analyzer.h"
#include "gui/sdl_renderer.h"
#include "hardware/rtl_sdr_device.h"
#include "openspectrum/control_state.h"
#include "openspectrum/frame_pool.h"
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
#include <condition_variable>
#include <csignal>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <exception>
#include <memory>
#include <mutex>
#include <queue>
#include <string>
#include <vector>

#if defined(__SSE__) || defined(_M_X64) || defined(_M_IX86_FP)
#include <pmmintrin.h> // _MM_SET_DENORMALS_ZERO_MODE
#include <xmmintrin.h> // _MM_SET_FLUSH_ZERO_MODE
#endif

// WinUSB has no DMA buffer limit; Linux usbfs defaults to 16 MB which
// is exhausted at 128 KB/buf × 64 = 8 MB when other USB overhead is counted.
// 32 buffers (4 MB) is safe on both platforms.
#ifdef _WIN32
#define STREAM_BUFF 64
#else
#define STREAM_BUFF 32
#endif

// Security: Use hardened compiler flags (defined in Makefile)
// -fstack-protector-strong, -D_FORTIFY_SOURCE=2, -O2, -Wall, -Wextra

using namespace openspectrum;

static std::atomic<bool> g_running{true};

// Async sample processing - thread-safe queue for async RTL-SDR callbacks
// OLD: static std::queue<std::vector<std::complex<float>>> g_sample_queue;
// NEW: Use FrameHandle queue for zero-allocation
static std::queue<FrameHandle> g_sample_queue;
static std::mutex g_sample_mutex;
static std::condition_variable g_sample_cv;
static size_t g_async_fft_size = 0;

// Accumulator for samples that don't match FFT size exactly
// OLD: static std::vector<std::complex<float>> g_sample_accumulator;
// NEW: Use FrameHandle for accumulator
static FrameHandle g_sample_accumulator_frame;
static std::mutex g_accumulator_mutex;

// Frame pool for sample buffers (shared across callbacks)
// shared_ptr (not unique_ptr) so FrameHandle's return path can hold a
// weak_ptr<FramePool> and safely no-op when the pool has been destroyed.
static std::shared_ptr<FramePool> g_frame_pool;

// Memory leak fix: Maximum queue size to prevent unbounded growth
// At 8ms timeout and typical sample rates, 32 buffers is ~256ms of data
// Each buffer at FFT 4096 = 4096 * 8 bytes = 32KB
// 32 * 32KB = 1MB max queue memory (was growing to 1GB+)
static const size_t MAX_SAMPLE_QUEUE_SIZE = 64;

// FTZ + DAZ on the calling thread. Eliminates the ~100-cycle microcode trap
// every time a denormal float is produced or consumed — common in dB spectra
// near the noise floor. Per-thread MXCSR, so call from each thread.
static inline void enable_ftz_daz() noexcept {
#if defined(__SSE__) || defined(_M_X64) || defined(_M_IX86_FP)
  _MM_SET_FLUSH_ZERO_MODE(_MM_FLUSH_ZERO_ON);
  _MM_SET_DENORMALS_ZERO_MODE(_MM_DENORMALS_ZERO_ON);
#endif
}

static void signal_handler(int signum) {
  // Only async-signal-safe operations are permitted here.
  // Setting a lock-free atomic is safe; LOG, condition_variable::notify_all,
  // and stop_streaming (which joins a thread) are not.
  // The main loop checks g_running every 8 ms (CV wait_for timeout) and
  // performs orderly shutdown — including dev.stop_streaming() — on exit.
  if (signum == SIGINT || signum == SIGTERM) {
    g_running = false;
  }
}

// Async callback for RTL-SDR samples using FrameHandle
// Accumulates samples until we have exactly g_async_fft_size
static void async_sample_callback(FrameHandle samples_frame) {
  // librtlsdr owns this thread; set FTZ/DAZ once per worker on first entry.
  thread_local bool ftz_initialized = false;
  if (!ftz_initialized) {
    enable_ftz_daz();
    ftz_initialized = true;
  }

  if (g_async_fft_size == 0 || !samples_frame) {
    // FFT size not set yet, skip
    return;
  }

  std::unique_lock<std::mutex> acc_lock(g_accumulator_mutex);

  // If we don't have an accumulator frame, get one from pool
  if (!g_sample_accumulator_frame) {
    if (g_frame_pool) {
      g_sample_accumulator_frame = g_frame_pool->acquire();
    }
    if (!g_sample_accumulator_frame) {
      // Pool not ready, skip
      return;
    }
    g_sample_accumulator_frame.reset();
  }

  // Add new samples to accumulator
  size_t const samples_count = samples_frame.size();
  size_t const current_accum_size = g_sample_accumulator_frame.size();
  size_t const new_total = current_accum_size + samples_count;

  // Ensure accumulator has enough capacity
  if (new_total > g_sample_accumulator_frame.capacity()) {
    // Need to reallocate - get a larger frame
    FrameHandle new_accum =
        g_frame_pool ? g_frame_pool->acquire() : FrameHandle(nullptr);
    if (!new_accum) {
      return; // Pool exhausted
    }
    new_accum.resize(std::max(new_total, g_async_fft_size));

    // Copy existing data to new accumulator
    if (current_accum_size > 0) {
      std::copy_n(g_sample_accumulator_frame.data(), current_accum_size,
                  new_accum.data());
    }

    // Add new samples
    std::copy_n(samples_frame.data(), samples_count,
                new_accum.data() + current_accum_size);
    new_accum.resize(current_accum_size + samples_count);

    // Replace old accumulator (old will be returned to pool)
    g_sample_accumulator_frame = std::move(new_accum);
  } else {
    // Copy samples to accumulator
    std::copy_n(samples_frame.data(), samples_count,
                g_sample_accumulator_frame.data() + current_accum_size);
    g_sample_accumulator_frame.resize(new_total);
  }

  // Log first few callbacks to verify samples are arriving
  static size_t callback_count = 0;
  if (callback_count++ < 5) {
    LOG_INFO("Async callback: received " + std::to_string(samples_count) +
             " samples, total accumulated: " +
             std::to_string(g_sample_accumulator_frame.size()));
  }

  // Process complete FFT chunks
  while (g_sample_accumulator_frame.size() >= g_async_fft_size) {
    // Get a frame for the FFT samples
    FrameHandle fft_samples_frame =
        g_frame_pool ? g_frame_pool->acquire() : FrameHandle(nullptr);
    if (!fft_samples_frame) {
      break; // Pool exhausted
    }

    fft_samples_frame.resize(g_async_fft_size);

    // Copy FFT-sized chunk from accumulator
    std::copy_n(g_sample_accumulator_frame.data(), g_async_fft_size,
                fft_samples_frame.data());

    // Remove processed samples from accumulator
    size_t const remaining =
        g_sample_accumulator_frame.size() - g_async_fft_size;
    if (remaining > 0) {
      // Shift remaining samples to front
      std::copy_n(g_sample_accumulator_frame.data() + g_async_fft_size,
                  remaining, g_sample_accumulator_frame.data());
    }
    g_sample_accumulator_frame.resize(remaining);

    // Unlock accumulator, queue the sample, then re-lock
    acc_lock.unlock();
    {
      std::lock_guard<std::mutex> queue_lock(g_sample_mutex);
      // Memory leak fix: Drop oldest sample if queue is full
      if (g_sample_queue.size() >= MAX_SAMPLE_QUEUE_SIZE) {
        g_sample_queue.pop(); // Drop oldest to prevent unbounded growth
        LOG_DEBUG("Sample queue full, dropping oldest sample");
      }
      g_sample_queue.push(std::move(fft_samples_frame));
    }
    g_sample_cv.notify_one();
    acc_lock.lock();
  }

  // If we have too many accumulated samples, trim to avoid memory issues
  if (g_sample_accumulator_frame.size() > g_async_fft_size * 2) {
    size_t const trim_to = g_sample_accumulator_frame.size() - g_async_fft_size;
    if (trim_to > 0) {
      std::copy_n(g_sample_accumulator_frame.data() + g_async_fft_size, trim_to,
                  g_sample_accumulator_frame.data());
    }
    g_sample_accumulator_frame.resize(trim_to);
  }
}

auto main(int argc, char *argv[]) -> int {
  enable_ftz_daz();

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
    // VSYNC on: the render loop now does only ~3-6 ms of work per frame and is
    // sample-delivery bound (~62 lines/s), so there is ample headroom to cap at
    // the display refresh. Without vsync, presenting ~62 fps into a 60 Hz panel
    // tears and beats at ~2 Hz — visible as a periodic waterfall "blink" and a
    // doubled spectrum peak while tuning. (Vsync appeared to do nothing in
    // earlier tests only because we were then stuck below 60 fps.)
    SdlRenderer renderer(DISPLAY_WIDTH, DISPLAY_HEIGHT, "OpenSpectrum SDR",
                         true);
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
    std::chrono::steady_clock::time_point iq_capture_start;

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
      iq_capture_start = std::chrono::steady_clock::now();
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

    // 2. Initialize frame pool for zero-allocation sample processing
    g_frame_pool = std::make_shared<FramePool>(FFT_SIZE, 32);
    LOG_INFO("FramePool initialized for FFT size: " + std::to_string(FFT_SIZE));

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
    dev.set_fft_size(FFT_SIZE); // Set FFT size for frame pool

    // CRITICAL: flush USB buffer before starting
    dev.reset_buffer();

    // Set up async callback and start streaming
    // Note: FFT_SIZE is used here, will be updated if FFT size changes
    // dynamically
    g_async_fft_size = FFT_SIZE;

    // Use frame-based callback for zero-allocation
    // The callback receives FrameHandle which automatically returns to pool
    auto frame_callback = [](FrameHandle samples_frame) {
      async_sample_callback(std::move(samples_frame));
    };
    dev.set_frame_callback(frame_callback);
    dev.start_streaming(STREAM_BUFF);
    LOG_INFO("RTL-SDR async streaming started with " +
             std::to_string(STREAM_BUFF) +
             " buffers, FFT size: " + std::to_string(FFT_SIZE));

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

    LOG_INFO("Display initialized: spectrum and waterfall");

    // Main processing loop
    LOG_INFO("Starting main loop. Press ESC or Ctrl+C to stop.");
    LOG_INFO("Controls: +/- Frequency, r/f Gain, 1-5 FFT size, UP/DOWN Window, "
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

    // === Frame-timing instrumentation (debug branch) ===
    // Splits each rendered frame into three wall-clock phases and logs rolling
    // per-second averages + maxima, to localize the Linux-vs-Windows throughput
    // gap. Only full frames (got_samples) are measured, since those are what
    // advance the waterfall. Promote to a keyboard-toggled on-screen overlay
    // later if the numbers prove useful.
    //   cpu          : remove_dc + apply_window + FFT + display updates
    //   render_build : SDL draw calls + texture upload + render-target switches
    //   present      : SDL_RenderPresent (swap/flush)
    using timing_clock = std::chrono::steady_clock;
    struct PhaseStat {
      double sum_ms = 0.0;
      double max_ms = 0.0;
      void add(double ms) {
        sum_ms += ms;
        if (ms > max_ms) {
          max_ms = ms;
        }
      }
      void reset() {
        sum_ms = 0.0;
        max_ms = 0.0;
      }
    };
    PhaseStat ts_cpu;
    PhaseStat ts_render;
    PhaseStat ts_present;
    uint64_t timing_frames = 0;
    auto timing_window_start = timing_clock::now();
    auto timing_ms = [](timing_clock::time_point a, timing_clock::time_point b) {
      return std::chrono::duration<double, std::milli>(b - a).count();
    };
    // Latest per-second snapshot, surfaced to the on-screen overlay ('T' key).
    double ov_fps = 0.0;
    double ov_cpu = 0.0;
    double ov_build = 0.0;
    double ov_present = 0.0;

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

        // Stop async streaming while reconfiguring
        dev.stop_streaming();

        // Clear the sample queue and accumulator
        {
          std::lock_guard<std::mutex> lock(g_sample_mutex);
          while (!g_sample_queue.empty()) {
            // Explicitly release FrameHandle to return frame to old pool before
            // destruction, then pop it from the queue
            g_sample_queue.front() = FrameHandle(nullptr);
            g_sample_queue.pop();
          }
        }
        {
          // Explicitly release accumulator frame to return to old pool before
          // destruction
          std::lock_guard<std::mutex> lock(g_accumulator_mutex);
          g_sample_accumulator_frame = FrameHandle(nullptr);
        }

        // Now safe to destroy old pools and create new ones
        g_frame_pool = std::make_shared<FramePool>(current_fft_size, 32);
        dev.set_fft_size(current_fft_size);

        // Update async FFT size and restart streaming
        g_async_fft_size = current_fft_size;

        // Reuse the outer frame_callback lambda (identical body).
        dev.set_frame_callback(frame_callback);
        dev.start_streaming(STREAM_BUFF);

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
          iq_logger.start_capture(control_state.get_frequency(),
                                  static_cast<uint32_t>(config.sample_rate_hz),
                                  control_state.get_gain(), current_fft_size,
                                  SignalProcessor::window_function_to_string(
                                      control_state.get_window()),
                                  "Manual capture");
          iq_capturing = true;
          iq_capture_start = std::chrono::steady_clock::now();
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
      {
        static uint32_t s_last_freq = 0;
        uint32_t const now_freq = control_state.get_frequency();
        control_state.apply_to_device(dev);
        if (s_last_freq != 0 && s_last_freq != now_freq) {
          // Frequency tuned — discard buffered samples from the old channel.
          // Scale label updates immediately (render_frequency_scale sees new
          // freq); dropping the queue makes the spectrum snap in sync rather
          // than lagging behind by up to MAX_SAMPLE_QUEUE_SIZE × FFT frames.
          std::lock_guard<std::mutex> lock(g_sample_mutex);
          while (!g_sample_queue.empty()) {
            g_sample_queue.front() = FrameHandle(nullptr); // return to pool
            g_sample_queue.pop();
          }
        }
        s_last_freq = now_freq;
      }

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

      // Update frequency scale (caches rebuild only when center_hz or rate
      // changes)
      renderer.render_frequency_scale(
          control_state.get_frequency(),
          static_cast<uint32_t>(config.sample_rate_hz),
          spectrum_display.height());

      // Push the latest timing snapshot so the 'T' overlay is drawn (inside
      // render_overlays) on whichever present path runs this frame.
      renderer.set_timing_overlay(control_state.timing_overlay_enabled(), ov_fps,
                                  ov_cpu, ov_build, ov_present);

      // === 2. Read samples from async queue (non-blocking) ===
      // Wait for samples with timeout (8ms = ~125fps max, reduced from 16ms)
      FrameHandle async_samples_frame;
      bool got_samples = false;
      {
        std::unique_lock<std::mutex> lock(g_sample_mutex);
        if (g_sample_queue.empty()) {
          // Wait for samples with timeout
          if (g_sample_cv.wait_for(lock, std::chrono::milliseconds(8), [] {
                return !g_sample_queue.empty() || !g_running.load();
              })) {
            if (!g_sample_queue.empty()) {
              async_samples_frame = std::move(g_sample_queue.front());
              g_sample_queue.pop();
              got_samples = true;
            }
          }
        } else {
          async_samples_frame = std::move(g_sample_queue.front());
          g_sample_queue.pop();
          got_samples = true;
        }

        // Throttle: at high sample rates the RTL-SDR callback can produce
        // frames faster than the render loop consumes them. Drain any backlog
        // and keep only the newest — older frames represent latency the user
        // cannot perceive on a 60 Hz display. Each move-assignment releases
        // the previously held FrameHandle back to the pool.
        while (got_samples && !g_sample_queue.empty()) {
          async_samples_frame = std::move(g_sample_queue.front());
          g_sample_queue.pop();
        }
      }

      // === 2.1. Update displays and render even if no new samples ===
      // This fixes keyboard input freeze: overlays (status bar, peak, IQ
      // status) are rendered as part of the main render call, so we must render
      // even when no samples arrive, otherwise keyboard changes aren't visible
      if (!got_samples) {
        // No new pixel data — re-present the last texture with updated
        // overlays.
        renderer.present_frame();
        continue;
      }

      // async_samples_frame is now guaranteed to be exactly current_fft_size
      // (accumulated in callback to match FFT size)

      // Copy frame data to samples vector for FFT processing
      // This is a temporary copy - in future we can modify FFT to use
      // FrameHandle directly
      if (samples.size() < current_fft_size) {
        samples.resize(current_fft_size);
      }
      std::copy_n(async_samples_frame.data(), current_fft_size,
                  samples.begin());

      // async_samples_frame will be automatically returned to pool when it goes
      // out of scope

      // === 2.5. IQ logging (if enabled) ===
      if (iq_capturing) {
        // Convert to vector for IQ logger (it expects vector)
        std::vector<std::complex<float>> iq_samples(
            samples.begin(), samples.begin() + current_fft_size);
        iq_logger.write_samples(iq_samples);
      }

      // === 3. Signal processing ===
      auto t_cpu_start = timing_clock::now();
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
      auto t_cpu_end = timing_clock::now();

      // === 7. Render directly into SDL texture (no combined_pixels buffer) ===
      const auto &spec_dirty_rects = spectrum_display.get_dirty_rects();
      const auto &wf_dirty_rects = waterfall_display.get_dirty_rects();

      auto t_render_start = timing_clock::now();
      bool render_ok;
      if (!spec_dirty_rects.empty() || !wf_dirty_rects.empty()) {
        if (waterfall_display.needs_full_render()) {
          // First frame after reset or LUT rebuild: full CPU→GPU upload.
          // Also seeds the GPU scroll texture for subsequent scroll frames.
          render_ok = renderer.render_displays(
              spectrum_display.pixel_data(),
              waterfall_display.get_pixels().data(), spectrum_display.height(),
              spec_dirty_rects, wf_dirty_rects);
        } else {
          // Steady state: GPU shifts existing waterfall texture up by one line
          // and uploads only the new bottom strip (~5 KB instead of ~1.5 MB).
          render_ok = renderer.render_displays_scroll(
              spectrum_display.pixel_data(),
              waterfall_display.get_new_line_rgba(), spectrum_display.height(),
              waterfall_display.height(), waterfall_display.get_line_height(),
              spec_dirty_rects);
        }
        spectrum_display.clear_dirty_rects();
        waterfall_display.clear_dirty_rects();
      } else {
        renderer.present_frame();
        render_ok = true;
      }

      if (!render_ok) {
        LOG_ERROR("Render failed");
        break;
      }
      auto t_render_end = timing_clock::now();

      // === 8. Frame-timing accumulation + per-second report (debug branch) ===
      {
        double const present_ms = renderer.last_present_ms();
        double render_build_ms = timing_ms(t_render_start, t_render_end) - present_ms;
        if (render_build_ms < 0.0) {
          render_build_ms = 0.0; // clock skew guard
        }
        ts_cpu.add(timing_ms(t_cpu_start, t_cpu_end));
        ts_render.add(render_build_ms);
        ts_present.add(present_ms);
        ++timing_frames;

        double const window_s =
            std::chrono::duration<double>(timing_clock::now() - timing_window_start)
                .count();
        if (window_s >= 1.0 && timing_frames > 0) {
          double const inv = 1.0 / static_cast<double>(timing_frames);
          ov_fps = static_cast<double>(timing_frames) / window_s;
          ov_cpu = ts_cpu.sum_ms * inv;
          ov_build = ts_render.sum_ms * inv;
          ov_present = ts_present.sum_ms * inv;
          char line[256];
          std::snprintf(
              line, sizeof(line),
              "FRAME-TIMING fps=%.1f frames=%llu | cpu avg=%.2f max=%.2f | "
              "render_build avg=%.2f max=%.2f | present avg=%.2f max=%.2f (ms)",
              ov_fps, static_cast<unsigned long long>(timing_frames),
              ov_cpu, ts_cpu.max_ms, ov_build, ts_render.max_ms,
              ov_present, ts_present.max_ms);
          LOG_INFO(std::string(line));
          ts_cpu.reset();
          ts_render.reset();
          ts_present.reset();
          timing_frames = 0;
          timing_window_start = timing_clock::now();
        }
      }

      // === 9. Check for IQ capture duration expiry ===
      if (iq_capturing && config.iq_capture_duration > 0.0) {
        double const elapsed =
            std::chrono::duration<double>(std::chrono::steady_clock::now() -
                                          iq_capture_start)
                .count();
        if (elapsed >= config.iq_capture_duration) {
          iq_logger.stop_capture();
          iq_capturing = false;
          LOG_INFO("IQ capture stopped after duration: " +
                   std::to_string(config.iq_capture_duration) + " seconds");
        }
      }
    }

    // Stop async streaming before cleanup
    dev.stop_streaming();

    // Stop IQ capture if still running
    if (iq_capturing) {
      iq_logger.stop_capture();
    }

    LOG_INFO("Main loop exited cleanly");
  } catch (const std::exception &e) {
    LOG_ERROR("Fatal error: " + std::string(e.what()));
    // Device will be cleaned up by destructor (calls stop_streaming())
    return 1;
  }

  LOG_INFO("Shutdown complete");
  return 0;
}
