#pragma once
#ifndef PLATFORM_SYSTEM_HPP
#define PLATFORM_SYSTEM_HPP
#include "events/PlatformEvents.hpp"
#include "utility/Delegate.hpp"
#include "PlatformTypes.hpp"
#include <memory>

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

    static PlatformWindowSystem* CreatePlatformWindowSystem(const PlatformWindowCreateInfo& createInfo, const void* vkInstancePtr);
    void Destroy();

    using CursorPosEvent = Delegate<void(const Events::CursorPosEventData&, void* userData)>;
    using CursorEnterEvent = Delegate<void(const int, void* userData)>;
    using ScrollEvent = Delegate<void(const Events::ScrollEventData&, void* userData)>;
    using CharEvent = Delegate<void(const char, void* userData)>;
    using PathDropEvent = Delegate<void(const Events::PathDropEventData&, void* userData)>;
    using MouseButtonEvent = Delegate<void(const Events::MouseButtonEventData&, void* userData)>;
    using KeyboardKeyEvent = Delegate<void(const Events::KeyboardKeyEventData&, void* userData)>;

    void AddCursorPosEventListener(CursorPosEvent listener, void* userData);
    void AddCursorEnterEventListener(CursorEnterEvent listener, void* userData);
    void AddScrollEventListener(ScrollEvent listener, void* userData);
    void AddCharEventListener(CharEvent listener, void* userData);
    void AddPathDropEventListener(PathDropEvent listener, void* userData);
    void AddMouseButtonEventListener(MouseButtonEvent listener, void* userData);
    void AddKeyboardKeyEventListener(KeyboardKeyEvent listener, void* userData);

    /** @brief Display is chosen during window creation, and isn't managed by `DisplaySystem` as it's later in the init process. */
    const DisplayInfo& GetActiveDisplayInfo() const noexcept;
    const void* GetWindowHandle() const noexcept;

private:
    
    PlatformWindowSystem();
    ~PlatformWindowSystem();

    std::unique_ptr<PlatformSystemImpl> impl;

};

#endif //!PLATFORM_SYSTEM_HPP
