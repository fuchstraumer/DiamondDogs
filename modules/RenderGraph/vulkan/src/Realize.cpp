#include "Realize.hpp"
#include "Declarations.hpp"
#include "FramePlan.hpp"
#include "GraphImpl.hpp"
#include "ShaderLibraryTypes.hpp"
#include "RhiHandle.hpp"
#include "RhiTypes.hpp"
#include "Records.hpp"
#include "VulkanMapping.hpp"
#include "CommandPool.hpp"
#include "CompiledGraph.hpp"
#include "Device.hpp"
#include "LoomGraph.hpp"
#include "ResourceContext.hpp"
#include "RhiResourceFlags.hpp"
#include "ResourceTypes.hpp"
#include "ShaderProgram.hpp"
#include "ShaderTypes.hpp"
#include "bind/BoundGraph.hpp"
#include "compile/CompiledData.hpp"
#include <cstddef>
#include "freeze/FrozenGraph.hpp"
#include "layout/LaidOutGraph.hpp"
#include "size/SizedGraph.hpp"
#include "LoomFormats.hpp"
#include "ImageDataFormats.hpp"
#include <vulkan/vulkan_core.h>
#include <algorithm>
#include <array>
#include <cstdint>
#include <format>
#include <memory>
#include <numeric>
#include <string>
#include <string_view>
#include <span>
#include <utility>
#include <vector>

// Realize: checks that this backend can run the graph, then creates what the graph runs on. Loom builds its own
// layouts and shader objects from the compiled slot table and the copied SPIR-V (step 5 fork F1), so nothing here
// reads the bundle.
namespace loom
{

namespace
{

    bool SlotIsSupported(const BindingSlot& slot)
    {
        switch (slot.Kind)
        {
        case lodestone::BindingKind::UniformBuffer:
            [[fallthrough]];
        case lodestone::BindingKind::StorageBuffer:
            [[fallthrough]];
        case lodestone::BindingKind::Texture:
            [[fallthrough]];
        case lodestone::BindingKind::StorageTexture:
            [[fallthrough]];
        case lodestone::BindingKind::Sampler:
            return slot.ArrayCount == 1u;
        default:
            break;
        }

        return false;
    }

    // The SRV and UAV usage bits of each shader stage that a use names.
    struct StageImageUsage
    {
        rhi::PipelineStage Stage;
        ImageUsageBits Sampled;
        ImageUsageBits Storage;
    };

    constexpr StageImageUsage k_StageImageUsages[]
    {
        { rhi::PipelineStage::VertexShader, ImageUsageBits::VertexShaderSRV, ImageUsageBits::VertexShaderUAV },
        { rhi::PipelineStage::FragmentShader, ImageUsageBits::FragmentShaderSRV, ImageUsageBits::FragmentShaderUAV },
        { rhi::PipelineStage::ComputeShader, ImageUsageBits::ComputeShaderSRV, ImageUsageBits::ComputeShaderUAV },
        { rhi::PipelineStage::TaskShader, ImageUsageBits::TaskShaderSRV, ImageUsageBits::TaskShaderUAV },
        { rhi::PipelineStage::MeshShader, ImageUsageBits::MeshShaderSRV, ImageUsageBits::MeshShaderUAV },
    };

