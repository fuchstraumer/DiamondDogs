#include "WindowStage.hpp"
#include "BootResult.hpp"
#include "BootTimings.hpp"
#include "ContentStage.hpp"
#include "CoreStage.hpp"
#include "LaunchStage.hpp"
#include "PlatformSystem.hpp"
#include "PlatformTypes.hpp"
#include "Swapchain.hpp"
#include <exception>
#include <memory>
#include <string>

namespace
{
    // HDR output needs both. Wanted, so that a device without them still boots in SDR
    constexpr std::string_view k_HdrInstanceExtension{ "VK_EXT_swapchain_colorspace" };
    constexpr std::string_view k_HdrDeviceExtension{ "VK_EXT_hdr_metadata" };
    constexpr std::string_view k_SwapchainExtension{ "VK_KHR_swapchain" };
    // the swapchain reads the surface capabilities through vkGetPhysicalDeviceSurfaceCapabilities2KHR
    constexpr std::string_view k_SurfaceCapabilitiesExtension{ "VK_KHR_get_surface_capabilities2" };
    // swapchain maintenance1 (present fences, release, present-mode switching) needs surface maintenance1, in its KHR
    // or EXT form. All Wanted: without them the swapchain uses core presentation
    constexpr std::string_view k_SurfaceMaintenanceKhr{ "VK_KHR_surface_maintenance1" };
    constexpr std::string_view k_SurfaceMaintenanceExt{ "VK_EXT_surface_maintenance1" };

    [[nodiscard]] bootstrap::Need SpineNeed(std::string_view name, bootstrap::NeedKind kind, bootstrap::NeedLevel level) noexcept;
}

namespace bootstrap
{
    WindowStage::WindowStage() noexcept = default;

    WindowStage::~WindowStage()
    {
        Stop();
    }

    BootResult WindowStage::Start(const ContentStage& _content, const PlatformWindowCreateInfo& create_info)
    {
        Stop();

        const LaunchStage& launch = _content.Core().Launch();
        const ScopedBootSpan stageSpan(launch.Timings(), Stage::Window, BootSpanKind::Stage, ToString(Stage::Window));

        try
        {
            platform = std::make_unique<PlatformWindowSystem>(create_info);
        }
        catch (const std::exception& error)
        {
            return BootResult
            {
                .Code = BootResultCode::WindowCreateFailed,
                .FailedStage = Stage::Window,
                .Detail = error.what(),
            };
        }

        content = &_content;

        for (const char* extension : platform->RequiredInstanceExtensions())
        {
            needs.push_back(SpineNeed(extension, NeedKind::InstanceExtension, NeedLevel::Required));
        }

        needs.push_back(SpineNeed(k_SurfaceCapabilitiesExtension, NeedKind::InstanceExtension, NeedLevel::Required));
        needs.push_back(SpineNeed(k_HdrInstanceExtension, NeedKind::InstanceExtension, NeedLevel::Wanted));
        needs.push_back(SpineNeed(k_SwapchainExtension, NeedKind::DeviceExtension, NeedLevel::Required));
        needs.push_back(SpineNeed(k_HdrDeviceExtension, NeedKind::DeviceExtension, NeedLevel::Wanted));
        needs.push_back(SpineNeed(k_SurfaceMaintenanceKhr, NeedKind::InstanceExtension, NeedLevel::Wanted));
        needs.push_back(SpineNeed(k_SurfaceMaintenanceExt, NeedKind::InstanceExtension, NeedLevel::Wanted));
        needs.push_back(SpineNeed(k_SwapchainMaintenance1Feature, NeedKind::DeviceFeature, NeedLevel::Wanted));
        needs.push_back(SpineNeed(k_FifoLatestReadyFeature, NeedKind::DeviceFeature, NeedLevel::Wanted));
        return SuccessIn(Stage::Window);
    }

    void WindowStage::Stop() noexcept
    {
        needs.clear();
        platform.reset();
        content = nullptr;
    }

    bool WindowStage::Started() const noexcept
    {
        return platform != nullptr;
    }

    const ContentStage& WindowStage::Content() const noexcept
    {
        return *content;
    }

    PlatformWindowSystem& WindowStage::Platform() const noexcept
    {
        return *platform;
    }

    std::span<const Need> WindowStage::Needs() const noexcept
    {
        return needs;
    }
}

namespace
{
    bootstrap::Need SpineNeed(std::string_view name, bootstrap::NeedKind kind, bootstrap::NeedLevel level) noexcept
    {
        return bootstrap::Need
        {
            .Name = name,
            .Kind = kind,
            .Level = level,
        };
    }
}
