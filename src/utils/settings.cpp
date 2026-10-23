// SPDX-License-Identifier: GPL-3.0-or-later

#include "utils/settings.h"
#include "control_state.h"

#include <algorithm>
#include <charconv>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <fstream>
#include <string>
#include <system_error>

namespace openspectrum {

namespace fs = std::filesystem;

namespace {

// The whole string must be the number.
template <typename T>
auto parse_full(const std::string &str, T &value) -> bool {
  const char *end = str.data() + str.size();
  auto [ptr, ec] = std::from_chars(str.data(), end, value);
  return ec == std::errc{} && ptr == end;
}

// Shortest text that reads back to the same value.
template <typename T> auto to_text(T value) -> std::string {
  char buf[32];
  auto [ptr, ec] = std::to_chars(buf, buf + sizeof(buf), value);
  return {buf, ptr};
}

// Empty for playback, which has no receiver. Direct sampling narrows the RTL
// to HF, so it keeps its own frequency.
auto receiver_prefix(const AppConfig &config) -> std::string {
  if (!config.play_file.empty()) {
    return "";
  }
  if (config.use_uhd) {
    return "uhd.";
  }
  return config.direct_sampling ? "rtl-hf." : "rtl.";
}

} // namespace

auto settings_path() -> fs::path {
#ifdef _WIN32
  // Wide: a user name outside the ANSI code page survives.
  const wchar_t *appdata = _wgetenv(L"APPDATA");
  if (appdata == nullptr || *appdata == L'\0') {
    return {};
  }
  return fs::path(appdata) / "OpenSpectrum" / "settings.ini";
#else
  const char *xdg = std::getenv("XDG_CONFIG_HOME");
  if (xdg != nullptr && *xdg != '\0') {
    return fs::path(xdg) / "openspectrum" / "settings.ini";
  }
  const char *home = std::getenv("HOME");
  if (home == nullptr || *home == '\0') {
    return {};
  }
  return fs::path(home) / ".config" / "openspectrum" / "settings.ini";
#endif
}

auto load_settings(const fs::path &path) -> Settings {
  Settings settings;
  std::ifstream in(path);
  std::string line;
  while (std::getline(in, line)) {
    if (!line.empty() && line.back() == '\r') {
      line.pop_back(); // CRLF read on Linux
    }
    size_t const eq = line.find('=');
    if (line.starts_with('#') || eq == std::string::npos || eq == 0) {
      continue;
    }
    settings[line.substr(0, eq)] = line.substr(eq + 1);
  }
  return settings;
}

auto save_settings(const fs::path &path, const Settings &settings) -> bool {
  if (path.empty()) {
    return false;
  }
  std::error_code ec;
  fs::create_directories(path.parent_path(), ec);
  fs::path tmp = path;
  tmp += ".tmp";
  std::ofstream out(tmp, std::ios::trunc);
  out << "# OpenSpectrum: the last session's settings, rewritten on exit\n";
  for (const auto &[key, value] : settings) {
    out << key << '=' << value << '\n';
  }
  out.close();
  if (out.fail()) {
    fs::remove(tmp, ec);
    return false;
  }
  fs::rename(tmp, path, ec); // replaces the old file, on Windows too
  return !ec;
}

void apply_settings(const Settings &settings, AppConfig &config) {
  auto get = [&settings](const std::string &key) -> const std::string * {
    auto it = settings.find(key);
    return it == settings.end() ? nullptr : &it->second;
  };

  if (const std::string *v = get("fft")) {
    const DeviceConstraints limits;
    size_t n = 0;
    if (parse_full(*v, n) && std::ranges::find(limits.supported_fft_sizes, n) !=
                                 limits.supported_fft_sizes.end()) {
      config.fft_size = n;
    }
  }
  if (const std::string *v = get("window")) {
    if (auto w = parse_window_name(*v)) {
      config.window_function = *w;
    }
  }
  if (const std::string *v = get("palette")) {
    for (size_t i = 0; i < ControlState::PALETTE_COUNT; ++i) {
      if (*v == ControlState::palette_name(i)) {
        config.palette_index = i;
      }
    }
  }

  std::string const rx = receiver_prefix(config);
  if (rx.empty()) {
    return;
  }
  if (const std::string *v = get(rx + "freq")) {
    uint64_t hz = 0;
    if (parse_full(*v, hz)) {
      config.center_freq_hz = static_cast<double>(hz);
    }
  }
  if (const std::string *v = get(rx + "gain")) {
    float db = 0.0F;
    if (parse_full(*v, db)) {
      config.gain_db = db;
    }
  }
}

void store_settings(Settings &settings, const ControlState &state,
                    const AppConfig &config) {
  settings["fft"] = std::to_string(state.get_fft_size());
  settings["window"] = window_name(state.get_window());
  settings["palette"] = ControlState::palette_name(state.get_palette_index());
  std::string const rx = receiver_prefix(config);
  if (!rx.empty()) {
    settings[rx + "freq"] = std::to_string(state.get_frequency());
    settings[rx + "gain"] = to_text(state.get_gain());
  }
}

} // namespace openspectrum
