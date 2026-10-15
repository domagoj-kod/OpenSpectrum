// SPDX-License-Identifier: GPL-3.0-or-later

#include "fft/fft_analyzer.h"
#include "gui/sdl_renderer.h"
#include "hardware/iq_playback.h"
#include "hardware/rtl_sdr_device.h"
#include "hardware/sample_source.h"
#ifdef OPENSPECTRUM_UHD
#include "hardware/uhd_device.h"
#endif
#include "control_state.h"
#include "signal/signal_processor.h"
#include "utils/arg_parser.h"
#include "utils/format.h"
#include "utils/frame_pool.h"
#include "utils/iq_logger.h"
#include "utils/logger.h"
#include "utils/sample_queue.h"
#include "utils/spectrogram_exporter.h"
#include "visualization/spectrum_display.h"
#include "visualization/waterfall_display.h"

#include <algorithm>
#include <atomic>
#include <cctype>
#include <chrono>
#include <complex>
#include <csignal>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <exception>
#include <memory>
#include <span>
#include <string>
#include <thread>
#include <vector>

// SDL3 requires the file defining main() to include SDL_main.h (no longer
// pulled in by SDL.h). A no-op on Linux; provides the entry-point shim on
// platforms that need one (e.g. Windows GUI subsystem).
#include <SDL3/SDL_main.h>

#if defined(__SSE__) || defined(_M_X64) || defined(_M_IX86_FP)
#include <pmmintrin.h> // _MM_SET_DENORMALS_ZERO_MODE
#include <xmmintrin.h> // _MM_SET_FLUSH_ZERO_MODE
#endif

// Security: Use hardened compiler flags (defined in Makefile)
// -fstack-protector-strong, -D_FORTIFY_SOURCE=2, -O2, -Wall, -Wextra

using namespace openspectrum;

static std::atomic<bool> g_running{true};

// IQ capture tap. The logger lives in main(); the callback writes raw samples
// to it BEFORE the drain-to-newest throttle, so captures are full-rate, not
// one frame per render tick.
static IqLogger *g_iq_logger = nullptr;

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
  // and SampleSource::stop (which joins a thread) are not.
  // The main loop checks g_running every 8 ms (CV wait_for timeout) and
  // performs orderly shutdown — including source->stop() — on exit.
  if (signum == SIGINT || signum == SIGTERM) {
    g_running = false;
  }
}

// The producer thread's frame callback: every SampleSource calls it.
static void async_sample_callback(SampleQueue &queue,
                                  FrameHandle samples_frame) {
  // The source owns this thread; set FTZ/DAZ once per worker on first entry.
  thread_local bool ftz_initialized = false;
  if (!ftz_initialized) {
    enable_ftz_daz();
    ftz_initialized = true;
  }

  // Faithful IQ capture: tap the full producer stream here, before the render
  // loop's drain-to-newest throttle discards any backlog. This only copies into
  // the logger's ring; the disk is written on the logger's own thread. Idle,
  // write_samples returns without taking a lock.
  if (samples_frame && g_iq_logger != nullptr) {
    g_iq_logger->write_samples(samples_frame.data(), samples_frame.size());
  }

  queue.push(std::move(samples_frame));
}

