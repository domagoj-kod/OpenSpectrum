// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cassert>
#include <complex>
#include <cstddef>
#include <cstdlib>
#include <memory>
#include <mutex>
#include <queue>
#include <utility>

#ifdef _WIN32
#include <malloc.h> // For _aligned_malloc, _aligned_free
#endif

namespace openspectrum {

// Cache-line aligned frame for FFT samples
struct alignas(64) FFTFrame {
  size_t capacity{0};
  size_t size{0};
  std::complex<float> *data{nullptr};

  FFTFrame() = default;

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
    return frame;
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
};

class FramePool;

// Lightweight frame handle with RAII for automatic pool return
class FrameHandle {
public:
  FrameHandle() = default;
  explicit FrameHandle(FFTFrame *frame, std::weak_ptr<FramePool> pool = {})
      : m_frame(frame), m_pool(std::move(pool)) {}

  ~FrameHandle() { release(); }

  FrameHandle(const FrameHandle &) = delete;
  FrameHandle &operator=(const FrameHandle &) = delete;

  FrameHandle(FrameHandle &&other) noexcept
      : m_frame(other.m_frame), m_pool(std::move(other.m_pool)) {
    other.m_frame = nullptr;
  }

  FrameHandle &operator=(FrameHandle &&other) noexcept {
    if (this != &other) {
      release();
      m_frame = other.m_frame;
      m_pool = std::move(other.m_pool);
      other.m_frame = nullptr;
    }
    return *this;
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
  // Return the frame to its pool, or destroy it if the pool is gone.
  // Defined below FramePool (needs its definition).
  void release();

  FFTFrame *m_frame = nullptr;
  std::weak_ptr<FramePool> m_pool;
};

// Thread-safe pool of pre-allocated FFT frames.
//
// Ownership note: FramePool MUST be constructed via std::make_shared so that
// shared_from_this() works. FrameHandle's return path captures a weak_ptr to
// the pool; if the pool has been destroyed by the time the handle is dropped,
// the frame is destroyed directly instead of touching the dead pool. This
// eliminates a destruction-order UAF when FrameHandles outlive g_frame_pool.
class FramePool : public std::enable_shared_from_this<FramePool> {
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
    // Only destroy frames sitting in the free queue. Frames currently held by
    // FrameHandles are owned by those handles — their return-func sees the
    // dead weak_ptr and destroys them itself. Destroying them here would
    // double-free.
    std::lock_guard<std::mutex> lock(m_mutex);
    while (!m_free_queue.empty()) {
      delete m_free_queue.front();
      m_free_queue.pop();
    }
  }

  FrameHandle acquire() {
    FFTFrame *frame = nullptr;
    {
      std::lock_guard<std::mutex> lock(m_mutex);
      if (!m_free_queue.empty()) {
        frame = m_free_queue.front();
        m_free_queue.pop();
      }
    }
    if (!frame) {
      frame = FFTFrame::create(m_frame_capacity);
      if (!frame)
        return FrameHandle();
    }
    frame->reset();
    return FrameHandle(frame, weak_from_this());
  }

  // FrameHandle's return path.
  void release_frame(FFTFrame *frame) {
    std::lock_guard<std::mutex> lock(m_mutex);
    m_free_queue.push(frame);
  }

  size_t frame_capacity() const { return m_frame_capacity; }

private:
  size_t m_frame_capacity;
  mutable std::mutex m_mutex;
  std::queue<FFTFrame *> m_free_queue;
};

inline void FrameHandle::release() {
  if (m_frame == nullptr) {
    return;
  }
  if (auto pool = m_pool.lock()) {
    pool->release_frame(m_frame);
  } else {
    // Pool is gone — frame ownership falls to us.
    delete m_frame;
  }
  m_frame = nullptr;
}

} // namespace openspectrum