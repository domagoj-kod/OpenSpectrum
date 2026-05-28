// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <atomic>
#include <cassert>
#include <complex>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <functional>
#include <memory>
#include <mutex>
#include <queue>
#include <unordered_set>
#include <utility>
#include <vector>

#ifdef _WIN32
#include <malloc.h> // For _aligned_malloc, _aligned_free
#endif

namespace openspectrum {

// Cache-line aligned frame for FFT samples
struct alignas(64) FFTFrame {
  size_t capacity;
  size_t size;
  FFTFrame *next;
  std::complex<float> *data;

  FFTFrame() : capacity(0), size(0), next(nullptr), data(nullptr) {}

  ~FFTFrame() {
#ifdef _WIN32
    _aligned_free(data);
#else
    free(data);
#endif
    data = nullptr;
  }

  static FFTFrame *create(size_t frame_capacity) {
    FFTFrame *frame = new FFTFrame();
    if (!frame)
      return nullptr;

    size_t const data_bytes = frame_capacity * sizeof(std::complex<float>);

#ifdef _WIN32
    frame->data =
        static_cast<std::complex<float> *>(_aligned_malloc(data_bytes, 64));
#else
    if (posix_memalign(reinterpret_cast<void **>(&frame->data), 64,
                       data_bytes) != 0) {
      delete frame;
      return nullptr;
    }
#endif

    if (!frame->data) {
      delete frame;
      return nullptr;
    }

    frame->capacity = frame_capacity;
    frame->size = 0;
    frame->next = nullptr;
    return frame;
  }

  static void destroy(FFTFrame *frame) {
    if (frame)
      delete frame;
  }

  std::complex<float> &operator[](size_t i) {
    assert(i < size);
    return data[i];
  }
  const std::complex<float> &operator[](size_t i) const {
    assert(i < size);
    return data[i];
  }
  void reset() { size = 0; }
  void resize(size_t new_size) {
    assert(new_size <= capacity);
    size = new_size;
  }
  [[nodiscard]] bool can_hold(size_t requested) const { return requested <= capacity; }
};

// Lightweight frame handle with RAII for automatic pool return
class FrameHandle {
public:
  using ReturnFunc = std::function<void(FFTFrame *)>;

  FrameHandle() = default;
  explicit FrameHandle(FFTFrame *frame, ReturnFunc return_func = nullptr)
      : m_frame(frame), m_return_func(std::move(return_func)) {}

  ~FrameHandle() {
    if (m_frame && m_return_func) {
      m_return_func(m_frame);
    } else if (m_frame) {
      FFTFrame::destroy(m_frame);
    }
  }

  FrameHandle(const FrameHandle &) = delete;
  FrameHandle &operator=(const FrameHandle &) = delete;

  FrameHandle(FrameHandle &&other) noexcept
      : m_frame(other.m_frame), m_return_func(other.m_return_func) {
    other.m_frame = nullptr;
    other.m_return_func = nullptr;
  }

  FrameHandle &operator=(FrameHandle &&other) noexcept {
    if (this != &other) {
      if (m_frame && m_return_func)
        m_return_func(m_frame);
      else if (m_frame)
        FFTFrame::destroy(m_frame);
      m_frame = other.m_frame;
      m_return_func = other.m_return_func;
      other.m_frame = nullptr;
      other.m_return_func = nullptr;
    }
    return *this;
  }

  [[nodiscard]] FFTFrame *get() const { return m_frame; }
  FFTFrame *release() {
    auto *f = m_frame;
    m_frame = nullptr;
    m_return_func = nullptr;
    return f;
  }
  explicit operator bool() const { return m_frame != nullptr; }

  std::complex<float> *data() { return m_frame ? m_frame->data : nullptr; }
  [[nodiscard]] const std::complex<float> *data() const {
    return m_frame ? m_frame->data : nullptr;
  }
  [[nodiscard]] size_t size() const { return m_frame ? m_frame->size : 0; }
  [[nodiscard]] size_t capacity() const { return m_frame ? m_frame->capacity : 0; }

  void reset() {
    if (m_frame)
      m_frame->reset();
  }
  void resize(size_t new_size) {
    if (m_frame)
      m_frame->resize(new_size);
  }

  std::complex<float> &operator[](size_t i) {
    assert(m_frame && i < m_frame->size);
    return (*m_frame)[i];
  }
  const std::complex<float> &operator[](size_t i) const {
    assert(m_frame && i < m_frame->size);
    return (*m_frame)[i];
  }

private:
  FFTFrame *m_frame = nullptr;
  ReturnFunc m_return_func;
};

// Thread-safe pool of pre-allocated FFT frames
class FramePool {
public:
  explicit FramePool(size_t frame_capacity, size_t initial_count = 8)
      : m_frame_capacity(frame_capacity) {
    for (size_t i = 0; i < initial_count; ++i) {
      FFTFrame *frame = FFTFrame::create(frame_capacity);
      if (frame)
        m_free_queue.push(frame);
    }
  }

  ~FramePool() {
    std::lock_guard<std::mutex> lock(m_mutex);
    while (!m_free_queue.empty()) {
      FFTFrame::destroy(m_free_queue.front());
      m_free_queue.pop();
    }
    for (auto *f : m_allocated_frames)
      FFTFrame::destroy(f);
  }

  FrameHandle acquire() {
    FFTFrame *frame = nullptr;
    {
      std::lock_guard<std::mutex> lock(m_mutex);
      if (!m_free_queue.empty()) {
        frame = m_free_queue.front();
        m_free_queue.pop();
      } else {
        frame = FFTFrame::create(m_frame_capacity);
        if (frame)
          m_allocated_frames.insert(frame);
        else
          return FrameHandle(nullptr, nullptr);
      }
    }
    frame->reset();
    auto return_func = [this](FFTFrame *f) {
      if (f) {
        std::lock_guard<std::mutex> lock(m_mutex);
        m_allocated_frames.erase(f);
        m_free_queue.push(f);
      }
    };
    return FrameHandle(frame, return_func);
  }

  struct Stats {
    size_t free_count;
    size_t allocated_count;
    size_t frame_capacity;
  };

  Stats get_stats() const {
    std::lock_guard<std::mutex> lock(m_mutex);
    return Stats{m_free_queue.size(), m_allocated_frames.size(),
                 m_frame_capacity};
  }

  size_t frame_capacity() const { return m_frame_capacity; }

private:
  size_t m_frame_capacity;
  mutable std::mutex m_mutex;
  std::queue<FFTFrame *> m_free_queue;
  std::unordered_set<FFTFrame *> m_allocated_frames;
};

} // namespace openspectrum