    // Every GPU use of an image row, and the transfer that its CpuAccess needs.
    ImageUsageBits ImageUsageOf(const CompiledData& data, int32_t row, CpuAccess cpu)
    {
        constexpr rhi::Access k_Storage = rhi::Access::ShaderStorageRead | rhi::Access::ShaderStorageWrite;
        ImageUsageBits usage = ImageUsageBits::Invalid;

        for (const PassUse& use : data.Uses)
        {
            if (use.Resource != row)
            {
                continue;
            }

            const bool sampled = (use.Scope.Accesses & rhi::Access::ShaderSampledRead) != rhi::Access::None;
            const bool storage = (use.Scope.Accesses & k_Storage) != rhi::Access::None;

            for (const StageImageUsage& stage : k_StageImageUsages)
            {
                if ((use.Scope.Stages & stage.Stage) == rhi::PipelineStage::None)
                {
                    continue;
                }

                if (sampled)
                {
                    usage |= stage.Sampled;
                }

                if (storage)
                {
                    usage |= stage.Storage;
                }
            }
        }

        if (cpu == CpuAccess::Readback)
        {
            usage |= ImageUsageBits::TransferSource;
        }

        if (cpu == CpuAccess::Upload)
        {
            usage |= ImageUsageBits::TransferDestination;
        }

        // OnNoHistory::Clear clears the copy with vkCmdClearColorImage
        const ImageDeclaration& declaration = data.Frozen.Images[data.Frozen.Resources[row].DeclarationIndex].Declaration;

        if (declaration.HistoryDepth > 0 && declaration.NoHistoryAction == OnNoHistory::Clear)
        {
            usage |= ImageUsageBits::TransferDestination;
        }

        return usage;
    }

    ImageType ImageTypeOf(lodestone::TextureDimension dimension)
    {
        switch (dimension)
        {
        case lodestone::TextureDimension::Texture1D:
            return ImageType::Type1D;
        case lodestone::TextureDimension::Texture3D:
            return ImageType::Type3D;
        default:
            break;
        }

        return ImageType::Type2D;
    }

    // The view must match how the shaders bind the image: as an array, or not. Cube images are refused before this.
    ImageViewType ViewTypeOf(const SizedResource& sized, int32_t layers)
    {
        const bool arrayed = sized.Arrayed || layers > 1;

        switch (sized.Dimension)
        {
        case lodestone::TextureDimension::Texture1D:
            return arrayed ? ImageViewType::Type1DArray : ImageViewType::Type1D;
        case lodestone::TextureDimension::Texture3D:
            return ImageViewType::Type3D;
        default:
            break;
        }

        return arrayed ? ImageViewType::Type2DArray : ImageViewType::Type2D;
    }

    int32_t MipCountOf(const MipSource& mips, const ImageExtent& extent)
    {
        if (mips.Kind != MipSource::Kind::Generated || mips.Count > 0)
        {
            return std::max(mips.Count, 1);
        }

        // Full: down to 1x1
        int32_t largest = std::max({ extent.Width, extent.Height, extent.Depth, 1 });
        int32_t count = 1;

        while (largest > 1)
        {
            largest /= 2;
            ++count;
        }

        return count;
    }

    rhi::ResourceFuture<GraphicsResource> CreateImage(GraphImpl& graph, int32_t row, std::string_view name)
    {
        const CompiledData& data = graph.Compiled.Data();
        const FrozenResource& frozen = data.Frozen.Resources[row];
        const ImageDeclaration& declaration = data.Frozen.Images[frozen.DeclarationIndex].Declaration;
        const SizedResource& sized = data.Sized.Resources[row];
        const bool volume = sized.Dimension == lodestone::TextureDimension::Texture3D;
        const int32_t layers = std::max(declaration.Layers, 1);
        const int32_t mips = MipCountOf(declaration.Mips, sized.Extent);

        ImageCreateInfo info{};
        info.Domain = ResourceDomain::GPUOnly;
        info.Usage = ImageUsageOf(data, row, declaration.Cpu);
        info.Type = ImageTypeOf(sized.Dimension);
        // CheckSupported refused a format with no ImageFormat
        info.Format = *ToImageFormat(declaration.Format);
        info.Width = static_cast<uint32_t>(std::max(sized.Extent.Width, 1));
        info.Height = static_cast<uint32_t>(std::max(sized.Extent.Height, 1));
        info.Depth = volume ? static_cast<uint32_t>(std::max(sized.Extent.Depth, 1)) : 1u;
        info.MipLevels = static_cast<uint32_t>(mips);
        info.ArrayLayers = static_cast<uint32_t>(layers);
        info.Name = name;

        ImageViewCreateInfo view{};
        view.ViewType = ViewTypeOf(sized, layers);
        view.Range = ImageRange{ ImageAspectFlags::Color, 0u, static_cast<uint32_t>(mips), 0u, static_cast<uint32_t>(layers) };
        info.ViewInfo = view;

        return graph.Resources.CreateImage(info);
    }

