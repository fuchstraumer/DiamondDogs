#include "CountingAllocator.hpp"
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <malloc.h>
#include <new>

// foundation links an override of operator new that sends every allocation to mimalloc. That override sits in one
// object file of foundation.lib, and the linker takes the whole object when any of its symbols is still unresolved.
// So this file defines every replaceable allocation function: with all of them defined here, the override object is
// not pulled in, and the two sets do not collide.

namespace
{
    thread_local bool t_Counting{ false };
    thread_local int64_t t_Count{ 0 };

    void* allocate(std::size_t size) noexcept
    {
        if (t_Counting)
        {
            ++t_Count;
        }

        return std::malloc(size != 0 ? size : 1);
    }

    void* allocateAligned(std::size_t size, std::size_t alignment) noexcept
    {
        if (t_Counting)
        {
            ++t_Count;
        }

        return _aligned_malloc(size != 0 ? size : 1, alignment);
    }

    void* allocateOrThrow(std::size_t size)
    {
        void* block = allocate(size);

        if (block == nullptr)
        {
            throw std::bad_alloc{};
        }

        return block;
    }

    void* allocateAlignedOrThrow(std::size_t size, std::size_t alignment)
    {
        void* block = allocateAligned(size, alignment);

        if (block == nullptr)
        {
            throw std::bad_alloc{};
        }

        return block;
    }
}

namespace test_alloc
{
    void StartCounting() noexcept
    {
        t_Count = 0;
        t_Counting = true;
    }

    int64_t StopCounting() noexcept
    {
        t_Counting = false;
        return t_Count;
    }
}

void* operator new(std::size_t size)
{
    return allocateOrThrow(size);
}

void* operator new[](std::size_t size)
{
    return allocateOrThrow(size);
}

void* operator new(std::size_t size, const std::nothrow_t&) noexcept
{
    return allocate(size);
}

void* operator new[](std::size_t size, const std::nothrow_t&) noexcept
{
    return allocate(size);
}

void* operator new(std::size_t size, std::align_val_t alignment)
{
    return allocateAlignedOrThrow(size, static_cast<std::size_t>(alignment));
}

void* operator new[](std::size_t size, std::align_val_t alignment)
{
    return allocateAlignedOrThrow(size, static_cast<std::size_t>(alignment));
}

void* operator new(std::size_t size, std::align_val_t alignment, const std::nothrow_t&) noexcept
{
    return allocateAligned(size, static_cast<std::size_t>(alignment));
}

void* operator new[](std::size_t size, std::align_val_t alignment, const std::nothrow_t&) noexcept
{
    return allocateAligned(size, static_cast<std::size_t>(alignment));
}

void operator delete(void* block) noexcept
{
    std::free(block);
}

void operator delete[](void* block) noexcept
{
    std::free(block);
}

void operator delete(void* block, const std::nothrow_t&) noexcept
{
    std::free(block);
}

void operator delete[](void* block, const std::nothrow_t&) noexcept
{
    std::free(block);
}

void operator delete(void* block, std::size_t) noexcept
{
    std::free(block);
}

void operator delete[](void* block, std::size_t) noexcept
{
    std::free(block);
}

void operator delete(void* block, std::align_val_t) noexcept
{
    _aligned_free(block);
}

void operator delete[](void* block, std::align_val_t) noexcept
{
    _aligned_free(block);
}

void operator delete(void* block, std::size_t, std::align_val_t) noexcept
{
    _aligned_free(block);
}

void operator delete[](void* block, std::size_t, std::align_val_t) noexcept
{
    _aligned_free(block);
}

void operator delete(void* block, std::align_val_t, const std::nothrow_t&) noexcept
{
    _aligned_free(block);
}

void operator delete[](void* block, std::align_val_t, const std::nothrow_t&) noexcept
{
    _aligned_free(block);
}
