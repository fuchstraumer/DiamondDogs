#pragma once
#ifndef DIAMOND_DOGS_BOOTSTRAP_WINDOW_STAGE_HPP
#define DIAMOND_DOGS_BOOTSTRAP_WINDOW_STAGE_HPP
#include "DeviceRequirements.hpp"
#include <memory>
#include <span>
#include <vector>

class PlatformWindowSystem;
struct PlatformWindowCreateInfo;

namespace bootstrap
{
    struct BootResult;
    class ContentStage;

    /** @brief The stage that creates the GLFW window, before the instance exists, so that the device stage can ask
     *  for the surface extensions and check present support. A headless executable skips it. */
    class WindowStage
    {
    public:
        WindowStage() noexcept;
        /** @brief Calls Stop. */
        ~WindowStage();
        // the device and GPU stages store a reference to this stage
        WindowStage(const WindowStage&) = delete;
        WindowStage& operator=(const WindowStage&) = delete;
        WindowStage(WindowStage&&) = delete;
        WindowStage& operator=(WindowStage&&) = delete;

        /** @brief Creates the window, with no surface and no swapchain, and records the window's device needs. Fails
         *  with WindowCreateFailed when GLFW or the window does not start. `content` must outlive this stage. */
        [[nodiscard]] BootResult Start(const ContentStage& _content, const PlatformWindowCreateInfo& create_info);
        /** @brief Destroys the window. Stop the device stage first: it destroys the surface. */
        void Stop() noexcept;

        [[nodiscard]] bool Started() const noexcept;
        [[nodiscard]] const ContentStage& Content() const noexcept;
        [[nodiscard]] PlatformWindowSystem& Platform() const noexcept;
        /** @brief The needs of the window: the surface instance extensions, VK_KHR_get_surface_capabilities2,
         *  VK_KHR_swapchain, and the HDR extensions as Wanted. SpineNeeds adds these to the needs of the GPU stage. */
        [[nodiscard]] std::span<const Need> Needs() const noexcept;

    private:
        const ContentStage* content{ nullptr };
        std::unique_ptr<PlatformWindowSystem> platform;
        std::vector<Need> needs;
    };
}

#endif // !DIAMOND_DOGS_BOOTSTRAP_WINDOW_STAGE_HPP
