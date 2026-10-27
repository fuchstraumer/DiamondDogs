#include "OceanFoam.hpp"
#include "CommandBuffer.hpp"
#include "Device.hpp"
#include "ImageDataFormats.hpp"
#include "ManifestDescriptors.hpp"
#include "OceanSimulation.hpp"
#include "ResourceContext.hpp"
#include "ResourceFlags.hpp"
#include "ResourceMessageReply.hpp"
#include "ResourceTypes.hpp"
#include "RhiAssert.hpp"
#include "RhiFlags.hpp"
#include "RhiHandle.hpp"
#include "RhiTypes.hpp"
#include "ShaderLibrary.hpp"
#include "ShaderManifestIndex.hpp"
#include "ShaderProgram.hpp"
#include "VariantKey.hpp"
#include <expected>
#include <algorithm>
#include <array>
#include <cassert>
#include <cmath>
#include <cstdint>
#include <format>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include <vulkan/vulkan_core.h>

namespace ocean
{
    namespace
    {
        constexpr std::string_view k_RenderModule{ "OceanRender" };
        constexpr std::string_view k_StepEntryPoint{ "OceanFoamStepCS" };
        constexpr uint32_t k_ClipmapLevels = 2u;
        constexpr uint64_t k_TimeoutNs = 10'000'000'000ull;
        // the workgroup of OceanFoamStepCS is 8 x 8
        constexpr int32_t k_GroupSize = 8;

        // OceanFoamStepParameters in OceanFoam.slang
        struct FoamStepPushConstants
        {
            std::array<int32_t, 4> WindowOrigins{};
            std::array<int32_t, 4> PreviousOrigins{};
            float DecayFactor{ 1.0f };
            float ProductionStep{ 0.0f };
            uint32_t Reset{ 0u };
            uint32_t Pad0{ 0u };
            std::array<float, 4> Drift{};
        };
        static_assert(sizeof(FoamStepPushConstants) == 64u);

        [[nodiscard]] ImageCreateInfo MakeClipmapImageInfo(int32_t resolution);
        [[nodiscard]] FoamStepPushConstants MakeStepPushConstants(const OceanFoamImpl& foam, double delta_seconds);
    }

    struct OceanFoamImpl
    {
        ~OceanFoamImpl();

        rhi::Device* Device{ nullptr };
        ResourceContext* Resources{ nullptr };
        FoamSettings Settings;

        GraphicsResource ClipmapImage;
        std::unique_ptr<rhi::ManifestProgram> Program;
        VkDescriptorPool DescriptorPool{ VK_NULL_HANDLE };
        // one per view buffer, because the shader reads the view uniform of its own frame
        std::vector<VkDescriptorSet> Sets;

        // the drift stays in double so that a long run does not lose it
        std::array<double, 2> Drift{};
        // the world xz the windows are centered on, for the fragment stage's level fade
        std::array<float, 2> Center{};
        double DeltaSeconds{ 0.0 };
        std::array<int32_t, 4> Origins{};
        std::array<int32_t, 4> PreviousOrigins{};
        bool HasHistory{ false };
        bool LayoutReady{ false };

        void RecordLayoutChange(rhi::CommandBuffer& command_buffer);
        void RecordDispatch(rhi::CommandBuffer& command_buffer, uint32_t frame_index);
    };

    OceanFoamImpl::~OceanFoamImpl()
    {
        if (Device == nullptr)
        {
            return;
        }

        if (DescriptorPool != VK_NULL_HANDLE)
        {
            vkDestroyDescriptorPool(Device->Handle().As<VkDevice>(), DescriptorPool, nullptr);
        }

        if (ClipmapImage)
        {
            std::shared_ptr<MessageReply> reply = Resources->DestroyResource(ClipmapImage);
            reply->WaitForCompletion(k_TimeoutNs);
        }
    }

    void OceanFoamImpl::RecordLayoutChange(rhi::CommandBuffer& command_buffer)
    {
        const rhi::ImageBarrier toGeneral
        {
            .Image=rhi::ImageHandle{ ClipmapImage.VkHandle },
            .SrcStages=rhi::PipelineStage::None,
            .SrcAccess=rhi::Access::None,
            .DstStages=rhi::PipelineStage::ComputeShader | rhi::PipelineStage::VertexShader | rhi::PipelineStage::FragmentShader,
            .DstAccess=rhi::Access::ShaderSampledRead | rhi::Access::ShaderStorageRead | rhi::Access::ShaderStorageWrite,
            .OldLayout=rhi::ImageLayout::Undefined,
            .NewLayout=rhi::ImageLayout::General
        };
        command_buffer.Barrier(rhi::DependencyInfo{ {}, {}, std::span{ &toGeneral, 1 } });
        LayoutReady = true;
    }

