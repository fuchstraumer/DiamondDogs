#pragma once
#ifndef CONTENT_COMPILER_COROUTINE_ALLOCATOR_HPP
#define CONTENT_COMPILER_COROUTINE_ALLOCATOR_HPP
#include <atomic>
#include <cstdint>

namespace TaskGraph
{
    
    // Coroutine memory tracking with granular statistics
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
