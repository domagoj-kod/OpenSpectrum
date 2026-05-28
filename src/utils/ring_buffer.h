// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <vector>
#include <stdexcept>

namespace openspectrum {

/// @brief A fixed-capacity ring buffer (circular buffer) for efficient FIFO operations
/// @tparam T Type of elements stored in the buffer
///
/// Features:
/// - O(1) push operations (amortized)
/// - O(1) random access via operator[]
/// - No dynamic allocations after construction
/// - Iterator support for range-based for loops
/// - Contiguous storage for cache efficiency
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

    /// @brief Default constructor (capacity must be set via resize() before use)
    RingBuffer() : m_capacity(0), m_size(0), m_head(0) {}

    /// @brief Resize the buffer capacity
    /// @param new_capacity New capacity for the buffer
    /// @note Existing elements are preserved up to the minimum of old and new capacity
    void resize(size_t new_capacity) {
        if (new_capacity == m_capacity) return;
        if (new_capacity == 0) {
            m_data.clear();
            m_capacity = 0;
            m_size = 0;
            m_head = 0;
            return;
        }

        std::vector<T> new_data(new_capacity);
        size_t elements_to_copy = std::min(m_size, new_capacity);

        for (size_t i = 0; i < elements_to_copy; ++i) {
            size_t old_idx = (m_head - m_size + i + m_capacity) % m_capacity;
            new_data[i] = std::move(m_data[old_idx]);
        }

        m_data = std::move(new_data);
        m_capacity = new_capacity;
        m_size = elements_to_copy;
        m_head = elements_to_copy % m_capacity;
    }

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

