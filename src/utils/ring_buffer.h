// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <stdexcept>
#include <vector>

namespace openspectrum {

// Fixed-capacity FIFO with O(1) push and indexing (0 = oldest) and no
// allocation after construction. A full buffer overwrites its oldest element.
// Waterfall history.
template <typename T> class RingBuffer {
public:
  // Throws std::invalid_argument if capacity is 0.
  explicit RingBuffer(size_t capacity)
      : m_data(capacity), m_capacity(capacity) {
    if (capacity == 0) {
      throw std::invalid_argument("RingBuffer capacity must be > 0");
    }
  }

  void push(const T &item) {
    m_data[m_head] = item;
    m_head = (m_head + 1) % m_capacity;
    if (m_size < m_capacity) {
      ++m_size;
    }
  }

  void push(T &&item) {
    m_data[m_head] = std::move(item);
    m_head = (m_head + 1) % m_capacity;
    if (m_size < m_capacity) {
      ++m_size;
    }
  }

  // Throws std::out_of_range if index >= size().
  T &operator[](size_t index) {
    if (index >= m_size) {
      throw std::out_of_range("RingBuffer index out of range");
    }
    size_t actual_index = (m_head - m_size + index + m_capacity) % m_capacity;
    return m_data[actual_index];
  }

  const T &operator[](size_t index) const {
    if (index >= m_size) {
      throw std::out_of_range("RingBuffer index out of range");
    }
    size_t actual_index = (m_head - m_size + index + m_capacity) % m_capacity;
    return m_data[actual_index];
  }

  // Throws std::out_of_range if buffer is empty.
  T &back() {
    if (m_size == 0)
      throw std::out_of_range("RingBuffer is empty");
    size_t idx = (m_head + m_capacity - 1) % m_capacity;
    return m_data[idx];
  }

  [[nodiscard]] const T &back() const {
    if (m_size == 0)
      throw std::out_of_range("RingBuffer is empty");
    size_t idx = (m_head + m_capacity - 1) % m_capacity;
    return m_data[idx];
  }

  [[nodiscard]] bool empty() const noexcept { return m_size == 0; }

  [[nodiscard]] bool full() const noexcept { return m_size == m_capacity; }

  [[nodiscard]] size_t size() const noexcept { return m_size; }

  [[nodiscard]] size_t capacity() const noexcept { return m_capacity; }

  void clear() noexcept {
    m_size = 0;
    m_head = 0;
  }

private:
  std::vector<T> m_data;
  size_t m_capacity;
  size_t m_size{0};
  size_t m_head{0}; // next write position
};

} // namespace openspectrum
