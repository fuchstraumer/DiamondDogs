#pragma once
#ifndef DIAMOND_DOGS_RENDERING_CONTEXT_HPP
#define DIAMOND_DOGS_RENDERING_CONTEXT_HPP
#include <memory>
#include <vector>
#include <string>

namespace vpr {
    class Instance;
    class PhysicalDevice;
    class Device;
    class Swapchain;
    class SurfaceKHR;
}

class PlatformWindow;

class RenderingContext {
    RenderingContext() noexcept = default;
    ~RenderingContext() noexcept = default;
    RenderingContext(const RenderingContext&) = delete;
    RenderingContext& operator=(const RenderingContext&) = delete;
public:
    
    static RenderingContext& Get() noexcept;
    static void SetShouldResize(const bool val);
    static bool ShouldResizeExchange(const bool val);

    void Construct(const char* cfg_file_path);
    void Destroy();

    vpr::Instance* Instance() noexcept;
    vpr::PhysicalDevice* PhysicalDevice(const size_t idx = 0) noexcept;
    vpr::Device* Device() noexcept;
    vpr::Swapchain* Swapchain() noexcept;
    PlatformWindow* Window() noexcept;
    
    const std::vector<std::string>& InstanceExtensions() const noexcept;
    const std::vector<std::string>& DeviceExtensions() const noexcept;

private:

    std::unique_ptr<PlatformWindow> window;
    std::unique_ptr<vpr::Instance> vulkanInstance;
    std::vector<std::unique_ptr<vpr::PhysicalDevice>> physicalDevices;
    std::unique_ptr<vpr::Device> logicalDevice;
    std::unique_ptr<vpr::Swapchain> swapchain;

    std::vector<std::string> instanceExtensions;
    std::vector<std::string> deviceExtensions;
    std::string windowMode;
    uint32_t syncMode;
    std::string syncModeStr;
    

};


#endif //!DIAMOND_DOGS_RENDERING_CONTEXT_HPP
