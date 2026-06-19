// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <vector>
#include <stdexcept>

namespace openspectrum {

/// @brief A fixed-capacity ring buffer (circular buffer) for efficient FIFO
/// operations
/// @tparam T Type of elements stored in the buffer
///
/// Features:
/// - O(1) push operations (amortized)
/// - O(1) random access via operator[]
/// - No dynamic allocations after construction
///
/// Use case: Waterfall display history where we need to:
/// 1. Add new spectrum lines at the end
/// 2. Remove old lines from the beginning when capacity is reached
/// 3. Random access to any line for rendering
/// 4. Avoid memory allocations during normal operation
template <typename T>
class RingBuffer {
public:
    /// @brief Construct a ring buffer with the specified capacity
    /// @param capacity Maximum number of elements the buffer can hold
    /// @throws std::invalid_argument if capacity is 0
    explicit RingBuffer(size_t capacity)
        : m_data(capacity), m_capacity(capacity), m_size(0), m_head(0) {
        if (capacity == 0) {
            throw std::invalid_argument("RingBuffer capacity must be > 0");
        }
    }

    /// @brief Default constructor (zero capacity; push/[] are no-ops until
    ///        a capacity-taking RingBuffer is assigned in)
    RingBuffer() : m_capacity(0), m_size(0), m_head(0) {}

    /// @brief Push an element to the end of the buffer
    /// @param item Element to push (copied)
    /// @note If buffer is full, oldest element is overwritten
    void push(const T& item) {
        if (m_capacity == 0) return;
        m_data[m_head] = item;
        m_head = (m_head + 1) % m_capacity;
        if (m_size < m_capacity) {
            ++m_size;
        }
    }

    /// @brief Push an element to the end of the buffer (move semantics)
    /// @param item Element to push (moved)
    /// @note If buffer is full, oldest element is overwritten
    void push(T&& item) {
        if (m_capacity == 0) return;
        m_data[m_head] = std::move(item);
        m_head = (m_head + 1) % m_capacity;
        if (m_size < m_capacity) {
            ++m_size;
        }
    }

    /// @brief Access element at index (0 = oldest, size()-1 = newest)
    /// @param index Index of element to access (0-based, 0 is oldest)
    /// @return Reference to element at index
    /// @throws std::out_of_range if index >= size()
    T& operator[](size_t index) {
        if (index >= m_size) {
            throw std::out_of_range("RingBuffer index out of range");
        }
        size_t actual_index = (m_head - m_size + index + m_capacity) % m_capacity;
        return m_data[actual_index];
    }

    /// @brief Const access element at index
    /// @param index Index of element to access
    /// @return Const reference to element at index
    /// @throws std::out_of_range if index >= size()
    const T& operator[](size_t index) const {
        if (index >= m_size) {
            throw std::out_of_range("RingBuffer index out of range");
        }
        size_t actual_index = (m_head - m_size + index + m_capacity) % m_capacity;
        return m_data[actual_index];
    }

    /// @brief Get the newest element (last pushed)
    /// @return Reference to newest element
    /// @throws std::out_of_range if buffer is empty
    T& back() {
        if (m_size == 0) throw std::out_of_range("RingBuffer is empty");
        size_t idx = (m_head + m_capacity - 1) % m_capacity;
        return m_data[idx];
    }

    /// @brief Get the newest element (const)
    /// @return Const reference to newest element
    /// @throws std::out_of_range if buffer is empty
    [[nodiscard]] const T& back() const {
        if (m_size == 0) throw std::out_of_range("RingBuffer is empty");
        size_t idx = (m_head + m_capacity - 1) % m_capacity;
        return m_data[idx];
    }

    /// @brief Check if buffer is empty
    [[nodiscard]] bool empty() const noexcept { return m_size == 0; }

    /// @brief Check if buffer is full
    [[nodiscard]] bool full() const noexcept { return m_size == m_capacity; }

    /// @brief Get current number of elements
    [[nodiscard]] size_t size() const noexcept { return m_size; }

    /// @brief Get maximum capacity
    [[nodiscard]] size_t capacity() const noexcept { return m_capacity; }

    /// @brief Clear all elements (keep capacity)
    void clear() noexcept {
        m_size = 0;
        m_head = 0;
    }

private:
    std::vector<T> m_data;
    size_t m_capacity;
    size_t m_size;
    size_t m_head;  // Next write position (newest element + 1)
};

} // namespace openspectrum
