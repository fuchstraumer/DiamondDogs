#pragma once
#ifndef CONTENT_COMPILER_COROUTINE_ALLOCATOR_HPP
#define CONTENT_COMPILER_COROUTINE_ALLOCATOR_HPP
#include <atomic>
#include <cstdint>

namespace TaskGraph
{
    
    /**
     * @brief Allocator for coroutine frames that uses a memory arena.
     * Uses a high water mark to track memory usage, and then at each frame boundary resets the
     * memory using a zero-initialized mimalloc heap realloc. This should remain quite fast,
     * and individual allocations can just use a linear allocator.
     * 
     * If we request memory and we don't have enough room, we'll also use a zero-init realloc
     */
    class CoroutineAllocator
    {
    public:
        
        static void* Allocate(std::size_t size);
        static void Deallocate(void* ptr, std::size_t size);

        // Statistics
        static size_t GetTotalAllocated();
        static size_t GetCurrentAllocated();
        static size_t GetActiveCoroutines();

    private:
        static std::atomic<size_t> globalTotalAllocated;
        static std::atomic<size_t> globalCurrentAllocated;
        static std::atomic<size_t> globalActiveCoroutines;
    };
}

#endif // CONTENT_COMPILER_COROUTINE_ALLOCATOR_HPP
