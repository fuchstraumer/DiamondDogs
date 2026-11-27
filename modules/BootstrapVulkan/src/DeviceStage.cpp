#include "DeviceStage.hpp"
#include "BootResult.hpp"
#include "BootTimings.hpp"
#include "ContentStage.hpp"
#include "CoreStage.hpp"
#include "Device.hpp"
#include "DeviceCapability.hpp"
#include "DeviceConfig.hpp"
#include "DeviceFilter.hpp"
#include "DeviceRequirements.hpp"
#include "LaunchStage.hpp"
#include "PlatformSystem.hpp"
#include "RhiContext.hpp"
#include "RhiContextVulkan.hpp"
#include "RhiFlags.hpp"
#include "RhiSystem.hpp"
#include "RhiSystemStart.hpp"
#include "VulkanCapabilities.hpp"
#include "WindowStage.hpp"
#include <algorithm>
#include <cstdint>
#include <format>
#include <memory>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace
{
    constexpr std::string_view k_DebugUtilsExtension{ "VK_EXT_debug_utils" };

    // the requirements in the RHI's vocabulary. The names view into the requirements
    struct RhiNeeds
    {
        std::vector<rhi::DeviceNeed> InstanceExtensions;
        std::vector<rhi::DeviceNeed> DeviceExtensions;
        std::vector<rhi::DeviceNeed> DeviceFeatures;
        std::vector<rhi::DeviceNeed> Capabilities;
        std::vector<std::string_view> Declined;
    };

    // a failure in the words of the needs: a Vulkan name that only a neutral capability asked for is reported as
    // that capability, with the Vulkan name in the detail
    struct NamedFailure
    {
        std::string Subject;
        std::string Source;
        std::string Detail;
    };

    [[nodiscard]] RhiNeeds ToRhiNeeds(const bootstrap::DeviceRequirements& requirements, const bootstrap::DeviceConfig& config);
    [[nodiscard]] std::vector<rhi::DeviceNeed> ToRhiNeeds(std::span<const bootstrap::Need> needs);
    [[nodiscard]] rhi::DeviceNeedLevel ToRhiLevel(bootstrap::NeedLevel level) noexcept;
    // adds the need, or raises the level of the entry with its name: Required wins, as in GatherDeviceRequirements
    void MergeRhiNeed(std::vector<rhi::DeviceNeed>& needs, std::string_view name, rhi::DeviceNeedLevel level);
    void AddNeutralNeeds(RhiNeeds& needs, const bootstrap::DeviceRequirements& requirements, const bootstrap::DeviceConfig& config);
    // the neutral capabilities whose Vulkan needs were all enabled, sorted
    [[nodiscard]] std::vector<bootstrap::DeviceCapability> EnabledCapabilities(const bootstrap::DeviceRequirements& requirements,
                                                                              const rhi::Device& device);
    // the source of the first need with this name, as "bundle/module/entry point", a module name, or "spine"
    [[nodiscard]] std::string SourceOf(const bootstrap::DeviceRequirements& requirements, std::string_view name);
    [[nodiscard]] NamedFailure NameFailure(const bootstrap::DeviceRequirements& requirements, std::string subject, std::string detail);
    [[nodiscard]] bootstrap::BootResultCode DeviceFailureCode(rhi::RhiSystemResultCode code) noexcept;
    [[nodiscard]] bootstrap::BootResult DeviceFailure(bootstrap::BootResultCode code, std::string subject, std::string source, std::string detail);
}

namespace bootstrap
{
    DeviceStage::DeviceStage() noexcept = default;

    DeviceStage::~DeviceStage()
    {
        Stop();
    }

    BootResult DeviceStage::Start(const ContentStage& _content,
                                  const DeviceRequirements& requirements,
                                  const DeviceConfig& config,
                                  WindowStage& _window)
    {
        return start(_content, requirements, config, &_window);
    }

    BootResult DeviceStage::StartHeadless(const ContentStage& _content,
                                          const DeviceRequirements& requirements,
                                          const DeviceConfig& config)
    {
        return start(_content, requirements, config, nullptr);
    }

