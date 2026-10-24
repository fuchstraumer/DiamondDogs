#pragma once
#ifndef PLATFORM_SYSTEM_HPP
#define PLATFORM_SYSTEM_HPP
#include "events/PlatformEvents.hpp"
#include "utility/delegate.hpp"
#include "PlatformTypes.hpp"
#include <memory>
#include <vector>


using CursorPosEvent = Delegate<void(const Events::CursorPosEventData&, void* user_data)>;
using CursorEnterEvent = Delegate<void(const int, void* user_data)>;
using ScrollEvent = Delegate<void(const Events::ScrollEventData&, void* user_data)>;
using CharEvent = Delegate<void(const char, void* user_data)>;
using PathDropEvent = Delegate<void(const Events::PathDropEventData&, void* user_data)>;
using MouseButtonEvent = Delegate<void(const Events::MouseButtonEventData&, void* user_data)>;
using KeyboardKeyEvent = Delegate<void(const Events::KeyboardKeyEventData&, void* user_data)>;
using ShouldResizeEvent = Delegate<void(const Events::ShouldResizeEventData&, void* user_data)>;
using ShouldCloseEvent = Delegate<void(void* user_data)>;

// PlatformSystem uses PImpl mostly to avoid any GLFW includes leaking into the public API
struct PlatformSystemImpl;
class Swapchain;

/**
 * @brief System that owns the core GLFW window and handles input events
 */
class PlatformWindowSystem
{

public:
    
    /** @brief Reads configuration values for the platform window and swapchain from given JSON path, and creates a default surface + swapchain alongside the window. */
    PlatformWindowSystem(const char* json_path, void* rhi_instance, void* rhi_device);
    /** @brief Uses a create info struct, and does not create a default surface or swapchain. Used primarily for unit testing. */
    PlatformWindowSystem(const PlatformWindowCreateInfo& create_info);
    PlatformWindowSystem(const PlatformWindowSystem&) = delete;
    PlatformWindowSystem& operator=(const PlatformWindowSystem&) = delete;
    ~PlatformWindowSystem();

    void Destroy();

    // Default swapchain just uses display info that we got from window creation as creation parameters. Also creates a surface using display parameters
    void CreateDefaultSwapchain(void* rhi_instance, void* rhi_device);
    /** @brief Returns the pairs that the window's surface offers, in driver order. Creates the surface when the window has none.
     * The HDR pairs need `VK_EXT_swapchain_colorspace`. */
    [[nodiscard]] std::vector<SurfaceFormatPair> QuerySurfaceFormats(void* rhi_instance, void* rhi_device);
    /** @brief Like CreateDefaultSwapchain, but with exactly `surface_format`, which must come from QuerySurfaceFormats.
     * Throws when the surface does not offer it. Sets no HDR metadata. */
    void CreateSwapchainWithSurfaceFormat(void* rhi_instance, void* rhi_device, const SurfaceFormatPair& surface_format);
    void CreateSwapchain(const SwapchainCreateInfo& create_info);
    void DestroySwapchain();
    /** @brief Recreates the active swapchain at the window's current size, with its last create info. Call it after
     * a present or acquire returns out of date or suboptimal, once the device has finished with the old images. */
    [[nodiscard]] SwapchainRecreateResult RecreateSwapchain();
    [[nodiscard]] const Swapchain* GetActiveSwapchain() const noexcept;
    /** @brief Returns the mutable swapchain, for Swapchain::SetHdrMetadata. */
    [[nodiscard]] Swapchain* GetActiveSwapchain() noexcept;

    void AddCursorPosEventListener(const CursorPosEvent& listener, void* user_data);
    void AddCursorEnterEventListener(const CursorEnterEvent& listener, void* user_data);
    void AddScrollEventListener(const ScrollEvent& listener, void* user_data);
    void AddCharEventListener(const CharEvent& listener, void* user_data);
    void AddPathDropEventListener(const PathDropEvent& listener, void* user_data);
    void AddMouseButtonEventListener(const MouseButtonEvent& listener, void* user_data);
    void AddKeyboardKeyEventListener(const KeyboardKeyEvent& listener, void* user_data);
    void AddShouldResizeEventListener(const ShouldResizeEvent& listener, void* user_data);
    void AddShouldCloseEventListener(const ShouldCloseEvent& listener, void* user_data);

    /** @brief Display is chosen during window creation, and isn't managed by `DisplaySystem` as it's later in the init process. */
    [[nodiscard]] const DisplayInfo& GetActiveDisplayInfo() const noexcept;
    /** @brief Returns the window handle for the GLFWwindow attached to this platform window system instance. */
    [[nodiscard]] void* GetWindowHandle() const noexcept;
    /** @brief Returns the primary display information, as in the underlying video mode and hardware configuration. Can differ from what is active for the system and rendering, though. */
    static DisplayInfo GetPrimaryDisplayInfo() noexcept;

    /** @brief Returns active present mode for the current swapchain attached to this platform window system instance. */
    [[nodiscard]] PresentMode GetPresentMode() const noexcept;
    /** @brief Returns active windowing mode for the GLFWwindow attached to this platform window system instance. */
    [[nodiscard]] PlatformWindowMode GetWindowMode() const noexcept;

    // Lifecycle management methods
    void Update();
    void WaitForEvents();
    [[nodiscard]] bool ShouldWindowClose() const;

    // Window and input management (mirroring deprecated RhiSystem functionality)
    void GetWindowSize(int& w, int& h) const;
    void SetWindowSize(int w, int h);
    void GetWindowPos(int& x, int& y) const;
    void GetFramebufferSize(int& w, int& h) const;
    [[nodiscard]] int GetMouseButton(int button) const;
    void GetCursorPosition(double& x, double& y) const;
    void SetCursorPosition(double x, double y);
    [[nodiscard]] int GetWindowAttribute(int attrib) const;
    void SetWindowAttribute(int attrib, int value);
    [[nodiscard]] int GetInputMode(int mode) const;
    void SetInputMode(int mode, int val);
    void SetWindowShouldClose(bool should_close);

    static bool IsHDREnabledOnSystem() noexcept;

private:

    std::unique_ptr<PlatformSystemImpl> impl;

};

#endif //!PLATFORM_SYSTEM_HPP