    void OceanFoamImpl::RecordDispatch(rhi::CommandBuffer& command_buffer, uint32_t frame_index)
    {
        // the simulation wrote Displacement and Whitecaps (and the last frame's draws sampled the clipmap), and this
        // step reads both and rewrites the clipmap in place
        const rhi::GlobalBarrier inputsToStep
        {
            rhi::PipelineStage::ComputeShader | rhi::PipelineStage::VertexShader | rhi::PipelineStage::FragmentShader,
            rhi::Access::ShaderStorageWrite,
            rhi::PipelineStage::ComputeShader,
            rhi::Access::ShaderSampledRead | rhi::Access::ShaderStorageRead | rhi::Access::ShaderStorageWrite
        };
        command_buffer.Barrier(rhi::DependencyInfo{ std::span{ &inputsToStep, 1 }, {}, {} });

        RhiAssert(command_buffer.BindShaders(Program->Program()));
        const rhi::DescriptorSetHandle set{ reinterpret_cast<uint64_t>(Sets[frame_index]) };
        command_buffer.BindDescriptorSets(rhi::PipelineBindPoint::Compute, Program->PipelineLayout(), 0, std::span{ &set, 1 });

        const FoamStepPushConstants constants = MakeStepPushConstants(*this, DeltaSeconds);
        const std::optional<rhi::PushConstantRange> range = Program->PushConstants();
        assert(range.has_value());
        command_buffer.PushConstants(Program->PipelineLayout(), range->StageFlags, 0u,
            std::as_bytes(std::span{ &constants, 1 }).first(std::min<size_t>(sizeof(constants), range->Size)));

        const uint32_t groups = static_cast<uint32_t>(Settings.Resolution / k_GroupSize);
        command_buffer.Dispatch(groups, groups, k_ClipmapLevels);

        // the draws sample the clipmap, and the next tick or step must not overwrite what this step reads before it ends
        const rhi::GlobalBarrier stepToReaders
        {
            rhi::PipelineStage::ComputeShader,
            rhi::Access::ShaderStorageWrite,
            rhi::PipelineStage::ComputeShader | rhi::PipelineStage::VertexShader | rhi::PipelineStage::FragmentShader,
            rhi::Access::ShaderSampledRead | rhi::Access::ShaderStorageRead | rhi::Access::ShaderStorageWrite
        };
        command_buffer.Barrier(rhi::DependencyInfo{ std::span{ &stepToReaders, 1 }, {}, {} });
    }

