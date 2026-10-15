// SPDX-License-Identifier: GPL-3.0-or-later
// A capture through a ring small enough to wrap and to overflow, read back and
// checked.

#undef NDEBUG // these are asserts; they must not compile out
#include "utils/iq_logger.h"
#include "utils/logger.h"

#include <cassert>
#include <chrono>
#include <cmath>
#include <complex>
#include <csignal>
#include <cstddef>
#include <cstdio>
#include <filesystem>
#include <string>
#include <thread>
#include <vector>

#ifndef _WIN32
#include <sys/resource.h>
#endif

using namespace openspectrum;

auto main() -> int {
  namespace fs = std::filesystem;
  IqLoggerConfig config;
  config.output_directory =
      (fs::temp_directory_path() / "openspectrum-test").string();
  config.buffer_size_bytes = 4096;
  IqLogger logger(config);
  // 2000 Hz gives a ring of 1000 samples. Batches of 333 wrap it, and every
  // fourth arrives with the ring full (the writer sleeps 5 ms between polls),
  // so it is dropped whole. Timing can move which batches are dropped, not
  // what must hold of the file. The drops are the point, so their warning is
  // muted.
  Logger::get_instance().set_level(LogLevel::ERROR);
  constexpr size_t kBatch = 333;
  constexpr size_t kBatches = 12;
  std::vector<std::complex<float>> batch(kBatch);
  // Idle, a write is a no-op (the producer calls it for every delivery).
  logger.write_samples(batch.data(), kBatch);
  assert(!logger.is_capturing());
  bool const started = logger.start_capture(0, 2000, 0.0F);
  assert(started && logger.is_capturing() && !logger.write_failed());
  for (size_t b = 0; b < kBatches; ++b) {
    for (size_t k = 0; k < kBatch; ++k) {
      batch[k] = {static_cast<float>(b * kBatch + k), 0.0F};
    }
    logger.write_samples(batch.data(), kBatch);
    if (b % 4 == 3) {
      std::this_thread::sleep_for(std::chrono::milliseconds(20));
    }
  }
  logger.stop_capture();
  assert(!logger.is_capturing() && !logger.write_failed());
  logger.write_samples(batch.data(), kBatch); // stopped: a no-op again

  // The file holds whole batches, in order, each one intact.
  std::vector<std::complex<float>> got(kBatch * kBatches);
  FILE *f = std::fopen(logger.get_data_filename().c_str(), "rb");
  assert(f != nullptr);
  size_t const n = std::fread(got.data(), sizeof got[0], got.size(), f);
  std::fclose(f);
  assert(n > 0 && n % kBatch == 0);
  float last = -1.0F;
  for (size_t i = 0; i < n; ++i) {
    float const v = got[i].real();
    assert(i % kBatch == 0 ? std::fmod(v, kBatch) == 0.0F && v > last
                           : v == last + 1.0F);
    last = v;
  }
  // The sidecar counts exactly the samples that are missing.
  std::string meta(4096, '\0');
  f = std::fopen(logger.get_metadata_filename().c_str(), "r");
  assert(f != nullptr);
  meta.resize(std::fread(meta.data(), 1, meta.size(), f));
  std::fclose(f);
  assert(meta.find(
             "\"dropped_samples\": " + std::to_string(kBatch * kBatches - n) +
             ",") != std::string::npos);

#ifndef _WIN32
  // A full disk, as a file size limit that ends inside a sample: the file is
  // cut to whole samples and the sidecar counts exactly those.
  signal(SIGXFSZ, SIG_IGN);
  rlimit const limit{10003, RLIM_INFINITY};
  setrlimit(RLIMIT_FSIZE, &limit);
  std::puts("expected: one \"IQ flush short write\" error");
  IqLogger full(config);
  bool const full_started = full.start_capture(0, 20000, 0.0F);
  assert(full_started);
  for (int b = 0; b < 5; ++b) { // 13 320 B
    full.write_samples(batch.data(), kBatch);
  }
  full.stop_capture();
  assert(full.write_failed());
  assert(fs::file_size(full.get_data_filename()) == 10000);
  f = std::fopen(full.get_metadata_filename().c_str(), "r");
  assert(f != nullptr);
  meta.assign(4096, '\0');
  meta.resize(std::fread(meta.data(), 1, meta.size(), f));
  std::fclose(f);
  assert(meta.find("\"sample_count\": 1250,") != std::string::npos);
  assert(meta.find("\"write_error\": true") != std::string::npos);
#endif
  fs::remove_all(config.output_directory);
}
