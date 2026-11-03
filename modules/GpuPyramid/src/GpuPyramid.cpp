#include "GpuPyramid.hpp"
#include "CommandBuffer.hpp"
#include "Device.hpp"
#include "ManifestDescriptors.hpp"
#include "ResourceTypes.hpp"
#include "RhiAssert.hpp"
#include "RhiFlags.hpp"
#include "RhiHandle.hpp"
#include "RhiTypes.hpp"
#include "ShaderLibrary.hpp"
#include "ShaderManifestIndex.hpp"
#include "ShaderProgram.hpp"
#include "VariantKey.hpp"
#include <vulkan/vulkan_core.h>
#include <algorithm>
#include <array>
#include <bit>
#include <cassert>
#include <cstddef>
#include <cstdint>
#include <expected>
#include <format>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace pyramid
{
    namespace
    {
        enum class Pipeline : uint8_t
        {
            Fast,
            General,
            Count
        };
        constexpr std::array<std::string_view, static_cast<size_t>(Pipeline::Count)> k_EntryPoints{ "NvproPyramidFastCS", "NvproPyramidGeneralCS" };
        constexpr std::array<std::string_view, 3> k_ReductionNames{ "PyramidAverage", "PyramidMin", "PyramidMax" };

        // PYRAMID_MAX_LEVELS in NvproPyramid.slang: the length of the PyramidLevels binding array
        constexpr uint32_t k_MaxLevels = 16u;
        constexpr std::string_view k_LevelsName{ "PyramidLevels" };
        // nvpro_pyramid_dispatch.hpp: the push constant is input level << 5 | level count
        constexpr uint32_t k_InputLevelShift = 5u;
        // the fast pipeline's subgroup shuffles assume this many lanes (see MipGenerator::Create)
        constexpr uint32_t k_FastSubgroupSize = 32u;

        // one dispatch of the schedule
        struct Dispatch
        {
            Pipeline Pipe{ Pipeline::General };
            uint32_t PushConstant{ 0u };
            uint32_t Groups{ 0u };
        };

        // nvpro_pyramid_dispatch.hpp: NvproPyramidState
        struct ScheduleState
        {
            uint32_t CurrentLevel{ 0u };
            uint32_t RemainingLevels{ 0u };
            uint32_t CurrentX{ 0u };
            uint32_t CurrentY{ 0u };
        };

        // nvproPyramidDefaultFastDispatcher<4, 5>: fills up to 5 levels when the input level's edges are divisible
        // by 4, and returns no dispatch otherwise. The original's default is 6. Measured on an RTX 4080 with the ocean's
        // two 4-layer rgba16f textures, 6 levels was 11-46% slower from 128 to 512 (for example 22.5 against 15.4 us at
        // 512): a 6-level workgroup takes 4096 texels, so there are a quarter as many workgroups, and after its
        // barrier only 4 threads of each one work
        [[nodiscard]] std::optional<Dispatch> FastDispatch(const ScheduleState& state)
        {
            constexpr uint32_t k_DivisibilityRequirement = 4u;
            constexpr uint32_t k_FastMaxLevels = 5u;

            if (state.CurrentX % k_DivisibilityRequirement != 0u || state.CurrentY % k_DivisibilityRequirement != 0u)
            {
                return std::nullopt;
            }

            // Choose the number of levels to fill.
            uint32_t x = state.CurrentX;
            uint32_t y = state.CurrentY;
            uint32_t levels = 0u;

            while (x % 2u == 0u && y % 2u == 0u && levels < state.RemainingLevels && levels < k_FastMaxLevels)
            {
                x /= 2u;
                y /= 2u;
                ++levels;
            }

            // Each workgroup handles up to 4096 input samples if levels > 5; 1024 otherwise.
            const uint32_t shift = levels > 5u ? 12u : 10u;
            const uint32_t mask = levels > 5u ? 4095u : 1023u;
            const uint32_t samples = state.CurrentX * state.CurrentY;
            return Dispatch{ Pipeline::Fast, state.CurrentLevel << k_InputLevelShift | levels, (samples + mask) >> shift };
        }

        // nvproPyramidDefaultGeneralDispatcher: 1 or 2 levels, any size
        [[nodiscard]] Dispatch GeneralDispatch(const ScheduleState& state)
        {
            // Use py2_4_8_8 pipeline parameters.
            constexpr uint32_t k_GeneralMaxLevels = 2u;
            constexpr uint32_t k_Threads = 4u * 32u;
            constexpr uint32_t k_TileWidth = 8u;
            constexpr uint32_t k_TileHeight = 8u;

            const uint32_t levels = std::min(state.RemainingLevels, k_GeneralMaxLevels);
            const uint32_t dstWidth = std::max(state.CurrentX >> levels, 1u);
            const uint32_t dstHeight = std::max(state.CurrentY >> levels, 1u);
            const uint32_t pushConstant = state.CurrentLevel << k_InputLevelShift | levels;

            if (levels == 1u)
            {
                // Each thread writes one sample.
                const uint32_t samples = dstWidth * dstHeight;
                return Dispatch{ Pipeline::General, pushConstant, (samples + (k_Threads - 1u)) / k_Threads };
            }

            // Each workgroup handles a tile.
            const uint32_t horizontalTiles = (dstWidth + (k_TileWidth - 1u)) / k_TileWidth;
            const uint32_t verticalTiles = (dstHeight + (k_TileHeight - 1u)) / k_TileHeight;
            return Dispatch{ Pipeline::General, pushConstant, horizontalTiles * verticalTiles };
        }

        // nvproCmdPyramidDispatch, as a list of dispatches: the fast pipeline when it applies, the general one when not
        [[nodiscard]] std::vector<Dispatch> PlanDispatches(uint32_t width, uint32_t height, uint32_t mip_levels, bool has_fast_pipeline)
        {
            std::vector<Dispatch> dispatches;
            ScheduleState state{ 0u, mip_levels - 1u, width, height };

            while (state.RemainingLevels != 0u)
            {
                const std::optional<Dispatch> fast = has_fast_pipeline ? FastDispatch(state) : std::nullopt;
                const Dispatch dispatch = fast.has_value() ? *fast : GeneralDispatch(state);
                const uint32_t levelsDone = dispatch.PushConstant & ((1u << k_InputLevelShift) - 1u);
                dispatches.push_back(dispatch);

                // Update the progress.
                state.CurrentLevel += levelsDone;
                state.RemainingLevels -= levelsDone;
                state.CurrentX = std::max(state.CurrentX >> levelsDone, 1u);
                state.CurrentY = std::max(state.CurrentY >> levelsDone, 1u);
            }

            return dispatches;
        }

        // full subgroups of exactly k_FastSubgroupSize lanes in compute, with shuffles
        [[nodiscard]] bool DeviceRunsFastPipeline(VkPhysicalDevice physical_device) noexcept
        {
            VkPhysicalDeviceVulkan13Properties properties13{ VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_3_PROPERTIES };
            VkPhysicalDeviceSubgroupProperties subgroupProperties{ VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_SUBGROUP_PROPERTIES, &properties13 };
            VkPhysicalDeviceProperties2 properties{ VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PROPERTIES_2, &subgroupProperties };
            vkGetPhysicalDeviceProperties2(physical_device, &properties);

            VkPhysicalDeviceVulkan13Features features13{ VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_3_FEATURES };
            VkPhysicalDeviceFeatures2 features{ VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2, &features13 };
            vkGetPhysicalDeviceFeatures2(physical_device, &features);

            constexpr VkSubgroupFeatureFlags k_RequiredOperations = VK_SUBGROUP_FEATURE_BASIC_BIT | VK_SUBGROUP_FEATURE_SHUFFLE_BIT;
            return (subgroupProperties.supportedStages & VK_SHADER_STAGE_COMPUTE_BIT) != 0u &&
                (subgroupProperties.supportedOperations & k_RequiredOperations) == k_RequiredOperations &&
                features13.subgroupSizeControl == VK_TRUE &&
                features13.computeFullSubgroups == VK_TRUE &&
                (properties13.requiredSubgroupSizeStages & VK_SHADER_STAGE_COMPUTE_BIT) != 0u &&
                properties13.minSubgroupSize <= k_FastSubgroupSize && k_FastSubgroupSize <= properties13.maxSubgroupSize;
        }

        [[nodiscard]] GraphicsResource ViewResource(VkImage image, VkImageView view, VkSampler sampler) noexcept
        {
            GraphicsResource resource{};
            resource.Type = ResourceType::Image;
            resource.VkHandle = reinterpret_cast<uint64_t>(image);
            resource.VkViewHandle = reinterpret_cast<uint64_t>(view);
            resource.VkSamplerHandle = reinterpret_cast<uint64_t>(sampler);
            return resource;
        }
    }

    struct MipGeneratorImpl
    {
        ~MipGeneratorImpl();

        rhi::Device* Device{ nullptr };
        const rhi::ShaderLibrary* Library{ nullptr };
        MipGeneratorCreateInfo Settings;
        const lodestone::ManifestIndex* Index{ nullptr };
        lodestone::VariantKey Variant{};
        // Fast is null when the device cannot run it
        std::array<std::unique_ptr<rhi::ManifestProgram>, static_cast<size_t>(Pipeline::Count)> Programs;
        VkSampler Sampler{ VK_NULL_HANDLE };

        [[nodiscard]] const rhi::ManifestProgram& Program(Pipeline pipeline) const noexcept
        {
            return *Programs[static_cast<size_t>(pipeline)];
        }
    };

    MipGeneratorImpl::~MipGeneratorImpl()
    {
        if (Device != nullptr && Sampler != VK_NULL_HANDLE)
        {
            vkDestroySampler(Device->Handle().As<VkDevice>(), Sampler, nullptr);
        }
    }

    struct MipTargetImpl
    {
        ~MipTargetImpl();

        VkDevice Device{ VK_NULL_HANDLE };
        // the generator that made this target: Record checks that it is the one recording
        const MipGeneratorImpl* Generator{ nullptr };
        uint32_t ArrayLayers{ 0u };
        // every level, sampled: PyramidInput
        VkImageView SampledView{ VK_NULL_HANDLE };
        // one per level, storage: PyramidLevels
        std::vector<VkImageView> LevelViews;
        VkDescriptorPool DescriptorPool{ VK_NULL_HANDLE };
        // one per pipeline: their programs reflect different resources (only the fast one samples)
        std::array<VkDescriptorSet, static_cast<size_t>(Pipeline::Count)> Sets{};
        std::vector<Dispatch> Dispatches;
    };

    MipTargetImpl::~MipTargetImpl()
    {
        if (Device == VK_NULL_HANDLE)
        {
            return;
        }

        if (DescriptorPool != VK_NULL_HANDLE)
        {
            vkDestroyDescriptorPool(Device, DescriptorPool, nullptr);
        }

        for (const VkImageView view : LevelViews)
        {
            vkDestroyImageView(Device, view, nullptr);
        }

        if (SampledView != VK_NULL_HANDLE)
        {
            vkDestroyImageView(Device, SampledView, nullptr);
        }
    }

    MipTarget::MipTarget(std::unique_ptr<MipTargetImpl>&& _impl) noexcept : impl{ std::move(_impl) } {}
    MipTarget::~MipTarget() = default;

    uint32_t MipTarget::DispatchCount() const noexcept
    {
        return static_cast<uint32_t>(impl->Dispatches.size());
    }

    uint32_t MipTarget::FastDispatchCount() const noexcept
    {
        return static_cast<uint32_t>(std::ranges::count_if(impl->Dispatches, [](const Dispatch& dispatch) { return dispatch.Pipe == Pipeline::Fast; }));
    }

    std::expected<std::unique_ptr<MipGenerator>, std::string> MipGenerator::Create(
        rhi::Device& device,
        const rhi::ShaderLibrary& library,
        const MipGeneratorCreateInfo& create_info)
    {
        auto impl = std::make_unique<MipGeneratorImpl>();
        impl->Device = &device;
        impl->Library = &library;
        impl->Settings = create_info;
        const VkDevice vkDevice = device.Handle().As<VkDevice>();
        const VkPhysicalDevice physicalDevice = device.GetPhysicalDevice().As<VkPhysicalDevice>();

        VkPhysicalDeviceFeatures features{};
        vkGetPhysicalDeviceFeatures(physicalDevice, &features);

        if (features.shaderStorageImageWriteWithoutFormat != VK_TRUE)
        {
            return std::unexpected(std::string{ "the device has no shaderStorageImageWriteWithoutFormat, which NvproPyramid needs until its levels declare a format" });
        }

        if (features.shaderStorageImageArrayDynamicIndexing != VK_TRUE)
        {
            return std::unexpected(std::string{ "the device has no shaderStorageImageArrayDynamicIndexing, which NvproPyramid needs to index PyramidLevels by the level" });
        }

        rhi::ShaderLibraryResult<const lodestone::ManifestIndex*> index = library.ModuleIndex(k_ModuleName);
        if (!index.has_value())
        {
            return std::unexpected(std::format("module {}: {}", k_ModuleName, rhi::ToString(index.error())));
        }

        impl->Index = *index;
        const std::string_view reduction = k_ReductionNames[static_cast<size_t>(create_info.Reduce)];
        std::expected<lodestone::VariantKey, lodestone::QueryError> variant = (*index)->Query()
            .Where("PYRAMID_REDUCTION", lodestone::AxisValueDomain::Type, reduction)
            .Where("PYRAMID_SHARED_F16", create_info.SharedF16)
            .SingleFor(k_EntryPoints);

        if (!variant.has_value())
        {
            return std::unexpected(std::format("no variant for reduction {} with shared fp16 {} (query error {}, axis '{}')",
                reduction, create_info.SharedF16, static_cast<int>(variant.error().Code), variant.error().AxisName));
        }

        impl->Variant = *variant;
        const bool fast = DeviceRunsFastPipeline(physicalDevice);

        for (size_t pipeline = 0; pipeline < impl->Programs.size(); ++pipeline)
        {
            const bool isFast = pipeline == static_cast<size_t>(Pipeline::Fast);

            if (isFast && !fast)
            {
                continue;
            }

            // the fast pipeline's shuffles pair lanes by their index, so each subgroup must be full and exactly 32
            rhi::ManifestProgramOptions options{};
            options.RequiredSubgroupSize = isFast ? k_FastSubgroupSize : 0u;
            options.RequireFullSubgroups = isFast;
            const std::array<std::string_view, 1> entryPoints{ k_EntryPoints[pipeline] };
            rhi::ShaderLibraryResult<std::unique_ptr<rhi::ManifestProgram>> program =
                library.CreateProgram(device.Handle(), k_ModuleName, *variant, entryPoints, options);

            if (!program.has_value())
            {
                return std::unexpected(std::format("{}: {}", k_EntryPoints[pipeline], rhi::ToString(program.error())));
            }

            impl->Programs[pipeline] = std::move(*program);
        }

        // the bilinear LOAD_REDUCE4 samples exactly between four texels of one level
        const VkSamplerCreateInfo samplerInfo
        {
            VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO, nullptr, 0,
            VK_FILTER_LINEAR, VK_FILTER_LINEAR, VK_SAMPLER_MIPMAP_MODE_NEAREST,
            VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE, VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE, VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE,
            0.0f, VK_FALSE, 1.0f, VK_FALSE, VK_COMPARE_OP_NEVER, 0.0f, VK_LOD_CLAMP_NONE,
            VK_BORDER_COLOR_FLOAT_TRANSPARENT_BLACK, VK_FALSE
        };

        if (vkCreateSampler(vkDevice, &samplerInfo, nullptr, &impl->Sampler) != VK_SUCCESS)
        {
            return std::unexpected(std::string{ "creating the pyramid sampler failed" });
        }

        return std::make_unique<MipGenerator>(std::move(impl));
    }

    MipGenerator::MipGenerator(std::unique_ptr<MipGeneratorImpl>&& _impl) noexcept : impl{ std::move(_impl) } {}
    MipGenerator::~MipGenerator() = default;

    bool MipGenerator::HasFastPipeline() const noexcept
    {
        return impl->Programs[static_cast<size_t>(Pipeline::Fast)] != nullptr;
    }

    std::expected<std::unique_ptr<MipTarget>, std::string> MipGenerator::CreateTarget(const MipTargetCreateInfo& create_info) const
    {
        const uint32_t fullChain = static_cast<uint32_t>(std::bit_width(std::max(create_info.Width, create_info.Height)));

        if (create_info.MipLevels < 2u || create_info.MipLevels > std::min(fullChain, k_MaxLevels) || create_info.ArrayLayers == 0u)
        {
            return std::unexpected(std::format("a {}x{} texture with {} levels and {} layers is not a pyramid target (2 to {} levels, at least 1 layer)",
                create_info.Width, create_info.Height, create_info.MipLevels, create_info.ArrayLayers, std::min(fullChain, k_MaxLevels)));
        }

        const VkDevice vkDevice = impl->Device->Handle().As<VkDevice>();
        VkFormatProperties formatProperties{};
        vkGetPhysicalDeviceFormatProperties(impl->Device->GetPhysicalDevice().As<VkPhysicalDevice>(), create_info.Format, &formatProperties);
        const VkFormatFeatureFlags needed = VK_FORMAT_FEATURE_STORAGE_IMAGE_BIT | VK_FORMAT_FEATURE_SAMPLED_IMAGE_BIT |
            (impl->Settings.Reduce == Reduction::Average ? VK_FORMAT_FEATURE_SAMPLED_IMAGE_FILTER_LINEAR_BIT : VkFormatFeatureFlags{ 0u });

        if ((formatProperties.optimalTilingFeatures & needed) != needed)
        {
            return std::unexpected(std::format("format {} lacks storage, sampled or linear-filter support", static_cast<int>(create_info.Format)));
        }

        auto target = std::make_unique<MipTargetImpl>();
        target->Device = vkDevice;
        target->Generator = impl.get();
        target->ArrayLayers = create_info.ArrayLayers;

        // the sampled view covers every level, and each storage view one level
        const auto createView = [&](uint32_t base_level, uint32_t level_count, VkImageView& view)
        {
            const VkImageViewCreateInfo viewInfo
            {
                VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO, nullptr, 0, create_info.Image, VK_IMAGE_VIEW_TYPE_2D_ARRAY, create_info.Format,
                VkComponentMapping{}, VkImageSubresourceRange{ VK_IMAGE_ASPECT_COLOR_BIT, base_level, level_count, 0u, create_info.ArrayLayers }
            };
            return vkCreateImageView(vkDevice, &viewInfo, nullptr, &view) == VK_SUCCESS;
        };

        if (!createView(0u, create_info.MipLevels, target->SampledView))
        {
            return std::unexpected(std::string{ "creating the sampled view failed" });
        }

        for (uint32_t level = 0u; level < create_info.MipLevels; ++level)
        {
            VkImageView view{ VK_NULL_HANDLE };

            if (!createView(level, 1u, view))
            {
                return std::unexpected(std::format("creating the view of level {} failed", level));
            }

            target->LevelViews.push_back(view);
        }

        constexpr std::array<VkDescriptorPoolSize, 3> k_PoolSizes
        {
            VkDescriptorPoolSize{ VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE, static_cast<uint32_t>(Pipeline::Count) },
            VkDescriptorPoolSize{ VK_DESCRIPTOR_TYPE_SAMPLER, static_cast<uint32_t>(Pipeline::Count) },
            VkDescriptorPoolSize{ VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, k_MaxLevels * static_cast<uint32_t>(Pipeline::Count) },
        };
        const VkDescriptorPoolCreateInfo poolInfo{ VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO, nullptr, 0,
            static_cast<uint32_t>(Pipeline::Count), static_cast<uint32_t>(k_PoolSizes.size()), k_PoolSizes.data() };

        if (vkCreateDescriptorPool(vkDevice, &poolInfo, nullptr, &target->DescriptorPool) != VK_SUCCESS)
        {
            return std::unexpected(std::string{ "creating the descriptor pool failed" });
        }

        const GraphicsResource input = ViewResource(create_info.Image, target->SampledView, impl->Sampler);
        BoundResources resources
        {
            { "PyramidInput", BoundResource{ input, VK_IMAGE_LAYOUT_GENERAL, {} } },
            { "PyramidSampler", BoundResource{ input, VK_IMAGE_LAYOUT_GENERAL, {} } },
        };

        // the elements of PyramidLevels past the last level view the last level. No dispatch writes them
        BoundResource levels{ GraphicsResource::Null(), VK_IMAGE_LAYOUT_GENERAL, {} };
        levels.Elements.reserve(k_MaxLevels);

        for (uint32_t level = 0u; level < k_MaxLevels; ++level)
        {
            const VkImageView view = target->LevelViews[std::min(level, create_info.MipLevels - 1u)];
            levels.Elements.push_back(ViewResource(create_info.Image, view, VK_NULL_HANDLE));
        }

        resources.emplace(k_LevelsName, std::move(levels));

        for (size_t pipeline = 0; pipeline < impl->Programs.size(); ++pipeline)
        {
            if (impl->Programs[pipeline] == nullptr)
            {
                continue;
            }

            const VkDescriptorSetLayout setLayout = impl->Programs[pipeline]->SetLayouts()[0].As<VkDescriptorSetLayout>();
            const VkDescriptorSetAllocateInfo setInfo{ VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO, nullptr, target->DescriptorPool, 1, &setLayout };

            if (vkAllocateDescriptorSets(vkDevice, &setInfo, &target->Sets[pipeline]) != VK_SUCCESS)
            {
                return std::unexpected(std::format("{}: descriptor set allocation failed", k_EntryPoints[pipeline]));
            }

            const std::array<std::string_view, 1> entryPoints{ k_EntryPoints[pipeline] };

            if (std::optional<std::string> error = WriteManifestDescriptors(vkDevice, target->Sets[pipeline], *impl->Index, impl->Variant, entryPoints, resources); error.has_value())
            {
                return std::unexpected(*error);
            }
        }

        target->Dispatches = PlanDispatches(create_info.Width, create_info.Height, create_info.MipLevels, HasFastPipeline());
        return std::make_unique<MipTarget>(std::move(target));
    }

    void MipGenerator::Record(rhi::CommandBuffer& command_buffer, std::span<const MipTarget* const> targets) const
    {
        size_t stepCount = 0u;

        for (const MipTarget* target : targets)
        {
            assert(target->impl->Generator == impl.get() && "a MipTarget can only be recorded by the generator that created it");
            stepCount = std::max(stepCount, target->impl->Dispatches.size());
        }

        // each step reads the level that the step before it wrote
        const rhi::GlobalBarrier betweenSteps{ k_WriteStages, k_WriteAccess, k_ReadStages, k_ReadAccess };
        std::optional<Pipeline> bound;

        for (size_t step = 0u; step < stepCount; ++step)
        {
            if (step != 0u)
            {
                command_buffer.Barrier(rhi::DependencyInfo{ std::span{ &betweenSteps, 1 }, {}, {} });
            }

            for (const MipTarget* target : targets)
            {
                const MipTargetImpl& targetImpl = *target->impl;

                if (step >= targetImpl.Dispatches.size())
                {
                    continue;
                }

                const Dispatch& dispatch = targetImpl.Dispatches[step];
                const rhi::ManifestProgram& program = impl->Program(dispatch.Pipe);

                if (bound != dispatch.Pipe)
                {
                    RhiAssert(command_buffer.BindShaders(program.Program()));
                    bound = dispatch.Pipe;
                }

                const rhi::DescriptorSetHandle set{ reinterpret_cast<uint64_t>(targetImpl.Sets[static_cast<size_t>(dispatch.Pipe)]) };
                command_buffer.BindDescriptorSets(rhi::PipelineBindPoint::Compute, program.PipelineLayout(), 0, std::span{ &set, 1 });
                // both entry points take NvproPyramidParameters as a push constant, so the range exists
                const std::optional<rhi::PushConstantRange> range = program.PushConstants();
                assert(range.has_value());
                command_buffer.PushConstants(program.PipelineLayout(), range->StageFlags, 0u, std::as_bytes(std::span{ &dispatch.PushConstant, 1 }));
                command_buffer.Dispatch(dispatch.Groups, 1u, targetImpl.ArrayLayers);
            }
        }
    }
}
