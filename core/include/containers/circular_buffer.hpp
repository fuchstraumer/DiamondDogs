#pragma once
#ifndef CORE_CONTAINERS_CIRCULAR_BUFFER_HPP
#include <type_traits>
#include <cstddef>

template<typename T, size_t Capacity>
struct circular_buffer
{
    static_assert(std::is_default_constructible_v<T>, "Type stored in circular buffer must be default-constructible!");
    static_assert(std::is_move_assignable_v<T>, "Type stored in circular buffer must be move-assignable!");

    void push(T&& elem) noexcept
    {
        data[tail] = std::move(elem);
        tail = ++tail % Capacity;
    }

    T pop() noexcept
    {
        T result = data[head];
        head = ++head % Capacity;
        return result;
    }

private:
    size_t head{ 0u };
    size_t tail{ 0u };
    T data[Capacity];
};

#endif //!CORE_CONTAINERS_CIRCULAR_BUFFER_HPP
