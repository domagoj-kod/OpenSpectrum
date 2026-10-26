// SPDX-License-Identifier: GPL-3.0-or-later
// Pins the zero-pad that 1.09 GHz collapsing to "1.9 GHz" slipped past, and
// that a sidecar write reports a failure.

#undef NDEBUG // these are asserts; they must not compile out
#include "utils/format.h"

#include <cassert>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <string>

using openspectrum::format_frequency;
using openspectrum::write_text_file;

auto main() -> int {
  assert(format_frequency(1009000000) == "1.009 GHz"); // reported bug
  assert(format_frequency(1090000000) == "1.090 GHz"); // reported bug
  assert(format_frequency(1000000000) == "1.000 GHz");
  assert(format_frequency(100009000) == "100.009 MHz"); // latent MHz pad
  assert(format_frequency(1005) == "1.005 kHz");        // latent kHz pad
  assert(format_frequency(999) == "999 Hz");
  assert(format_frequency(5800000000) == "5.800 GHz"); // past 32 bits

  namespace fs = std::filesystem;
  fs::path const dir = fs::temp_directory_path() / "openspectrum-format-test";
  fs::remove_all(dir);
  fs::create_directories(dir);
  std::string const path = (dir / "a.meta.json").string();
  assert(write_text_file(path, "{}\n"));
  std::ifstream in(path);
  assert(std::string(std::istreambuf_iterator<char>(in), {}) == "{}\n");
  in.close();
  // A directory in its place: the open fails, and says so.
  fs::remove(path);
  fs::create_directory(path);
  assert(!write_text_file(path, "{}\n"));
  assert(!write_text_file((dir / "missing" / "b.json").string(), "{}\n"));
  fs::remove_all(dir);
}
