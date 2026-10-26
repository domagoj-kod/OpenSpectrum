// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstddef>
#include <cstdint>
#include <string>

namespace openspectrum {

struct SpectrogramExportConfig {
  std::string output_directory = "spectrograms";
  std::string filename_prefix = "spectrogram";
  bool include_metadata = true;
  int png_compression_level = 8; // 0-9, 9 = maximum compression
};

struct ExportResult {
  bool success = false;
  std::string filename;
  std::string metadata_filename;
  std::string error_message; // with success: the PNG, but not its sidecar
};

// Writes a PNG of the display and a JSON metadata sidecar.
class SpectrogramExporter {
public:
  explicit SpectrogramExporter(
      const SpectrogramExportConfig &config = SpectrogramExportConfig());
  ~SpectrogramExporter() = default;

  SpectrogramExporter(const SpectrogramExporter &) = delete;
  SpectrogramExporter &operator=(const SpectrogramExporter &) = delete;

  // Writes a frame the renderer has already composited: tightly packed RGBA,
  // overlays included.
  ExportResult export_framebuffer(const uint8_t *rgba, size_t width,
                                  size_t height, uint64_t center_freq_hz,
                                  uint32_t sample_rate_hz, float gain_db,
                                  size_t fft_size,
                                  const std::string &window_function,
                                  const std::string &color_map = "jet",
                                  const std::string &notes = "");

  const SpectrogramExportConfig &get_config() const { return m_config; }

private:
  bool create_output_directory();
  // The caller passes the timestamp so the PNG name and the JSON field agree.
  std::string generate_filename(const std::string &extension,
                                const std::string &iso8601_timestamp) const;
  // From the PNG name, so the pair shares timestamp and sequence.
  static std::string metadata_filename_for(const std::string &png_filename);
  ExportResult write_png(const std::string &filename, const uint8_t *data,
                         int width, int height, int stride_bytes);
  bool write_metadata(const std::string &filename, int image_width,
                      int image_height, const std::string &image_type,
                      uint64_t center_freq_hz, uint32_t sample_rate_hz,
                      float gain_db, size_t fft_size,
                      const std::string &window_function,
                      const std::string &color_map, const std::string &notes,
                      const std::string &iso8601_timestamp);

  SpectrogramExportConfig m_config;
  uint32_t m_export_count = 0;
};

} // namespace openspectrum
