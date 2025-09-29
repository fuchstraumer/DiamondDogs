#include "ResourceDataArena.hpp"

ResourceDataArena::ResourceDataArena()
{
    heap = mi_heap_new();
}

ResourceDataArena::~ResourceDataArena()
{
    mi_heap_destroy(heap);
    mi_heap_delete(heap);
}

ResourceDataArena::ResourceDataArena(ResourceDataArena&& other) noexcept :
    heap(other.heap)
{
    other.heap = nullptr;
}

ResourceDataArena& ResourceDataArena::operator=(ResourceDataArena&& other) noexcept
{
    if (this != &other)
    {
        heap = other.heap;
    }
    return *this;
}

void ResourceDataArena::reset()
{
    mi_heap_t* new_heap = mi_heap_new();
    mi_heap_delete(heap);
    heap = new_heap;
}

void* ResourceDataArena::Allocate(size_t size)
{
    if (size > MI_SMALL_SIZE_MAX)
    {
        return mi_heap_malloc(heap, size);
    }
    else
    {
        return mi_heap_malloc_small(heap, size);
    }
}

void ResourceDataArena::Free(void* ptr)
{
    // no op
}