    void DeviceStage::Stop() noexcept
    {
        // the surface belongs to the instance, so it goes first
        if (window != nullptr)
        {
            window->Platform().DestroySurface();
        }

        context = rhi::RhiContext{};
        rhi.reset();
        window = nullptr;
        content = nullptr;
        validation = rhi::ValidationLayers::None;
        capabilities.clear();
    }

    bool DeviceStage::Started() const noexcept
    {
        return rhi != nullptr;
    }

    const ContentStage& DeviceStage::Content() const noexcept
    {
        return *content;
    }

    const rhi::RhiContext& DeviceStage::Context() const noexcept
    {
        return context;
    }

    rhi::RhiSystem& DeviceStage::Rhi() const noexcept
    {
        return *rhi;
    }

    rhi::ValidationLayers DeviceStage::Validation() const noexcept
    {
        return validation;
    }

    DeviceFilter DeviceStage::Filter() const noexcept
    {
        return DeviceFilter
        {
            .Supports = &rhi::RhiSystem::SupportsCapabilityFn,
            .User = rhi.get(),
            .Limits = rhi->Limits(),
            .Capabilities = capabilities,
        };
    }

    BootResult DeviceStage::start(const ContentStage& _content,
                                  const DeviceRequirements& requirements,
                                  const DeviceConfig& config,
                                  WindowStage* window_stage)
    {
        Stop();

        BootTimings& timings = _content.Core().Launch().Timings();
        const ScopedBootSpan stageSpan(timings, Stage::Device, BootSpanKind::Stage, ToString(Stage::Device));
        const RhiNeeds needs = ToRhiNeeds(requirements, config);

        content = &_content;
        window = window_stage;
        validation = config.Validation;
        rhi = std::make_unique<rhi::RhiSystem>();

        const rhi::InstanceStartInfo instanceInfo
        {
            .VkVersion = config.ApiVersion,
            .ValidationLevel = config.Validation,
            .ApplicationName = config.ApplicationName,
            .EngineName = "DiamondDogs",
            .Extensions = needs.InstanceExtensions,
        };
        rhi::RhiSystemResult instanceStarted;

        {
            const ScopedBootSpan instanceSpan(timings, Stage::Device, BootSpanKind::Step, "instance");

            instanceStarted = rhi->StartInstance(instanceInfo);
        }

        if (!instanceStarted.Succeeded())
        {
            NamedFailure named = NameFailure(requirements, std::move(instanceStarted.Subject), std::move(instanceStarted.Detail));

            Stop();
            return DeviceFailure(BootResultCode::InstanceCreateFailed,
                                 std::move(named.Subject),
                                 std::move(named.Source),
                                 std::move(named.Detail));
        }

        uint64_t presentSurface = 0u;

        if (window != nullptr)
        {
            const ScopedBootSpan surfaceSpan(timings, Stage::Device, BootSpanKind::Step, "surface");

            if (!window->Platform().CreateSurface(rhi->GetInstance()))
            {
                Stop();
                return DeviceFailure(BootResultCode::SurfaceCreateFailed, "", "", "glfwCreateWindowSurface failed");
            }

            presentSurface = window->Platform().GetSurfaceHandle();
        }

        const rhi::DeviceStartInfo deviceInfo
        {
            .Extensions = needs.DeviceExtensions,
            .Features = needs.DeviceFeatures,
            .Capabilities = needs.Capabilities,
            .Declined = needs.Declined,
            .RequiredLimits = requirements.RequiredLimits,
            .PresentSurface = presentSurface,
        };
        rhi::RhiSystemResult deviceStarted;

        {
            const ScopedBootSpan deviceSpan(timings, Stage::Device, BootSpanKind::Step, "device");

            deviceStarted = rhi->StartDevice(deviceInfo);
        }

        if (!deviceStarted.Succeeded())
        {
            const BootResultCode code = DeviceFailureCode(deviceStarted.Code);
            NamedFailure named = NameFailure(requirements, std::move(deviceStarted.Subject), std::move(deviceStarted.Detail));

            Stop();
            return DeviceFailure(code, std::move(named.Subject), std::move(named.Source), std::move(named.Detail));
        }

        capabilities = EnabledCapabilities(requirements, *rhi->GetDevice());
        context = rhi::MakeVulkanRhiContext(*rhi, validation != rhi::ValidationLayers::None);

        return SuccessIn(Stage::Device);
    }
}

