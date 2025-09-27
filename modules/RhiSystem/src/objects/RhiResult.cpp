#include "RhiResult.hpp"
#include "RhiDefines.hpp"

#ifdef RHI_SYSTEM_USE_VULKAN
    #include <vulkan/vulkan_core.h>
#elif defined(RHI_SYSTEM_USE_DX12)
    #include <winerror.h>
    #include <comdef.h>
#endif

namespace rhi
{
    bool Result::IsSuccess() const noexcept
    {
#ifdef RHI_SYSTEM_USE_VULKAN
        return nativeResult >= 0;  // VkResult: >= 0 is success
#elif defined(RHI_SYSTEM_USE_DX12)
        return SUCCEEDED(static_cast<HRESULT>(nativeResult));  // HRESULT: use Windows macro
#else
        return nativeResult >= 0;
#endif
    }

    bool Result::IsFailure() const noexcept
    {
        return !IsSuccess();
    }

    bool Result::IsError() const noexcept
    {
        return IsFailure();
    }

    Result::Code Result::GetCode() const noexcept
    {
#ifdef RHI_SYSTEM_USE_VULKAN
        // Direct mapping for most Vulkan results
        return static_cast<Code>(nativeResult);
#elif defined(RHI_SYSTEM_USE_DX12)
        return MapHResultToCode(static_cast<HRESULT>(nativeResult));
#else
        return Code::UnknownError;
#endif
    }

    Result::operator bool() const noexcept
    {
        return IsSuccess();
    }

    std::string_view Result::GetMessage() const noexcept
    {
#ifdef RHI_SYSTEM_USE_VULKAN
        switch (static_cast<VkResult>(nativeResult))
        {
            case VK_SUCCESS: return "Success";
            case VK_NOT_READY: return "Not ready";
            case VK_TIMEOUT: return "Timeout";
            case VK_EVENT_SET: return "Event set";
            case VK_EVENT_RESET: return "Event reset";
            case VK_INCOMPLETE: return "Incomplete";
            case VK_ERROR_OUT_OF_HOST_MEMORY: return "Out of host memory";
            case VK_ERROR_OUT_OF_DEVICE_MEMORY: return "Out of device memory";
            case VK_ERROR_INITIALIZATION_FAILED: return "Initialization failed";
            case VK_ERROR_DEVICE_LOST: return "Device lost";
            case VK_ERROR_MEMORY_MAP_FAILED: return "Memory map failed";
            case VK_ERROR_LAYER_NOT_PRESENT: return "Layer not present";
            case VK_ERROR_EXTENSION_NOT_PRESENT: return "Extension not present";
            case VK_ERROR_FEATURE_NOT_PRESENT: return "Feature not present";
            case VK_ERROR_INCOMPATIBLE_DRIVER: return "Incompatible driver";
            case VK_ERROR_TOO_MANY_OBJECTS: return "Too many objects";
            case VK_ERROR_FORMAT_NOT_SUPPORTED: return "Format not supported";
            case VK_ERROR_SURFACE_LOST_KHR: return "Surface lost";
            case VK_ERROR_OUT_OF_DATE_KHR: return "Out of date";
            case VK_SUBOPTIMAL_KHR: return "Suboptimal";
            case VK_ERROR_INCOMPATIBLE_DISPLAY_KHR: return "Incompatible display";
            case VK_ERROR_VALIDATION_FAILED_EXT: return "Validation failed";
            case VK_ERROR_INVALID_SHADER_NV: return "Invalid shader";
            default: return "Unknown Vulkan error";
        }
#elif defined(RHI_SYSTEM_USE_DX12)
        switch (static_cast<HRESULT>(nativeResult))
        {
            case S_OK: return "Success";
            case S_FALSE: return "False/Not ready";
            case E_OUTOFMEMORY: return "Out of memory";
            case E_INVALIDARG: return "Invalid argument";
            case E_FAIL: return "General failure";
            case E_NOTIMPL: return "Not implemented";
            case E_NOINTERFACE: return "Interface not supported";
            case DXGI_ERROR_DEVICE_REMOVED: return "Device removed";
            case DXGI_ERROR_DEVICE_RESET: return "Device reset";
            case DXGI_ERROR_DRIVER_INTERNAL_ERROR: return "Driver internal error";
            case DXGI_ERROR_INVALID_CALL: return "Invalid call";
            case DXGI_ERROR_WAS_STILL_DRAWING: return "Still drawing";
            case DXGI_ERROR_UNSUPPORTED: return "Unsupported";
            default: return "Unknown DirectX error";
        }
#else
        return "Unknown error (no graphics API selected)";
#endif
    }

    Result FromVulkan(int32_t vkResult) noexcept
    {
        return Result(vkResult);
    }

    Result FromDirectX(int32_t hresult) noexcept
    {
        return Result(hresult);
    }

#ifdef RHI_SYSTEM_USE_DX12
    Result::Code Result::MapHResultToCode(HRESULT hr) const noexcept
    {
        switch (hr)
        {
            case S_OK: return Code::Success;
            case S_FALSE: return Code::NotReady;
            case E_OUTOFMEMORY: return Code::OutOfHostMemory;
            case E_INVALIDARG: return Code::ValidationFailedEXT;
            case E_FAIL: return Code::InitializationFailed;
            case DXGI_ERROR_DEVICE_REMOVED: return Code::DeviceLost;
            case DXGI_ERROR_DEVICE_RESET: return Code::DeviceLost;
            case DXGI_ERROR_DRIVER_INTERNAL_ERROR: return Code::IncompatibleDriver;
            case DXGI_ERROR_INVALID_CALL: return Code::ValidationFailedEXT;
            case DXGI_ERROR_WAS_STILL_DRAWING: return Code::NotReady;
            case DXGI_ERROR_UNSUPPORTED: return Code::FeatureNotPresent;
            default: return Code::UnknownError;
        }
    }
#endif

} // namespace rhi