    /// @brief Emplace an element at the end of the buffer
    /// @tparam Args Types of arguments to forward to T's constructor
    /// @param args Arguments to forward to T's constructor
    /// @return Reference to the emplaced element
    template <typename... Args>
    T& emplace(Args&&... args) {
        if (m_capacity == 0) throw std::runtime_error("RingBuffer has zero capacity");
        size_t idx = m_head;
        m_data[idx] = T(std::forward<Args>(args)...);
        m_head = (m_head + 1) % m_capacity;
        if (m_size < m_capacity) {
            ++m_size;
        }
        return m_data[idx];
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

    /// @brief Safe access with bounds checking
    /// @param index Index to access
    /// @return Pointer to element, or nullptr if out of range
    T* get(size_t index) noexcept {
        if (index >= m_size) return nullptr;
        size_t actual_index = (m_head - m_size + index + m_capacity) % m_capacity;
        return &m_data[actual_index];
    }

    /// @brief Const safe access with bounds checking
    /// @param index Index to access
    /// @return Pointer to element, or nullptr if out of range
    [[nodiscard]] const T* get(size_t index) const noexcept {
        if (index >= m_size) return nullptr;
        size_t actual_index = (m_head - m_size + index + m_capacity) % m_capacity;
        return &m_data[actual_index];
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

    /// @brief Get the oldest element (first pushed)
    /// @return Reference to oldest element
    /// @throws std::out_of_range if buffer is empty
    T& front() {
        if (m_size == 0) throw std::out_of_range("RingBuffer is empty");
        size_t idx = (m_head - m_size + m_capacity) % m_capacity;
        return m_data[idx];
    }

    /// @brief Get the oldest element (const)
    /// @return Const reference to oldest element
    /// @throws std::out_of_range if buffer is empty
    [[nodiscard]] const T& front() const {
        if (m_size == 0) throw std::out_of_range("RingBuffer is empty");
        size_t idx = (m_head - m_size + m_capacity) % m_capacity;
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

    // ========== ITERATOR SUPPORT ==========

    /// @brief Iterator for range-based for loops
    class Iterator {
    public:
        using iterator_category = std::random_access_iterator_tag;
        using value_type = T;
        using difference_type = std::ptrdiff_t;
        using pointer = T*;
        using reference = T&;

        Iterator(RingBuffer* buffer, size_t index)
            : m_buffer(buffer), m_index(index) {}

        reference operator*() const { return (*m_buffer)[m_index]; }
        pointer operator->() const { return &((*m_buffer)[m_index]); }

        Iterator& operator++() { ++m_index; return *this; }
        Iterator operator++(int) { Iterator tmp = *this; ++m_index; return tmp; }
        Iterator& operator--() { --m_index; return *this; }
        Iterator operator--(int) { Iterator tmp = *this; --m_index; return tmp; }

        Iterator operator+(difference_type n) const { return Iterator(m_buffer, m_index + n); }
        Iterator operator-(difference_type n) const { return Iterator(m_buffer, m_index - n); }

        difference_type operator-(const Iterator& other) const { 
            return static_cast<difference_type>(m_index) - static_cast<difference_type>(other.m_index); 
        }

        bool operator==(const Iterator& other) const { 
            return m_buffer == other.m_buffer && m_index == other.m_index; 
        }
        bool operator!=(const Iterator& other) const { return !(*this == other); }
        bool operator<(const Iterator& other) const { return m_index < other.m_index; }
        bool operator<=(const Iterator& other) const { return m_index <= other.m_index; }
        bool operator>(const Iterator& other) const { return m_index > other.m_index; }
        bool operator>=(const Iterator& other) const { return m_index >= other.m_index; }

        Iterator& operator+=(difference_type n) { m_index += n; return *this; }
        Iterator& operator-=(difference_type n) { m_index -= n; return *this; }

        reference operator[](difference_type n) const { return (*m_buffer)[m_index + n]; }

    private:
        RingBuffer* m_buffer;
        size_t m_index;
    };

    /// @brief Const iterator
    class ConstIterator {
    public:
        using iterator_category = std::random_access_iterator_tag;
        using value_type = T;
        using difference_type = std::ptrdiff_t;
        using pointer = const T*;
        using reference = const T&;

        ConstIterator(const RingBuffer* buffer, size_t index)
            : m_buffer(buffer), m_index(index) {}

        reference operator*() const { return (*m_buffer)[m_index]; }
        pointer operator->() const { return &((*m_buffer)[m_index]); }

        ConstIterator& operator++() { ++m_index; return *this; }
        ConstIterator operator++(int) { ConstIterator tmp = *this; ++m_index; return tmp; }
        ConstIterator& operator--() { --m_index; return *this; }
        ConstIterator operator--(int) { ConstIterator tmp = *this; --m_index; return tmp; }

        ConstIterator operator+(difference_type n) const { return ConstIterator(m_buffer, m_index + n); }
        ConstIterator operator-(difference_type n) const { return ConstIterator(m_buffer, m_index - n); }

        difference_type operator-(const ConstIterator& other) const { 
            return static_cast<difference_type>(m_index) - static_cast<difference_type>(other.m_index); 
        }

        bool operator==(const ConstIterator& other) const { 
            return m_buffer == other.m_buffer && m_index == other.m_index; 
        }
        bool operator!=(const ConstIterator& other) const { return !(*this == other); }

    private:
        const RingBuffer* m_buffer;
        size_t m_index;
    };

    Iterator begin() { return Iterator(this, 0); }
    Iterator end() { return Iterator(this, m_size); }
    [[nodiscard]] ConstIterator begin() const { return ConstIterator(this, 0); }
    [[nodiscard]] ConstIterator end() const { return ConstIterator(this, m_size); }
    [[nodiscard]] ConstIterator cbegin() const { return ConstIterator(this, 0); }
    [[nodiscard]] ConstIterator cend() const { return ConstIterator(this, m_size); }

    /// @brief Reverse iterator support
    std::reverse_iterator<Iterator> rbegin() { return std::reverse_iterator<Iterator>(end()); }
    std::reverse_iterator<Iterator> rend() { return std::reverse_iterator<Iterator>(begin()); }
    [[nodiscard]] std::reverse_iterator<ConstIterator> rbegin() const { return std::reverse_iterator<ConstIterator>(cend()); }
    [[nodiscard]] std::reverse_iterator<ConstIterator> rend() const { return std::reverse_iterator<ConstIterator>(cbegin()); }
    [[nodiscard]] std::reverse_iterator<ConstIterator> crbegin() const { return rbegin(); }
    [[nodiscard]] std::reverse_iterator<ConstIterator> crend() const { return rend(); }

private:
    std::vector<T> m_data;
    size_t m_capacity;
    size_t m_size;
    size_t m_head;  // Next write position (newest element + 1)
};

} // namespace openspectrum