namespace
{
    RhiNeeds ToRhiNeeds(const bootstrap::DeviceRequirements& requirements, const bootstrap::DeviceConfig& config)
    {
        RhiNeeds needs
        {
            .InstanceExtensions = ToRhiNeeds(requirements.InstanceExtensions),
            .DeviceExtensions = ToRhiNeeds(requirements.DeviceExtensions),
            .DeviceFeatures = ToRhiNeeds(requirements.DeviceFeatures),
            .Capabilities = ToRhiNeeds(requirements.Capabilities),
        };

        // the instance extensions are declined here, because StartInstance takes no declined list
        const auto declined = [&config](const rhi::DeviceNeed& need)
        {
            return need.Level == rhi::DeviceNeedLevel::Wanted && std::ranges::contains(config.Declined, need.Name);
        };

        std::erase_if(needs.InstanceExtensions, declined);

        // validation reports through our messenger, and objects get names for capture tools, only with debug
        // utils. Nsight runs have no validation and still want the names
        const bool listsDebugUtils = std::ranges::contains(needs.InstanceExtensions,
                                                           std::string_view{ k_DebugUtilsExtension },
                                                           &rhi::DeviceNeed::Name);
        const bool declinesDebugUtils = std::ranges::contains(config.Declined, k_DebugUtilsExtension);

        if (!listsDebugUtils && !declinesDebugUtils)
        {
            needs.InstanceExtensions.push_back(
                rhi::DeviceNeed{ .Name = k_DebugUtilsExtension, .Level = rhi::DeviceNeedLevel::Wanted });
        }

        for (const std::string& name : config.Declined)
        {
            needs.Declined.emplace_back(name);
        }

        AddNeutralNeeds(needs, requirements, config);
        return needs;
    }

    rhi::DeviceNeedLevel ToRhiLevel(bootstrap::NeedLevel level) noexcept
    {
        if (level == bootstrap::NeedLevel::Required)
        {
            return rhi::DeviceNeedLevel::Required;
        }

        return rhi::DeviceNeedLevel::Wanted;
    }

    void MergeRhiNeed(std::vector<rhi::DeviceNeed>& needs, std::string_view name, rhi::DeviceNeedLevel level)
    {
        const auto found = std::ranges::find(needs, name, &rhi::DeviceNeed::Name);

        if (found == needs.end())
        {
            needs.push_back(rhi::DeviceNeed{ .Name = name, .Level = level });
            return;
        }

        if (level == rhi::DeviceNeedLevel::Required)
        {
            found->Level = rhi::DeviceNeedLevel::Required;
        }
    }

    void AddNeutralNeeds(RhiNeeds& needs, const bootstrap::DeviceRequirements& requirements, const bootstrap::DeviceConfig& config)
    {
        for (const bootstrap::Need& need : requirements.NeutralCapabilities)
        {
            const rhi::DeviceNeedLevel level = ToRhiLevel(need.Level);
            // a declined neutral name declines its Vulkan names. Declined has no effect on a Required need
            const bool declined = level == rhi::DeviceNeedLevel::Wanted && std::ranges::contains(config.Declined, need.Name);

            for (const bootstrap::VulkanEnable& enable : bootstrap::VulkanEnablesOf(need.Neutral))
            {
                std::vector<rhi::DeviceNeed>& kind = enable.Kind == bootstrap::NeedKind::DeviceExtension ?
                                                     needs.DeviceExtensions :
                                                     needs.DeviceFeatures;

                MergeRhiNeed(kind, enable.Name, level);

                if (declined)
                {
                    needs.Declined.push_back(enable.Name);
                }
            }
        }
    }

