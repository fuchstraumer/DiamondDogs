#include "PlatformTypes.hpp"
#include "vulkan/vulkan_core.h"
#include <combaseapi.h>
#include <cstddef>
#include <dxgi.h>
#include <dxgi1_3.h>
#include <dxgicommon.h>
#include <stringapiset.h>
#include <winerror.h>
#include <wingdi.h>
#include <winnls.h>
#include <winnt.h>
#include <winuser.h>
#define NTDDI_VERSION 0x0A00000F

#include <windows.h>
#include <wrl/client.h>
#include <dxgi1_6.h>

#include "PlatformDisplayInfo.hpp"

#include <vulkan/vulkan.h>
#include <GLFW/glfw3.h>

#include <algorithm>
#include <cstdint>
#include <format>
#include <print>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace
{
    std::string_view VkFormatName(VkFormat _format);
    std::string DescribeVkFormat(VkFormat _format);
    std::string_view VkColorSpaceName(VkColorSpaceKHR _colorSpace);
    std::string DescribeVkColorSpace(VkColorSpaceKHR _colorSpace);
    std::string_view DxgiColorSpaceName(DXGI_COLOR_SPACE_TYPE _colorSpace);
    std::string_view AdvancedColorModeName(DISPLAYCONFIG_ADVANCED_COLOR_MODE _mode);
    std::string_view ColorEncodingName(DISPLAYCONFIG_COLOR_ENCODING _encoding);
    std::string NarrowWide(const wchar_t* _wide);
    bool VkCallSucceeded(const char* _call, VkResult _result);
    std::vector<VkExtensionProperties> EnumerateInstanceExtensions();
    std::vector<VkExtensionProperties> EnumerateDeviceExtensions(VkPhysicalDevice _physicalDevice);
    bool HasExtension(std::span<const VkExtensionProperties> _extensions, std::string_view _name);
    void ProbeVulkanSurface();
    void ProbeVulkanSurfaceOnWindow(GLFWwindow* _window);
    void ProbePhysicalDevices(VkInstance _instance, VkSurfaceKHR _surface);
    void PrintPhysicalDevice(VkPhysicalDevice _physicalDevice, VkSurfaceKHR _surface);
    void PrintSurfaceFormats(VkPhysicalDevice _physicalDevice, VkSurfaceKHR _surface);
    void ProbeDxgiOutputs();
    void PrintDxgiAdapterOutputs(IDXGIAdapter1* _adapter, uint32_t adapter_index);
    void PrintDxgiOutput(IDXGIOutput* _output, uint32_t adapter_index, uint32_t output_index);
    void ProbeDisplayConfig();
    void ProbePlatformSystem();
    std::string_view DisplayValueSourceName(DisplayValueSource _source);
    void PrintTargetName(const DISPLAYCONFIG_PATH_INFO& _path);
    void PrintAdvancedColorInfo(const DISPLAYCONFIG_PATH_INFO& _path);
    void PrintSdrWhiteLevel(const DISPLAYCONFIG_PATH_INFO& _path);
    template<typename T>
    DISPLAYCONFIG_DEVICE_INFO_HEADER MakeDeviceInfoHeader(
        const DISPLAYCONFIG_PATH_INFO& _path,
        DISPLAYCONFIG_DEVICE_INFO_TYPE _type);
}

int main(int argc, char** argv)
{
    bool probePlatform = false;
    for (int32_t argIndex = 1; argIndex < argc; ++argIndex)
    {
        if (std::string_view{ argv[argIndex] } == "--platform")
        {
            probePlatform = true;
        }
    }

    ProbeVulkanSurface();
    ProbeDxgiOutputs();
    ProbeDisplayConfig();

    if (probePlatform)
    {
        ProbePlatformSystem();
    }

    return 0;
}

