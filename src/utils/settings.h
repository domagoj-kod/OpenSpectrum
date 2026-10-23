// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "utils/arg_parser.h"
#include "utils/attributes.h"

#include <filesystem>
#include <map>
#include <string>

namespace openspectrum {

class ControlState;

// The last session's settings as key=value lines: fft, window and palette,
// and freq/gain per receiver (rtl., rtl-hf., uhd.), whose ranges differ. A
// save keeps the other receivers' keys.
using Settings = std::map<std::string, std::string>;

// %APPDATA%\OpenSpectrum\settings.ini, or settings.ini under
// $XDG_CONFIG_HOME/openspectrum (default ~/.config); empty if neither is set.
OS_COLD std::filesystem::path settings_path();
// A missing file reads as empty; lines without a key and '=' are skipped.
OS_COLD Settings load_settings(const std::filesystem::path &path);
// Through a temporary file and a rename: a crash mid-write keeps the old file.
OS_COLD bool save_settings(const std::filesystem::path &path,
                           const Settings &settings);

// Saved values onto config; unparsable or unsupported ones are skipped, and
// ControlState clamps the rest to the receiver. Playback takes no freq/gain:
// its sidecar has them.
OS_COLD void apply_settings(const Settings &settings, AppConfig &config);
// The session's values back, under the keys apply_settings reads.
OS_COLD void store_settings(Settings &settings, const ControlState &state,
                            const AppConfig &config);

} // namespace openspectrum
