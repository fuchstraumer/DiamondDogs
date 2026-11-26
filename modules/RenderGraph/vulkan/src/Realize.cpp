#include "Realize.hpp"
#include "Declarations.hpp"
#include "FramePlan.hpp"
#include "GraphImpl.hpp"
#include "ResourceFlags.hpp"
#include "ResourceFuture.hpp"
#include "RhiFlags.hpp"
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
#include "ResourcePlacement.hpp"
#include "RhiResourceFlags.hpp"
#include "ResourceTypes.hpp"
#include "ShaderProgram.hpp"
#include "ShaderTypes.hpp"
#include "alias/Packing.hpp"
#include "bind/BoundGraph.hpp"
#include "compile/CompiledData.hpp"
#include <cstddef>
#include "freeze/FrozenGraph.hpp"
#include "layout/LaidOutGraph.hpp"
#include "size/SizedGraph.hpp"
#include "LoomFormats.hpp"
#include "ImageDataFormats.hpp"
#include "containers/SmallVector.hpp"
#include <vulkan/vulkan_core.h>
#include <algorithm>
#include <cstdint>
#include <format>
#include <numeric>
#include <string>
#include <string_view>
#include <span>
#include <utility>
#include <vector>

using namespace foundation; // smallvector and other foundation utilities

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

    bool HasAccess(const PassUse& use, rhi::Access access)
    {
        return (use.Scope.Accesses & access) != rhi::Access::None;
    }

    bool IsDepthFormat(lodestone::TextureFormat format)
    {
        return format == lodestone::TextureFormat::Depth16Unorm || format == lodestone::TextureFormat::Depth24Plus ||
               format == lodestone::TextureFormat::Depth24PlusStencil8 || format == lodestone::TextureFormat::Depth32Float;
    }

    // The attachment and transfer usages of one use (step 7): targets, and a Record pass's copies
    ImageUsageBits TargetUsageOf(const PassUse& use, bool stencil)
    {
        ImageUsageBits usage = ImageUsageBits::Invalid;

        if (HasAccess(use, rhi::Access::ColorAttachmentRead))
        {
            usage |= ImageUsageBits::RenderTargetRead;
        }

        if (HasAccess(use, rhi::Access::ColorAttachmentWrite))
        {
            usage |= ImageUsageBits::RenderTargetWrite;
        }

        if (HasAccess(use, rhi::Access::DepthStencilAttachmentRead))
        {
            usage |= stencil ? ImageUsageBits::DepthStencilRead : ImageUsageBits::DepthRead;
        }

        if (HasAccess(use, rhi::Access::DepthStencilAttachmentWrite))
        {
            usage |= stencil ? ImageUsageBits::DepthStencilWrite : ImageUsageBits::DepthWrite;
        }

        if (HasAccess(use, rhi::Access::TransferRead))
        {
            usage |= ImageUsageBits::TransferSource;
        }

        if (HasAccess(use, rhi::Access::TransferWrite))
        {
            usage |= ImageUsageBits::TransferDestination;
        }

        return usage;
    }

    // Every GPU use of an image row, and the transfer that its CpuAccess needs.
    ImageUsageBits ImageUsageOf(const CompiledData& data, int32_t row, CpuAccess cpu)
    {
        constexpr rhi::Access k_Storage = rhi::Access::ShaderStorageRead | rhi::Access::ShaderStorageWrite;
        ImageUsageBits usage = ImageUsageBits::Invalid;
        const ImageDeclaration& declaration = data.Frozen.Images[data.Frozen.Resources[row].DeclarationIndex].Declaration;
        const bool stencil = declaration.Format == lodestone::TextureFormat::Depth24PlusStencil8;

        for (const PassUse& use : data.Uses)
        {
            if (use.Resource != row)
            {
                continue;
            }

            usage |= TargetUsageOf(use, stencil);
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

    // An image at `extent`: Size's extent, or for an OfTarget image the one its target's allocation gives
    ImageCreateInfo ImageInfoOf(const GraphImpl& graph, int32_t row, std::string_view name, const ImageExtent& extent)
    {
        const CompiledData& data = graph.Compiled.Data();
        const FrozenResource& frozen = data.Frozen.Resources[row];
        const ImageDeclaration& declaration = data.Frozen.Images[frozen.DeclarationIndex].Declaration;
        const SizedResource& sized = data.Sized.Resources[row];
        const bool volume = sized.Dimension == lodestone::TextureDimension::Texture3D;
        const int32_t layers = std::max(sized.Layers, 1);
        const int32_t mips = MipCountOf(declaration.Mips, extent);

        ImageCreateInfo info{};
        info.Domain = ResourceDomain::GPUOnly;
        info.Usage = ImageUsageOf(data, row, declaration.Cpu);
        info.Type = ImageTypeOf(sized.Dimension);
        // CheckSupported refused a format with no ImageFormat
        info.Format = *ToImageFormat(declaration.Format);
        info.Width = static_cast<uint32_t>(std::max(extent.Width, 1));
        info.Height = static_cast<uint32_t>(std::max(extent.Height, 1));
        info.Depth = volume ? static_cast<uint32_t>(std::max(extent.Depth, 1)) : 1u;
        info.MipLevels = static_cast<uint32_t>(mips);
        info.ArrayLayers = static_cast<uint32_t>(layers);
        info.Name = name;

        // a depth target's view sees its depth (and stencil) aspect
        ImageAspectFlags aspect = ImageAspectFlags::Color;

        if (IsDepthFormat(declaration.Format))
        {
            const bool stencil = declaration.Format == lodestone::TextureFormat::Depth24PlusStencil8;
            aspect = stencil ? ImageAspectFlags::DepthStencil : ImageAspectFlags::Depth;
        }

        ImageViewCreateInfo view{};
        view.ViewType = ViewTypeOf(sized, layers);
        view.Range = ImageRange{ aspect, 0u, static_cast<uint32_t>(mips), 0u, static_cast<uint32_t>(layers) };
        info.ViewInfo = view;
        return info;
    }

    rhi::ResourceFuture<GraphicsResource> CreateImage(GraphImpl& graph, int32_t row, std::string_view name,
                                                      const ImageExtent& extent)
    {
        return graph.Resources.CreateImage(ImageInfoOf(graph, row, name, extent));
    }

    bool IsTargetSized(const CompiledData& data, int32_t row)
    {
        const FrozenResource& frozen = data.Frozen.Resources[row];

        if (frozen.Kind != ResourceKind::Image || frozen.Origin != ResourceOrigin::Declared)
        {
            return false;
        }

        return data.Frozen.Images[frozen.DeclarationIndex].Declaration.Extent.Kind == ExtentSource::Kind::Target;
    }

    // The name of a copy: the key, and #n for a history ring's other copies
    std::string CopyName(const CompiledData& data, int32_t row, int32_t copy)
    {
        const std::string_view key = data.Frozen.Resources[row].Key.Name;

        if (copy == row)
        {
            return std::string{ key };
        }

        return std::format("{}#{}", key, copy - data.ExtraCopyBase[row] + 1);
    }

    // The resources this backend can create: buffers, images (an OfTarget image when its target first reports an
    // extent), samplers. A cube waits for a request; a format with no ImageFormat cannot be created.
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

        if (!ToImageFormat(declaration.Format).has_value())
        {
            return RealizeError::ResourceKindNotYet;
        }

        return RealizeError::Invalid;
    }

    // A slot's reflected shader stages as pipeline stages
    rhi::PipelineStage PipelineStagesOf(lodestone::ShaderStageKind kinds)
    {
        constexpr std::pair<lodestone::ShaderStageKind, rhi::PipelineStage> k_Table[]
        {
            { lodestone::ShaderStageKind::Vertex, rhi::PipelineStage::VertexShader },
            { lodestone::ShaderStageKind::Fragment, rhi::PipelineStage::FragmentShader },
            { lodestone::ShaderStageKind::Compute, rhi::PipelineStage::ComputeShader },
            { lodestone::ShaderStageKind::Task, rhi::PipelineStage::TaskShader },
            { lodestone::ShaderStageKind::Mesh, rhi::PipelineStage::MeshShader },
        };

        const uint32_t bits = static_cast<uint32_t>(kinds);
        rhi::PipelineStage result = rhi::PipelineStage::None;

        for (const auto& [kind, stage] : k_Table)
        {
            if ((bits & static_cast<uint32_t>(kind)) != 0u)
            {
                result |= stage;
            }
        }

        return result;
    }

    // The uniform and storage usage bits of each shader stage that a use names
    struct StageBufferUsage
    {
        rhi::PipelineStage Stage;
        BufferUsageBits Uniform;
        BufferUsageBits Storage;
    };

    constexpr StageBufferUsage k_StageBufferUsages[]
    {
        { rhi::PipelineStage::VertexShader, BufferUsageBits::VertexShaderUBO, BufferUsageBits::VertexShaderUAV },
        { rhi::PipelineStage::FragmentShader, BufferUsageBits::FragmentShaderUBO, BufferUsageBits::FragmentShaderUAV },
        { rhi::PipelineStage::ComputeShader, BufferUsageBits::ComputeShaderUBO, BufferUsageBits::ComputeShaderUAV },
        { rhi::PipelineStage::TaskShader, BufferUsageBits::TaskShaderUBO, BufferUsageBits::TaskShaderUAV },
        { rhi::PipelineStage::MeshShader, BufferUsageBits::MeshShaderUBO, BufferUsageBits::MeshShaderUAV },
    };

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
            const bool uniform = (use.Scope.Accesses & rhi::Access::UniformRead) != rhi::Access::None;
            const bool storage = (use.Scope.Accesses & k_Storage) != rhi::Access::None;
            const bool indirect = (use.Scope.Accesses & rhi::Access::IndirectCommandRead) != rhi::Access::None;

            for (const StageBufferUsage& stage : k_StageBufferUsages)
            {
                if ((use.Scope.Stages & stage.Stage) == rhi::PipelineStage::None)
                {
                    continue;
                }

                if (uniform)
                {
                    usage |= stage.Uniform;
                }

                if (storage)
                {
                    usage |= stage.Storage;
                }
            }

            // one Vulkan usage serves both indirect forms
            if (indirect)
            {
                usage |= BufferUsageBits::DispatchIndirect | BufferUsageBits::DrawIndirect;
            }

            if ((use.Scope.Accesses & rhi::Access::IndexRead) != rhi::Access::None)
            {
                usage |= BufferUsageBits::IndexInput;
            }

            if ((use.Scope.Accesses & rhi::Access::VertexAttributeRead) != rhi::Access::None)
            {
                usage |= BufferUsageBits::VertexInput;
            }

            if ((use.Scope.Accesses & rhi::Access::TransferRead) != rhi::Access::None)
            {
                usage |= BufferUsageBits::TransferSource;
            }

            if ((use.Scope.Accesses & rhi::Access::TransferWrite) != rhi::Access::None)
            {
                usage |= BufferUsageBits::TransferDestination;
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
            SmallVector<VkDescriptorSetLayoutBinding, 16> bindings;

            for (int32_t slotIndex = bound.FirstSlot; slotIndex < bound.FirstSlot + bound.SlotCount; ++slotIndex)
            {
                const BindingSlot& slot = data.Slots[slotIndex];

                if (std::cmp_not_equal(slot.Group, group))
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
        SmallVector<rhi::DescriptorSetLayoutHandle, 8> layoutHandles(pipeline.SetLayouts.size());

        for (size_t index = 0u; index < pipeline.SetLayouts.size(); ++index)
        {
            layoutHandles[index].Set(pipeline.SetLayouts[index]);
        }

        rhi::PushConstantRange pushConstant
        {
            pipeline.PushStages,
            0u,
            compiled.PushConstantBytes
        };
        
        const std::span<rhi::PushConstantRange> pushSpan
        {
            &pushConstant,
            hasPush ? 1u : 0u
        };

        // 4 should be WAYYYY more than enough for 99.9% of shaders, except raytracing payloads
        // where it's whatever, we spill to the heap to handle those stupid things
        foundation::SmallVector<rhi::ShaderBinaryOptions, 4> options;
        options.reserve(compiled.Stages.size());

        for (const CompiledStage& stage : compiled.Stages)
        {
            std::span<rhi::DescriptorSetLayoutHandle> layoutSpan{ layoutHandles };
            const std::span<const uint32_t> spirvWords = SpirvWords(stage);
            options.emplace_back(spirvWords,
                                 ToRhiShaderStages(stage.Stage),
                                 stage.EntryPoint,
                                 pushSpan,
                                 layoutSpan);
        }

        if (rhi::ShaderProgram::CreateFromBinary(graph.Device.Handle(), options, pipeline.Program).IsFailure())
        {
            return RealizeError::ShaderCreateFailed;
        }

        return RealizeError::Invalid;
    }

}

    // A buffer row's create info: every GPU use, and the copies its CpuAccess needs
    BufferCreateInfo RowBufferInfo(const CompiledData& data, int32_t row)
    {
        const FrozenResource& frozen = data.Frozen.Resources[row];
        const BufferDeclaration& declaration = data.Frozen.Buffers[frozen.DeclarationIndex];

        BufferCreateInfo info{};
        info.Domain = ResourceDomain::GPUOnly;
        info.Usage = UsageOf(data, row, declaration.Cpu);
        info.Size = static_cast<size_t>(data.Sized.Resources[row].ByteSize);
        info.Name = frozen.Key.Name;
        return info;
    }

    // Asks the device what each candidate needs, with the create info its row is made from, packs them, and asks for
    // the heaps. The requirement queries run here, on the calling thread; the heaps come later, as futures.
    void PlanAliasing(GraphImpl& graph)
    {
        const CompiledData& data = graph.Compiled.Data();
        std::vector<MemoryNeed> needs;
        needs.reserve(data.AliasCandidates.size());

        for (const AliasCandidate& candidate : data.AliasCandidates)
        {
            rhi::placement::MemoryRequirements requirements{};

            if (candidate.Class == AliasClass::Image)
            {
                const ImageCreateInfo info = ImageInfoOf(graph, candidate.Row, data.Frozen.Resources[candidate.Row].Key.Name,
                                                         data.Sized.Resources[candidate.Row].Extent);
                requirements = rhi::placement::GetImageMemoryRequirements(graph.Resources, info);
            }
            else
            {
                requirements = rhi::placement::GetBufferMemoryRequirements(graph.Resources, RowBufferInfo(data, candidate.Row));
            }

            needs.push_back(MemoryNeed
            {
                .Size = requirements.Size,
                .Alignment = requirements.Alignment,
                .MemoryTypeBits = requirements.MemoryTypeBits
            });
        }

        graph.Aliases = PackAliases(data.AliasCandidates, needs, data.RowCount);
        graph.Heaps.reserve(graph.Aliases.Heaps.size());

        for (const AliasHeap& heap : graph.Aliases.Heaps)
        {
            const rhi::placement::MemoryHeapCreateInfo info
            {
                .Size = heap.Size,
                .MemoryTypeBits = heap.MemoryTypeBits,
                .Name = heap.Class == AliasClass::Image ? "Loom.AliasHeap.Images" : "Loom.AliasHeap.Buffers",
            };

            graph.Heaps.push_back(rhi::placement::CreateMemoryHeap(graph.Resources, info));
        }
    }

    // The copies of each history ring after its first, made like the first (same create info, a numbered name).
    void CreateHistoryCopies(GraphImpl& graph)
    {
        const CompiledData& data = graph.Compiled.Data();
        const int32_t rowCount = data.RowCount;
        graph.ExtraCopies.clear();
        graph.ExtraCopies.reserve(data.CopyRows.size() - static_cast<size_t>(rowCount));

        for (int32_t copy = rowCount; copy < static_cast<int32_t>(data.CopyRows.size()); ++copy)
        {
            const int32_t row = data.CopyRows[copy];
            const FrozenResource& frozen = data.Frozen.Resources[row];
            const std::string name = CopyName(data, row, copy);

            // an OfTarget copy waits for its target's first extent, like the row's first copy
            if (IsTargetSized(data, row))
            {
                graph.ExtraCopies.emplace_back();
                continue;
            }

            if (frozen.Kind == ResourceKind::Image)
            {
                graph.ExtraCopies.push_back(CreateImage(graph, row, name, data.Sized.Resources[row].Extent));
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

    RealizeError CheckSupported(const CompiledData& data, const RealizeInputs& inputs)
    {
        if (data.CodeFormat != lodestone::ShaderCodeFormat::Spirv)
        {
            return RealizeError::CodeNotSpirv;
        }

        if (inputs.TargetSettleSeconds < 0.0)
        {
            return RealizeError::SettleUnset;
        }

        // vertices are pulled (step 7 Q8)
        for (const FrozenPass& pass : data.Frozen.Passes)
        {
            if (!pass.VertexBuffers.empty())
            {
                return RealizeError::BindingNotYet;
            }
        }

        // LoomFrame holds a fixed count of targets
        if (data.Frozen.FrameTargets.size() > k_LoomMaxFrameTargets)
        {
            return RealizeError::ResourceKindNotYet;
        }

        for (const FrozenFrameTarget& target : data.Frozen.FrameTargets)
        {
            const auto boundHere = [&target](const FrameTargetBinding& binding)
            {
                return binding.Key.Hash == target.Key.Hash;
            };

            if (std::ranges::none_of(inputs.FrameTargets, boundHere))
            {
                return RealizeError::FrameTargetUnbound;
            }
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
        SmallVector<VkDescriptorPoolSize, 8> poolSizes;

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
            pass.InstanceGeneration.assign(static_cast<size_t>(instances), 0u);
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
                FrameSlotRuntime{ rhi::CommandPool(graph.Device.Handle(), rhi::CommandPool::Type::Graphics, family), 0u });

            if (slot.Pool.AllocateCommandBuffers(1u).IsFailure())
            {
                return RealizeError::CommandPoolFailed;
            }
        }

        return RealizeError::Invalid;
    }

    void CreateResources(GraphImpl& graph, MemoryAliasing aliasing)
    {
        const CompiledData& data = graph.Compiled.Data();
        const int32_t resourceCount = static_cast<int32_t>(data.Frozen.Resources.size());
        graph.Rows.resize(resourceCount);
        graph.States.assign(data.RowCount, ResourceState{ .Readiness = ResourceReadiness::Pending, .Epoch = 0u });

        if (aliasing == MemoryAliasing::On)
        {
            PlanAliasing(graph);
        }

        for (int32_t row = 0; row < resourceCount; ++row)
        {
            const FrozenResource& frozen = data.Frozen.Resources[row];

            // an import waits for Supply
            if (frozen.Origin != ResourceOrigin::Declared)
            {
                continue;
            }

            // an OfTarget image waits for its target's first extent (Tick makes it)
            if (IsTargetSized(data, row))
            {
                graph.Rows[row].Owned = true;
                continue;
            }

            // a placed row waits for its heap (CreatePlacedRows)
            if (graph.Aliases.IsPlaced(row))
            {
                graph.PendingPlacedRows.push_back(row);
                graph.Rows[row].Owned = true;
                graph.States[row].Epoch = 1u;
                continue;
            }

            switch (frozen.Kind)
            {
            case ResourceKind::Buffer:
                graph.Rows[row].Future = graph.Resources.CreateBuffer(RowBufferInfo(data, row), {});
                break;
            case ResourceKind::Image:
                graph.Rows[row].Future = CreateImage(graph, row, frozen.Key.Name, data.Sized.Resources[row].Extent);
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

        // LoomFrame: one uniform buffer, written by vkCmdUpdateBuffer at the start of each frame
        const auto loomFrame = [](const BindingSlot& slot)
        {
            return slot.BuiltIn == BuiltInBinding::LoomFrame;
        };

        graph.HasLoomFrame = std::ranges::any_of(data.Slots, loomFrame);

        if (!graph.HasLoomFrame)
        {
            return;
        }

        BufferUsageBits usage = BufferUsageBits::TransferDestination;

        for (const BindingSlot& slot : data.Slots)
        {
            if (!loomFrame(slot))
            {
                continue;
            }

            const rhi::PipelineStage stages = PipelineStagesOf(slot.Stages);

            for (const StageBufferUsage& stage : k_StageBufferUsages)
            {
                if ((stages & stage.Stage) != rhi::PipelineStage::None)
                {
                    usage |= stage.Uniform;
                }
            }
        }

        BufferCreateInfo info{};
        info.Domain = ResourceDomain::GPUOnly;
        info.Usage = usage;
        info.Size = sizeof(LoomFrameConstants);
        info.Name = "LoomFrame";
        graph.LoomFrameBuffer = graph.Resources.CreateBuffer(info, {});
    }

    RealizeError PrepareTargets(GraphImpl& graph, const RealizeInputs& inputs)
    {
        const CompiledData& data = graph.Compiled.Data();
        graph.Targets.resize(data.FrameTargets.size());

        for (size_t target = 0u; target < data.FrameTargets.size(); ++target)
        {
            const KeyRef key = data.Frozen.FrameTargets[target].Key;

            const auto boundHere = [key](const FrameTargetBinding& binding)
            {
                return binding.Key.Hash == key.Hash;
            };

            // CheckSupported found every binding
            const auto binding = std::ranges::find_if(inputs.FrameTargets, boundHere);
            TargetRuntime& runtime = graph.Targets[target];
            runtime.Target = &binding->Target;

            // a frame-target row is always recordable; each acquire starts a new epoch in Undefined
            ResourceState& state = graph.States[data.FrameTargets[target].Row];
            state.Readiness = ResourceReadiness::Created;
            state.FinalLayout = runtime.Target->FinalLayout();
        }

        const auto draws = [](const FrozenPass& pass)
        {
            return pass.Kind == PassKind::Draw;
        };

        if (std::ranges::any_of(data.Frozen.Passes, draws) && !LoadRasterFunctions(graph.VulkanDevice, graph.Raster))
        {
            return RealizeError::DrawStateUnavailable;
        }

        return RealizeError::Invalid;
    }

    void CreatePlacedRows(GraphImpl& graph)
    {
        if (graph.PendingPlacedRows.empty())
        {
            return;
        }

        const auto settled = [](const rhi::ResourceFuture<GraphicsResource>& heap)
        {
            const rhi::FutureState state = heap.State();
            return state == rhi::FutureState::Complete || state == rhi::FutureState::Failed;
        };

        if (!std::ranges::all_of(graph.Heaps, settled))
        {
            return;
        }

        const CompiledData& data = graph.Compiled.Data();

        for (const int32_t row : graph.PendingPlacedRows)
        {
            const AliasPlacement& placement = graph.Aliases.Placements[row];
            const rhi::ResourceFuture<GraphicsResource>& heap = graph.Heaps[placement.Heap];
            const bool image = data.Frozen.Resources[row].Kind == ResourceKind::Image;

            // a failed heap costs the memory it would have saved, not the row: the row gets its own memory. The
            // barriers still treat it as aliased, which only waits longer than it must.
            if (heap.State() != rhi::FutureState::Complete)
            {
                graph.Rows[row].Future = image ? CreateImage(graph, row, data.Frozen.Resources[row].Key.Name,
                                                             data.Sized.Resources[row].Extent)
                                               : graph.Resources.CreateBuffer(RowBufferInfo(data, row), {});
                continue;
            }

            const rhi::placement::Placement where{ .Heap = heap.Get(), .Offset = placement.Offset };

            if (image)
            {
                const ImageCreateInfo info = ImageInfoOf(graph, row, data.Frozen.Resources[row].Key.Name,
                                                         data.Sized.Resources[row].Extent);
                graph.Rows[row].Future = rhi::placement::CreatePlacedImage(graph.Resources, info, where);
                continue;
            }

            graph.Rows[row].Future = rhi::placement::CreatePlacedBuffer(graph.Resources, RowBufferInfo(data, row), where);
        }

        graph.PendingPlacedRows.clear();
        graph.AllRowsSettled = false;
    }

    int32_t TargetOfImage(const CompiledData& data, int32_t row) noexcept
    {
        if (!IsTargetSized(data, row))
        {
            return -1;
        }

        return data.Frozen.Images[data.Frozen.Resources[row].DeclarationIndex].ExtentTarget.Value;
    }

    rhi::ResourceFuture<GraphicsResource> CreateTargetImage(GraphImpl& graph, int32_t row, int32_t copy, Extent2D extent)
    {
        const CompiledData& data = graph.Compiled.Data();
        const float scale = data.Frozen.Images[data.Frozen.Resources[row].DeclarationIndex].Declaration.Extent.Scale;
        const Extent2D scaled = ScaledExtent(extent, scale);
        const ImageExtent imageExtent{ .Width = scaled.Width, .Height = scaled.Height, .Depth = 1 };
        return CreateImage(graph, row, CopyName(data, row, copy), imageExtent);
    }

    void PrepareCoreState(GraphImpl& graph)
    {
        const CompiledData& data = graph.Compiled.Data();
        PrepareFramePlan(graph.Compiled, graph.Plan);
        graph.Params.Prepare(graph.Compiled);
        graph.Sync.assign(data.CopyRows.size(), ResourceSync{});
        graph.Controls.assign(data.Passes.size(), PassControl{});

        // the most a Tick writes: every barrier of a plan, one wait per row and per target, every slot of every pass
        graph.WaitScratch.reserve(static_cast<size_t>(data.RowCount));
        graph.NextImages.reserve(data.CopyRows.size());
        graph.Retired.reserve(data.CopyRows.size());
        graph.Destroys.reserve(data.CopyRows.size());
        graph.WriteScratch.reserve(data.Slots.size());
        graph.BufferInfoScratch.reserve(data.Slots.size());
        graph.ImageInfoScratch.reserve(data.Slots.size());
        graph.ImageBarrierScratch.reserve(data.CopyRows.size());
    }

}