namespace
{
    std::string_view VkFormatName(VkFormat _format)
    {
        switch (_format)
        {
            case VK_FORMAT_R8G8B8A8_UNORM: return "VK_FORMAT_R8G8B8A8_UNORM";
            case VK_FORMAT_R8G8B8A8_SRGB: return "VK_FORMAT_R8G8B8A8_SRGB";
            case VK_FORMAT_B8G8R8A8_UNORM: return "VK_FORMAT_B8G8R8A8_UNORM";
            case VK_FORMAT_B8G8R8A8_SRGB: return "VK_FORMAT_B8G8R8A8_SRGB";
            case VK_FORMAT_A2R10G10B10_UNORM_PACK32: return "VK_FORMAT_A2R10G10B10_UNORM_PACK32";
            case VK_FORMAT_A2B10G10R10_UNORM_PACK32: return "VK_FORMAT_A2B10G10R10_UNORM_PACK32";
            case VK_FORMAT_R16G16B16A16_UNORM: return "VK_FORMAT_R16G16B16A16_UNORM";
            case VK_FORMAT_R16G16B16A16_SFLOAT: return "VK_FORMAT_R16G16B16A16_SFLOAT";
            case VK_FORMAT_B10G11R11_UFLOAT_PACK32: return "VK_FORMAT_B10G11R11_UFLOAT_PACK32";
            default: return {};
        }
    }

    std::string DescribeVkFormat(VkFormat _format)
    {
        const std::string_view name = VkFormatName(_format);
        if (name.empty())
        {
            return std::format("VkFormat({})", static_cast<int32_t>(_format));
        }

        return std::string{ name };
    }

    std::string_view VkColorSpaceName(VkColorSpaceKHR _colorSpace)
    {
        switch (_colorSpace)
        {
            case VK_COLOR_SPACE_SRGB_NONLINEAR_KHR: return "VK_COLOR_SPACE_SRGB_NONLINEAR_KHR";
            case VK_COLOR_SPACE_DISPLAY_P3_NONLINEAR_EXT: return "VK_COLOR_SPACE_DISPLAY_P3_NONLINEAR_EXT";
            case VK_COLOR_SPACE_EXTENDED_SRGB_LINEAR_EXT: return "VK_COLOR_SPACE_EXTENDED_SRGB_LINEAR_EXT";
            case VK_COLOR_SPACE_EXTENDED_SRGB_NONLINEAR_EXT: return "VK_COLOR_SPACE_EXTENDED_SRGB_NONLINEAR_EXT";
            case VK_COLOR_SPACE_DISPLAY_P3_LINEAR_EXT: return "VK_COLOR_SPACE_DISPLAY_P3_LINEAR_EXT";
            case VK_COLOR_SPACE_DCI_P3_NONLINEAR_EXT: return "VK_COLOR_SPACE_DCI_P3_NONLINEAR_EXT";
            case VK_COLOR_SPACE_BT709_LINEAR_EXT: return "VK_COLOR_SPACE_BT709_LINEAR_EXT";
            case VK_COLOR_SPACE_BT709_NONLINEAR_EXT: return "VK_COLOR_SPACE_BT709_NONLINEAR_EXT";
            case VK_COLOR_SPACE_BT2020_LINEAR_EXT: return "VK_COLOR_SPACE_BT2020_LINEAR_EXT";
            case VK_COLOR_SPACE_HDR10_ST2084_EXT: return "VK_COLOR_SPACE_HDR10_ST2084_EXT";
            case VK_COLOR_SPACE_DOLBYVISION_EXT: return "VK_COLOR_SPACE_DOLBYVISION_EXT";
            case VK_COLOR_SPACE_HDR10_HLG_EXT: return "VK_COLOR_SPACE_HDR10_HLG_EXT";
            case VK_COLOR_SPACE_ADOBERGB_LINEAR_EXT: return "VK_COLOR_SPACE_ADOBERGB_LINEAR_EXT";
            case VK_COLOR_SPACE_ADOBERGB_NONLINEAR_EXT: return "VK_COLOR_SPACE_ADOBERGB_NONLINEAR_EXT";
            case VK_COLOR_SPACE_PASS_THROUGH_EXT: return "VK_COLOR_SPACE_PASS_THROUGH_EXT";
            default: return {};
        }
    }