    // The resources step 6 can create: buffers, images of a known extent, samplers. An image sized from a frame
    // target waits for step 7, and a cube for a request; a format with no ImageFormat cannot be created.
    RealizeError CheckResource(const CompiledData& data, int32_t row)
    {
        const FrozenResource& frozen = data.Frozen.Resources[row];

        if (frozen.Kind != ResourceKind::Image)
        {
            return RealizeError::Invalid;
        }

        if (data.Sized.Resources[row].Cube)
        {
            return RealizeError::ResourceKindNotYet;
        }

        if (frozen.Origin != ResourceOrigin::Declared)
        {
            return RealizeError::Invalid;
        }

        const ImageDeclaration& declaration = data.Frozen.Images[frozen.DeclarationIndex].Declaration;

        if (declaration.Extent.Kind == ExtentSource::Kind::Target || !ToImageFormat(declaration.Format).has_value())
        {
            return RealizeError::ResourceKindNotYet;
        }

        return RealizeError::Invalid;
    }

    // Every GPU use of a buffer row, in RhiResources' usage bits, and the CPU copy that its CpuAccess needs.
    BufferUsageBits UsageOf(const CompiledData& data, int32_t row, CpuAccess cpu)
    {
        BufferUsageBits usage = BufferUsageBits::Invalid;

        for (const PassUse& use : data.Uses)
        {
            if (use.Resource != row)
            {
                continue;
            }

            constexpr rhi::Access k_Storage = rhi::Access::ShaderStorageRead | rhi::Access::ShaderStorageWrite;
            const bool compute = (use.Scope.Stages & rhi::PipelineStage::ComputeShader) != rhi::PipelineStage::None;
            const bool uniform = (use.Scope.Accesses & rhi::Access::UniformRead) != rhi::Access::None;
            const bool storage = (use.Scope.Accesses & k_Storage) != rhi::Access::None;
            const bool indirect = (use.Scope.Accesses & rhi::Access::IndirectCommandRead) != rhi::Access::None;

            if (compute && uniform)
            {
                usage |= BufferUsageBits::ComputeShaderUBO;
            }

            if (compute && storage)
            {
                usage |= BufferUsageBits::ComputeShaderUAV;
            }

            if (indirect)
            {
                usage |= BufferUsageBits::DispatchIndirect;
            }
        }

        if (cpu == CpuAccess::Readback)
        {
            usage |= BufferUsageBits::TransferSource;
        }

        if (cpu == CpuAccess::Upload)
        {
            usage |= BufferUsageBits::TransferDestination;
        }

        // OnNoHistory::Clear fills the copy with vkCmdFillBuffer
        const BufferDeclaration& declaration = data.Frozen.Buffers[data.Frozen.Resources[row].DeclarationIndex];

        if (declaration.HistoryDepth > 0 && declaration.NoHistoryAction == OnNoHistory::Clear)
        {
            usage |= BufferUsageBits::TransferDestination;
        }

        return usage;
    }

    // The ring phases a pass's sets cycle through: the least common multiple of the copy counts of the rows it binds.
    int32_t PhasesOf(const CompiledData& data, const BoundPass& bound)
    {
        int32_t phases = 1;

        for (int32_t slotIndex = bound.FirstSlot; slotIndex < bound.FirstSlot + bound.SlotCount; ++slotIndex)
        {
            const ResourceIndex resource = data.Slots[slotIndex].Resource;

            if (resource.IsValid())
            {
                phases = std::lcm(phases, data.CopyCounts[resource.Value]);
            }
        }

        return phases;
    }

