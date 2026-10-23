// SPDX-License-Identifier: GPL-3.0-or-later

#include "fft/fft_analyzer.h"
#include "gui/plot_overlays.h"
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
#include "utils/frame_timing.h"
#include "utils/iq_logger.h"
#include "utils/logger.h"
#include "utils/sample_queue.h"
#include "utils/settings.h"
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
#include <exception>
#include <filesystem>
#include <memory>
#include <span>
#include <string>
#include <thread>
#include <vector>

// Required in the file that defines main().
#include <SDL3/SDL_main.h>

#if defined(__SSE__) || defined(_M_X64) || defined(_M_IX86_FP)
#include <pmmintrin.h> // _MM_SET_DENORMALS_ZERO_MODE
#include <xmmintrin.h> // _MM_SET_FLUSH_ZERO_MODE
#endif

using namespace openspectrum;

static std::atomic<bool> g_running{true};

// FTZ + DAZ avoid the microcode trap on denormals. MXCSR is per thread, so
// call it from each.
static inline void enable_ftz_daz() noexcept {
#if defined(__SSE__) || defined(_M_X64) || defined(_M_IX86_FP)
  _MM_SET_FLUSH_ZERO_MODE(_MM_FLUSH_ZERO_ON);
  _MM_SET_DENORMALS_ZERO_MODE(_MM_DENORMALS_ZERO_ON);
#endif
}

static void signal_handler(int signum) {
  // Async-signal-safe only (no LOG): the main loop polls g_running.
  if (signum == SIGINT || signum == SIGTERM) {
    g_running = false;
  }
}

static void async_sample_callback(SampleQueue &queue, IqLogger &iq_logger,
                                  FrameHandle samples_frame) {
  // The source owns this thread: FTZ/DAZ once, on first entry.
  thread_local bool ftz_initialized = false;
  if (!ftz_initialized) {
    enable_ftz_daz();
    ftz_initialized = true;
  }

  if (samples_frame) {
    iq_logger.write_samples(samples_frame.data(), samples_frame.size());
  }

  queue.push(std::move(samples_frame));
}