    std::vector<bootstrap::DeviceCapability> EnabledCapabilities(const bootstrap::DeviceRequirements& requirements,
                                                                 const rhi::Device& device)
    {
        std::vector<bootstrap::DeviceCapability> enabled;

        for (const bootstrap::Need& need : requirements.NeutralCapabilities)
        {
            const auto isEnabled = [&device](const bootstrap::VulkanEnable& enable)
            {
                if (enable.Kind == bootstrap::NeedKind::DeviceExtension)
                {
                    return device.HasExtension(enable.Name);
                }

                return device.IsFeatureEnabled(enable.Name);
            };
            const std::span<const bootstrap::VulkanEnable> enables = bootstrap::VulkanEnablesOf(need.Neutral);

            if (!enables.empty() && std::ranges::all_of(enables, isEnabled))
            {
                enabled.push_back(need.Neutral);
            }
        }

        std::ranges::sort(enabled);

        const auto [first, last] = std::ranges::unique(enabled);

        enabled.erase(first, last);
        return enabled;
    }

    std::vector<rhi::DeviceNeed> ToRhiNeeds(std::span<const bootstrap::Need> needs)
    {
        std::vector<rhi::DeviceNeed> converted;

        converted.reserve(needs.size());

        for (const bootstrap::Need& need : needs)
        {
            const rhi::DeviceNeedLevel level = need.Level == bootstrap::NeedLevel::Required ?
                                               rhi::DeviceNeedLevel::Required :
                                               rhi::DeviceNeedLevel::Wanted;

            converted.push_back(rhi::DeviceNeed{ .Name = need.Name, .Level = level });
        }

        return converted;
    }

    std::string SourceOf(const bootstrap::DeviceRequirements& requirements, std::string_view name)
    {
        const std::span<const bootstrap::Need> kinds[]
        {
            requirements.InstanceExtensions,
            requirements.DeviceExtensions,
            requirements.DeviceFeatures,
            requirements.Capabilities,
        };

        for (const std::span<const bootstrap::Need> needs : kinds)
        {
            const auto found = std::ranges::find(needs, name, &bootstrap::Need::Name);

            if (found != needs.end())
            {
                return bootstrap::FormatNeedSource(found->Source);
            }
        }

        // a version, a limit or "present": the device fell short, and no single need names it
        return "";
    }

    NamedFailure NameFailure(const bootstrap::DeviceRequirements& requirements, std::string subject, std::string detail)
    {
        std::string source = SourceOf(requirements, subject);

        if (!source.empty())
        {
            return NamedFailure{ .Subject = std::move(subject), .Source = std::move(source), .Detail = std::move(detail) };
        }

        for (const bootstrap::Need& need : requirements.NeutralCapabilities)
        {
            const std::span<const bootstrap::VulkanEnable> enables = bootstrap::VulkanEnablesOf(need.Neutral);

            if (std::ranges::contains(enables, std::string_view{ subject }, &bootstrap::VulkanEnable::Name))
            {
                return NamedFailure
                {
                    .Subject = std::string{ need.Name },
                    .Source = bootstrap::FormatNeedSource(need.Source),
                    .Detail = std::format("{}: {}", subject, detail),
                };
            }
        }

        return NamedFailure{ .Subject = std::move(subject), .Source = "", .Detail = std::move(detail) };
    }

    bootstrap::BootResultCode DeviceFailureCode(rhi::RhiSystemResultCode code) noexcept
    {
        switch (code)
        {
        case rhi::RhiSystemResultCode::NoPhysicalDevice:
        case rhi::RhiSystemResultCode::NoDeviceMeetsRequirements:
            return bootstrap::BootResultCode::NoDeviceMeetsRequirements;
        default:
            return bootstrap::BootResultCode::DeviceCreateFailed;
        }
    }

    bootstrap::BootResult DeviceFailure(bootstrap::BootResultCode code, std::string subject, std::string source, std::string detail)
    {
        return bootstrap::BootResult
        {
            .Code = code,
            .FailedStage = bootstrap::Stage::Device,
            .Subject = std::move(subject),
            .Source = std::move(source),
            .Detail = std::move(detail),
        };
    }
}
