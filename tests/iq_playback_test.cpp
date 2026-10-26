// SPDX-License-Identifier: GPL-3.0-or-later
// What the capture writer puts in a sidecar, playback reads back; and a
// capture past 2 GiB opens: its size must not pass through a long, which is
// 32 bits on Windows and made --play call such a file empty.

#undef NDEBUG // these are asserts; they must not compile out
#include "hardware/iq_playback.h"
#include "utils/iq_logger.h"

#include <cassert>
#include <complex>
#include <cstdint>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <system_error>
#include <vector>

using namespace openspectrum;

auto main() -> int {
  namespace fs = std::filesystem;

  // A key or format string renamed on one side would make --play fall back
  // to the command line's frequency and rate, or to cf32, without a word.
  for (IqSampleFormat const format :
       {IqSampleFormat::CS16, IqSampleFormat::CF32}) {
    IqLoggerConfig config;
    config.output_directory =
        (fs::temp_directory_path() / "openspectrum-playback-test").string();
    config.format = format;
    IqLogger logger(config);
    assert(logger.start_capture(5'800'000'000, 2'400'000, 28.5F, 4096, "Hann",
                                "", "test"));
    std::vector<std::complex<float>> const samples(4096, {0.25F, -0.25F});
    logger.write_samples(samples.data(), samples.size());
    logger.stop_capture();

    IqCaptureMeta meta;
    assert(
        IqPlaybackSource::read_meta_sidecar(logger.get_data_filename(), meta));
    assert(meta.center_freq_hz == 5'800'000'000 && // past 32 bits
           meta.sample_rate_hz == 2'400'000);
    assert(meta.gain_db == 28.5F && meta.format == format);
    fs::remove_all(config.output_directory);
  }

  fs::path const path = fs::temp_directory_path() / "openspectrum-test-2g.iq";
  // Sized, never written: sparse on Linux; NTFS reserves the space.
  std::ofstream(path, std::ios::binary).close();
  std::error_code ec;
  fs::resize_file(path, (uint64_t{1} << 31) + 16, ec);
  if (ec) {
    std::fprintf(stderr, "iq_playback_test: cannot make a 2 GiB file: %s\n",
                 ec.message().c_str());
    fs::remove(path, ec);
    return 1;
  }

  bool opened = false;
  {
    IqPlaybackSource source(path.string(), 2'048'000, IqSampleFormat::CF32);
    opened = source.open();
  } // closed before the remove: Windows will not delete an open file
  fs::remove(path, ec);
  assert(opened);
}
