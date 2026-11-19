#include "PlanetSession.hpp"
#include "BootResult.hpp"
#include "CommandBuffer.hpp"
#include "CommandPool.hpp"
#include "ContentStage.hpp"
#include "CoreStage.hpp"
#include "Device.hpp"
#include "DeviceConfig.hpp"
#include "DeviceRequirements.hpp"
#include "DeviceStage.hpp"
#include "Fence.hpp"
#include "GpuStage.hpp"
#include "LaunchStage.hpp"
#include "LoadedBundle.hpp"
#include "ModuleDescriptor.hpp"
#include "PlanetShaders.hpp"
#include "Queue.hpp"
#include "ResourceContext.hpp"
#include "RhiAssert.hpp"
#include "RhiHandle.hpp"
#include "RhiResult.hpp"
#include "RhiSystem.hpp"
#include "RhiTypes.hpp"
#include "ShaderBundle.hpp"
#include "ShaderLibrary.hpp"
#include "SpineNeeds.hpp"
#include <array>
#include <cstdio>
#include <filesystem>
#include <memory>
#include <print>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <vector>
#include <vulkan/vulkan_core.h>

namespace planet
{
    namespace
    {
        constexpr std::string_view k_FrameBoundaryExtension{ "VK_EXT_frame_boundary" };
        constexpr std::string_view k_FrameBoundaryFeature{ "VkPhysicalDeviceFrameBoundaryFeaturesEXT.frameBoundary" };

        constexpr std::array<bootstrap::Need, 2> k_FrameBoundaryNeeds
        {
            bootstrap::Need
            {
                .Name = k_FrameBoundaryExtension,
                .Kind = bootstrap::NeedKind::DeviceExtension,
                .Level = bootstrap::NeedLevel::Wanted,
            },
            bootstrap::Need
            {
                .Name = k_FrameBoundaryFeature,
                .Kind = bootstrap::NeedKind::DeviceFeature,
                .Level = bootstrap::NeedLevel::Wanted,
            },
        };

        constexpr std::array<bootstrap::BundleRequest, 1> k_Bundles
        {
            bootstrap::BundleRequest
            {
                .FileName = "ShaderLibrary.ldmanifest",
                .BuildTreePath = PLANET_GEN_MANIFEST,
                .Profile = rhi::k_SpirvProfileName,
            },
        };

        [[nodiscard]] bool Booted(const bootstrap::BootResult& result);
    }

    struct PlanetSessionImpl
    {
        bootstrap::LaunchStage Launch;
        bootstrap::CoreStage Core;
        bootstrap::ContentStage Content;
        bootstrap::DeviceStage Device;
        bootstrap::GpuStage Gpu;
        std::unique_ptr<rhi::ShaderLibrary> Library;
        std::unique_ptr<rhi::CommandPool> Pool;
        std::string DeviceName;
        bool FrameBoundariesEnabled{ false };
    };

    std::unique_ptr<PlanetSession> PlanetSession::Start(std::span<char* const> arguments,
                                                        const PlanetSessionConfig& config)
    {
        std::unique_ptr<PlanetSessionImpl> impl = std::make_unique<PlanetSessionImpl>();

        if (!Booted(impl->Launch.Start(arguments, std::span<const bootstrap::ModuleDescriptor>{}, bootstrap::Stage::Gpu)))
        {
            return nullptr;
        }

        if (!Booted(impl->Core.Start(impl->Launch)))
        {
            return nullptr;
        }

        if (!Booted(impl->Content.Start(impl->Core, k_Bundles)))
        {
            return nullptr;
        }

        std::vector<bootstrap::Need> needs = bootstrap::HeadlessSpineNeeds();
        needs.append_range(bootstrap::ShaderObjectNeeds());
        needs.append_range(config.ExtraNeeds);
        const bootstrap::DeviceRequirements requirements = bootstrap::GatherDeviceRequirements(impl->Content, needs);
        bootstrap::DeviceConfig deviceConfig
        {
            .ApiVersion = rhi::ApiVersion::Latest,
            .Validation = config.Validation,
            .ApplicationName = std::string{ config.ApplicationName },
        };

        if (config.ConfigMode == ConfigFileMode::Read &&
            !Booted(bootstrap::ReadDeviceConfig(impl->Launch, deviceConfig)))
        {
            return nullptr;
        }

        if (!Booted(impl->Device.StartHeadless(impl->Content, requirements, deviceConfig)))
        {
            return nullptr;
        }

        if (!Booted(impl->Gpu.StartHeadless(impl->Device)))
        {
            return nullptr;
        }

        const std::filesystem::path& bundlePath = impl->Content.Bundles().front().Path;
        rhi::ShaderLibraryResult<std::unique_ptr<rhi::ShaderLibrary>> library =
            rhi::ShaderLibrary::Open(bundlePath, rhi::k_SpirvProfileName);

        if (!library.has_value())
        {
            std::println(stderr, "Failed to open {}: {}", bundlePath.string(), rhi::ToString(library.error()));
            return nullptr;
        }

        impl->Library = std::move(*library);
        rhi::Device& device = *impl->Device.Rhi().GetDevice();
        impl->Pool = std::make_unique<rhi::CommandPool>(
            device.Handle(),
            rhi::CommandPool::Type::Compute,
            device.GetQueueFamilyIndices().Graphics);
        const rhi::Result allocated = impl->Pool->AllocateCommandBuffers(1u);

        if (allocated.IsFailure())
        {
            std::println(stderr, "PlanetSession: failed to allocate command buffer: {}", allocated.GetMessage());
            return nullptr;
        }

        VkPhysicalDeviceProperties properties{};
        vkGetPhysicalDeviceProperties(device.GetPhysicalDevice().As<VkPhysicalDevice>(), &properties);
        impl->DeviceName = properties.deviceName;
        impl->FrameBoundariesEnabled =
            device.HasExtension(k_FrameBoundaryExtension) && device.IsFeatureEnabled(k_FrameBoundaryFeature);

        return std::make_unique<PlanetSession>(std::move(impl));
    }

