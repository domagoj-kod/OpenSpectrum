// SPDX-License-Identifier: MIT
#pragma once

#include <cstddef>
#include <cstdint>
#include <mutex>
#include <string>

#include "spectrum_display.h"

namespace openspectrum {

// Configuration for spectrogram export
struct SpectrogramExportConfig {
  std::string output_directory = "spectrograms";
  std::string filename_prefix = "spectrogram";
  bool include_metadata = true; // Generate JSON metadata file
  int png_compression_level = 8; // 0-9, 9 = maximum compression
};

// Result of an export operation
struct ExportResult {
  bool success = false;
  std::string filename;
  std::string metadata_filename;
  std::string error_message;
};

// Spectrogram exporter for PNG and metadata files
// Handles exporting spectrum and waterfall displays as PNG images
// with optional JSON metadata for ML analysis
class SpectrogramExporter {
public:
  explicit SpectrogramExporter(const SpectrogramExportConfig &config = SpectrogramExportConfig());
  ~SpectrogramExporter() = default;

  // Non-copyable, non-movable (mutex)
  SpectrogramExporter(const SpectrogramExporter &) = delete;
  SpectrogramExporter &operator=(const SpectrogramExporter &) = delete;

  // Export combined spectrogram (spectrum + waterfall as single image)
  ExportResult export_combined(
      const PixelBuffer &spectrum_pixels,
      const PixelBuffer &waterfall_pixels,
      size_t display_width,
      size_t spectrum_height,
      size_t waterfall_height,
      uint32_t center_freq_hz,
      uint32_t sample_rate_hz,
      float gain_db,
      size_t fft_size,
      const std::string &window_function,
      const std::string &color_map = "jet",
      const std::string &notes = "");

  // Export spectrum only
  ExportResult export_spectrum(
      const PixelBuffer &pixels,
      size_t width,
      size_t height,
      uint32_t center_freq_hz,
      uint32_t sample_rate_hz,
      float gain_db,
      size_t fft_size,
      const std::string &window_function,
      const std::string &color_map = "jet",
      const std::string &notes = "");

  // Export waterfall only
  ExportResult export_waterfall(
      const PixelBuffer &pixels,
      size_t width,
      size_t height,
      uint32_t center_freq_hz,
      uint32_t sample_rate_hz,
      float gain_db,
      size_t fft_size,
      const std::string &window_function,
      const std::string &color_map = "jet",
      const std::string &notes = "");

  // Get current configuration
  const SpectrogramExportConfig &get_config() const { return m_config; }

  // Set configuration
  void set_config(const SpectrogramExportConfig &config);

private:
  // Internal helper methods
  bool create_output_directory();
  std::string generate_filename(const std::string &extension) const;
  std::string generate_metadata_filename() const;
  ExportResult write_png(const std::string &filename, const uint8_t *data,
                         int width, int height, int stride_bytes);
  void write_metadata(
      const std::string &filename,
      int image_width,
      int image_height,
      const std::string &image_type,
      uint32_t center_freq_hz,
      uint32_t sample_rate_hz,
      float gain_db,
      size_t fft_size,
      const std::string &window_function,
      const std::string &color_map,
      const std::string &notes);
  std::string escape_json_string(const std::string &str) const;
  std::string get_iso8601_timestamp() const;
  double get_unix_timestamp() const;

  SpectrogramExportConfig m_config;
  mutable std::mutex m_mutex;
  uint32_t m_export_count = 0; // For unique filenames on multiple exports
};

} // namespace openspectrum
