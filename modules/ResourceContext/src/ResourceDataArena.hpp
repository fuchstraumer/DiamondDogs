#pragma once
#ifndef RESOURCE_CONTEXT_RESOURCE_DATA_ARENA_HPP
#define RESOURCE_CONTEXT_RESOURCE_DATA_ARENA_HPP
#include "mimalloc.h"

class ResourceDataArena
{
public:

    ResourceDataArena();
    ~ResourceDataArena();

    ResourceDataArena(const ResourceDataArena&) = delete;
    ResourceDataArena& operator=(const ResourceDataArena&) = delete;

    ResourceDataArena(ResourceDataArena&& other) noexcept;
    ResourceDataArena& operator=(ResourceDataArena&& other) noexcept;

    template<typename T, typename...Args>
    T* create(Args&&... args)
    {
        void* memory = nullptr;
        // yay we can branch at compile time bc this is a template!
        if constexpr (sizeof(T) < MI_SMALL_SIZE_MAX)
        {
            memory = mi_heap_malloc_small(heap, sizeof(T));
        }
        else
        {
            memory = mi_heap_malloc(heap, sizeof(T));
        }
        return new(memory) T(std::forward<Args>(args)...);
    }

    void clear();
    void reset();

    void* Allocate(size_t size);
    void Free(void* ptr);

private:
    mi_heap_t* heap;
};

#endif // RESOURCE_CONTEXT_RESOURCE_DATA_ARENA_HPP