auto main(int argc, char *argv[]) -> int {
  enable_ftz_daz();

  // The command line over the saved settings over the defaults. The first
  // parse only tells apply_settings the receiver; the second re-applies the
  // flags on top.
  std::filesystem::path const settings_file = settings_path();
  Settings settings = load_settings(settings_file);
  AppConfig const config = [&] {
    AppConfig saved = parse_arguments(argc, argv);
    apply_settings(settings, saved);
    return parse_arguments(argc, argv, saved);
  }();

  // --fft-size must be in the list the 1-7 keys and the README use, so the
  // three cannot drift.
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

  std::signal(SIGINT, signal_handler);
  std::signal(SIGTERM, signal_handler);

  LOG_INFO("Configuration: freq=" +
           std::to_string(static_cast<uint64_t>(config.center_freq_hz)) +
           "Hz, rate=" +
           std::to_string(static_cast<uint32_t>(config.sample_rate_hz)) +
           "Hz, gain=" + std::to_string(config.gain_db) +
           "dB, fft_size=" + std::to_string(config.fft_size) + ", window=" +
           SignalProcessor::window_function_to_string(config.window_function));

  const size_t FFT_SIZE = config.fft_size;
  // A fixed 720p logical canvas, scaled by SDL to any window. Not a CLI knob:
  // the waterfall depth, the panel layout and the render cost stay the same on
  // every machine. 1080p was rejected: 3.4x the pixels for resolution the
  // per-column mean discards.
  constexpr size_t DISPLAY_WIDTH = 1280;
  constexpr size_t DISPLAY_HEIGHT = 720;
  const size_t WATERFALL_LINES = DISPLAY_HEIGHT / 2;

  try {
    SdlRenderer renderer(DISPLAY_WIDTH, DISPLAY_HEIGHT, "OpenSpectrum SDR");
    if (!renderer.is_valid()) {
      LOG_ERROR("Failed to initialize SDL3 renderer");
      return 1;
    }
    LOG_INFO("SDL3 renderer initialized");

    // Frequency and gain wait for the source's limits.
    ControlState control_state;
    control_state.set_fft_size(config.fft_size);
    // A non-default --fft-size sets the changed flag, but the setup below
    // already builds for it: clear it so only real key changes reinitialize.
    control_state.clear_fft_change_flag();
    control_state.set_window(config.window_function);
    control_state.set_palette(config.palette_index);
    LOG_INFO("Runtime controls initialized");

    // --play: the sidecar overrides centre frequency and sample rate;
    // effective_rate is what everything downstream uses.
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

    IqLoggerConfig iq_logger_config;
    if (!config.iq_output_file.empty()) {
      iq_logger_config.filename_prefix = config.iq_output_file;
    }
    iq_logger_config.format = config.iq_format;
    IqLogger iq_logger(iq_logger_config);
    std::chrono::steady_clock::time_point iq_capture_start;

    iq_logger.set_complete_callback([](const std::string &filename,
                                       const std::string &meta_filename) {
      LOG_INFO("IQ capture complete: " + filename + " (" + meta_filename + ")");
    });

    SpectrogramExporter spectrogram_exporter;
    LOG_INFO("Spectrogram exporter initialized");

    SampleQueue sample_queue(FFT_SIZE);
    auto frame_callback = [&sample_queue,
                           &iq_logger](FrameHandle samples_frame) {
      async_sample_callback(sample_queue, iq_logger, std::move(samples_frame));
    };

    // The one place a receiver is chosen.
    std::unique_ptr<SampleSource> source;

    if (playback_mode) {
      source = std::make_unique<IqPlaybackSource>(
          config.play_file, effective_rate, play_meta.format);
      if (!source->open()) {
        LOG_ERROR("Failed to open IQ playback file");
        return 1;
      }
#ifdef OPENSPECTRUM_UHD
    } else if (config.use_uhd) {
      LOG_INFO("Initializing USRP (UHD)...");
      auto usrp = std::make_unique<UhdDevice>();
      if (!usrp->open()) {
        LOG_ERROR("Failed to open a USRP");
        return 1;
      }
      source = std::move(usrp);
#endif
    } else {
      LOG_INFO("Initializing RTL-SDR device...");
      auto rtl = std::make_unique<RtlSdrDevice>();
      if (!rtl->open()) {
        LOG_ERROR("Failed to open RTL-SDR device");
        return 1;
      }

      // CLI-only by design: a bias tee toggled into a shorted antenna is a
      // hardware risk. Direct sampling narrows limits() below to HF.
      rtl->set_freq_correction(config.ppm_correction);
      if (config.bias_tee) {
        rtl->set_bias_tee(true);
      }
      if (config.direct_sampling) {
        rtl->set_direct_sampling(2);
      }
      source = std::move(rtl);
    }

    if (!source->set_sample_rate(effective_rate)) {
      return 1;
    }

    {
      DeviceConstraints constraints = control_state.get_constraints();
      const TuningLimits tuning = source->limits();
      constraints.min_frequency_hz = tuning.min_frequency_hz;
      constraints.max_frequency_hz = tuning.max_frequency_hz;
      constraints.min_gain_db = tuning.min_gain_db;
      constraints.max_gain_db = tuning.max_gain_db;
      constraints.span_hz = effective_rate;
      if (!source->tunable()) {
        // Playback keeps the capture's tuning; these label any export.
        constraints.min_frequency_hz = constraints.max_frequency_hz =
            start_freq;
        constraints.min_gain_db = constraints.max_gain_db = start_gain;
      }
      control_state.set_constraints(constraints);
      control_state.set_frequency(start_freq);
      control_state.set_gain(start_gain);
      control_state.mark_status_dirty();
    }

    // Shared by the command line, Ctrl+S and the duration timer. The drop
    // counter is sampled at both ends: the sidecar reports this capture only.
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

    SignalProcessor signal_processor(FFT_SIZE);
    signal_processor.set_window(config.window_function);

    FftAnalyzer fft_analyzer(FFT_SIZE);
    fft_analyzer.set_window_coherent_gain(
        SignalProcessor::get_coherent_gain(config.window_function));

    SpectrumDisplay spectrum_display(renderer.plot_width(), DISPLAY_HEIGHT / 2);
    WaterfallDisplay waterfall_display(renderer.plot_width(),
                                       DISPLAY_HEIGHT / 2, WATERFALL_LINES);

    spectrum_display.set_db_range(-120.0F, 0.0F);
    waterfall_display.set_db_range(-120.0F, 0.0F);

    // Index order MUST match ControlState::palette_name and PALETTE_COUNT!
    static constexpr SpectrumPalette::ColorMap kPaletteMap[] = {
        SpectrumPalette::ColorMap::JET, SpectrumPalette::ColorMap::VIRIDIS,
        SpectrumPalette::ColorMap::HOT, SpectrumPalette::ColorMap::GRAYSCALE,
        SpectrumPalette::ColorMap::BLUE_RED};
    static_assert(std::size(kPaletteMap) == ControlState::PALETTE_COUNT,
                  "palette map must match ControlState::PALETTE_COUNT");

    {
      const auto map = kPaletteMap[control_state.get_palette_index()];
      spectrum_display.set_color_map(map);
      waterfall_display.set_color_map(map);
    }

    LOG_INFO("Display initialized: spectrum and waterfall");

    LOG_INFO("Starting main loop. Press ESC or Ctrl+C to stop.");
    LOG_INFO("Controls: +/- Frequency, r/f Gain, 1-7 FFT size, UP/DOWN Window, "
             "c/Shift+C Palette, m Max-hold, a Average, x Reset traces, "
             "Ctrl+S Toggle IQ logging, e Export spectrogram, "
             "Shift/Ctrl for fine/coarse");

    size_t current_fft_size = config.fft_size;

    // Kept across frames: no allocation, and present_frame() on the no-samples
    // path still has the last geometry.
    std::vector<SDL_Vertex> spec_verts;
    std::vector<int> spec_idx;

    Markers markers;
    CursorTracker cursor;
    Trigger trigger;

    float peak_db = -140.0F;

    // The waterfall age axis held at the freeze: live, its labels would keep
    // growing on a frozen snapshot.
    double frozen_wf_top_seconds = 0.0;

    FrameTiming frame_timing;
    using timing_clock = FrameTiming::clock;

    // The metadata of an armed export, stashed because the readback lands one
    // iteration later. Drained at the top of the loop.
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

    // --max-fps; 0 is uncapped (vsync alone). The sleep_until at the loop top
    // paces every path. Windows' timer granularity can undershoot the cap.
    const auto frame_interval =
        config.max_fps > 0
            ? std::chrono::duration_cast<timing_clock::duration>(
                  std::chrono::duration<double>(1.0 / config.max_fps))
            : timing_clock::duration::zero();
    auto next_frame_time = timing_clock::now();

    while (g_running.load(std::memory_order_relaxed)) {
      // Reset the schedule after a heavy frame: no burst to catch up.
      if (frame_interval != timing_clock::duration::zero()) {
        std::this_thread::sleep_until(next_frame_time);
        next_frame_time += frame_interval;
        auto const now = timing_clock::now();
        if (next_frame_time < now) {
          next_frame_time = now;
        }
      }

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

      if (!renderer.poll_events(&control_state)) {
        g_running = false;
        break;
      }

      if (control_state.window_changed()) {
        signal_processor.set_window(control_state.get_window());
        fft_analyzer.set_window_coherent_gain(
            SignalProcessor::get_coherent_gain(control_state.get_window()));
        control_state.clear_window_change_flag();
        LOG_INFO("Window function changed to: " +
                 std::string(SignalProcessor::window_function_to_string(
                     control_state.get_window())));
      }

      if (control_state.palette_changed()) {
        const auto map = kPaletteMap[control_state.get_palette_index()];
        spectrum_display.set_color_map(map);
        waterfall_display.set_color_map(map);
        control_state.clear_palette_change_flag();
      }

      // The display resets only on a transition, so this is free every frame.
      spectrum_display.set_max_hold(control_state.max_hold_enabled());
      spectrum_display.set_averaging(control_state.averaging_enabled());
      if (control_state.trace_reset_requested()) {
        control_state.clear_trace_reset();
        spectrum_display.reset_traces();
      }

      if (control_state.fft_size_changed()) {
        size_t const new_fft_size = control_state.get_fft_size();
        control_state.clear_fft_change_flag();

        LOG_INFO("Reinitializing with FFT size: " +
                 std::to_string(new_fft_size));

        current_fft_size = new_fft_size;

        // The queue is reconfigured with the source stopped.
        source->stop();
        sample_queue.reconfigure(current_fft_size);
        source->start(current_fft_size);

        signal_processor = SignalProcessor(current_fft_size);
        signal_processor.set_window(control_state.get_window());

        fft_analyzer = FftAnalyzer(current_fft_size);
        fft_analyzer.set_window_coherent_gain(
            SignalProcessor::get_coherent_gain(control_state.get_window()));

        waterfall_display.reset();
      }

      if (control_state.iq_logging_toggle_requested()) {
        control_state.clear_iq_logging_toggle();
        if (iq_logger.is_capturing()) {
          stop_capture();
          LOG_INFO("IQ logging stopped");
        } else {
          start_capture(current_fft_size, "Manual capture");
        }
      }

      // The renderer reads back the next composited frame; the drain at the
      // loop top writes the PNG. The metadata is stashed now so it matches the
      // frame even if a control changes before the readback.
      if (control_state.spectrogram_export_requested()) {
        control_state.clear_spectrogram_export();
        pending_export.armed = true;
        pending_export.center_hz = control_state.get_frequency();
        pending_export.rate_hz = effective_rate;
        pending_export.gain_db = control_state.get_gain();
        pending_export.fft_size = current_fft_size;
        pending_export.window = SignalProcessor::window_function_to_string(
            control_state.get_window());
        // Lower case: the sidecar says "jet".
        pending_export.color_map =
            ControlState::palette_name(control_state.get_palette_index());
        std::ranges::transform(pending_export.color_map,
                               pending_export.color_map.begin(), [](char c) {
                                 return static_cast<char>(std::tolower(
                                     static_cast<unsigned char>(c)));
                               });
        renderer.request_capture();
      }

      // Ctrl+left-click on either pane recentres there. Ignored while frozen,
      // which would relabel the held frame; playback's pinned range makes it
      // a no-op.
      if (auto click = renderer.take_tune(); click && !trigger.frozen) {
        const PlotSpan span{static_cast<double>(control_state.get_frequency()),
                            static_cast<double>(effective_rate),
                            static_cast<float>(spectrum_display.width())};
        const auto plot_h = static_cast<float>(spectrum_display.height() +
                                               waterfall_display.height());
        if (click->x >= 0.0F && click->x < span.width && click->y >= 0.0F &&
            click->y < plot_h) {
          uint64_t const prev = control_state.get_frequency();
          control_state.set_frequency(span.tune_at(click->x));
          if (control_state.get_frequency() != prev) {
            LOG_INFO("[TUNE] " + format_frequency(prev) + " -> " +
                     format_frequency(control_state.get_frequency()));
            control_state.mark_status_dirty();
          }
        }
      }

      // Playback has no tuner: the keys only move the labels.
      if (source->tunable()) {
        static uint64_t s_last_freq = 0;
        uint64_t const now_freq = control_state.get_frequency();
        control_state.apply_to_device(*source);
        if (s_last_freq != 0 && s_last_freq != now_freq) {
          // Drop the queued frames of the old channel so the spectrum snaps.
          sample_queue.clear();
        }
        s_last_freq = now_freq;
      }

      if (control_state.status_changed()) {
        auto sf = control_state.get_status_fields();
        renderer.set_status_fields(
            {sf.freq, sf.gain, sf.fft, sf.window, sf.palette, sf.trace});
        control_state.clear_status_dirty();
      }

      // Every frame: the panel caches text by string.
      if (iq_logger.is_capturing() && iq_logger.write_failed()) {
        // A short write (disk full) ended the writing.
        stop_capture();
        iq_capture_failed = true;
        LOG_ERROR("IQ capture ended early on a write error");
      }
      if (iq_logger.is_capturing()) {
        auto const seconds = std::chrono::duration_cast<std::chrono::seconds>(
            std::chrono::steady_clock::now() - iq_capture_start);
        renderer.render_iq_status("REC " + std::to_string(seconds.count()) +
                                  "s");
      } else {
        // Idle, or the last capture failed (stays until the next starts).
        renderer.render_iq_status(iq_capture_failed ? "REC ERR" : "");
      }

      // Unconditionally: the renderer hides it (< -140 dBFS) when the signal
      // drops, instead of freezing at the last peak.
      renderer.render_peak_indicator(peak_db);

      renderer.render_frequency_scale(control_state.get_frequency(),
                                      effective_rate,
                                      spectrum_display.height());

      FrameTiming::Averages const &perf = frame_timing.averages();
      renderer.set_timing_overlay(perf.fps, perf.cpu_ms, perf.render_build_ms,
                                  perf.present_ms);

      // Left-margin axes: the dB range and the age of the oldest waterfall row.
      {
        double wf_top_seconds = frozen_wf_top_seconds;
        if (!trigger.frozen) {
          // 0 while history is too short: the time axis stays hidden.
          WaterfallDisplay::CursorTime oldest;
          wf_top_seconds = waterfall_display.sample_at_y(0.0F, oldest)
                               ? oldest.seconds_ago
                               : 0.0;
          frozen_wf_top_seconds = wf_top_seconds;
        }
        renderer.set_left_axes(true, spectrum_display.min_db(),
                               spectrum_display.max_db(), wf_top_seconds);
      }

      // Markers, cursor readout and amplitude trigger
      {
        const PlotSpan span{static_cast<double>(control_state.get_frequency()),
                            static_cast<double>(effective_rate),
                            static_cast<float>(spectrum_display.width())};
        const auto spec_h = static_cast<float>(spectrum_display.height());
        const float db_min = spectrum_display.min_db();
        const float db_max = spectrum_display.max_db();

        markers.apply(renderer.take_clicks(), renderer.take_clear_markers(),
                      span,
                      spec_h + static_cast<float>(waterfall_display.height()));
        renderer.set_markers(markers.draw_list(span, spectrum_display));

        // Every frame, on both the sample and no-sample paths.
        renderer.set_cursor_readout(cursor.update(
            renderer.cursor_active(), renderer.cursor_x(), renderer.cursor_y(),
            span, spectrum_display, waterfall_display));

        if (auto set_y = renderer.take_trigger_set()) {
          trigger.set_from_y(*set_y, spec_h, db_min, db_max);
        }
        // Space resumes and re-arms. It always repaints the status bar: that
        // dismisses a transient message, bypassing the dirty flag.
        if (control_state.unfreeze_requested()) {
          control_state.clear_unfreeze();
          trigger.frozen = false;
          control_state.mark_status_dirty();
        }
        renderer.set_trigger(trigger.armed,
                             trigger.line_y(spec_h, db_min, db_max),
                             trigger.threshold_db, trigger.frozen);
      }

      // If frozen, skip dequeue and processing, keep presenting so the overlays
      // and input stay live. The bounded queue catches up on resume.
      if (trigger.frozen) {
        renderer.present_frame();
        continue;
      }

      FrameHandle async_samples_frame =
          sample_queue.pop_newest(std::chrono::milliseconds(8));
      bool const got_samples = static_cast<bool>(async_samples_frame);

      // Overlays are part of the render call: without it keys would show
      // nothing while no samples arrive.
      if (!got_samples) {
        renderer.present_frame();
        continue;
      }

      std::span<std::complex<float>> samples(async_samples_frame.data(),
                                             current_fft_size);

      auto t_cpu_start = timing_clock::now();
      openspectrum::SignalProcessor::remove_dc(samples);
      signal_processor.apply_window(samples);

      fft_analyzer.execute(samples);
      peak_db = fft_analyzer.get_max_db();
      trigger.check(peak_db);

      const auto &db_spectrum = fft_analyzer.get_db_spectrum();
      spectrum_display.update_spectrum(db_spectrum);
      waterfall_display.add_spectrum_line(db_spectrum);

      auto t_cpu_end = timing_clock::now();

      spectrum_display.build_vertices(
          static_cast<float>(renderer.plot_width()),
          static_cast<float>(spectrum_display.height()), spec_verts, spec_idx);
      const auto &wf_dirty_rects = waterfall_display.get_dirty_rects();

      auto t_render_start = timing_clock::now();
      bool render_ok;
      if (waterfall_display.needs_full_render()) {
        // After a reset or LUT rebuild; also seeds the scroll texture.
        render_ok = renderer.render_displays(
            spec_verts, spec_idx, waterfall_display.get_pixels().data(),
            spectrum_display.height(), wf_dirty_rects);
      } else {
        // Steady state: one new line.
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

      frame_timing.add(FrameTiming::ms(t_cpu_start, t_cpu_end),
                       FrameTiming::ms(t_render_start, t_render_end),
                       renderer.last_present_ms());

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

    source->stop();

    // The producer is stopped, so no callback races this.
    if (iq_logger.is_capturing()) {
      stop_capture();
    }

    // Only on a clean exit (Esc, window close, Ctrl+C): a failed start or a
    // crash keeps the last good file.
    store_settings(settings, control_state, config);
    if (!save_settings(settings_file, settings)) {
      LOG_WARNING("Settings could not be saved");
    }

    LOG_INFO("Main loop exited cleanly");
  } catch (const std::exception &e) {
    LOG_ERROR("Fatal error: " + std::string(e.what()));
    return 1;
  }

  LOG_INFO("Shutdown complete");
  return 0;
}
