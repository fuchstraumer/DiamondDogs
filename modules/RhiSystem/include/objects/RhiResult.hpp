#pragma once
#ifndef DIAMOND_DOGS_RHI_RESULT_HPP
#define DIAMOND_DOGS_RHI_RESULT_HPP
#include "RhiDefines.hpp"
#include <cstdint>
#include <string_view>

#ifdef RHI_SYSTEM_USE_VULKAN
    enum VkResult : int;
#elif defined(RHI_SYSTEM_USE_DX12)
    typedef long HRESULT;
#endif

namespace rhi
{
    /**
     * @brief Cross-API result wrapper that abstracts Vulkan VkResult and DirectX 12 HRESULT
     * Provides consistent success/failure checking and error message retrieval across graphics APIs
     */
    class Result
    {
    public:

        /** @brief Standardized result codes for common success and error states, providing a consistent interface across APIs */
        enum class Code : int32_t
        {
            Success = 0,
            NotReady = 1,
            Timeout = 2,
            Incomplete = 5,
            
            // Error codes (negative values)
            OutOfHostMemory = -1,
            OutOfDeviceMemory = -2,
            InitializationFailed = -3,
            DeviceLost = -4,
            MemoryMapFailed = -5,
            LayerNotPresent = -6,
            ExtensionNotPresent = -7,
            FeatureNotPresent = -8,
            IncompatibleDriver = -9,
            TooManyObjects = -10,
            FormatNotSupported = -11,
            SurfaceLostKHR = -1000000000,
            OutOfDateKHR = -1000001004,
            IncompatibleDisplayKHR = -1000003001,
            ValidationFailedEXT = -1000011001,
            InvalidShaderNV = -1000012000,
            
            // Generic error for unmapped platform-specific errors
            UnknownError = -999999999
        };

    private:
        int32_t nativeResult;

    public:
        constexpr Result() noexcept : nativeResult{ 0 } {}
        constexpr Result(Code code) noexcept : nativeResult{ static_cast<int32_t>(code) } {}
        explicit constexpr Result(int32_t rawResult) noexcept : nativeResult{ rawResult } {}
#ifdef RHI_SYSTEM_USE_VULKAN
        explicit constexpr Result(VkResult vkResult) noexcept;
#elif defined(RHI_SYSTEM_USE_DX12)
        explicit constexpr Result(HRESULT hresult) noexcept;
#endif

        bool IsSuccess() const noexcept;
        bool IsFailure() const noexcept;
        bool IsError() const noexcept;


        Code GetCode() const noexcept;

        /** @brief Returns the underlying `VkResult` or `HRESULT` value */
        constexpr int32_t GetNativeResult() const noexcept
        {
            return nativeResult;
        }

        /** @brief Use as needed to cast to underlying API/platform-specific result type, like `RhiHandle` */
        template<typename T>
        constexpr T As() const noexcept
        {
            return static_cast<T>(nativeResult);
        }


        std::string_view GetMessage() const noexcept;
        

        explicit operator bool() const noexcept;

        constexpr bool operator==(const Result& other) const noexcept
        {
            return nativeResult == other.nativeResult;
        }

        constexpr bool operator!=(const Result& other) const noexcept
        {
            return nativeResult != other.nativeResult;
        }

        constexpr bool operator==(Code code) const noexcept
        {
            return static_cast<Code>(nativeResult) == code;
        }

        constexpr bool operator!=(Code code) const noexcept
        {
            return static_cast<Code>(nativeResult) != code;
        }

        constexpr static Result Success() noexcept
        {
            return Result(Result::Code::Success);
        }

        constexpr static Result Failure(Result::Code code = Result::Code::UnknownError) noexcept
        {
            return Result(code);
        }
    };

    Result FromVulkan(int32_t vkResult) noexcept;
    Result FromDirectX(int32_t hresult) noexcept;

} 

#endif // !DIAMOND_DOGS_RHI_RESULT_HPP
