#pragma once
#ifndef FOUNDATION_CONTAINERS_SMALLVECTOR_HPP
#define FOUNDATION_CONTAINERS_SMALLVECTOR_HPP
#include <array>
#include <cstdint>

namespace foundation
{
    // Intended to replace std::vector for small fixed-size arrays,
    // while providing the exact same API as std::vector for ease of use
    // and to make it easier as a drop-in replacement
    template<typename T, int64_t N>
    class SmallVector
    {
    public:
        SmallVector() noexcept = default;
        ~SmallVector() noexcept = default;

        [[nodiscard]] int64_t size() const noexcept { return m_size; }
        [[nodiscard]] bool empty() const noexcept { return m_size == 0; }
        [[nodiscard]] bool full() const noexcept { return m_size == N; }

        void clear() noexcept
        {
            // invoke dtor
            for (int i = 0; std::cmp_less(i, m_size); ++i)
            {
                storage[i].~T();
            }
            m_size = 0;
        }

    private:
        // doing my hated m_ prefix just because the stdlib-alike interface
        // takes size() as a function
        int64_t m_size{ 0u };
        std::array<T, N> storage;
    };
}

#endif // !FOUNDATION_CONTAINERS_SMALLVECTOR_HPP
