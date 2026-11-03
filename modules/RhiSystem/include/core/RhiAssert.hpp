#pragma once
#ifndef DIAMOND_DOGS_RHI_ASSERT_HPP
#define DIAMOND_DOGS_RHI_ASSERT_HPP

#include "RhiResult.hpp"

namespace rhi
{
    // fixed to avoid bugs in non-debug builds bc of uninitialized variable
    using DeviceLossHandler = void(*)(const Result& deviceLossResult, const char* message);
    inline DeviceLossHandler g_DeviceLossHandler = nullptr;
}

#if defined(_DEBUG) || defined(DEBUG)
    namespace rhi
    {
        // Defined in src/core/RhiAssert.cpp, which keeps <format> out of this header.
        void AssertImpl(const Result& result, const char* expr, const char* file, int line);
    }

    #define RhiAssert(expr) ::rhi::AssertImpl((expr), #expr, __FILE__, __LINE__)
#else
    #define RhiAssert(expr) (void)(expr)
#endif

#endif // !DIAMOND_DOGS_RHI_ASSERT_HPP