    int32_t GroupCountOf(const CompiledData& data, int32_t pass)
    {
        const BoundPass& bound = data.BoundPasses[pass];
        int32_t groups = 0;

        for (int32_t slotIndex = bound.FirstSlot; slotIndex < bound.FirstSlot + bound.SlotCount; ++slotIndex)
        {
            groups = std::max(groups, static_cast<int32_t>(data.Slots[slotIndex].Group) + 1);
        }

        return groups;
    }

    RealizeError CreatePipeline(GraphImpl& graph, const CompiledPipeline& compiled, PipelineRuntime& pipeline)
    {
        const CompiledData& data = graph.Compiled.Data();
        const BoundPass& bound = data.BoundPasses[compiled.LayoutPass];
        const int32_t groups = GroupCountOf(data, compiled.LayoutPass);

        for (int32_t group = 0; group < groups; ++group)
        {
            std::vector<VkDescriptorSetLayoutBinding> bindings;

            for (int32_t slotIndex = bound.FirstSlot; slotIndex < bound.FirstSlot + bound.SlotCount; ++slotIndex)
            {
                const BindingSlot& slot = data.Slots[slotIndex];

                if (static_cast<int32_t>(slot.Group) != group)
                {
                    continue;
                }

                // CheckSupported let only kinds with a descriptor type through
                const VkDescriptorType type = *ToDescriptorType(slot.Kind, data.SlotWritable[slotIndex] != 0u);
                bindings.push_back(VkDescriptorSetLayoutBinding{ slot.Binding, type, slot.ArrayCount, ToVkShaderStages(slot.Stages), nullptr });
            }

            const VkDescriptorSetLayoutCreateInfo layoutInfo
            {
                VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO,
                nullptr,
                0,
                static_cast<uint32_t>(bindings.size()),
                bindings.data()
            };

            VkDescriptorSetLayout setLayout = VK_NULL_HANDLE;

            if (vkCreateDescriptorSetLayout(graph.VulkanDevice, &layoutInfo, nullptr, &setLayout) != VK_SUCCESS)
            {
                return RealizeError::DescriptorLayoutFailed;
            }

            pipeline.SetLayouts.push_back(setLayout);
        }

        // Slang puts every stage's push constant block at offset 0, so one range covers the pipeline
        const VkPushConstantRange pushRange{ ToVkShaderStages(compiled.PushConstantStages), 0u, compiled.PushConstantBytes };
        const bool hasPush = compiled.PushConstantBytes > 0u;

        const VkPipelineLayoutCreateInfo pipelineLayoutInfo
        {
            VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO,
            nullptr,
            0,
            static_cast<uint32_t>(pipeline.SetLayouts.size()),
            pipeline.SetLayouts.data(),
            hasPush ? 1u : 0u,
            hasPush ? &pushRange : nullptr
        };

        if (vkCreatePipelineLayout(graph.VulkanDevice, &pipelineLayoutInfo, nullptr, &pipeline.Layout) != VK_SUCCESS)
        {
            return RealizeError::PipelineLayoutFailed;
        }

        pipeline.LayoutHandle.Set(pipeline.Layout);
        pipeline.PushStages = ToRhiShaderStages(compiled.PushConstantStages);
        pipeline.PushBytes = compiled.PushConstantBytes;

        // ShaderBinaryOptions keeps spans, so this storage outlives the create call
        std::vector<rhi::DescriptorSetLayoutHandle> layoutHandles(pipeline.SetLayouts.size());

        for (size_t index = 0u; index < pipeline.SetLayouts.size(); ++index)
        {
            layoutHandles[index].Set(pipeline.SetLayouts[index]);
        }

        std::vector<rhi::PushConstantRange> pushRanges;

        if (hasPush)
        {
            pushRanges.push_back(rhi::PushConstantRange{ pipeline.PushStages, 0u, compiled.PushConstantBytes });
        }

        std::vector<rhi::ShaderBinaryOptions> options;
        options.reserve(compiled.Stages.size());

        for (const CompiledStage& stage : compiled.Stages)
        {
            options.emplace_back(std::span<const uint32_t>{ stage.Code }, ToRhiShaderStages(stage.Stage), stage.EntryPoint,
                                 std::span<rhi::PushConstantRange>{ pushRanges },
                                 std::span<rhi::DescriptorSetLayoutHandle>{ layoutHandles });
        }

        if (rhi::ShaderProgram::CreateFromBinary(graph.Device.Handle(), options, pipeline.Program).IsFailure())
        {
            return RealizeError::ShaderCreateFailed;
        }

        return RealizeError::Invalid;
    }

}

