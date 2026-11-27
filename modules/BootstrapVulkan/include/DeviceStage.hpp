#pragma once
#ifndef DIAMOND_DOGS_BOOTSTRAP_DEVICE_STAGE_HPP
#define DIAMOND_DOGS_BOOTSTRAP_DEVICE_STAGE_HPP
#include "DeviceCapability.hpp"
#include "DeviceFilter.hpp"
#include "RhiContext.hpp"
#include "RhiFlags.hpp"
#include <memory>
#include <vector>

namespace rhi
{
    class RhiSystem;
}

namespace bootstrap
{
    struct BootResult;
    struct DeviceConfig;
    struct DeviceRequirements;
    class ContentStage;
    class WindowStage;

    /** @brief The stage that creates the instance, the window surface, and the device with only the enables that
     *  the requirements ask for. */
    class DeviceStage
    {
    public:
        DeviceStage() noexcept;
        /** @brief Calls Stop. */
        ~DeviceStage();
        // the GPU stage and every system on the device store a reference to this stage
        DeviceStage(const DeviceStage&) = delete;
        DeviceStage& operator=(const DeviceStage&) = delete;
        DeviceStage(DeviceStage&&) = delete;
        DeviceStage& operator=(DeviceStage&&) = delete;

        /** @brief Creates the instance, then the window surface, then chooses the best physical device that meets
         *  every Required need and presents to the surface, then creates the device with only the planned enables.
         *  Fails with InstanceCreateFailed, SurfaceCreateFailed, NoDeviceMeetsRequirements (naming the need, where
         *  it came from, and the closest device) or DeviceCreateFailed, and then stops what it started.
         *  `_content`, `_window` and every name in `requirements` must outlive this stage. */
        [[nodiscard]] BootResult Start(const ContentStage& _content,
                                       const DeviceRequirements& requirements,
                                       const DeviceConfig& config,
                                       WindowStage& _window);
        /** @brief The headless path: no surface, and no present check. */
        [[nodiscard]] BootResult StartHeadless(const ContentStage& _content,
                                               const DeviceRequirements& requirements,
                                               const DeviceConfig& config);
        /** @brief Destroys the window surface (and with it any swapchain), then the device and the instance. Stop
         *  the GPU stage first. */
        void Stop() noexcept;

        [[nodiscard]] bool Started() const noexcept;
        [[nodiscard]] const ContentStage& Content() const noexcept;
        [[nodiscard]] const rhi::RhiContext& Context() const noexcept;
        [[nodiscard]] rhi::RhiSystem& Rhi() const noexcept;
        [[nodiscard]] rhi::ValidationLayers Validation() const noexcept;
        [[nodiscard]] DeviceFilter Filter() const noexcept;

    private:
        // `window_stage` is null on the headless path
        [[nodiscard]] BootResult start(const ContentStage& _content,
                                       const DeviceRequirements& requirements,
                                       const DeviceConfig& config,
                                       WindowStage* window_stage);

        const ContentStage* content{ nullptr };
        WindowStage* window{ nullptr };
        std::unique_ptr<rhi::RhiSystem> rhi;
        rhi::RhiContext context;
        rhi::ValidationLayers validation{ rhi::ValidationLayers::None };
        // the neutral capabilities that the device was created with, sorted
        std::vector<DeviceCapability> capabilities;
    };
}

#endif // !DIAMOND_DOGS_BOOTSTRAP_DEVICE_STAGE_HPP