    std::string DescribeVkColorSpace(VkColorSpaceKHR _colorSpace)
    {
        const std::string_view name = VkColorSpaceName(_colorSpace);
        if (name.empty())
        {
            return std::format("VkColorSpaceKHR({})", static_cast<int32_t>(_colorSpace));
        }

        return std::string{ name };
    }

    std::string_view DxgiColorSpaceName(DXGI_COLOR_SPACE_TYPE _colorSpace)
    {
        switch (_colorSpace)
        {
            case DXGI_COLOR_SPACE_RGB_FULL_G22_NONE_P709: return "RGB_FULL_G22_NONE_P709";
            case DXGI_COLOR_SPACE_RGB_FULL_G10_NONE_P709: return "RGB_FULL_G10_NONE_P709";
            case DXGI_COLOR_SPACE_RGB_STUDIO_G22_NONE_P709: return "RGB_STUDIO_G22_NONE_P709";
            case DXGI_COLOR_SPACE_RGB_STUDIO_G22_NONE_P2020: return "RGB_STUDIO_G22_NONE_P2020";
            case DXGI_COLOR_SPACE_RGB_FULL_G2084_NONE_P2020: return "RGB_FULL_G2084_NONE_P2020";
            case DXGI_COLOR_SPACE_RGB_STUDIO_G2084_NONE_P2020: return "RGB_STUDIO_G2084_NONE_P2020";
            case DXGI_COLOR_SPACE_RGB_FULL_G22_NONE_P2020: return "RGB_FULL_G22_NONE_P2020";
            case DXGI_COLOR_SPACE_RGB_STUDIO_G24_NONE_P709: return "RGB_STUDIO_G24_NONE_P709";
            case DXGI_COLOR_SPACE_RGB_STUDIO_G24_NONE_P2020: return "RGB_STUDIO_G24_NONE_P2020";
            case DXGI_COLOR_SPACE_CUSTOM: return "CUSTOM";
            default: return "unlisted";
        }
    }

    std::string_view AdvancedColorModeName(DISPLAYCONFIG_ADVANCED_COLOR_MODE _mode)
    {
        switch (_mode)
        {
            case DISPLAYCONFIG_ADVANCED_COLOR_MODE_SDR: return "SDR";
            case DISPLAYCONFIG_ADVANCED_COLOR_MODE_WCG: return "WCG";
            case DISPLAYCONFIG_ADVANCED_COLOR_MODE_HDR: return "HDR";
            default: return "unlisted";
        }
    }

    std::string_view ColorEncodingName(DISPLAYCONFIG_COLOR_ENCODING _encoding)
    {
        switch (_encoding)
        {
            case DISPLAYCONFIG_COLOR_ENCODING_RGB: return "RGB";
            case DISPLAYCONFIG_COLOR_ENCODING_YCBCR444: return "YCBCR444";
            case DISPLAYCONFIG_COLOR_ENCODING_YCBCR422: return "YCBCR422";
            case DISPLAYCONFIG_COLOR_ENCODING_YCBCR420: return "YCBCR420";
            case DISPLAYCONFIG_COLOR_ENCODING_INTENSITY: return "INTENSITY";
            default: return "unlisted";
        }
    }

    std::string NarrowWide(const wchar_t* _wide)
    {
        const int32_t byteCount = WideCharToMultiByte(CP_UTF8, 0, _wide, -1, nullptr, 0, nullptr, nullptr);
        if (byteCount <= 0)
        {
            return {};
        }

        std::string narrow(static_cast<size_t>(byteCount - 1), '\0');
        WideCharToMultiByte(CP_UTF8, 0, _wide, -1, narrow.data(), byteCount, nullptr, nullptr);

        return narrow;
    }

    bool VkCallSucceeded(const char* _call, VkResult _result)
    {
        if (_result == VK_SUCCESS)
        {
            return true;
        }

        std::println("[Vulkan] {} failed: VkResult {}", _call, static_cast<int32_t>(_result));

        return false;
    }

