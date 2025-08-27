#pragma once
#ifndef PLATFORM_SYSTEM_HPP
#define PLATFORM_SYSTEM_HPP
#include "events/PlatformEvents.hpp"
#include <memory>

struct GLFWWindow;

namespace Platform
{
    struct PlatformSystemImpl; // mostly so I don't expose a bunch of standard library headers and classes

    /**
     * @brief System that owns the core GLFW window, creates/manages rendering surfaces (and hardware monitors for them), and handles input events.
     */
    class PlatformWindowSystem
    {
    public:
        
        static HDRCapabilities GetHDRCapabilities() noexcept;
        static size_t GetNumDisplays() noexcept;
        static DisplayInfo GetDisplayInfo(size_t displayIndex) noexcept;

        static PlatformWindowSystem* CreatePlatformWindowSystem();
        void Destroy();
    private:
        
        PlatformWindowSystem();
        ~PlatformWindowSystem();
        bool initialized{ false };
        struct GLFWWindow* window{ nullptr };
        std::unique_ptr<PlatformSystemImpl> impl;
    };
}

#endif //!PLATFORM_SYSTEM_HPP
