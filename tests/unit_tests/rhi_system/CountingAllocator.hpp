#pragma once
#ifndef RHI_SYSTEM_TESTS_COUNTING_ALLOCATOR_HPP
#define RHI_SYSTEM_TESTS_COUNTING_ALLOCATOR_HPP
#include <cstdint>

// CountingAllocator.cpp replaces every global operator new and delete in the test executable, so that a test can
// count the heap allocations that code under test makes

namespace test_alloc
{
    /** @brief Starts counting the heap allocations that the calling thread makes, and resets the count. */
    void StartCounting() noexcept;

    /** @brief Stops counting, and returns the number of allocations since StartCounting. */
    [[nodiscard]] int64_t StopCounting() noexcept;
}

#endif // !RHI_SYSTEM_TESTS_COUNTING_ALLOCATOR_HPP