    // The copies of each history ring after its first, made like the first (same create info, a numbered name).
    void CreateHistoryCopies(GraphImpl& graph)
    {
        const CompiledData& data = graph.Compiled.Data();
        const int32_t rowCount = static_cast<int32_t>(data.Frozen.Resources.size());
        graph.ExtraCopies.clear();
        graph.ExtraCopies.reserve(data.CopyRows.size() - static_cast<size_t>(rowCount));

        for (int32_t copy = rowCount; copy < static_cast<int32_t>(data.CopyRows.size()); ++copy)
        {
            const int32_t row = data.CopyRows[copy];
            const FrozenResource& frozen = data.Frozen.Resources[row];
            const std::string name = std::format("{}#{}", frozen.Key.Name, copy - data.ExtraCopyBase[row] + 1);

            if (frozen.Kind == ResourceKind::Image)
            {
                graph.ExtraCopies.push_back(CreateImage(graph, row, name));
                continue;
            }

            const BufferDeclaration& declaration = data.Frozen.Buffers[frozen.DeclarationIndex];

            BufferCreateInfo info{};
            info.Domain = ResourceDomain::GPUOnly;
            info.Usage = UsageOf(data, row, declaration.Cpu);
            info.Size = static_cast<size_t>(data.Sized.Resources[row].ByteSize);
            info.Name = name;
            graph.ExtraCopies.push_back(graph.Resources.CreateBuffer(info, {}));
        }
    }

    RealizeError CheckSupported(const CompiledData& data)
    {
        if (data.CodeFormat != lodestone::ShaderCodeFormat::Spirv)
        {
            return RealizeError::CodeNotSpirv;
        }

        for (int32_t passIndex = 0; passIndex < static_cast<int32_t>(data.Frozen.Passes.size()); ++passIndex)
        {
            const FrozenPass& pass = data.Frozen.Passes[passIndex];

            if (pass.Kind != PassKind::Dispatch)
            {
                return RealizeError::PassKindNotYet;
            }

        }

        if (!data.Frozen.FrameTargets.empty())
        {
            return RealizeError::ResourceKindNotYet;
        }

        for (int32_t rowIndex = 0; rowIndex < static_cast<int32_t>(data.Frozen.Resources.size()); ++rowIndex)
        {
            const RealizeError resource = CheckResource(data, rowIndex);

            if (resource != RealizeError::Invalid)
            {
                return resource;
            }
        }

        for (const BindingSlot& slot : data.Slots)
        {
            if (slot.Form != lodestone::PlacementKind::Bound)
            {
                return RealizeError::PlacementNotBound;
            }

            if (!SlotIsSupported(slot))
            {
                return RealizeError::BindingNotYet;
            }
        }

        return RealizeError::Invalid;
    }

