// SPDX-License-Identifier: GPL-3.0-or-later

#include "openspectrum/spectrogram_exporter.h"

#include "format.h"
#include "time_utils.h"

#include <cstdio>
#include <ctime>
#include <iomanip>
#include <sstream>
#include <sys/stat.h>
#include <sys/types.h>

// Build version stamped into exported metadata (see Makefile VERSION_DEF).
#ifndef OPENSPECTRUM_VERSION
#define OPENSPECTRUM_VERSION dev
#endif
#define OS_STRINGIFY2(x) #x
#define OS_STRINGIFY(x) OS_STRINGIFY2(x)

// stb_image_write keeps its implementation block outside the include guard,
// so one #include with STB_IMAGE_WRITE_IMPLEMENTATION defined is sufficient
// to get both the declarations and the implementation in this TU.
#define STB_IMAGE_WRITE_IMPLEMENTATION
#include "stb_image_write.h"

#ifdef _WIN32
#include <direct.h>
#define mkdir(dir, mode) _mkdir(dir)
#else
#include <unistd.h>
#endif

namespace openspectrum {

SpectrogramExporter::SpectrogramExporter(const SpectrogramExportConfig &config)
    : m_config(config) {}

// Create output directory if it doesn't exist
bool SpectrogramExporter::create_output_directory() {
  if (m_config.output_directory.empty()) {
    return true;
  }

  struct stat info;
  if (stat(m_config.output_directory.c_str(), &info) == 0 &&
      (info.st_mode & S_IFDIR)) {
    return true;
  }

  // Try to create directory
#ifdef _WIN32
  if (_mkdir(m_config.output_directory.c_str()) == 0) {
    return true;
  }
#else
  if (mkdir(m_config.output_directory.c_str(), 0755) == 0) {
    return true;
  }
#endif

  return false;
}

// Generate ISO 8601 timestamp
std::string SpectrogramExporter::get_iso8601_timestamp() const {
  auto now = std::chrono::system_clock::now();
  auto in_time_t = std::chrono::system_clock::to_time_t(now);

  std::stringstream ss;
  std::tm const tm = safe_gmtime(in_time_t);
  ss << std::put_time(&tm, "%Y%m%dT%H%M%SZ");
  return ss.str();
}

// Get current Unix timestamp with microseconds
double SpectrogramExporter::get_unix_timestamp() const {
  auto now = std::chrono::system_clock::now();
  auto duration = now.time_since_epoch();
  return static_cast<double>(
             std::chrono::duration_cast<std::chrono::microseconds>(duration)
                 .count()) /
         1000000.0;
}

// Generate filename with timestamp and optional frequency
std::string SpectrogramExporter::generate_filename(
    const std::string &extension,
    const std::string &iso8601_timestamp) const {
  std::string filename = m_config.filename_prefix + "_" + iso8601_timestamp;

  // Prepend output directory
  if (!m_config.output_directory.empty()) {
    filename = m_config.output_directory + "/" + filename;
  }

  // Add export count for multiple exports in same second
  if (m_export_count > 0) {
    filename += "_" + std::to_string(m_export_count);
  }

  filename += "." + extension;

  return filename;
}

std::string SpectrogramExporter::metadata_filename_for(
    const std::string &png_filename) {
  size_t const last_dot = png_filename.find_last_of('.');
  if (last_dot != std::string::npos) {
    return png_filename.substr(0, last_dot) + ".meta.json";
  }
  return png_filename + ".meta.json";
}

// Escape string for JSON output
std::string
SpectrogramExporter::escape_json_string(const std::string &str) const {
  std::string result;
  result.reserve(str.size() * 2);
  for (char c : str) {
    switch (c) {
    case '"':
      result += "\\\"";
      break;
    case '\\':
      result += "\\\\";
      break;
    case '\b':
      result += "\\b";
      break;
    case '\f':
      result += "\\f";
      break;
    case '\n':
      result += "\\n";
      break;
    case '\r':
      result += "\\r";
      break;
    case '\t':
      result += "\\t";
      break;
    default:
      if (c >= 0 && c < 32) {
        // Skip control characters
      } else {
        result += c;
      }
    }
  }
  return result;
}

// Write PNG file using stb_image_write
ExportResult SpectrogramExporter::write_png(const std::string &filename,
                                            const uint8_t *data, int width,
                                            int height, int stride_bytes) {
  ExportResult result;

  if (data == nullptr || width <= 0 || height <= 0) {
    result.error_message = "Invalid PNG data: null data or zero dimensions";
    return result;
  }

  // stbi_write_png_compression_level is a process-wide global. Set/restore
  // is safe within a single exporter (member mutex serialises calls) but
  // races across exporter instances. Serialise the global window here.
  static std::mutex s_stbi_compression_mutex;
  int success = 0;
  {
    std::lock_guard<std::mutex> lock(s_stbi_compression_mutex);
    int previous_compression = stbi_write_png_compression_level;
    stbi_write_png_compression_level = m_config.png_compression_level;

    // Write PNG file (RGBA = 4 components)
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

// Write JSON metadata file
void SpectrogramExporter::write_metadata(
    const std::string &filename, int image_width, int image_height,
    const std::string &image_type, uint32_t center_freq_hz,
    uint32_t sample_rate_hz, float gain_db, size_t fft_size,
    const std::string &window_function, const std::string &color_map,
    const std::string &notes, const std::string &iso8601_timestamp) {

  std::stringstream ss;
  double timestamp = get_unix_timestamp();

  ss << "{\n";
  ss << "  \"version\": \"1.0\",\n";
  ss << R"(  "export_timestamp_iso8601": ")" << iso8601_timestamp << "\",\n";
  ss << "  \"export_timestamp_unix\": " << timestamp << ",\n";

  // Capture parameters
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

  // Image parameters
  ss << "  \"image\": {\n";
  ss << R"(    "type": ")" << image_type << "\",\n";
  ss << "    \"width\": " << image_width << ",\n";
  ss << "    \"height\": " << image_height << ",\n";
  ss << "    \"format\": \"PNG\",\n";
  ss << R"(    "color_map": ")" << escape_json_string(color_map) << "\",\n";
  ss << "    \"compression_level\": " << m_config.png_compression_level << "\n";
  ss << "  },\n";

