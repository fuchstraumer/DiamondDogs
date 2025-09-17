#pragma once
#ifndef PLATFORM_SYSTEM_HPP
#define PLATFORM_SYSTEM_HPP
#include "events/PlatformEvents.hpp"
#include "utility/Delegate.hpp"
#include "PlatformTypes.hpp"
#include <memory>


using CursorPosEvent = Delegate<void(const Events::CursorPosEventData&, void* userData)>;
using CursorEnterEvent = Delegate<void(const int, void* userData)>;
using ScrollEvent = Delegate<void(const Events::ScrollEventData&, void* userData)>;
using CharEvent = Delegate<void(const char, void* userData)>;
using PathDropEvent = Delegate<void(const Events::PathDropEventData&, void* userData)>;
using MouseButtonEvent = Delegate<void(const Events::MouseButtonEventData&, void* userData)>;
using KeyboardKeyEvent = Delegate<void(const Events::KeyboardKeyEventData&, void* userData)>;
using ShouldResizeEvent = Delegate<void(const Events::ShouldResizeEventData&, void* userData)>;
using ShouldCloseEvent = Delegate<void(void* userData)>;

// PlatformSystem uses PImpl mostly to avoid any GLFW includes leaking into the public API
struct PlatformSystemImpl;

/**
 * @brief System that owns the core GLFW window and handles input events
 */
class PlatformWindowSystem
{
public:

    // Following functions are available before initialization, to allow querying of system capabilities and selection/configuration of hardware devices
    static size_t GetNumDisplays() noexcept;
    static DisplayInfo GetDisplayInfo(const size_t displayIndex) noexcept;

    /** @brief Factory method that reads configuration values for the platform window and swapchain from given JSON path, and creates a default surface + swapchain alongside the window. */
    static std::unique_ptr<PlatformWindowSystem> CreatePlatformWindowSystem(const char* jsonPath,
                                                                            const uint64_t vkInstanceHandle,
                                                                            const uint64_t vkDeviceHandle,
                                                                            const uint64_t vkPhysicalDeviceHandle);
    /** @brief Factory method that uses a create info struct, and does not create a default surface or swapchain. Used primarily for unit testing. */
    static std::unique_ptr<PlatformWindowSystem> CreatePlatformWindowSystem(const PlatformWindowCreateInfo& createInfo,
                                                                            const uint64_t vkInstanceHandle);
    void Destroy();

    // Default swapchain just uses display info that we got from window creation as creation parameters. Also creates a surface using display parameters
    void CreateDefaultSwapchain(const uint64_t vkInstanceHandle, const uint64_t vkDeviceHandle, const uint64_t vkPhysicalDeviceHandle);
    void CreateSwapchain(const SwapchainCreateInfo& createInfo);
    void DestroySwapchain();

    void AddCursorPosEventListener(CursorPosEvent listener, void* userData);
    void AddCursorEnterEventListener(CursorEnterEvent listener, void* userData);
    void AddScrollEventListener(ScrollEvent listener, void* userData);
    void AddCharEventListener(CharEvent listener, void* userData);
    void AddPathDropEventListener(PathDropEvent listener, void* userData);
    void AddMouseButtonEventListener(MouseButtonEvent listener, void* userData);
    void AddKeyboardKeyEventListener(KeyboardKeyEvent listener, void* userData);
    void AddShouldResizeEventListener(ShouldResizeEvent listener, void* userData);
    void AddShouldCloseEventListener(ShouldCloseEvent listener, void* userData);

    /** @brief Display is chosen during window creation, and isn't managed by `DisplaySystem` as it's later in the init process. */
    const DisplayInfo& GetActiveDisplayInfo() const noexcept;
    const void* GetWindowHandle() const noexcept;

    // Lifecycle management methods
    void Update();
    void WaitForEvents();
    bool ShouldWindowClose() const;

    bool IsHDRCapable() const noexcept;

    // Window and input management (mirroring deprecated RhiSystem functionality)
    void GetWindowSize(int& w, int& h) const;
    void GetFramebufferSize(int& w, int& h) const;
    int GetMouseButton(int button) const;
    void GetCursorPosition(double& x, double& y) const;
    void SetCursorPosition(double x, double y);
    int GetWindowAttribute(int attrib) const;
    void SetWindowAttribute(int attrib, int value);
    int GetInputMode(int mode) const;
    void SetInputMode(int mode, int val);

    static bool IsHDREnabledOnSystem() noexcept;

private:
    
    PlatformWindowSystem();
    ~PlatformWindowSystem();

    std::unique_ptr<PlatformSystemImpl> impl;

};

#endif //!PLATFORM_SYSTEM_HPP