    PlanetSession::PlanetSession(std::unique_ptr<PlanetSessionImpl>&& _impl) noexcept :
        impl{ std::move(_impl) }
    {}

    PlanetSession::~PlanetSession() = default;

    rhi::Device& PlanetSession::Device() noexcept
    {
        return *impl->Device.Rhi().GetDevice();
    }

    ResourceContext& PlanetSession::Resources() noexcept
    {
        return impl->Gpu.Resources();
    }

    const rhi::ShaderLibrary& PlanetSession::Library() const noexcept
    {
        return *impl->Library;
    }

    bootstrap::DeviceFilter PlanetSession::Filter() const noexcept
    {
        return impl->Device.Filter();
    }

    std::string_view PlanetSession::DeviceName() const noexcept
    {
        return impl->DeviceName;
    }

    bool PlanetSession::HasFrameBoundaries() const noexcept
    {
        return impl->FrameBoundariesEnabled;
    }

    PlanetGpuResult<std::unique_ptr<PlanetShaders>> PlanetSession::CreateShaders(
        CrustFlagChoice crust_flags,
        std::span<const std::string_view> entry_points) const
    {
        return PlanetShaders::Create(
            *impl->Device.Rhi().GetDevice(),
            *impl->Library,
            impl->Device.Filter(),
            crust_flags,
            entry_points);
    }

    rhi::CommandBuffer PlanetSession::BeginCommands()
    {
        RhiAssert(impl->Pool->Reset());
        rhi::CommandBuffer commandBuffer(impl->Pool->GetCommandBuffer(0u));
        RhiAssert(commandBuffer.Begin());
        return commandBuffer;
    }

    void PlanetSession::DiscardCommands(rhi::CommandBuffer& command_buffer)
    {
        RhiAssert(command_buffer.End());
    }

    bool PlanetSession::SubmitAndWait(rhi::CommandBuffer& command_buffer)
    {
        RhiAssert(command_buffer.End());
        rhi::Device& device = Device();
        const rhi::CommandBufferHandle handle = command_buffer.Handle();
        rhi::Fence fence(device.Handle());
        const rhi::Result submitted = rhi::QueueSubmit(
            device.GetGeneralQueue(),
            rhi::SubmitInfo{ {}, std::span{ &handle, 1 }, {} },
            fence.Handle());

        if (submitted.IsFailure())
        {
            std::println(stderr, "PlanetSession: queue submit failed: {}", submitted.GetMessage());
            return false;
        }

        if (!fence.Wait())
        {
            std::println(stderr, "PlanetSession: the submit did not finish");
            return false;
        }

        return true;
    }

    bool PlanetSession::SubmitFrameEndAndWait(rhi::CommandBuffer& command_buffer, uint64_t frame_id)
    {
        RhiAssert(command_buffer.End());
        rhi::Device& device = Device();
        const VkFrameBoundaryEXT boundary
        {
            .sType = VK_STRUCTURE_TYPE_FRAME_BOUNDARY_EXT,
            .flags = VK_FRAME_BOUNDARY_FRAME_END_BIT_EXT,
            .frameID = frame_id,
        };
        const VkCommandBufferSubmitInfo commandBufferInfo
        {
            .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_SUBMIT_INFO,
            .commandBuffer = command_buffer.Handle().As<VkCommandBuffer>(),
        };
        const VkSubmitInfo2 submitInfo
        {
            .sType = VK_STRUCTURE_TYPE_SUBMIT_INFO_2,
            .pNext = &boundary,
            .commandBufferInfoCount = 1u,
            .pCommandBufferInfos = &commandBufferInfo,
        };
        rhi::Fence fence(device.Handle());

        if (vkQueueSubmit2(device.GetGeneralQueue().As<VkQueue>(), 1u, &submitInfo, fence.Handle().As<VkFence>()) != VK_SUCCESS)
        {
            std::println(stderr, "PlanetSession: vkQueueSubmit2 with frame boundary failed");
            return false;
        }

        if (!fence.Wait())
        {
            std::println(stderr, "PlanetSession: the frame-boundary submit did not finish");
            return false;
        }

        return true;
    }

    std::span<const bootstrap::Need> FrameBoundaryNeeds() noexcept
    {
        return k_FrameBoundaryNeeds;
    }

    namespace
    {
        bool Booted(const bootstrap::BootResult& result)
        {
            if (!result.Succeeded())
            {
                std::println(stderr, "{}", bootstrap::FormatFailure(result));
            }

            return result.Succeeded();
        }
    }
}