    std::vector<VkExtensionProperties> EnumerateInstanceExtensions()
    {
        uint32_t extensionCount = 0;
        const VkResult countResult = vkEnumerateInstanceExtensionProperties(nullptr, &extensionCount, nullptr);
        if (!VkCallSucceeded("vkEnumerateInstanceExtensionProperties", countResult))
        {
            return {};
        }

        std::vector<VkExtensionProperties> extensions(extensionCount);
        const VkResult listResult = vkEnumerateInstanceExtensionProperties(nullptr, &extensionCount, extensions.data());
        if (!VkCallSucceeded("vkEnumerateInstanceExtensionProperties", listResult))
        {
            return {};
        }

        return extensions;
    }

    std::vector<VkExtensionProperties> EnumerateDeviceExtensions(VkPhysicalDevice _physicalDevice)
    {
        uint32_t extensionCount = 0;
        const VkResult countResult = vkEnumerateDeviceExtensionProperties(_physicalDevice, nullptr, &extensionCount, nullptr);
        if (!VkCallSucceeded("vkEnumerateDeviceExtensionProperties", countResult))
        {
            return {};
        }

        std::vector<VkExtensionProperties> extensions(extensionCount);
        const VkResult listResult
            = vkEnumerateDeviceExtensionProperties(_physicalDevice, nullptr, &extensionCount, extensions.data());
        if (!VkCallSucceeded("vkEnumerateDeviceExtensionProperties", listResult))
        {
            return {};
        }

        return extensions;
    }

    bool HasExtension(std::span<const VkExtensionProperties> _extensions, std::string_view _name)
    {
        return std::ranges::any_of(
            _extensions,
            [_name](const VkExtensionProperties& _extension)
            {
                return _name == std::string_view{ _extension.extensionName };
            });
    }

    void ProbeVulkanSurface()
    {
        if (glfwInit() != GLFW_TRUE)
        {
            std::println("[Vulkan] glfwInit failed");
            return;
        }

        glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);
        glfwWindowHint(GLFW_VISIBLE, GLFW_FALSE);
        GLFWwindow* window = glfwCreateWindow(64, 64, "DisplayProbe", nullptr, nullptr);
        if (window == nullptr)
        {
            std::println("[Vulkan] glfwCreateWindow failed");
            glfwTerminate();
            return;
        }

        ProbeVulkanSurfaceOnWindow(window);

