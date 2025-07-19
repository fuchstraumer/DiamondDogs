#pragma once
#ifndef CORE_CONTAINERS_CIRCULAR_BUFFER_HPP
#include <type_traits>
#include <cstddef>
#include <array>

/**
 * @brief Fixed-size circular buffer with constant-time operations
 * 
 * A circular buffer implementation that wraps around when full, overwriting
 * the oldest elements. All operations are O(1) and the buffer size is fixed at compile time.
 * 
 * @tparam T The type of elements stored in the buffer
 * @tparam Capacity Maximum number of elements the buffer can hold
 */
template<typename T, size_t Capacity>
struct circular_buffer
{
private:
    size_t head{ 0u };
    size_t tail{ 0u };
    std::array<T, Capacity> data;
public:

    static_assert(std::is_default_constructible_v<T>, "Type stored in circular buffer must be default-constructible!");
    static_assert(std::is_move_assignable_v<T>, "Type stored in circular buffer must be move-assignable!");
    using value_type = T;
    using size_type = size_t;
    using reference = value_type&;
    using const_reference = const value_type&;
    using pointer = value_type*;
    using const_pointer = const value_type*;
    using iterator = decltype(data)::iterator;
    using const_iterator = decltype(data)::const_iterator;

    /// Default constructor
    circular_buffer() noexcept = default;
    /// Destructor
    ~circular_buffer() noexcept = default;

    /// Copy constructor
    circular_buffer(const circular_buffer&) noexcept = default;
    /// Copy assignment operator
    circular_buffer& operator=(const circular_buffer&) noexcept = default;
    

    /**
     * @brief Add an element to the buffer
     * 
     * @param elem Element to add (moved into the buffer)
     * @note If buffer is full, overwrites the oldest element
     */
    void push(T&& elem) noexcept
    {
        data[tail] = std::move(elem);
        tail = ++tail % Capacity;
    }

    /**
     * @brief Remove and return the oldest element from the buffer
     * 
     * @return The oldest element (moved out of the buffer)
     * @note Behavior is undefined if buffer is empty
     */
    T&& pop() noexcept
    {
        T result = std::move(data[head]);
        head = ++head % Capacity;
        return std::move(result);
    }

    /**
     * @brief Access element by index
     * 
     * @param index Index of the element to access
     * @return Reference to the element
     * @note No bounds checking is performed
     */
    T& operator[](size_t index) noexcept
    {
        return data[index];
    }

    /**
     * @brief Access element by index (const version)
     * 
     * @param index Index of the element to access
     * @return Const reference to the element
     * @note No bounds checking is performed
     */
    const T& operator[](size_t index) const noexcept
    {
        return data[index];
    }

    /**
     * @brief Access element by index with bounds checking
     * 
     * @param index Index of the element to access
     * @return Reference to the element
     * @throws std::out_of_range if index is out of bounds
     */
    reference at(size_t index)
    {
        return data.at(index);
    }

    /**
     * @brief Access element by index with bounds checking (const version)
     * 
     * @param index Index of the element to access
     * @return Const reference to the element
     * @throws std::out_of_range if index is out of bounds
     */
    const_reference at(size_t index) const
    {
        return data.at(index);
    }

    /**
     * @brief Check if the buffer is empty
     * 
     * @return True if buffer contains no elements
     */
    bool empty() const noexcept
    {
        return head == tail;
    }

    /**
     * @brief Check if the buffer is full
     * 
     * @return True if buffer is at maximum capacity
     */
    bool full() const noexcept
    {
        return (tail + 1) % Capacity == head;
    }

    /**
     * @brief Get the number of elements currently in the buffer
     * 
     * @return Number of elements
     */
    size_type size() const noexcept
    {
        return (tail - head + Capacity) % Capacity;
    }
    
    /// Return iterator to beginning of underlying storage
    iterator begin() noexcept
    {
        return data.begin();
    }

    /// Return const iterator to beginning of underlying storage
    const_iterator begin() const noexcept
    {
        return data.begin();
    }

    /// Return iterator to end of underlying storage
    iterator end() noexcept
    {
        return data.end();
    }

    /// Return const iterator to end of underlying storage
    const_iterator end() const noexcept
    {
        return data.end();
    }

    /// Return const iterator to beginning of underlying storage
    const_iterator cbegin() const noexcept
    {
        return data.cbegin();
    }

    /// Return const iterator to end of underlying storage
    const_iterator cend() const noexcept
    {
        return data.cend();
    }

};

#endif //!CORE_CONTAINERS_CIRCULAR_BUFFER_HPP
