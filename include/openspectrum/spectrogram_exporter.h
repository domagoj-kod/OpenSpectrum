// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstddef>
#include <cstdint>
#include <mutex>
#include <string>

namespace openspectrum {

// Configuration for spectrogram export
struct SpectrogramExportConfig {
  std::string output_directory = "spectrograms";
  std::string filename_prefix = "spectrogram";
  bool include_metadata = true;  // Generate JSON metadata file
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
  explicit SpectrogramExporter(
      const SpectrogramExportConfig &config = SpectrogramExportConfig());
  ~SpectrogramExporter() = default;

  // Non-copyable, non-movable (mutex)
  SpectrogramExporter(const SpectrogramExporter &) = delete;
  SpectrogramExporter &operator=(const SpectrogramExporter &) = delete;

  // Export a pre-composited frame: tightly packed RGBA captured straight from
  // the renderer (base spectrum + waterfall plus the dB/time axis strip,
  // frequency scale, and HUD overlays). The caller has already stitched and
  // annotated the image, so this only writes the PNG and its metadata sidecar.
  ExportResult export_framebuffer(const uint8_t *rgba, size_t width,
                                  size_t height, uint32_t center_freq_hz,
                                  uint32_t sample_rate_hz, float gain_db,
                                  size_t fft_size,
                                  const std::string &window_function,
                                  const std::string &color_map = "jet",
                                  const std::string &notes = "");

  // Get current configuration
  const SpectrogramExportConfig &get_config() const { return m_config; }

private:
  // Internal helper methods
  bool create_output_directory();
  // Build a filename using a caller-provided timestamp so the PNG name and
  // the JSON's export_timestamp_iso8601 field always agree, even if the
  // wall clock crosses a second between the two reads.
  std::string generate_filename(const std::string &extension,
                                const std::string &iso8601_timestamp) const;
  // Derive the metadata filename from an already-generated PNG name so the
  // pair shares the same timestamp/sequence even if export crosses a second.
  static std::string metadata_filename_for(const std::string &png_filename);
  ExportResult write_png(const std::string &filename, const uint8_t *data,
                         int width, int height, int stride_bytes);
  void write_metadata(const std::string &filename, int image_width,
                      int image_height, const std::string &image_type,
                      uint32_t center_freq_hz, uint32_t sample_rate_hz,
                      float gain_db, size_t fft_size,
                      const std::string &window_function,
                      const std::string &color_map, const std::string &notes,
                      const std::string &iso8601_timestamp);

  SpectrogramExportConfig m_config;
  mutable std::mutex m_mutex;
  uint32_t m_export_count = 0; // For unique filenames on multiple exports
};

} // namespace openspectrum
