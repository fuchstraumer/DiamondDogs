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
class Swapchain;

/**
 * @brief System that owns the core GLFW window and handles input events
 */
class PlatformWindowSystem
{
    PlatformWindowSystem(const PlatformWindowSystem&) = delete;
    PlatformWindowSystem& operator=(const PlatformWindowSystem&) = delete;

public:
    
    /** @brief Reads configuration values for the platform window and swapchain from given JSON path, and creates a default surface + swapchain alongside the window. */
    PlatformWindowSystem(const char* jsonPath, void* RhiInstance, void* RhiDevice);
    /** @brief Uses a create info struct, and does not create a default surface or swapchain. Used primarily for unit testing. */
    PlatformWindowSystem(const PlatformWindowCreateInfo& createInfo);

    ~PlatformWindowSystem();

    void Destroy();

    // Default swapchain just uses display info that we got from window creation as creation parameters. Also creates a surface using display parameters
    void CreateDefaultSwapchain(void* rhiInstance, void* rhiDevice);
    void CreateSwapchain(const SwapchainCreateInfo& createInfo);
    void DestroySwapchain();
    const Swapchain* GetActiveSwapchain() const noexcept;

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
    /** @brief Returns the window handle for the GLFWwindow attached to this platform window system instance. */
    const void* GetWindowHandle() const noexcept;
    /** @brief Returns the primary display information, as in the underlying video mode and hardware configuration. Can differ from what is active for the system and rendering, though. */
    static DisplayInfo GetPrimaryDisplayInfo() noexcept;

    /** @brief Returns active present mode for the current swapchain attached to this platform window system instance. */
    PresentMode GetPresentMode() const noexcept;
    /** @brief Returns active windowing mode for the GLFWwindow attached to this platform window system instance. */
    PlatformWindowMode GetWindowMode() const noexcept;

    // Lifecycle management methods
    void Update();
    void WaitForEvents();
    bool ShouldWindowClose() const;

    // Window and input management (mirroring deprecated RhiSystem functionality)
    void GetWindowSize(int& w, int& h) const;
    void GetWindowPos(int& x, int& y) const;
    void GetFramebufferSize(int& w, int& h) const;
    int GetMouseButton(int button) const;
    void GetCursorPosition(double& x, double& y) const;
    void SetCursorPosition(double x, double y);
    int GetWindowAttribute(int attrib) const;
    void SetWindowAttribute(int attrib, int value);
    int GetInputMode(int mode) const;
    void SetInputMode(int mode, int val);
    void SetWindowShouldClose(bool shouldClose);

    static bool IsHDREnabledOnSystem() noexcept;

private:

    std::unique_ptr<PlatformSystemImpl> impl;

};

#endif //!PLATFORM_SYSTEM_HPP
