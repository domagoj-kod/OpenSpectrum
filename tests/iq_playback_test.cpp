// SPDX-License-Identifier: GPL-3.0-or-later
// A capture past 2 GiB opens for playback: its size must not pass through a
// long, which is 32 bits on Windows and made --play call such a file empty.

#undef NDEBUG // these are asserts; they must not compile out
#include "hardware/iq_playback.h"

#include <cassert>
#include <cstdint>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <system_error>

using namespace openspectrum;

auto main() -> int {
  namespace fs = std::filesystem;
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
