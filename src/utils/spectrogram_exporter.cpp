// SPDX-License-Identifier: GPL-3.0-or-later

#include "utils/spectrogram_exporter.h"

#include "utils/format.h"
#include "utils/time_utils.h"

#include <cstdio>
#include <filesystem>
#include <iomanip>
#include <mutex>
#include <sstream>

#define STB_IMAGE_WRITE_IMPLEMENTATION
#include "stb_image_write.h"

namespace openspectrum {

SpectrogramExporter::SpectrogramExporter(const SpectrogramExportConfig &config)
    : m_config(config) {}

bool SpectrogramExporter::create_output_directory() {
  if (m_config.output_directory.empty()) {
    return true;
  }
  std::error_code ec;
  std::filesystem::create_directories(m_config.output_directory, ec);
  return std::filesystem::is_directory(m_config.output_directory);
}

std::string SpectrogramExporter::generate_filename(
    const std::string &extension, const std::string &iso8601_timestamp) const {
  std::string filename = m_config.filename_prefix + "_" + iso8601_timestamp;

  if (!m_config.output_directory.empty()) {
    filename = m_config.output_directory + "/" + filename;
  }

  if (m_export_count > 0) {
    filename += "_" + std::to_string(m_export_count);
  }

  filename += "." + extension;

  return filename;
}

std::string
SpectrogramExporter::metadata_filename_for(const std::string &png_filename) {
  size_t const last_dot = png_filename.find_last_of('.');
  if (last_dot != std::string::npos) {
    return png_filename.substr(0, last_dot) + ".meta.json";
  }
  return png_filename + ".meta.json";
}

ExportResult SpectrogramExporter::write_png(const std::string &filename,
                                            const uint8_t *data, int width,
                                            int height, int stride_bytes) {
  ExportResult result;

  if (data == nullptr || width <= 0 || height <= 0) {
    result.error_message = "Invalid PNG data: null data or zero dimensions";
    return result;
  }

  // stbi_write_png_compression_level is a process-wide global, so the set and
  // restore are serialised across exporters here.
  static std::mutex s_stbi_compression_mutex;
  int success = 0;
  {
    std::lock_guard<std::mutex> lock(s_stbi_compression_mutex);
    int previous_compression = stbi_write_png_compression_level;
    stbi_write_png_compression_level = m_config.png_compression_level;

    success =
        stbi_write_png(filename.c_str(), width, height, 4, data, stride_bytes);

    stbi_write_png_compression_level = previous_compression;
  }

  if (success) {
    result.success = true;
    result.filename = filename;
  } else {
    result.error_message = "Failed to write PNG file: " + filename;
  }

  return result;
}

bool SpectrogramExporter::write_metadata(
    const std::string &filename, int image_width, int image_height,
    const std::string &image_type, uint64_t center_freq_hz,
    uint32_t sample_rate_hz, float gain_db, size_t fft_size,
    const std::string &window_function, const std::string &color_map,
    const std::string &notes, const std::string &iso8601_timestamp) {

  std::stringstream ss;
  // Fixed notation: six significant digits rounded a Unix time to 10000 s.
  ss << std::fixed << std::setprecision(3);
  double timestamp = get_unix_timestamp();

  ss << "{\n";
  ss << "  \"version\": \"1.0\",\n";
  ss << R"(  "export_timestamp_iso8601": ")" << iso8601_timestamp << "\",\n";
  ss << "  \"export_timestamp_unix\": " << timestamp << ",\n";

  ss << "  \"capture\": {\n";
  ss << "    \"center_frequency_hz\": " << center_freq_hz << ",\n";
  ss << R"(    "center_frequency_formatted": ")"
     << escape_json_string(format_frequency(center_freq_hz)) << "\",\n";
  ss << "    \"sample_rate_hz\": " << sample_rate_hz << ",\n";
  ss << "    \"gain_db\": " << gain_db << ",\n";
  ss << "    \"fft_size\": " << fft_size << ",\n";
  ss << R"(    "window_function": ")" << escape_json_string(window_function)
     << "\"\n";
  ss << "  },\n";

  ss << "  \"image\": {\n";
  ss << R"(    "type": ")" << image_type << "\",\n";
  ss << "    \"width\": " << image_width << ",\n";
  ss << "    \"height\": " << image_height << ",\n";
  ss << "    \"format\": \"PNG\",\n";
  ss << R"(    "color_map": ")" << escape_json_string(color_map) << "\",\n";
  ss << "    \"compression_level\": " << m_config.png_compression_level << "\n";
  ss << "  },\n";

  ss << "  \"application\": {\n";
  ss << "    \"name\": \"OpenSpectrum\",\n";
  ss << R"(    "version": ")" << OS_STRINGIFY(OPENSPECTRUM_VERSION) << "\",\n";
#ifdef _WIN32
  ss << "    \"platform\": \"Windows\"\n";
#elif __APPLE__
  ss << "    \"platform\": \"macOS\"\n";
#else
  ss << "    \"platform\": \"Linux\"\n";
#endif
  ss << "  }" << (notes.empty() ? "\n" : ",\n");

  if (!notes.empty()) {
    ss << R"(  "notes": ")" << escape_json_string(notes) << "\"\n";
  }

  ss << "}";

  return write_text_file(filename, ss.str());
}

ExportResult SpectrogramExporter::export_framebuffer(
    const uint8_t *rgba, size_t width, size_t height, uint64_t center_freq_hz,
    uint32_t sample_rate_hz, float gain_db, size_t fft_size,
    const std::string &window_function, const std::string &color_map,
    const std::string &notes) {

  ExportResult result;

  if (rgba == nullptr || width == 0 || height == 0) {
    result.error_message = "Empty framebuffer provided";
    return result;
  }

  if (!create_output_directory()) {
    result.error_message =
        "Failed to create output directory: " + m_config.output_directory;
    return result;
  }

  int stride = static_cast<int>(width * 4);

  // One timestamp read, shared by the filename and the metadata.
  std::string const iso_ts = get_iso8601_timestamp();
  std::string png_filename = generate_filename("png", iso_ts);

  result = write_png(png_filename, rgba, static_cast<int>(width),
                     static_cast<int>(height), stride);

  if (result.success && m_config.include_metadata) {
    std::string meta_filename = metadata_filename_for(png_filename);
    if (write_metadata(meta_filename, static_cast<int>(width),
                       static_cast<int>(height), "combined_axes",
                       center_freq_hz, sample_rate_hz, gain_db, fft_size,
                       window_function, color_map, notes, iso_ts)) {
      result.metadata_filename = meta_filename;
    } else {
      // The PNG stands; only its sidecar is missing.
      result.error_message = "Cannot write metadata: " + meta_filename;
    }
  }

  m_export_count++;
  return result;
}

} // namespace openspectrum
