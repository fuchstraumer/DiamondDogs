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
    #include <stdexcept>
    #include <format>

    namespace rhi
    {

        inline void AssertImpl(const Result& result, const char* expr, const char* file, int line)
        {
            if (result.IsFailure())
            {
                // if tdr, try to use device fault extension to extract more about the fault
                if (result.IsDeviceLost() && (g_DeviceLossHandler != nullptr))
                {
                    g_DeviceLossHandler(result, std::format("Device lost detected: {} [{}] at {}:{}", 
                        result.GetMessage(), expr, file, line).c_str());
                }
                throw std::runtime_error(std::format("RHI failed: {} [{}] at {}:{}", 
                    result.GetMessage(), expr, file, line));
            }
        }
    }

    #define RhiAssert(expr) ::rhi::AssertImpl((expr), #expr, __FILE__, __LINE__)
#else
    #define RhiAssert(expr) (void)(expr)
#endif

#endif // !DIAMOND_DOGS_RHI_ASSERT_HPP