    std::expected<std::unique_ptr<OceanFoam>, std::string> OceanFoam::Create(
        rhi::Device& device,
        ResourceContext& resources,
        const rhi::ShaderLibrary& library,
        const OceanSimulation& simulation,
        std::span<const GraphicsResource> view_buffers,
        const GraphicsResource& sampler,
        const FoamSettings& settings)
    {
        if (settings.Resolution <= 0 || settings.Resolution % k_GroupSize != 0)
        {
            return std::unexpected(std::format("foam resolution {} is not a positive multiple of {}", settings.Resolution, k_GroupSize));
        }

        auto impl = std::make_unique<OceanFoamImpl>();
        impl->Device = &device;
        impl->Resources = &resources;
        impl->Settings = settings;
        const VkDevice vkDevice = device.Handle().As<VkDevice>();

        std::shared_ptr<GraphicsResourceReply> imageReply = resources.CreateImage(MakeClipmapImageInfo(settings.Resolution), {});

        if (imageReply == nullptr || imageReply->WaitForCompletion(k_TimeoutNs) != MessageReply::Status::Completed)
        {
            return std::unexpected(std::string{ "creating the foam clipmap failed" });
        }

        impl->ClipmapImage = imageReply->GetResource();

        rhi::ShaderLibraryResult<const lodestone::ManifestIndex*> index = library.ModuleIndex(k_RenderModule);

        if (!index.has_value())
        {
            return std::unexpected(std::format("module {}: {}", k_RenderModule, rhi::ToString(index.error())));
        }

        const std::array<std::string_view, 1> entryPoints{ k_StepEntryPoint };
        std::expected<lodestone::VariantKey, lodestone::QueryError> variant = (*index)->Query().SingleFor(entryPoints);

        if (!variant.has_value())
        {
            return std::unexpected(std::format("no {} variant for {}", k_RenderModule, k_StepEntryPoint));
        }

        rhi::ShaderLibraryResult<std::unique_ptr<rhi::ManifestProgram>> program =
            library.CreateProgram(device.Handle(), k_RenderModule, *variant, entryPoints);

        if (!program.has_value())
        {
            return std::unexpected(std::format("{}: {}", k_StepEntryPoint, rhi::ToString(program.error())));
        }

        impl->Program = std::move(*program);

        // per set: the view uniform, Displacement and Whitecaps, the sampler, and the clipmap as a storage image
        const uint32_t setCount = static_cast<uint32_t>(view_buffers.size());
        const std::array<VkDescriptorPoolSize, 4> poolSizes
        {
            VkDescriptorPoolSize{ VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, setCount },
            VkDescriptorPoolSize{ VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE, 2u * setCount },
            VkDescriptorPoolSize{ VK_DESCRIPTOR_TYPE_SAMPLER, setCount },
            VkDescriptorPoolSize{ VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, setCount },
        };
        const VkDescriptorPoolCreateInfo poolInfo{ VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO, nullptr, 0,
            setCount, static_cast<uint32_t>(poolSizes.size()), poolSizes.data() };

        if (vkCreateDescriptorPool(vkDevice, &poolInfo, nullptr, &impl->DescriptorPool) != VK_SUCCESS)
        {
            return std::unexpected(std::string{ "foam descriptor pool creation failed" });
        }

        const VkDescriptorSetLayout setLayout = impl->Program->SetLayouts()[0].As<VkDescriptorSetLayout>();

        for (const GraphicsResource& viewBuffer : view_buffers)
        {
            VkDescriptorSet& set = impl->Sets.emplace_back(VK_NULL_HANDLE);
            const VkDescriptorSetAllocateInfo setInfo{ VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO, nullptr, impl->DescriptorPool, 1, &setLayout };

            if (vkAllocateDescriptorSets(vkDevice, &setInfo, &set) != VK_SUCCESS)
            {
                return std::unexpected(std::string{ "foam descriptor set allocation failed" });
            }

            // the compute passes keep every texture in General
            const BoundResources bound
            {
                { "View", { viewBuffer } },
                { "Displacement", { simulation.Displacement(), VK_IMAGE_LAYOUT_GENERAL } },
                { "Whitecaps", { simulation.Whitecaps(), VK_IMAGE_LAYOUT_GENERAL } },
                { "OceanSampler", { sampler } },
                { "FoamClipmapStorage", { impl->ClipmapImage } },
            };

            if (std::optional<std::string> error = WriteManifestDescriptors(vkDevice, set, **index, *variant, entryPoints, bound); error.has_value())
            {
                return std::unexpected(*error);
            }
        }

        return std::make_unique<OceanFoam>(std::move(impl));
    }

    OceanFoam::OceanFoam(std::unique_ptr<OceanFoamImpl>&& _impl) noexcept : impl{ std::move(_impl) } {}
    OceanFoam::~OceanFoam() = default;

    const FoamSettings& OceanFoam::Settings() const noexcept
    {
        return impl->Settings;
    }

    void OceanFoam::SetSettings(const FoamSettings& settings) noexcept
    {
        FoamSettings updated = settings;
        updated.Resolution = impl->Settings.Resolution;
        updated.TexelSizes = impl->Settings.TexelSizes;
        impl->Settings = updated;
    }

    void OceanFoam::Advance(double delta_seconds, std::array<float, 2> camera_xz, float wind_direction_radians, float wind_speed) noexcept
    {
        const std::array<double, 2> velocity = DriftVelocity(wind_direction_radians, wind_speed);
        impl->Drift[0] += velocity[0] * delta_seconds;
        impl->Drift[1] += velocity[1] * delta_seconds;
        impl->DeltaSeconds = delta_seconds;
        impl->Center = camera_xz;

        // the window of each level is centered on the camera's point in the drift frame, in whole texels
        const double halfSize = static_cast<double>(impl->Settings.Resolution / 2);

        for (size_t level = 0u; level < k_ClipmapLevels; ++level)
        {
            const double texelSize = static_cast<double>(impl->Settings.TexelSizes[level]);

            for (size_t axis = 0u; axis < 2u; ++axis)
            {
                const double framePosition = static_cast<double>(camera_xz[axis]) - impl->Drift[axis];
                impl->Origins[2u * level + axis] = static_cast<int32_t>(std::floor(framePosition / texelSize - halfSize));
            }
        }
    }