        glfwDestroyWindow(window);
        glfwTerminate();
    }

    void ProbeVulkanSurfaceOnWindow(GLFWwindow* _window)
    {
        const std::vector<VkExtensionProperties> instanceExtensions = EnumerateInstanceExtensions();
        const bool hasSwapchainColorSpace
            = HasExtension(instanceExtensions, VK_EXT_SWAPCHAIN_COLOR_SPACE_EXTENSION_NAME);
        const bool hasSurfaceCapabilities2
            = HasExtension(instanceExtensions, VK_KHR_GET_SURFACE_CAPABILITIES_2_EXTENSION_NAME);
        std::println("[Vulkan] VK_EXT_swapchain_colorspace available: {}", hasSwapchainColorSpace);
        std::println("[Vulkan] VK_KHR_get_surface_capabilities2 available: {}", hasSurfaceCapabilities2);

        uint32_t glfwExtensionCount = 0;
        const char** glfwExtensions = glfwGetRequiredInstanceExtensions(&glfwExtensionCount);
        std::vector<const char*> extensionNames(glfwExtensions, glfwExtensions + glfwExtensionCount);
        if (hasSwapchainColorSpace)
        {
            extensionNames.push_back(VK_EXT_SWAPCHAIN_COLOR_SPACE_EXTENSION_NAME);
        }

        if (hasSurfaceCapabilities2)
        {
            extensionNames.push_back(VK_KHR_GET_SURFACE_CAPABILITIES_2_EXTENSION_NAME);
        }

        VkApplicationInfo applicationInfo{};
        applicationInfo.sType = VK_STRUCTURE_TYPE_APPLICATION_INFO;
        applicationInfo.pApplicationName = "DisplayProbe";
        applicationInfo.applicationVersion = VK_MAKE_VERSION(1, 0, 0);
        applicationInfo.apiVersion = VK_API_VERSION_1_3;

        VkInstanceCreateInfo createInfo{};
        createInfo.sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO;
        createInfo.pApplicationInfo = &applicationInfo;
        createInfo.enabledExtensionCount = static_cast<uint32_t>(extensionNames.size());
        createInfo.ppEnabledExtensionNames = extensionNames.data();

        VkInstance instance = VK_NULL_HANDLE;
        const VkResult instanceResult = vkCreateInstance(&createInfo, nullptr, &instance);
        if (!VkCallSucceeded("vkCreateInstance", instanceResult))
        {
            return;
        }

        VkSurfaceKHR surface = VK_NULL_HANDLE;
        const VkResult surfaceResult = glfwCreateWindowSurface(instance, _window, nullptr, &surface);
        if (VkCallSucceeded("glfwCreateWindowSurface", surfaceResult))
        {
            ProbePhysicalDevices(instance, surface);
            vkDestroySurfaceKHR(instance, surface, nullptr);
        }

        vkDestroyInstance(instance, nullptr);
    }

    void ProbePhysicalDevices(VkInstance _instance, VkSurfaceKHR _surface)
    {
        uint32_t deviceCount = 0;
        const VkResult countResult = vkEnumeratePhysicalDevices(_instance, &deviceCount, nullptr);
        if (!VkCallSucceeded("vkEnumeratePhysicalDevices", countResult))
        {
            return;
        }

        std::vector<VkPhysicalDevice> physicalDevices(deviceCount);
        const VkResult listResult = vkEnumeratePhysicalDevices(_instance, &deviceCount, physicalDevices.data());
        if (!VkCallSucceeded("vkEnumeratePhysicalDevices", listResult))
        {
            return;
        }

        for (VkPhysicalDevice physicalDevice : physicalDevices)
        {
            PrintPhysicalDevice(physicalDevice, _surface);
        }
    }

    void PrintPhysicalDevice(VkPhysicalDevice _physicalDevice, VkSurfaceKHR _surface)
    {
        VkPhysicalDeviceProperties properties{};
        vkGetPhysicalDeviceProperties(_physicalDevice, &properties);
        std::println("[Vulkan] physical device: {}", std::string_view{ properties.deviceName });

        const std::vector<VkExtensionProperties> deviceExtensions = EnumerateDeviceExtensions(_physicalDevice);
        const bool hasHdrMetadata = HasExtension(deviceExtensions, VK_EXT_HDR_METADATA_EXTENSION_NAME);
        std::println("[Vulkan]   VK_EXT_hdr_metadata available: {}", hasHdrMetadata);

        PrintSurfaceFormats(_physicalDevice, _surface);
    }

    void PrintSurfaceFormats(VkPhysicalDevice _physicalDevice, VkSurfaceKHR _surface)
    {
        uint32_t formatCount = 0;
        const VkResult countResult
            = vkGetPhysicalDeviceSurfaceFormatsKHR(_physicalDevice, _surface, &formatCount, nullptr);
        if (!VkCallSucceeded("vkGetPhysicalDeviceSurfaceFormatsKHR", countResult))
        {
            return;
        }

        std::vector<VkSurfaceFormatKHR> surfaceFormats(formatCount);
        const VkResult listResult
            = vkGetPhysicalDeviceSurfaceFormatsKHR(_physicalDevice, _surface, &formatCount, surfaceFormats.data());
        if (!VkCallSucceeded("vkGetPhysicalDeviceSurfaceFormatsKHR", listResult))
        {
            return;
        }

        for (const VkSurfaceFormatKHR& surfaceFormat : surfaceFormats)
        {
            std::println(
                "[Vulkan]   surface format {}, color space {}",
                DescribeVkFormat(surfaceFormat.format),
                DescribeVkColorSpace(surfaceFormat.colorSpace));
        }
    }

    void ProbeDxgiOutputs()
    {
        Microsoft::WRL::ComPtr<IDXGIFactory6> factory;
        const HRESULT factoryResult = CreateDXGIFactory2(0, IID_PPV_ARGS(&factory));
        if (FAILED(factoryResult))
        {
            std::println("[DXGI] CreateDXGIFactory2 failed: HRESULT 0x{:08X}", static_cast<uint32_t>(factoryResult));
            return;
        }

        for (uint32_t adapterIndex = 0;; ++adapterIndex)
        {
            Microsoft::WRL::ComPtr<IDXGIAdapter1> adapter;
            const HRESULT adapterResult = factory->EnumAdapters1(adapterIndex, adapter.GetAddressOf());
            if (adapterResult == DXGI_ERROR_NOT_FOUND)
            {
                break;
            }

            if (FAILED(adapterResult))
            {
                std::println("[DXGI] EnumAdapters1 failed: HRESULT 0x{:08X}", static_cast<uint32_t>(adapterResult));
                break;
            }

            PrintDxgiAdapterOutputs(adapter.Get(), adapterIndex);
        }
    }

    void PrintDxgiAdapterOutputs(IDXGIAdapter1* _adapter, uint32_t adapter_index)
    {
        for (uint32_t outputIndex = 0;; ++outputIndex)
        {
            Microsoft::WRL::ComPtr<IDXGIOutput> output;
            const HRESULT outputResult = _adapter->EnumOutputs(outputIndex, output.GetAddressOf());
            if (outputResult == DXGI_ERROR_NOT_FOUND)
            {
                break;
            }

            if (FAILED(outputResult))
            {
                std::println("[DXGI] EnumOutputs failed: HRESULT 0x{:08X}", static_cast<uint32_t>(outputResult));
                break;
            }

            PrintDxgiOutput(output.Get(), adapter_index, outputIndex);
        }
    }

    void PrintDxgiOutput(IDXGIOutput* _output, uint32_t adapter_index, uint32_t output_index)
    {
        Microsoft::WRL::ComPtr<IDXGIOutput6> output6;
        const HRESULT queryResult = _output->QueryInterface(IID_PPV_ARGS(&output6));
        if (FAILED(queryResult))
        {
            std::println(
                "[DXGI] adapter {} output {}: IDXGIOutput6 unavailable, HRESULT 0x{:08X}",
                adapter_index,
                output_index,
                static_cast<uint32_t>(queryResult));
            return;
        }

        DXGI_OUTPUT_DESC1 desc{};
        const HRESULT descResult = output6->GetDesc1(&desc);
        if (FAILED(descResult))
        {
            std::println(
                "[DXGI] adapter {} output {}: GetDesc1 failed, HRESULT 0x{:08X}",
                adapter_index,
                output_index,
                static_cast<uint32_t>(descResult));
            return;
        }

        std::println("[DXGI] adapter {} output {}", adapter_index, output_index);
        std::println("  DeviceName: {}", NarrowWide(desc.DeviceName));
        std::println(
            "  ColorSpace: {} ({})",
            static_cast<uint32_t>(desc.ColorSpace),
            DxgiColorSpaceName(desc.ColorSpace));
        std::println("  BitsPerColor: {}", desc.BitsPerColor);
        std::println("  MinLuminance: {}", desc.MinLuminance);
        std::println("  MaxLuminance: {}", desc.MaxLuminance);
        std::println("  MaxFullFrameLuminance: {}", desc.MaxFullFrameLuminance);
        std::println("  RedPrimary: {}, {}", desc.RedPrimary[0], desc.RedPrimary[1]);
        std::println("  GreenPrimary: {}, {}", desc.GreenPrimary[0], desc.GreenPrimary[1]);
        std::println("  BluePrimary: {}, {}", desc.BluePrimary[0], desc.BluePrimary[1]);
        std::println("  WhitePoint: {}, {}", desc.WhitePoint[0], desc.WhitePoint[1]);
    }

    void ProbeDisplayConfig()
    {
        uint32_t pathCount = 0;
        uint32_t modeCount = 0;
        const LONG sizeResult = GetDisplayConfigBufferSizes(QDC_ONLY_ACTIVE_PATHS, &pathCount, &modeCount);
        if (sizeResult != ERROR_SUCCESS)
        {
            std::println("[DisplayConfig] GetDisplayConfigBufferSizes failed: {}", sizeResult);
            return;
        }

        std::vector<DISPLAYCONFIG_PATH_INFO> paths(pathCount);
        std::vector<DISPLAYCONFIG_MODE_INFO> modes(modeCount);
        const LONG queryResult
            = QueryDisplayConfig(QDC_ONLY_ACTIVE_PATHS, &pathCount, paths.data(), &modeCount, modes.data(), nullptr);
        if (queryResult != ERROR_SUCCESS)
        {
            std::println("[DisplayConfig] QueryDisplayConfig failed: {}", queryResult);
            return;
        }

        paths.resize(pathCount);
        std::println("[DisplayConfig] active paths: {}", pathCount);

        for (const DISPLAYCONFIG_PATH_INFO& path : paths)
        {
            PrintTargetName(path);
            PrintAdvancedColorInfo(path);
            PrintSdrWhiteLevel(path);
        }
    }

    void PrintTargetName(const DISPLAYCONFIG_PATH_INFO& _path)
    {
        DISPLAYCONFIG_TARGET_DEVICE_NAME targetName{};
        targetName.header = MakeDeviceInfoHeader<DISPLAYCONFIG_TARGET_DEVICE_NAME>(
            _path,
            DISPLAYCONFIG_DEVICE_INFO_GET_TARGET_NAME);
        const LONG result = DisplayConfigGetDeviceInfo(&targetName.header);
        if (result != ERROR_SUCCESS)
        {
            std::println("  target name: DisplayConfigGetDeviceInfo failed: {}", result);
            return;
        }

        std::println("  monitor friendly name: {}", NarrowWide(targetName.monitorFriendlyDeviceName));
    }

    void PrintAdvancedColorInfo(const DISPLAYCONFIG_PATH_INFO& _path)
    {
        DISPLAYCONFIG_GET_ADVANCED_COLOR_INFO_2 colorInfo{};
        colorInfo.header = MakeDeviceInfoHeader<DISPLAYCONFIG_GET_ADVANCED_COLOR_INFO_2>(
            _path,
            DISPLAYCONFIG_DEVICE_INFO_GET_ADVANCED_COLOR_INFO_2);
        const LONG result = DisplayConfigGetDeviceInfo(&colorInfo.header);
        if (result != ERROR_SUCCESS)
        {
            std::println("  advanced color info 2: DisplayConfigGetDeviceInfo failed: {}", result);
            return;
        }

        std::println("  advancedColorSupported: {}", static_cast<bool>(colorInfo.advancedColorSupported));
        std::println("  advancedColorActive: {}", static_cast<bool>(colorInfo.advancedColorActive));
        std::println(
            "  advancedColorLimitedByPolicy: {}",
            static_cast<bool>(colorInfo.advancedColorLimitedByPolicy));
        std::println(
            "  highDynamicRangeSupported: {}",
            static_cast<bool>(colorInfo.highDynamicRangeSupported));
        std::println(
            "  highDynamicRangeUserEnabled: {}",
            static_cast<bool>(colorInfo.highDynamicRangeUserEnabled));
        std::println("  wideColorSupported: {}", static_cast<bool>(colorInfo.wideColorSupported));
        std::println("  wideColorUserEnabled: {}", static_cast<bool>(colorInfo.wideColorUserEnabled));
        std::println(
            "  activeColorMode: {} ({})",
            static_cast<uint32_t>(colorInfo.activeColorMode),
            AdvancedColorModeName(colorInfo.activeColorMode));
        std::println(
            "  colorEncoding: {} ({})",
            static_cast<uint32_t>(colorInfo.colorEncoding),
            ColorEncodingName(colorInfo.colorEncoding));
        std::println("  bitsPerColorChannel: {}", colorInfo.bitsPerColorChannel);
    }

    void PrintSdrWhiteLevel(const DISPLAYCONFIG_PATH_INFO& _path)
    {
        DISPLAYCONFIG_SDR_WHITE_LEVEL whiteLevel{};
        whiteLevel.header = MakeDeviceInfoHeader<DISPLAYCONFIG_SDR_WHITE_LEVEL>(
            _path,
            DISPLAYCONFIG_DEVICE_INFO_GET_SDR_WHITE_LEVEL);
        const LONG result = DisplayConfigGetDeviceInfo(&whiteLevel.header);
        if (result != ERROR_SUCCESS)
        {
            std::println("  SDR white level: DisplayConfigGetDeviceInfo failed: {}", result);
            return;
        }

        const double whiteNits = static_cast<double>(whiteLevel.SDRWhiteLevel) / 1000.0 * 80.0;
        std::println("  SDRWhiteLevel raw: {}", whiteLevel.SDRWhiteLevel);
        std::println("  SDRWhiteLevel nits if 1000 = 80 nits: {}", whiteNits);
    }

    std::string_view DisplayValueSourceName(DisplayValueSource _source)
    {
        switch (_source)
        {
            case DisplayValueSource::Dxgi: return "Dxgi";
            case DisplayValueSource::Edid: return "Edid";
            case DisplayValueSource::Fallback: return "Fallback";
            default: return "Invalid";
        }
    }

    void ProbePlatformSystem()
    {
        const DisplayInfo displayInfo = RetrievePlatformPrimaryDisplayInfo();
        const DisplayColorCapabilities& caps = displayInfo.ColorCapabilities;

        std::println("[PlatformSystem] RetrievePlatformPrimaryDisplayInfo");
        std::println("  Width x Height: {} x {}", displayInfo.Width, displayInfo.Height);
        std::println("  RefreshRate: {}", displayInfo.RefreshRateCapabilities.RefreshRate);
        std::println("  hdrSupported: {}", caps.hdrSupported);
        std::println("  hdrEnabled: {}", caps.hdrEnabled);
        std::println("  wcgSupported: {}", caps.wcgSupported);
        std::println("  wcgEnabled: {}", caps.wcgEnabled);
        std::println("  advancedColorLimitedByPolicy: {}", caps.advancedColorLimitedByPolicy);
        std::println("  sdrWhiteLevel (nits): {}", caps.sdrWhiteLevel);
        std::println("  bitsPerColorChannel: {}", caps.bitsPerColorChannel);
        std::println("  ColorMode: {}", static_cast<uint32_t>(caps.ColorMode));
        std::println("  MaxLuminance: {}", caps.MaxLuminance);
        std::println("  MinLuminance: {}", caps.MinLuminance);
        std::println("  MaxAverageLuminance: {}", caps.MaxAverageLuminance);
        std::println("  LuminanceSource: {}", DisplayValueSourceName(caps.LuminanceSource));
        std::println("  RedPrimary: {}, {}", caps.RedPrimaryX, caps.RedPrimaryY);
        std::println("  GreenPrimary: {}, {}", caps.GreenPrimaryX, caps.GreenPrimaryY);
        std::println("  BluePrimary: {}, {}", caps.BluePrimaryX, caps.BluePrimaryY);
        std::println("  WhitePoint: {}, {}", caps.WhitePointX, caps.WhitePointY);
        std::println("  HasChromaticityData: {}", caps.HasChromaticityData);
        std::println("  ChromaticitySource: {}", DisplayValueSourceName(caps.ChromaticitySource));
        std::println("  DetectedColorSpace (ColorSpace enum value): {}", static_cast<uint32_t>(caps.DetectedColorSpace));
        std::println("  ColorSpaceConfidence: {}", caps.ColorSpaceConfidence);
    }

    template<typename T>
    DISPLAYCONFIG_DEVICE_INFO_HEADER MakeDeviceInfoHeader(
        const DISPLAYCONFIG_PATH_INFO& _path,
        DISPLAYCONFIG_DEVICE_INFO_TYPE _type)
    {
        DISPLAYCONFIG_DEVICE_INFO_HEADER header{};
        header.type = _type;
        header.size = static_cast<uint32_t>(sizeof(T));
        header.adapterId = _path.targetInfo.adapterId;
        header.id = _path.targetInfo.id;

        return header;
    }
}