    RealizeError CreatePipelines(GraphImpl& graph)
    {
        const CompiledData& data = graph.Compiled.Data();
        graph.Pipelines.resize(data.Pipelines.size());

        for (size_t index = 0u; index < data.Pipelines.size(); ++index)
        {
            const CompiledPipeline& compiled = data.Pipelines[index];

            if (compiled.LayoutPass < 0)
            {
                continue;
            }

            const RealizeError error = CreatePipeline(graph, compiled, graph.Pipelines[index]);

            if (error != RealizeError::Invalid)
            {
                return error;
            }
        }

        return RealizeError::Invalid;
    }

    RealizeError CreateDescriptors(GraphImpl& graph, int32_t frame_slots)
    {
        const CompiledData& data = graph.Compiled.Data();
        const int32_t passCount = static_cast<int32_t>(data.Passes.size());
        graph.Passes.resize(passCount);

        uint32_t setCount = 0u;
        std::vector<VkDescriptorPoolSize> poolSizes;

        const auto countType = [&poolSizes](VkDescriptorType type, int32_t instances)
        {
            const auto sameType = [type](const VkDescriptorPoolSize& size)
            {
                return size.type == type;
            };

            const auto existing = std::ranges::find_if(poolSizes, sameType);

            if (existing == poolSizes.end())
            {
                poolSizes.push_back(VkDescriptorPoolSize{ type, static_cast<uint32_t>(instances) });
                return;
            }

            existing->descriptorCount += static_cast<uint32_t>(instances);
        };

        for (int32_t passIndex = 0; passIndex < passCount; ++passIndex)
        {
            const int32_t pipeline = data.LaidOut.Passes[passIndex].Pipeline;

            if (pipeline == k_NoPipeline)
            {
                continue;
            }

            const BoundPass& bound = data.BoundPasses[passIndex];
            PassRuntime& pass = graph.Passes[passIndex];
            pass.GroupCount = static_cast<int32_t>(graph.Pipelines[pipeline].SetLayouts.size());
            pass.Phases = PhasesOf(data, bound);
            const int32_t instances = frame_slots * pass.Phases;
            pass.WrittenEpochs.assign(static_cast<size_t>(instances) * bound.SlotCount, 0u);
            setCount += static_cast<uint32_t>(pass.GroupCount * instances);

            for (int32_t slotIndex = bound.FirstSlot; slotIndex < bound.FirstSlot + bound.SlotCount; ++slotIndex)
            {
                // CheckSupported let only kinds with a descriptor type through
                countType(*ToDescriptorType(data.Slots[slotIndex].Kind, data.SlotWritable[slotIndex] != 0u), instances);
            }
        }

        if (setCount == 0u)
        {
            return RealizeError::Invalid;
        }

        const VkDescriptorPoolCreateInfo poolInfo
        {
            VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO,
            nullptr,
            0,
            setCount,
            static_cast<uint32_t>(poolSizes.size()),
            poolSizes.data()
        };

        if (vkCreateDescriptorPool(graph.VulkanDevice, &poolInfo, nullptr, &graph.DescriptorPool) != VK_SUCCESS)
        {
            return RealizeError::DescriptorPoolFailed;
        }

        for (int32_t passIndex = 0; passIndex < passCount; ++passIndex)
        {
            PassRuntime& pass = graph.Passes[passIndex];

            if (pass.GroupCount == 0)
            {
                continue;
            }

            const PipelineRuntime& pipeline = graph.Pipelines[data.LaidOut.Passes[passIndex].Pipeline];
            const int32_t instances = frame_slots * pass.Phases;
            pass.Sets.resize(static_cast<size_t>(pass.GroupCount) * instances, VK_NULL_HANDLE);

            for (int32_t instance = 0; instance < instances; ++instance)
            {
                const VkDescriptorSetAllocateInfo allocateInfo
                {
                    VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO,
                    nullptr,
                    graph.DescriptorPool,
                    static_cast<uint32_t>(pass.GroupCount),
                    pipeline.SetLayouts.data()
                };

                VkDescriptorSet* sets = pass.Sets.data() + static_cast<size_t>(instance) * pass.GroupCount;

                if (vkAllocateDescriptorSets(graph.VulkanDevice, &allocateInfo, sets) != VK_SUCCESS)
                {
                    return RealizeError::DescriptorPoolFailed;
                }
            }
        }

        return RealizeError::Invalid;
    }