    void OceanFoam::FillView(std::array<float, 4>& persistent_foam_parameters, std::array<float, 4>& foam_drift, std::array<float, 4>& foam_residual) const noexcept
    {
        const FoamSettings& settings = impl->Settings;
        persistent_foam_parameters = { settings.Enabled ? 1.0f : 0.0f, settings.ThresholdRaise, settings.TexelSizes[0], settings.TexelSizes[1] };
        foam_drift = { static_cast<float>(impl->Drift[0]), static_cast<float>(impl->Drift[1]), impl->Center[0], impl->Center[1] };
        foam_residual = { settings.ResidualAmount, settings.ResidualSoftness, settings.ResidualScale, 0.0f };
    }

    void OceanFoam::RecordStep(rhi::CommandBuffer& command_buffer, uint32_t frame_index)
    {
        // the draws bind the clipmap in General even while the foam is off, so the layout change happens once, here
        if (!impl->LayoutReady)
        {
            impl->RecordLayoutChange(command_buffer);
        }

        if (!impl->Settings.Enabled)
        {
            // nothing writes the foam while it is off, so it has no history
            impl->HasHistory = false;
            return;
        }

        impl->RecordDispatch(command_buffer, frame_index);
        impl->PreviousOrigins = impl->Origins;
        impl->HasHistory = true;
    }

    void OceanFoam::Invalidate() noexcept
    {
        impl->HasHistory = false;
    }

    std::array<double, 2> OceanFoam::Drift() const noexcept
    {
        return impl->Drift;
    }

    void OceanFoam::SetDrift(const std::array<double, 2>& drift) noexcept
    {
        impl->Drift = drift;
    }

    std::array<double, 2> OceanFoam::DriftVelocity(float wind_direction_radians, float wind_speed) const noexcept
    {
        // the wind angle is the angle of the wave vector in (world x, world z): texture x is world x, texture y is world z
        const double speed = static_cast<double>(wind_speed) * static_cast<double>(impl->Settings.WindDrift);
        const double angle = static_cast<double>(wind_direction_radians);
        return { std::cos(angle) * speed, std::sin(angle) * speed };
    }

    std::array<int32_t, 4> OceanFoam::WindowOrigins() const noexcept
    {
        return impl->Origins;
    }

    GraphicsResource OceanFoam::Clipmap() const noexcept
    {
        return impl->ClipmapImage;
    }

    namespace
    {
        ImageCreateInfo MakeClipmapImageInfo(int32_t resolution)
        {
            ImageCreateInfo info{};
            info.Domain = ResourceDomain::GPUOnly;
            info.Flags = ResourceCreationFlags::UserDataAsNameString;
            // storage for the step, sampled by the step and by the surface, and a transfer destination for tests and clears
            info.Usage = ImageUsageBits::UavComputeShader |
                         ImageUsageBits::SampledImage |
                         ImageUsageBits::SrvComputeShader |
                         ImageUsageBits::SrvFragmentShader |
                         ImageUsageBits::SrvVertexShader |
                         ImageUsageBits::TransferDestination;
            info.Format = CommonFormats::R16_Float;
            info.Width = static_cast<uint32_t>(resolution);
            info.Height = static_cast<uint32_t>(resolution);
            info.ArrayLayers = k_ClipmapLevels;
            info.MipLevels = 1u;
            info.ViewInfo = ImageViewCreateInfo{};
            info.ViewInfo->ViewType = ImageViewType::Type2DArray;
            info.ViewInfo->Range.BaseMipLevel = 0u;
            info.ViewInfo->Range.LevelCount = 1u;
            info.ViewInfo->Range.BaseArrayLayer = 0u;
            info.ViewInfo->Range.LayerCount = k_ClipmapLevels;
            info.Name = "Ocean_FoamClipmap";
            return info;
        }

        FoamStepPushConstants MakeStepPushConstants(const OceanFoamImpl& foam, double delta_seconds)
        {
            const FoamSettings& settings = foam.Settings;
            const double decayTime = std::max(static_cast<double>(settings.DecayTime), 1.0e-3);
            FoamStepPushConstants constants{};
            constants.WindowOrigins = foam.Origins;
            constants.PreviousOrigins = foam.HasHistory ? foam.PreviousOrigins : foam.Origins;
            constants.DecayFactor = static_cast<float>(std::exp(-delta_seconds / decayTime));
            // the production rate is gain / tau, so the steady state is gain times the averaged coverage
            constants.ProductionStep = static_cast<float>(static_cast<double>(settings.PersistenceGain) / decayTime * delta_seconds);
            constants.Reset = foam.HasHistory ? 0u : 1u;
            constants.Drift = { static_cast<float>(foam.Drift[0]), static_cast<float>(foam.Drift[1]), 0.0f, 0.0f };
            return constants;
        }
    }
}
