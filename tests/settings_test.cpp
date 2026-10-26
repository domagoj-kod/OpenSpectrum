// SPDX-License-Identifier: GPL-3.0-or-later
// Saved settings: a round trip through the file, junk skipped, receivers kept
// apart, a NaN gain dropped.

#undef NDEBUG // these are asserts; they must not compile out
#include "control_state.h"
#include "utils/settings.h"

#include <cassert>
#include <filesystem>
#include <fstream>

using namespace openspectrum;

auto main() -> int {
  namespace fs = std::filesystem;
  const fs::path dir = fs::temp_directory_path() / "openspectrum-settings-test";
  fs::remove_all(dir);
  const fs::path path = dir / "sub" / "settings.ini";

  // Missing file: empty, and the defaults stay.
  assert(load_settings(path).empty());
  AppConfig config;
  apply_settings({}, config);
  assert(config.fft_size == 4096 && config.center_freq_hz == 92600000.0);

  // A session on the RTL, saved into a folder that does not exist yet.
  ControlState state;
  state.set_frequency(144800000);
  state.set_gain(28.7F);
  state.set_fft_size(16384);
  state.set_window(WindowFunction::FLAT_TOP);
  state.set_palette(2);
  Settings settings;
  store_settings(settings, state, config);
  assert(save_settings(path, settings) &&
         !fs::exists(dir / "sub/settings.ini.tmp"));

  // Read back onto fresh defaults: every value returns.
  AppConfig rtl;
  apply_settings(load_settings(path), rtl);
  assert(rtl.center_freq_hz == 144800000.0 && rtl.gain_db == 28.7F);
  assert(rtl.fft_size == 16384 &&
         rtl.window_function == WindowFunction::FLAT_TOP);
  assert(rtl.palette_index == 2);

  // The USRP and playback do not take the RTL's frequency and gain; the view
  // settings are shared.
  AppConfig uhd;
  uhd.use_uhd = true;
  apply_settings(load_settings(path), uhd);
  assert(uhd.center_freq_hz == 92600000.0 && uhd.gain_db == 10.0F);
  assert(uhd.fft_size == 16384);
  AppConfig play;
  play.play_file = "x.iq";
  apply_settings(load_settings(path), play);
  assert(play.center_freq_hz == 92600000.0 && play.palette_index == 2);

  // A USRP save keeps the RTL's keys; a playback save adds none.
  Settings both = load_settings(path);
  ControlState usrp;
  usrp.set_frequency(1296000000); // in the default (RTL) range
  store_settings(both, usrp, uhd);
  store_settings(both, usrp, play);
  assert(both.at("rtl.freq") == "144800000" &&
         both.at("uhd.freq") == "1296000000");
  assert(!both.contains("freq") && both.size() == 7);

  // Junk, CRLF, unknown keys, an unsupported FFT size and an unknown window or
  // palette are skipped; what parses is taken.
  {
    std::ofstream out(path, std::ios::binary | std::ios::trunc);
    out << "# comment\r\nno equals sign\r\n=nokey\r\nfft=4097\r\n"
           "window=kaiser\r\npalette=PINK\r\nrtl.freq=12abc\r\n"
           "rtl.gain=33.5\r\nfuture.key=1\r\n";
  }
  AppConfig junk;
  apply_settings(load_settings(path), junk);
  assert(junk.fft_size == 4096 &&
         junk.window_function == WindowFunction::BLACKMAN_HARRIS);
  assert(junk.palette_index == 0 && junk.center_freq_hz == 92600000.0);
  assert(junk.gain_db == 33.5F);

  // from_chars reads "nan", and NaN passes std::clamp: set_gain, where every
  // gain lands, drops it, so it neither reaches the receiver nor comes back.
  {
    std::ofstream out(path, std::ios::binary | std::ios::trunc);
    out << "rtl.gain=nan\n";
  }
  AppConfig nan_gain;
  apply_settings(load_settings(path), nan_gain);
  ControlState held;
  held.set_gain(nan_gain.gain_db);
  assert(held.get_gain() == 20.0F); // ControlState's default
  Settings resaved;
  store_settings(resaved, held, nan_gain);
  assert(resaved.at("rtl.gain") == "20");

  // The --window names, any case, '_' for '-'.
  assert(parse_window_name("Flat_Top") == WindowFunction::FLAT_TOP);
  assert(!parse_window_name("B-Harris"));
  assert(parse_window_name(window_name(WindowFunction::HANN)) ==
         WindowFunction::HANN);

  assert(!save_settings({}, settings));
  fs::remove_all(dir);
  return 0;
}