    RealizeError CreateFrameSlots(GraphImpl& graph, int32_t frame_slots)
    {
        const uint32_t family = graph.Device.GetQueueFamilyIndices().Graphics;
        graph.FrameSlots.reserve(static_cast<size_t>(frame_slots));

        for (int32_t frameSlot = 0; frameSlot < frame_slots; ++frameSlot)
        {
            FrameSlotRuntime& slot = graph.FrameSlots.emplace_back(
                FrameSlotRuntime{ rhi::CommandPool(graph.Device.Handle(), rhi::CommandPool::Type::Compute, family), 0u });

            if (slot.Pool.AllocateCommandBuffers(1u).IsFailure())
            {
                return RealizeError::CommandPoolFailed;
            }
        }

        return RealizeError::Invalid;
    }

    void CreateResources(GraphImpl& graph)
    {
        const CompiledData& data = graph.Compiled.Data();
        const int32_t rowCount = static_cast<int32_t>(data.Frozen.Resources.size());
        graph.Rows.resize(rowCount);
        graph.States.assign(rowCount, ResourceState{ .Readiness = ResourceReadiness::Pending, .Epoch = 0u });

        for (int32_t row = 0; row < rowCount; ++row)
        {
            const FrozenResource& frozen = data.Frozen.Resources[row];

            // an import waits for Supply
            if (frozen.Origin != ResourceOrigin::Declared)
            {
                continue;
            }

            switch (frozen.Kind)
            {
            case ResourceKind::Buffer:
            {
                const BufferDeclaration& declaration = data.Frozen.Buffers[frozen.DeclarationIndex];

                BufferCreateInfo info{};
                info.Domain = ResourceDomain::GPUOnly;
                info.Usage = UsageOf(data, row, declaration.Cpu);
                info.Size = static_cast<size_t>(data.Sized.Resources[row].ByteSize);
                info.Name = frozen.Key.Name;
                graph.Rows[row].Future = graph.Resources.CreateBuffer(info, {});
                break;
            }
            case ResourceKind::Image:
                graph.Rows[row].Future = CreateImage(graph, row, frozen.Key.Name);
                break;
            case ResourceKind::Sampler:
                graph.Rows[row].Future = graph.Resources.CreateSampler(data.Frozen.Samplers[frozen.DeclarationIndex].CreateInfo);
                break;
            case ResourceKind::Invalid:
                break;
            }

            // a graph image starts with no contents: its first barrier changes it from Undefined
            graph.Rows[row].Owned = true;
            graph.States[row].Epoch = 1u;
        }

        CreateHistoryCopies(graph);
    }

    void PrepareCoreState(GraphImpl& graph)
    {
        const CompiledData& data = graph.Compiled.Data();
        PrepareFramePlan(graph.Compiled, graph.Plan);
        graph.Params.Prepare(graph.Compiled);
        graph.Sync.assign(data.CopyRows.size(), ResourceSync{});
        graph.Controls.assign(data.Passes.size(), PassControl{});

        // the most a Tick writes: every barrier of a plan, one wait per row, every slot of every pass
        graph.WaitScratch.reserve(data.Frozen.Resources.size());
        graph.WriteScratch.reserve(data.Slots.size());
        graph.BufferInfoScratch.reserve(data.Slots.size());
        graph.ImageInfoScratch.reserve(data.Slots.size());
        graph.ImageBarrierScratch.reserve(data.CopyRows.size());
        graph.SetScratch.reserve(8u);

        for (const PassRuntime& pass : graph.Passes)
        {
            graph.SetScratch.reserve(std::max(graph.SetScratch.capacity(), static_cast<size_t>(pass.GroupCount)));
        }
    }

}
