#pragma once
#ifndef CORE_CONTAINERS_CIRCULAR_BUFFER_HPP
#include <type_traits>
#include <cstddef>
#include <array>

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

    circular_buffer() noexcept = default;
    ~circular_buffer() noexcept = default;

    circular_buffer(const circular_buffer&) noexcept = default;
    circular_buffer& operator=(const circular_buffer&) noexcept = default;
    

    void push(T&& elem) noexcept
    {
        data[tail] = std::move(elem);
        tail = ++tail % Capacity;
    }

    T&& pop() noexcept
    {
        T result = std::move(data[head]);
        head = ++head % Capacity;
        return std::move(result);
    }

    T& operator[](size_t index) noexcept
    {
        return data[index];
    }

    const T& operator[](size_t index) const noexcept
    {
        return data[index];
    }

    reference at(size_t index)
    {
        return data.at(index);
    }

    const_reference at(size_t index) const
    {
        return data.at(index);
    }

    bool empty() const noexcept
    {
        return head == tail;
    }

    bool full() const noexcept
    {
        return (tail + 1) % Capacity == head;
    }

    size_type size() const noexcept
    {
        return (tail - head + Capacity) % Capacity;
    }
    
    iterator begin() noexcept
    {
        return data.begin();
    }

    const_iterator begin() const noexcept
    {
        return data.begin();
    }

    iterator end() noexcept
    {
        return data.end();
    }

    const_iterator end() const noexcept
    {
        return data.end();
    }

    const_iterator cbegin() const noexcept
    {
        return data.cbegin();
    }

    const_iterator cend() const noexcept
    {
        return data.cend();
    }

};

#endif //!CORE_CONTAINERS_CIRCULAR_BUFFER_HPP