auto main(int argc, char *argv[]) -> int {
  enable_ftz_daz();

  // Parse command-line arguments
  AppConfig const config = parse_arguments(argc, argv);

  // Validate --fft-size against the one list of supported sizes — the same list
  // the 1-7 keys and the README use. Membership, not "power of two in a range":
  // the range check let the CLI reach sizes the UI cannot select and the manual
  // does not document (and it still had to hardcode a bound so a pathological
  // 2^30 could not ask FramePool for gigabytes before the UI came up). One list
  // means adding a size is one edit, and the three surfaces cannot drift.
  DeviceConstraints const limits;
  const auto &allowed = limits.supported_fft_sizes;
  if (std::find(allowed.begin(), allowed.end(), config.fft_size) ==
      allowed.end()) {
    std::string list;
    for (size_t const s : allowed) {
      list += (list.empty() ? "" : ", ") + std::to_string(s);
    }
    LOG_ERROR("Unsupported FFT size " + std::to_string(config.fft_size) +
              " - must be one of: " + list);
    return 1;
  }

  LOG_INFO("Starting SDR Spectrum Analyzer");

  // Register signal handlers
  std::signal(SIGINT, signal_handler);
  std::signal(SIGTERM, signal_handler);

  // Log configuration
  LOG_INFO("Configuration: freq=" +
           std::to_string(static_cast<uint64_t>(config.center_freq_hz)) +
           "Hz, rate=" +
           std::to_string(static_cast<uint32_t>(config.sample_rate_hz)) +
           "Hz, gain=" + std::to_string(config.gain_db) +
           "dB, fft_size=" + std::to_string(config.fft_size) + ", window=" +
           SignalProcessor::window_function_to_string(config.window_function));

  // Configuration
  const size_t FFT_SIZE = config.fft_size;
  // Fixed 720p logical canvas. SDL renders the whole scene at this resolution
  // and the GPU point-samples it up to any window/display (see the logical
  // presentation in SdlRenderer). Fixed, not a CLI knob, so the waterfall
  // history depth (~12 s at 30 fps), the instrument panel layout and the
  // per-frame render cost are identical on a 1366x768 laptop and a 4K panel —
  // only the final upscale blit grows. Bumping this raises render/GPU cost on
  // every machine forever; 1080p was rejected (3.4x the pixels, downscaled on
  // sub-1080p displays, buys resolution the mean-trace decimation discards).
  constexpr size_t DISPLAY_WIDTH = 1280;
  constexpr size_t DISPLAY_HEIGHT = 720;
  const size_t WATERFALL_LINES = DISPLAY_HEIGHT / 2;

  try {
    // 1. Initialize SDL3 renderer FIRST (before hardware)
    // VSYNC on: the render loop does ~3.5 ms of work per frame at the 4096
    // default and ~9 ms at 65536 (T470, cpu+render_build+present), far inside a
    // 33 ms frame at --max-fps 30, and is sample-delivery bound (~62 lines/s) —
    // so there is ample headroom to cap at the display refresh. Without vsync,
    // presenting ~62 fps into a 60 Hz panel
    // tears and beats at ~2 Hz — visible as a periodic waterfall "blink" and a
    // doubled spectrum peak while tuning.
    SdlRenderer renderer(DISPLAY_WIDTH, DISPLAY_HEIGHT, "OpenSpectrum SDR");
    if (!renderer.is_valid()) {
      LOG_ERROR("Failed to initialize SDL3 renderer");
      return 1;
    }
    LOG_INFO("SDL3 renderer initialized");

    // 1.5 Initialize runtime controls
    // Frequency and gain are set further down, once the source's tuning
    // limits are known (they clamp against the constraints in force).
    ControlState control_state;
    control_state.set_fft_size(config.fft_size);
    // A non-default --fft-size (!= the 4096 ControlState default) flips the
    // fft-changed flag, which would make the first loop iteration redundantly
    // reinitialize FFT-dependent state that the setup below already builds for
    // config.fft_size (and needlessly stop/restart the sample source). Clear it
    // so the reinit path only fires on real runtime '1'-'5' changes.
    control_state.clear_fft_change_flag();
    control_state.set_window(config.window_function);
    LOG_INFO("Runtime controls initialized");

    // 1.55 Playback mode (--play): replay a .iq capture instead of opening
    // hardware. The .meta.json sidecar (written by the IQ logger) overrides
    // center frequency and sample rate so the freq scale matches the capture;
    // effective_rate is what every downstream consumer uses from here on.
    const bool playback_mode = !config.play_file.empty();
    uint32_t effective_rate = static_cast<uint32_t>(config.sample_rate_hz);
    auto start_freq = static_cast<uint64_t>(config.center_freq_hz);
    float start_gain = config.gain_db;
    IqCaptureMeta play_meta;
    if (playback_mode) {
      if (IqPlaybackSource::read_meta_sidecar(config.play_file, play_meta)) {
        start_freq = play_meta.center_freq_hz;
        effective_rate = play_meta.sample_rate_hz;
        start_gain = play_meta.gain_db.value_or(start_gain);
        LOG_INFO("Playback metadata: freq=" + format_frequency(start_freq) +
                 ", rate=" + std::to_string(effective_rate) + " Hz");
      } else {
        LOG_WARNING("No .meta.json sidecar for " + config.play_file +
                    " — using command-line freq/rate");
      }
    }

    // 1.6 Initialize IQ logger (if enabled)
    IqLoggerConfig iq_logger_config;
    if (!config.iq_output_file.empty()) {
      iq_logger_config.filename_prefix = config.iq_output_file;
    }
    iq_logger_config.format = config.iq_format;
    IqLogger iq_logger(iq_logger_config);
    g_iq_logger = &iq_logger; // producer-thread capture tap (see callback)
    std::chrono::steady_clock::time_point iq_capture_start;

    // Set up callback for when capture completes
    iq_logger.set_complete_callback([](const std::string &filename,
                                       const std::string &meta_filename) {
      LOG_INFO("IQ capture complete: " + filename + " (" + meta_filename + ")");
    });

    // 1.7 Initialize spectrogram exporter (config defaults: spectrograms/
    // output dir, metadata sidecar on)
    SpectrogramExporter spectrogram_exporter;
    LOG_INFO("Spectrogram exporter initialized");

    // 2. The queue from the producer thread, with its frame pool
    SampleQueue sample_queue(FFT_SIZE);

    // 2. Initialize the sample source: RTL-SDR hardware, or file playback.
    // Both feed the same FrameHandle callback path; everything downstream is
    // source-agnostic.
    auto frame_callback = [&sample_queue](FrameHandle samples_frame) {
      async_sample_callback(sample_queue, std::move(samples_frame));
    };

    // The one place a receiver is chosen. Everything after this block talks
    // to the SampleSource interface only.
    std::unique_ptr<SampleSource> source;

    if (playback_mode) {
      source = std::make_unique<IqPlaybackSource>(
          config.play_file, effective_rate, play_meta.format);
      if (!source->open()) {
        LOG_ERROR("Failed to open IQ playback file");
        return 1;
      }
    } else if (config.use_uhd) {
#ifdef OPENSPECTRUM_UHD
      LOG_INFO("Initializing USRP (UHD)...");
      auto usrp = std::make_unique<UhdDevice>();
      if (!usrp->open() || !usrp->set_sample_rate(effective_rate)) {
        LOG_ERROR("Failed to open a USRP at the requested sample rate");
        return 1;
      }
      source = std::move(usrp);
#else
      LOG_ERROR("--device uhd needs a build with UHD support (make UHD=1)");
      return 1;
#endif
    } else {
      LOG_INFO("Initializing RTL-SDR device...");
      auto rtl = std::make_unique<RtlSdrDevice>();
      if (!rtl->open()) {
        LOG_ERROR("Failed to open RTL-SDR device");
        return 1;
      }

      // One-shot device options (CLI-only by design: bias-T accidentally
      // toggled into a shorted antenna is a hardware risk, and direct sampling
      // needs the tuner reconfigured before streaming starts). Direct sampling
      // also narrows the limits() read below to the HF range.
      rtl->set_freq_correction(config.ppm_correction);
      if (config.bias_tee) {
        rtl->set_bias_tee(true);
      }
      if (config.direct_sampling) {
        rtl->set_direct_sampling(2);
      }
      rtl->set_sample_rate(effective_rate);
      source = std::move(rtl);
    }

    // Tuning range comes from the source; set_frequency/set_gain clamp to it.
    {
      DeviceConstraints constraints = control_state.get_constraints();
      const TuningLimits tuning = source->limits();
      constraints.min_frequency_hz = tuning.min_frequency_hz;
      constraints.max_frequency_hz = tuning.max_frequency_hz;
      constraints.min_gain_db = tuning.min_gain_db;
      constraints.max_gain_db = tuning.max_gain_db;
      if (!source->tunable()) {
        // Playback: the samples keep the capture's tuning whatever the keys
        // say, and these values label any capture or export made from it.
        constraints.min_frequency_hz = constraints.max_frequency_hz =
            start_freq;
        constraints.min_gain_db = constraints.max_gain_db = start_gain;
      }
      control_state.set_constraints(constraints);
      control_state.set_frequency(start_freq);
      control_state.set_gain(start_gain);
      control_state.mark_status_dirty();
    }

    // IQ capture start/stop, shared by the command line, Ctrl+S and the
    // duration timer. The source's drop counter is sampled at both ends so the
    // sidecar reports the losses of this capture only.
    uint64_t drops_at_capture_start = 0;
    bool iq_capture_failed = false;
    auto start_capture = [&](size_t fft_size, const char *notes) {
      drops_at_capture_start = source->dropped_samples();
      iq_capture_failed = !iq_logger.start_capture(
          control_state.get_frequency(), effective_rate,
          control_state.get_gain(), fft_size,
          SignalProcessor::window_function_to_string(
              control_state.get_window()),
          notes, source->name());
      if (iq_capture_failed) {
        LOG_ERROR("IQ logging could not start");
        return;
      }
      iq_capture_start = std::chrono::steady_clock::now();
      LOG_INFO("IQ logging started: " + iq_logger.get_data_filename());
    };
    auto stop_capture = [&] {
      iq_logger.set_dropped_samples(source->dropped_samples() -
                                    drops_at_capture_start);
      iq_logger.stop_capture();
    };

    if (config.iq_logging_enabled) {
      start_capture(FFT_SIZE, "Command-line capture");
    }

    source->set_frame_callback(frame_callback);
    if (source->tunable()) {
      control_state.apply_to_device(*source);
    }
    source->start(FFT_SIZE);
    LOG_INFO(std::string(source->name()) + " started: freq=" +
             format_frequency(control_state.get_frequency()) +
             ", rate=" + std::to_string(effective_rate) + " Hz, gain=" +
             ControlState::format_gain(control_state.get_gain()) +
             ", FFT size: " + std::to_string(FFT_SIZE));

    // 3. Initialize signal processor
    SignalProcessor signal_processor(FFT_SIZE);
    signal_processor.set_window(config.window_function);

    // 4. Initialize FFT analyzer
    FftAnalyzer fft_analyzer(FFT_SIZE);
    fft_analyzer.set_window_coherent_gain(
        SignalProcessor::get_coherent_gain(config.window_function));

    // 5. Initialize displays (split vertically: spectrum on top, waterfall
    // below)
    // Displays span the plot region only; the right gutter is the instrument
    // panel. plot_width() == full width when the window is too narrow for a
    // panel, so the narrow-window case is unchanged.
    SpectrumDisplay spectrum_display(renderer.plot_width(), DISPLAY_HEIGHT / 2);
    WaterfallDisplay waterfall_display(renderer.plot_width(),
                                       DISPLAY_HEIGHT / 2, WATERFALL_LINES);

    spectrum_display.set_db_range(-120.0F, 0.0F);
    waterfall_display.set_db_range(-120.0F, 0.0F);

    // Runtime color palettes ('c' / Shift+C). Index order MUST match
    // ControlState::palette_name and ControlState::PALETTE_COUNT.
    static constexpr SpectrumPalette::ColorMap kPaletteMap[] = {
        SpectrumPalette::ColorMap::JET, SpectrumPalette::ColorMap::VIRIDIS,
        SpectrumPalette::ColorMap::HOT, SpectrumPalette::ColorMap::GRAYSCALE,
        SpectrumPalette::ColorMap::BLUE_RED};
    static_assert(std::size(kPaletteMap) == ControlState::PALETTE_COUNT,
                  "palette map must match ControlState::PALETTE_COUNT");

    // Apply the startup palette; ControlState is the single source of truth.
    {
      const auto map = kPaletteMap[control_state.get_palette_index()];
      spectrum_display.set_color_map(map);
      waterfall_display.set_color_map(map);
    }

    LOG_INFO("Display initialized: spectrum and waterfall");

    // Main processing loop
    LOG_INFO("Starting main loop. Press ESC or Ctrl+C to stop.");
    LOG_INFO("Controls: +/- Frequency, r/f Gain, 1-7 FFT size, UP/DOWN Window, "
             "c/Shift+C Palette, m Max-hold, a Average, x Reset traces, "
             "Ctrl+S Toggle IQ logging, e Export spectrogram, "
             "Shift/Ctrl for fine/coarse");

    size_t current_fft_size = config.fft_size;

    // Reusable spectrum draw geometry — refilled each frame by
    // SpectrumDisplay::build_vertices(), kept across iterations so the capacity
    // is retained (no per-frame allocation) and so present_frame() on the
    // no-samples path still has the last geometry available.
    std::vector<SDL_Vertex> spec_verts;
    std::vector<int> spec_idx;

    // Persistent frequency markers (Hz). Anchored to absolute frequency, so
    // they slide with retuning and hide when tuned out of the current span.
    // Left-click drops, right-click removes nearest, Delete clears all.
    constexpr size_t kMaxMarkers = 14; // fits the panel list in every state
    std::vector<double> markers;

    // Track peak amplitude for display
    float peak_db = -140.0F;

    // Amplitude trigger (#5). Shift+left-drag in the spectrum pane sets a dB
    // threshold and arms it; a bin peak crossing the threshold freezes the
    // display until Space resumes + re-arms. The threshold is stored as a dB
    // value; the line's screen y is recomputed each frame from the spectrum's
    // current (autoscaled) dB range so it tracks the view.
    float trig_threshold_db = -40.0F;
    bool trig_armed = false;
    bool frozen = false;
    // Waterfall "seconds-ago" axis value held at the moment of freezing. The
    // axis is computed live (now - oldest-line stamp); without holding it, a
    // frozen snapshot's labels keep inflating as wall-clock advances even
    // though no new lines are added. Captured each live frame; reused frozen.
    double frozen_wf_top_seconds = 0.0;

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
    auto timing_ms = [](timing_clock::time_point a,
                        timing_clock::time_point b) {
      return std::chrono::duration<double, std::milli>(b - a).count();
    };
    // Latest per-second snapshot, surfaced to the on-screen overlay ('T' key).
    double ov_fps = 0.0;
    double ov_cpu = 0.0;
    double ov_build = 0.0;
    double ov_present = 0.0;

    // Pending spectrogram export. The 'e' key arms a renderer-side capture of
    // the next fully composited frame (base + axis/freq/HUD overlays); the
    // metadata snapshot is stashed here because the readback only lands after
    // that frame presents, one iteration later. Drained at the top of the loop.
    struct PendingExport {
      bool armed = false;
      uint64_t center_hz = 0;
      uint32_t rate_hz = 0;
      float gain_db = 0.0F;
      size_t fft_size = 0;
      std::string window;
      std::string color_map;
    } pending_export;
    std::vector<uint8_t> capture_buf;
    int capture_w = 0;
    int capture_h = 0;

    // Frame-rate cap (--max-fps, default 30). A positive cap throttles the
    // render loop below the display refresh to cut GPU/battery draw for long
    // unattended runs; 0 gives a zero interval = uncapped (vsync alone). The
    // sleep_until at the loop top paces every path (frozen/no-sample/render)
    // uniformly; vsync stays on so frames remain tear-free. Windows' ~15 ms
    // default timer granularity can make the effective rate undershoot the cap
    // slightly — harmless (it undershoots power too).
    const auto frame_interval =
        config.max_fps > 0
            ? std::chrono::duration_cast<timing_clock::duration>(
                  std::chrono::duration<double>(1.0 / config.max_fps))
            : timing_clock::duration::zero();
    auto next_frame_time = timing_clock::now();

    while (g_running.load(std::memory_order_relaxed)) {
      // Frame-rate cap: park until the next scheduled frame. No-op when
      // uncapped. Reset the schedule if a heavy frame put us behind, so we
      // don't burst to catch up (which would defeat the power cap).
      if (frame_interval != timing_clock::duration::zero()) {
        std::this_thread::sleep_until(next_frame_time);
        next_frame_time += frame_interval;
        auto const now = timing_clock::now();
        if (next_frame_time < now) {
          next_frame_time = now;
        }
      }

      // === 1.0. Drain a completed export capture (armed a prior frame) ===
      if (pending_export.armed &&
          renderer.take_capture(capture_buf, capture_w, capture_h)) {
        pending_export.armed = false;
        auto result = spectrogram_exporter.export_framebuffer(
            capture_buf.data(), static_cast<size_t>(capture_w),
            static_cast<size_t>(capture_h), pending_export.center_hz,
            pending_export.rate_hz, pending_export.gain_db,
            pending_export.fft_size, pending_export.window,
            pending_export.color_map, "");
        if (result.success) {
          LOG_INFO("Spectrogram exported: " + result.filename);
          if (!result.metadata_filename.empty()) {
            LOG_INFO("Metadata written: " + result.metadata_filename);
          }
          renderer.render_status_bar("Exported: " + result.filename);
        } else {
          LOG_ERROR("Spectrogram export failed: " + result.error_message);
          renderer.render_status_bar("Export failed!");
        }
      }

      // === 1. Process SDL3 events (must be first in loop) ===
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

      // === 1.1b. Check for color palette change ('c' / Shift+C) ===
      if (control_state.palette_changed()) {
        const auto map = kPaletteMap[control_state.get_palette_index()];
        spectrum_display.set_color_map(map);  // recolors next frame
        waterfall_display.set_color_map(map); // repaints full history via LUT
        control_state.clear_palette_change_flag();
      }

      // === 1.1c. Trace modes ('m' max-hold, 'a' averaging, 'x' reset) ===
      // Plain bool pushes; the display resets its trace state only on an
      // actual transition, so doing this every frame costs nothing.
      spectrum_display.set_max_hold(control_state.max_hold_enabled());
      spectrum_display.set_averaging(control_state.averaging_enabled());
      if (control_state.trace_reset_requested()) {
        control_state.clear_trace_reset();
        spectrum_display.reset_traces();
      }

      // === 1.2. Check for FFT size change and reinitialize if needed ===
      if (control_state.fft_size_changed()) {
        size_t const new_fft_size = control_state.get_fft_size();
        control_state.clear_fft_change_flag();

        LOG_INFO("Reinitializing with FFT size: " +
                 std::to_string(new_fft_size));

        // Reinitialize FFT-dependent components
        current_fft_size = new_fft_size;

        // The queue is reconfigured with the source stopped
        source->stop();
        sample_queue.reconfigure(current_fft_size);
        source->start(current_fft_size);

        // Recreate signal processor with new size
        signal_processor = SignalProcessor(current_fft_size);
        signal_processor.set_window(control_state.get_window());

        // Recreate FFT analyzer with new size (using move semantics)
        fft_analyzer = FftAnalyzer(current_fft_size);
        fft_analyzer.set_window_coherent_gain(
            SignalProcessor::get_coherent_gain(control_state.get_window()));

        // Clear waterfall to avoid size mismatch
        waterfall_display.reset();
      }

      // === 1.2.5. Check for IQ logging toggle request ===
      if (control_state.iq_logging_toggle_requested()) {
        control_state.clear_iq_logging_toggle();
        if (iq_logger.is_capturing()) {
          stop_capture();
          LOG_INFO("IQ logging stopped");
        } else {
          start_capture(current_fft_size, "Manual capture");
        }
      }

      // === 1.2.7. Check for spectrogram export request ===
      // Arm a WYSIWYG capture: the renderer reads back the next composited
      // frame (base + dB/time axis strip + frequency scale + HUD) just before
      // its swap; the drain at the loop top writes the PNG once it lands. Stash
      // the metadata now so it matches the armed frame even if controls change
      // before the readback.
      if (control_state.spectrogram_export_requested()) {
        control_state.clear_spectrogram_export();
        pending_export.armed = true;
        pending_export.center_hz = control_state.get_frequency();
        pending_export.rate_hz = effective_rate;
        pending_export.gain_db = control_state.get_gain();
        pending_export.fft_size = current_fft_size;
        pending_export.window = SignalProcessor::window_function_to_string(
            control_state.get_window());
        // Lower case, so the default stays "jet" as in earlier sidecars.
        pending_export.color_map =
            ControlState::palette_name(control_state.get_palette_index());
        std::ranges::transform(pending_export.color_map,
                               pending_export.color_map.begin(), [](char c) {
                                 return static_cast<char>(std::tolower(
                                     static_cast<unsigned char>(c)));
                               });
        renderer.request_capture();
      }

      // === 1.3. Apply control state changes to device (batch update) ===
      // Playback mode has no tuner: freq/gain keys only move the on-screen
      // labels, and there is no stale-channel backlog to drop.
      if (source->tunable()) {
        static uint64_t s_last_freq = 0;
        uint64_t const now_freq = control_state.get_frequency();
        control_state.apply_to_device(*source);
        if (s_last_freq != 0 && s_last_freq != now_freq) {
          // Frequency tuned — discard buffered samples from the old channel.
          // Scale label updates immediately (render_frequency_scale sees new
          // freq); dropping the queue makes the spectrum snap in sync rather
          // than lagging behind by up to MAX_SAMPLE_QUEUE_SIZE × FFT frames.
          sample_queue.clear();
        }
        s_last_freq = now_freq;
      }

      // Feed the panel's status grid (freq/gain/fft/window/palette/trace).
      if (control_state.status_changed()) {
        auto sf = control_state.get_status_fields();
        renderer.set_status_fields(
            {sf.freq, sf.gain, sf.fft, sf.window, sf.palette, sf.trace});
        control_state.clear_status_dirty();
      }

      // IQ logging status, every frame: the panel caches text by string, so an
      // unchanged "REC 12s" costs a lookup.
      if (iq_logger.is_capturing() && iq_logger.write_failed()) {
        // A short write (disk full) ended the writing.
        stop_capture();
        iq_capture_failed = true;
        LOG_ERROR("IQ capture ended early on a write error");
      }
      if (iq_logger.is_capturing()) {
        auto const seconds = std::chrono::duration_cast<std::chrono::seconds>(
            std::chrono::steady_clock::now() - iq_capture_start);
        // Kept short to fit the panel column.
        renderer.render_iq_status("REC " + std::to_string(seconds.count()) +
                                  "s");
      } else {
        // Idle, or the last capture failed (stays until the next one starts)
        renderer.render_iq_status(iq_capture_failed ? "REC ERR" : "");
      }

      // Latest peak for the panel readout, every frame. Pass it unconditionally
      // so the renderer tracks the live value and hides it (< -140 dBFS) when
      // the signal drops, instead of freezing at the last valid peak.
      renderer.render_peak_indicator(peak_db);

      // Update frequency scale (caches rebuild only when center_hz or rate
      // changes)
      renderer.render_frequency_scale(control_state.get_frequency(),
                                      effective_rate,
                                      spectrum_display.height());

      // Push the latest timing snapshot for the panel's always-on PERF block.
      renderer.set_timing_overlay(ov_fps, ov_cpu, ov_build, ov_present);

      // Left-margin axes: live dB range from the spectrum, and the age of the
      // oldest visible waterfall row (0 while history is still too short → the
      // time axis stays hidden until there is something to label).
      {
        double wf_top_seconds = frozen_wf_top_seconds;
        if (!frozen) {
          // Live: age of the oldest visible row (0 while history is too short
          // → the time axis stays hidden until there is something to label).
          WaterfallDisplay::CursorTime oldest;
          wf_top_seconds = waterfall_display.sample_at_y(0.0F, oldest)
                               ? oldest.seconds_ago
                               : 0.0;
          // Remember it so a freeze on this frame holds these labels static.
          frozen_wf_top_seconds = wf_top_seconds;
        }
        renderer.set_left_axes(true, spectrum_display.min_db(),
                               spectrum_display.max_db(), wf_top_seconds);
      }

      // === Frequency markers (persistent vertical reference lines) ===
      {
        const double rate = static_cast<double>(effective_rate);
        const double center =
            static_cast<double>(control_state.get_frequency());
        const double left_hz = center - rate / 2.0;
        const float region_w = static_cast<float>(spectrum_display.width());
        const float total_h = static_cast<float>(spectrum_display.height() +
                                                 waterfall_display.height());

        if (renderer.take_clear_markers()) {
          markers.clear();
        }
        for (const auto &click : renderer.take_clicks()) {
          if (click.x < 0.0F || click.x >= region_w || click.y < 0.0F ||
              click.y >= total_h) {
            continue;
          }
          if (click.right) {
            // Remove the nearest on-screen marker within a small pixel radius.
            double best = 12.0;
            int best_i = -1;
            for (size_t i = 0; i < markers.size(); ++i) {
              const double mx = ((markers[i] - left_hz) / rate) *
                                static_cast<double>(region_w);
              if (mx < 0.0 || mx >= static_cast<double>(region_w)) {
                continue;
              }
              const double d = std::abs(mx - static_cast<double>(click.x));
              if (d < best) {
                best = d;
                best_i = static_cast<int>(i);
              }
            }
            if (best_i >= 0) {
              markers.erase(markers.begin() + best_i);
            }
          } else if (markers.size() < kMaxMarkers) {
            markers.push_back(left_hz + (static_cast<double>(click.x) /
                                         static_cast<double>(region_w)) *
                                            rate);
          }
        }

        const float spec_h = static_cast<float>(spectrum_display.height());
        std::vector<SdlRenderer::Marker> draw_markers;
        draw_markers.reserve(markers.size());
        int idx = 1;
        for (const double f : markers) {
          SdlRenderer::Marker m;
          m.index = idx++;
          m.freq_hz = f;
          const double xrel = (f - left_hz) / rate;
          m.on_screen = (xrel >= 0.0 && xrel < 1.0);
          m.x = static_cast<float>(xrel * static_cast<double>(region_w));
          if (m.on_screen) {
            SpectrumDisplay::CursorSample s;
            if (spectrum_display.sample_at_x(m.x, region_w, spec_h, s)) {
              m.db = s.db;
            }
          }
          draw_markers.push_back(m);
        }
        renderer.set_markers(std::move(draw_markers));
      }

      // === Cursor readout (mouse hover over the spectrum pane) ===
      // Marker style: cursor X -> frequency; amplitude = the trace's dB at that
      // frequency, with the dot snapped onto the drawn curve (mouse Y ignored).
      // Computed every frame so it tracks the pointer on both the sample and
      // no-sample present paths; uses the spectrum's current data.
      {
        // Smoothed amplitude: the raw spectrum is noisy, so the displayed dB is
        // EMA'd to stop the digits churning. Snap (reset) when the cursor moves
        // to a new bin or re-enters, so it stays responsive rather than
        // lagging.
        static double s_db_ema = 0.0;
        static size_t s_db_bin = 0;
        static bool s_db_valid = false;

        SdlRenderer::CursorReadout readout;
        const float spec_h = static_cast<float>(spectrum_display.height());
        const float wf_h = static_cast<float>(waterfall_display.height());
        const float region_w = static_cast<float>(spectrum_display.width());
        const float cx = renderer.cursor_x();
        const float cy = renderer.cursor_y();
        const double rate = static_cast<double>(effective_rate);
        const double center =
            static_cast<double>(control_state.get_frequency());
        // Cursor X -> frequency: shared by both panes.
        const double freq_hz =
            center - rate / 2.0 +
            (static_cast<double>(cx) / static_cast<double>(region_w)) * rate;

        bool in_spectrum = false;
        if (renderer.cursor_active() && cx >= 0.0F && cx < region_w) {
          SpectrumDisplay::CursorSample sample;
          if (cy >= 0.0F && cy < spec_h &&
              spectrum_display.sample_at_x(cx, region_w, spec_h, sample)) {
            in_spectrum = true;
            if (!s_db_valid || sample.bin != s_db_bin) {
              s_db_ema = sample.db; // snap on new bin / re-entry
            } else {
              s_db_ema += 0.2 * (static_cast<double>(sample.db) - s_db_ema);
            }
            s_db_bin = sample.bin;
            s_db_valid = true;

            readout.pane = SdlRenderer::CursorReadout::Pane::Spectrum;
            readout.x = cx;
            readout.dot_y = sample.dot_y;
            readout.freq_hz = freq_hz;
            readout.db = static_cast<float>(s_db_ema);
          } else if (cy >= spec_h && cy < spec_h + wf_h) {
            WaterfallDisplay::CursorTime when;
            if (waterfall_display.sample_at_y(cy - spec_h, when)) {
              readout.pane = SdlRenderer::CursorReadout::Pane::Waterfall;
              readout.x = cx;
              readout.dot_y = cy; // render-space row for the horizontal marker
              readout.freq_hz = freq_hz;
              readout.seconds_ago = when.seconds_ago;
            }
          }
        }
        if (!in_spectrum) {
          s_db_valid = false; // left the spectrum pane; next entry snaps
        }
        renderer.set_cursor_readout(readout);
      }

      // === Amplitude trigger: input + freeze state machine (#5) ===
      {
        const float spec_h = static_cast<float>(spectrum_display.height());
        const float db_max = spectrum_display.max_db();
        const float db_min = spectrum_display.min_db();

        // Shift+left-drag sets the threshold from the pointer's y in the
        // spectrum pane; dragging onto the bottom edge disarms (hides the
        // line).
        if (auto set_y = renderer.take_trigger_set()) {
          const float y = std::clamp(*set_y, 0.0F, spec_h);
          if (y >= spec_h - 4.0F) {
            trig_armed = false;
          } else if (db_max > db_min) {
            const float frac = y / spec_h; // 0 = top (max dB)
            trig_threshold_db = db_max - frac * (db_max - db_min);
            trig_armed = true;
          }
        }

        // Space resumes from a freeze and re-arms (threshold/armed persist),
        // and always repaints the status bar — this is what dismisses a sticky
        // transient message (TRIGGERED, "Exported: ...") back to FREQ|GAIN|...,
        // since those bypass the dirty-flag path and otherwise linger until the
        // next control change.
        if (control_state.unfreeze_requested()) {
          control_state.clear_unfreeze();
          frozen = false;
          control_state.mark_status_dirty();
        }

        // Feed the renderer this frame's line position + state. Recompute the y
        // from the (possibly autoscaled) range so the line tracks the view.
        float line_y = 0.0F;
        if (db_max > db_min) {
          const float frac = (db_max - trig_threshold_db) / (db_max - db_min);
          line_y = std::clamp(frac, 0.0F, 1.0F) * spec_h;
        }
        renderer.set_trigger(trig_armed, line_y, trig_threshold_db, frozen);
      }

      // While frozen, hold the last composited frame: skip dequeue/processing
      // so the spectrum + waterfall stay put, but keep presenting so the
      // overlays (trigger line, FROZEN banner, cursor readout) stay live and
      // input keeps flowing. The producer keeps filling the (bounded) queue;
      // the drain-to-newest throttle catches up on resume.
      if (frozen) {
        renderer.present_frame();
        continue;
      }

      // === 2. Read samples from async queue (non-blocking) ===
      // Wait for samples with timeout (8ms = ~125fps max). Only the newest
      // frame comes back; the backlog is dropped.
      FrameHandle async_samples_frame =
          sample_queue.pop_newest(std::chrono::milliseconds(8));
      bool const got_samples = static_cast<bool>(async_samples_frame);

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
      // (accumulated in callback to match FFT size). Process the pooled frame
      // buffer in place via a span — no copy into a separate samples vector.
      // The frame is released back to the pool at the end of the iteration.
      std::span<std::complex<float>> samples(async_samples_frame.data(),
                                             current_fft_size);

      // IQ logging happens in async_sample_callback (full-rate producer tap),
      // not here — the render loop only sees the post-throttle newest frame.

      // === 3. Signal processing ===
      auto t_cpu_start = timing_clock::now();
      openspectrum::SignalProcessor::remove_dc(samples);
      signal_processor.apply_window(samples);

      // === 4. FFT execution ===
      fft_analyzer.execute(samples);

      // === 4.1. Get peak amplitude for status display ===
      peak_db = fft_analyzer.get_max_db();

      // === 4.2. Amplitude trigger: fire on a threshold crossing ===
      // Freezing halts the stream, so a level test is enough — no rising-edge
      // tracking needed (the next armed frame after resume re-evaluates fresh).
      // This frame still renders below, so the triggering frame is the one
      // held.
      if (trig_armed && peak_db >= trig_threshold_db) {
        frozen = true; // the panel shows the trigger row while frozen
      }

      // === 5. Get spectral data ===
      const auto &db_spectrum = fft_analyzer.get_db_spectrum();

      // === 6. Update displays ===
      spectrum_display.update_spectrum(db_spectrum);

      waterfall_display.add_spectrum_line(db_spectrum);
      auto t_cpu_end = timing_clock::now();

      // === 7. Render: spectrum as GPU geometry, waterfall via texture ===
      // Build the spectrum bars once per frame (reuses spec_verts/spec_idx
      // capacity). The waterfall still picks full-upload vs GPU-scroll.
      spectrum_display.build_vertices(
          static_cast<float>(renderer.plot_width()),
          static_cast<float>(spectrum_display.height()), spec_verts, spec_idx);
      const auto &wf_dirty_rects = waterfall_display.get_dirty_rects();

      auto t_render_start = timing_clock::now();
      bool render_ok;
      if (waterfall_display.needs_full_render()) {
        // First frame after reset or LUT rebuild: full waterfall upload. Also
        // seeds the GPU scroll texture for subsequent scroll frames.
        render_ok = renderer.render_displays(
            spec_verts, spec_idx, waterfall_display.get_pixels().data(),
            spectrum_display.height(), wf_dirty_rects);
      } else {
        // Steady state: GPU shifts the waterfall texture up by one line and
        // uploads only the new bottom strip (~5 KB instead of ~1.5 MB).
        render_ok = renderer.render_displays_scroll(
            spec_verts, spec_idx, waterfall_display.get_new_line_rgba(),
            spectrum_display.height(), waterfall_display.height(),
            waterfall_display.get_line_height());
      }
      waterfall_display.mark_rendered();

      if (!render_ok) {
        LOG_ERROR("Render failed");
        break;
      }
      auto t_render_end = timing_clock::now();

      // === 8. Frame-timing accumulation + per-second report (debug branch) ===
      {
        double const present_ms = renderer.last_present_ms();
        double render_build_ms =
            timing_ms(t_render_start, t_render_end) - present_ms;
        if (render_build_ms < 0.0) {
          render_build_ms = 0.0; // clock skew guard
        }
        ts_cpu.add(timing_ms(t_cpu_start, t_cpu_end));
        ts_render.add(render_build_ms);
        ts_present.add(present_ms);
        ++timing_frames;

        double const window_s = std::chrono::duration<double>(
                                    timing_clock::now() - timing_window_start)
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
              ov_fps, static_cast<unsigned long long>(timing_frames), ov_cpu,
              ts_cpu.max_ms, ov_build, ts_render.max_ms, ov_present,
              ts_present.max_ms);
          LOG_INFO(std::string(line));
          ts_cpu.reset();
          ts_render.reset();
          ts_present.reset();
          timing_frames = 0;
          timing_window_start = timing_clock::now();
        }
      }

      // === 9. Check for IQ capture duration expiry ===
      if (iq_logger.is_capturing() && config.iq_capture_duration > 0.0) {
        double const elapsed =
            std::chrono::duration<double>(std::chrono::steady_clock::now() -
                                          iq_capture_start)
                .count();
        if (elapsed >= config.iq_capture_duration) {
          stop_capture();
          LOG_INFO("IQ capture stopped after duration: " +
                   std::to_string(config.iq_capture_duration) + " seconds");
        }
      }
    }

    // Stop the sample source before cleanup
    source->stop();

    // Stop IQ capture if still running. The producer is already stopped
    // above, so no callback can race the gate here.
    if (iq_logger.is_capturing()) {
      stop_capture();
    }

    LOG_INFO("Main loop exited cleanly");
  } catch (const std::exception &e) {
    LOG_ERROR("Fatal error: " + std::string(e.what()));
    // Device will be cleaned up by destructor (calls stop())
    return 1;
  }

  LOG_INFO("Shutdown complete");
  return 0;
}
