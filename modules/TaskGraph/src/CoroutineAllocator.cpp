#include "CoroutineAllocator.hpp"
#include "mimalloc.h"

namespace TaskGraph
{
    
    // Static member definitions
    std::atomic<size_t> CoroutineAllocator::globalTotalAllocated{0};
    std::atomic<size_t> CoroutineAllocator::globalCurrentAllocated{0};
    std::atomic<size_t> CoroutineAllocator::globalActiveCoroutines{0};


    void* CoroutineAllocator::Allocate(std::size_t size)
    {

        // Use mimalloc for actual allocation
        void* ptr = mi_malloc(size);
        if (!ptr)
        {
            throw std::bad_alloc();
        }
        
        // Update global statistics
        globalTotalAllocated.fetch_add(size, std::memory_order_relaxed);
        globalCurrentAllocated.fetch_add(size, std::memory_order_relaxed);
        globalActiveCoroutines.fetch_add(1, std::memory_order_relaxed);
        
        return ptr;
    }

    void CoroutineAllocator::Deallocate(void* ptr, std::size_t size)
    {
        if (!ptr)
        {
            return;
        }
        
        // Update global statistics
        globalCurrentAllocated.fetch_sub(size, std::memory_order_relaxed);
        globalActiveCoroutines.fetch_sub(1, std::memory_order_relaxed);

        // Free using mimalloc
        mi_free(ptr);
    }

    size_t CoroutineAllocator::GetTotalAllocated()
    {
        return globalTotalAllocated.load(std::memory_order_relaxed);
    }

    size_t CoroutineAllocator::GetCurrentAllocated()
    {
        return globalCurrentAllocated.load(std::memory_order_relaxed);
    }

    size_t CoroutineAllocator::GetActiveCoroutines()
    {
        return globalActiveCoroutines.load(std::memory_order_relaxed);
    }

}
