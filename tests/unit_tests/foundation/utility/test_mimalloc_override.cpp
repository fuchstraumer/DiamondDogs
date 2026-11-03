#include <gtest/gtest.h>

#include <mimalloc.h>

#include <new>

// foundation/src/miMallocOverride.cpp must be in foundation's source list. If it is not, ::operator new comes from the UCRT.
TEST(MiMallocOverrideTest, GlobalOperatorNewReturnsMimallocMemory)
{
    void* block = ::operator new(64);

    const bool inMimallocHeap = mi_is_in_heap_region(block);

    ::operator delete(block);

    EXPECT_TRUE(inMimallocHeap);
}