  // Application info
  ss << "  \"application\": {\n";
  ss << "    \"name\": \"OpenSpectrum\",\n";
  ss << "    \"version\": \"" << OS_STRINGIFY(OPENSPECTRUM_VERSION) << "\",\n";
#ifdef _WIN32
  ss << "    \"platform\": \"Windows\"\n";
#elif __APPLE__
  ss << "    \"platform\": \"macOS\"\n";
#else
  ss << "    \"platform\": \"Linux\"\n";
#endif
  ss << "  }" << (notes.empty() ? "\n" : ",\n");

  // Notes
  if (!notes.empty()) {
    ss << R"(  "notes": ")" << escape_json_string(notes) << "\"\n";
  }

  ss << "}";

  // Write to file
  std::string json = ss.str();
  FILE *f = std::fopen(filename.c_str(), "w");
  if (f != nullptr) {
    std::fwrite(json.c_str(), 1, json.size(), f);
    std::fclose(f);
  }
}

ExportResult SpectrogramExporter::export_framebuffer(
    const uint8_t *rgba, size_t width, size_t height, uint32_t center_freq_hz,
    uint32_t sample_rate_hz, float gain_db, size_t fft_size,
    const std::string &window_function, const std::string &color_map,
    const std::string &notes) {

  std::lock_guard<std::mutex> lock(m_mutex);
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

  // Single timestamp read shared by the filename and the metadata field.
  std::string const iso_ts = get_iso8601_timestamp();
  std::string png_filename = generate_filename("png", iso_ts);

  result = write_png(png_filename, rgba, static_cast<int>(width),
                     static_cast<int>(height), stride);

  if (result.success && m_config.include_metadata) {
    std::string meta_filename = metadata_filename_for(png_filename);
    write_metadata(meta_filename, static_cast<int>(width),
                   static_cast<int>(height), "combined_axes", center_freq_hz,
                   sample_rate_hz, gain_db, fft_size, window_function,
                   color_map, notes, iso_ts);
    result.metadata_filename = meta_filename;
  }

  m_export_count++;
  return result;
}

} // namespace openspectrum
