#pragma once
#ifndef DIAMOND_DOGS_RHI_ASSERT_HPP
#define DIAMOND_DOGS_RHI_ASSERT_HPP

#if defined(_DEBUG) || defined(DEBUG)
    #include "RhiResult.hpp"
    #include <stdexcept>
    #include <format>

    namespace rhi
    {
        inline void AssertImpl(const Result& result, const char* expr, const char* file, int line)
        {
            if (result.IsFailure())
            {
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
