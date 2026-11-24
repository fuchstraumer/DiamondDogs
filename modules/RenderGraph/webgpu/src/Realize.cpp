#include "Realize.hpp"
#include "Declarations.hpp"
#include "FramePlan.hpp"
#include "GraphImpl.hpp"
#include "ImageDataFormats.hpp"
#include "LoomFormats.hpp"
#include "LoomGraph.hpp"
#include "Records.hpp"
#include "ResourceContext.hpp"
#include "ResourceFlags.hpp"
#include "ResourceFuture.hpp"
#include "ResourceTypes.hpp"
#include "RhiFlags.hpp"
#include "RhiResourceFlags.hpp"
#include "ShaderLibraryTypes.hpp"
#include "WebGpuMapping.hpp"
#include "bind/BoundGraph.hpp"
#include "compile/CompiledData.hpp"
#include "freeze/FrozenGraph.hpp"
#include "layout/LaidOutGraph.hpp"
#include "size/SizedGraph.hpp"
#include <utility>
#include <webgpu/webgpu_cpp.h>
#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <format>
#include <numeric>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

// Realize: checks that this backend can run the graph, then creates what the graph runs on. Loom builds its own bind
// group layouts and shader modules from the compiled slot table and the copied WGSL, so nothing here reads the bundle.
namespace loom
{

namespace
{

    // The constants block of a uniform buffer delivery is padded to this, the size of the largest WGSL uniform alignment
    constexpr uint64_t k_ConstantsPadBytes = 16u;
    // The alignment of a pass's slice of the params buffer, at least: the device may need more
    constexpr uint32_t k_MinParamsOffsetAlignment = 256u;

    bool SlotIsSupported(const BindingSlot& slot);
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

    ImageUsageBits ImageUsageOf(const CompiledData& data, int32_t row, CpuAccess cpu);
    ImageType ImageTypeOf(lodestone::TextureDimension dimension);
    // The layers of an image row: the declaration's for a graph image, the promise's for an import.
    int32_t LayersOf(const CompiledData& data, int32_t row);
    // The format of an image row in foundation's terms, or no value when there is none.
    std::optional<ImageFormat> FormatOfRow(const CompiledData& data, int32_t row);
    // The view must match how the shaders bind the image: as an array, or not. Cube images are refused before this.
    ImageViewType ViewTypeOf(const SizedResource& sized, int32_t layers);
    int32_t MipCountOf(const MipSource& mips, const ImageExtent& extent);
    rhi::ResourceFuture<GraphicsResource> CreateImage(GraphImpl& graph, int32_t row, std::string_view name);
    RealizeError CheckResource(const CompiledData& data, int32_t row);
    BufferUsageBits UsageOf(const CompiledData& data, int32_t row, CpuAccess cpu);
    int32_t PhasesOf(const CompiledData& data, const BoundPass& bound);
    int32_t GroupCountOf(const CompiledData& data, const CompiledPipeline& compiled);
    SlotTraits TraitsOf(const CompiledData& data, const BindingSlot& slot);
    bool IsConstantsSlot(const CompiledPipeline& compiled, const BindingSlot& slot);
    RealizeError CreatePipeline(GraphImpl& graph, const CompiledPipeline& compiled, PipelineRuntime& pipeline);
    void CreateHistoryCopies(GraphImpl& graph);
    uint64_t AlignUp(uint64_t value, uint64_t alignment);

}

    RealizeError CheckSupported(const CompiledData& data)
    {
        if (data.CodeFormat != lodestone::ShaderCodeFormat::Wgsl)
        {
            return RealizeError::CodeNotWgsl;
        }

        for (const FrozenPass& pass : data.Frozen.Passes)
        {
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

            // the LoomFrame buffer is not written by this backend yet
            if (!SlotIsSupported(slot) || slot.BuiltIn != BuiltInBinding::None)
            {
                return RealizeError::BindingNotYet;
            }
        }

        // WebGPU has no push constants: a WGSL profile delivers constants in a uniform buffer
        for (const CompiledPipeline& pipeline : data.Pipelines)
        {
            if (pipeline.ConstantsDelivery == lodestone::EntryPointConstantsDelivery::PushConstant)
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

            if (compiled.LayoutPass >= 0)
            {
                const RealizeError error = CreatePipeline(graph, compiled, graph.Pipelines[index]);

                if (error != RealizeError::Invalid)
                {
                    return error;
                }
            }
        }

        return RealizeError::Invalid;
    }

    void CreatePassRuntimes(GraphImpl& graph)
    {
        const CompiledData& data = graph.Compiled.Data();
        const int32_t passCount = static_cast<int32_t>(data.Passes.size());
        graph.Passes.resize(passCount);

        for (int32_t passIndex = 0; passIndex < passCount; ++passIndex)
        {
            const int32_t pipeline = data.LaidOut.Passes[passIndex].Pipeline;

            if (pipeline != k_NoPipeline)
            {
                const BoundPass& bound = data.BoundPasses[passIndex];
                PassRuntime& pass = graph.Passes[passIndex];
                pass.GroupCount = static_cast<int32_t>(graph.Pipelines[pipeline].NumGroupLayouts);
                pass.Phases = PhasesOf(data, bound);
                pass.BindGroups.resize(static_cast<size_t>(pass.GroupCount) * pass.Phases);
                pass.WrittenEpochs.assign(static_cast<size_t>(pass.Phases) * bound.SlotCount, 0u);
                pass.PhaseGeneration.assign(static_cast<size_t>(pass.Phases), 0u);
            }
        }
    }

    RealizeError CreateParamsBuffer(GraphImpl& graph)
    {
        const CompiledData& data = graph.Compiled.Data();

        // the alignment of a uniform buffer offset is a device limit, and 256 is the most any device asks
        uint32_t alignment = k_MinParamsOffsetAlignment;
        wgpu::Limits limits{};

        if (graph.Device.GetLimits(&limits) == wgpu::Status::Success)
        {
            alignment = std::max(limits.minUniformBufferOffsetAlignment, k_MinParamsOffsetAlignment);
        }

        uint64_t total = 0u;
        uint64_t largest = 0u;

        for (int32_t passIndex = 0; passIndex < static_cast<int32_t>(data.Passes.size()); ++passIndex)
        {
            const int32_t pipeline = data.LaidOut.Passes[passIndex].Pipeline;

            if (pipeline != k_NoPipeline &&
                data.Pipelines[pipeline].ConstantsDelivery == lodestone::EntryPointConstantsDelivery::UniformBuffer)
            {
                PassRuntime& pass = graph.Passes[passIndex];
                pass.ParamsOffset = total;
                pass.ParamsSize = AlignUp(std::max<uint64_t>(data.Pipelines[pipeline].ConstantsBytes, 1u), k_ConstantsPadBytes);
                total += AlignUp(pass.ParamsSize, alignment);
                largest = std::max(largest, pass.ParamsSize);
            }
        }

        if (total == 0u)
        {
            return RealizeError::Invalid;
        }

        wgpu::BufferDescriptor descriptor{};
        descriptor.label = "loom params";
        descriptor.usage = wgpu::BufferUsage::Uniform | wgpu::BufferUsage::CopyDst;
        descriptor.size = total;
        graph.ParamsBuffer = graph.Device.CreateBuffer(&descriptor);
        graph.ParamsScratch.assign(static_cast<size_t>(largest), std::byte{ 0 });

        return graph.ParamsBuffer ? RealizeError::Invalid : RealizeError::ParamsBufferFailed;
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
            if (frozen.Origin == ResourceOrigin::Declared)
            {
                if (frozen.Kind == ResourceKind::Buffer)
                {
                    const BufferDeclaration& declaration = data.Frozen.Buffers[frozen.DeclarationIndex];

                    BufferCreateInfo info{};
                    info.Domain = ResourceDomain::GPUOnly;
                    info.Usage = UsageOf(data, row, declaration.Cpu);
                    info.Size = static_cast<size_t>(data.Sized.Resources[row].ByteSize);
                    info.Name = frozen.Key.Name;
                    graph.Rows[row].Future = graph.Resources.CreateBuffer(info, {});
                }
                else if (frozen.Kind == ResourceKind::Image)
                {
                    graph.Rows[row].Future = CreateImage(graph, row, frozen.Key.Name);
                }
                else if (frozen.Kind == ResourceKind::Sampler)
                {
                    graph.Rows[row].Future = graph.Resources.CreateSampler(data.Frozen.Samplers[frozen.DeclarationIndex].CreateInfo);
                }

                graph.Rows[row].Owned = true;
                graph.States[row].Epoch = 1u;
            }
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
        graph.EmptyCopyCount = static_cast<uint32_t>(data.CopyRows.size());
        // the most a pass binds in one group: every slot, and the constants block
        graph.EntryScratch.reserve(data.Slots.size() + 1u);
    }

namespace
{

    bool SlotIsSupported(const BindingSlot& slot)
    {
        return slot.ArrayCount == 1u &&
               (slot.Kind == lodestone::BindingKind::UniformBuffer ||
                slot.Kind == lodestone::BindingKind::StorageBuffer ||
                slot.Kind == lodestone::BindingKind::Texture ||
                slot.Kind == lodestone::BindingKind::StorageTexture ||
                slot.Kind == lodestone::BindingKind::Sampler);
    }

    // Every GPU use of an image row, and the transfer that its CpuAccess needs.
    ImageUsageBits ImageUsageOf(const CompiledData& data, int32_t row, CpuAccess cpu)
    {
        constexpr rhi::Access k_Storage = rhi::Access::ShaderStorageRead | rhi::Access::ShaderStorageWrite;
        ImageUsageBits usage = ImageUsageBits::Invalid;

        for (const PassUse& use : data.Uses)
        {
            if (use.Resource == row)
            {
                const bool sampled = (use.Scope.Accesses & rhi::Access::ShaderSampledRead) != rhi::Access::None;
                const bool storage = (use.Scope.Accesses & k_Storage) != rhi::Access::None;

                for (const StageImageUsage& stage : k_StageImageUsages)
                {
                    if ((use.Scope.Stages & stage.Stage) != rhi::PipelineStage::None)
                    {
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

        // OnNoHistory::Clear writes zeroes into the copy with a queue write, which is a copy destination
        const ImageDeclaration& declaration = data.Frozen.Images[data.Frozen.Resources[row].DeclarationIndex].Declaration;

        if (declaration.HistoryDepth > 0 && declaration.NoHistoryAction == OnNoHistory::Clear)
        {
            usage |= ImageUsageBits::TransferDestination;
        }

        return usage;
    }

    ImageType ImageTypeOf(lodestone::TextureDimension dimension)
    {
        if (dimension == lodestone::TextureDimension::Texture1D)
        {
            return ImageType::Type1D;
        }

        if (dimension == lodestone::TextureDimension::Texture3D)
        {
            return ImageType::Type3D;
        }

        return ImageType::Type2D;
    }

    int32_t LayersOf(const CompiledData& data, int32_t row)
    {
        const FrozenResource& frozen = data.Frozen.Resources[row];

        if (frozen.Origin == ResourceOrigin::Declared)
        {
            return std::max(data.Frozen.Images[frozen.DeclarationIndex].Declaration.Layers, 1);
        }

        return std::max(data.Frozen.Imports[frozen.DeclarationIndex].Expectation.Layers, 1);
    }

    std::optional<ImageFormat> FormatOfRow(const CompiledData& data, int32_t row)
    {
        const FrozenResource& frozen = data.Frozen.Resources[row];

        if (frozen.Origin == ResourceOrigin::Declared)
        {
            return ToImageFormat(data.Frozen.Images[frozen.DeclarationIndex].Declaration.Format);
        }

        return ToImageFormat(data.Frozen.Imports[frozen.DeclarationIndex].Expectation.Format);
    }

    ImageViewType ViewTypeOf(const SizedResource& sized, int32_t layers)
    {
        const bool arrayed = sized.Arrayed || layers > 1;

        if (sized.Dimension == lodestone::TextureDimension::Texture1D)
        {
            return arrayed ? ImageViewType::Type1DArray : ImageViewType::Type1D;
        }

        if (sized.Dimension == lodestone::TextureDimension::Texture3D)
        {
            return ImageViewType::Type3D;
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

    // The resources this backend can create: buffers, images of a known extent, samplers. An image sized from a frame
    // target waits for a later step, and a cube for a request; a format with no ImageFormat or no WebGPU format cannot
    // be created.
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

        if (declaration.Extent.Kind == ExtentSource::Kind::Target)
        {
            return RealizeError::ResourceKindNotYet;
        }

        const std::optional<ImageFormat> format = ToImageFormat(declaration.Format);

        if (!format.has_value() || !ToWgpuTextureFormat(*format).has_value())
        {
            return RealizeError::ResourceKindNotYet;
        }

        const SizedResource& sized = data.Sized.Resources[row];
        const bool arrayed = (sized.Arrayed || std::max(declaration.Layers, 1) > 1) &&
                             sized.Dimension != lodestone::TextureDimension::Texture3D;

        // WebGPU has no 1D array and no 3D array texture
        if (!ToWgpuViewDimension(sized.Dimension, arrayed).has_value() &&
            sized.Dimension != lodestone::TextureDimension::Invalid)
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
            if (use.Resource == row)
            {
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
        }

        if (cpu == CpuAccess::Readback)
        {
            usage |= BufferUsageBits::TransferSource;
        }

        if (cpu == CpuAccess::Upload)
        {
            usage |= BufferUsageBits::TransferDestination;
        }

        // OnNoHistory::Clear clears the copy with a command encoder, which needs a copy destination
        const BufferDeclaration& declaration = data.Frozen.Buffers[data.Frozen.Resources[row].DeclarationIndex];

        if (declaration.HistoryDepth > 0 && declaration.NoHistoryAction == OnNoHistory::Clear)
        {
            usage |= BufferUsageBits::TransferDestination;
        }

        return usage;
    }

    // The ring phases a pass's groups cycle through: the least common multiple of the copy counts of the rows it binds.
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

    // The slots' highest group, and the constants block's group when the pipeline delivers constants in a uniform buffer
    int32_t GroupCountOf(const CompiledData& data, const CompiledPipeline& compiled)
    {
        const BoundPass& bound = data.BoundPasses[compiled.LayoutPass];
        int32_t groups = 0;

        for (int32_t slotIndex = bound.FirstSlot; slotIndex < bound.FirstSlot + bound.SlotCount; ++slotIndex)
        {
            groups = std::max(groups, static_cast<int32_t>(data.Slots[slotIndex].Group) + 1);
        }

        if (compiled.ConstantsDelivery == lodestone::EntryPointConstantsDelivery::UniformBuffer)
        {
            groups = std::max(groups, static_cast<int32_t>(compiled.ConstantsGroup) + 1);
        }

        return groups;
    }

    SlotTraits TraitsOf(const CompiledData& data, const BindingSlot& slot)
    {
        SlotTraits traits;
        const int32_t row = slot.Resource.Value;
        const FrozenResource& frozen = data.Frozen.Resources[row];

        if (frozen.Kind == ResourceKind::Image)
        {
            const SizedResource& sized = data.Sized.Resources[row];
            const std::optional<ImageFormat> format = FormatOfRow(data, row);
            const bool arrayed = (sized.Arrayed || LayersOf(data, row) > 1) &&
                                 sized.Dimension != lodestone::TextureDimension::Texture3D;

            if (format.has_value())
            {
                traits.StorageFormat = ToWgpuTextureFormat(*format).value_or(wgpu::TextureFormat::Undefined);
                traits.SampleType = ToWgpuSampleType(*format);
            }

            traits.ViewDimension = ToWgpuViewDimension(sized.Dimension, arrayed).value_or(wgpu::TextureViewDimension::e2D);
            return traits;
        }

        if (frozen.Kind == ResourceKind::Sampler)
        {
            const SamplerCreateInfo& info = data.Frozen.Samplers[frozen.DeclarationIndex].CreateInfo;
            const bool filtering = info.MagFilter == ImageFilteringMode::Linear ||
                                   info.MinFilter == ImageFilteringMode::Linear ||
                                   info.MipFilter == ImageFilteringMode::Linear;

            if (info.CompareEnabled)
            {
                traits.SamplerType = wgpu::SamplerBindingType::Comparison;
            }
            else
            {
                traits.SamplerType = filtering ? wgpu::SamplerBindingType::Filtering : wgpu::SamplerBindingType::NonFiltering;
            }
        }

        return traits;
    }

    // Bind gave the constants uniform buffer a slot of its own, with no resource row
    bool IsConstantsSlot(const CompiledPipeline& compiled, const BindingSlot& slot)
    {
        return compiled.ConstantsDelivery == lodestone::EntryPointConstantsDelivery::UniformBuffer &&
               slot.Kind == lodestone::BindingKind::UniformBuffer &&
               slot.Group == compiled.ConstantsGroup &&
               slot.Binding == compiled.ConstantsBinding;
    }

    void ExtractLayoutEntries(const CompiledPipeline& compiled,
                              const CompiledData& data,
                              const BoundPass& bound,
                              int32_t& group,
                              std::vector<wgpu::BindGroupLayoutEntry>& entries,
                              bool& constants_placed)
    {
        for (int32_t slotIndex = bound.FirstSlot; slotIndex < bound.FirstSlot + bound.SlotCount; ++slotIndex)
        {
            const BindingSlot& slot = data.Slots[slotIndex];

            if (std::cmp_equal(slot.Group, group))
            {
                SlotTraits traits;
                bool slotValid = true;

                if (slot.Resource.IsValid())
                {
                    traits = TraitsOf(data, slot);
                }
                else if (IsConstantsSlot(compiled, slot))
                {
                    constants_placed = true;
                }
                else
                {
                    slotValid = false;
                }

                if (slotValid)
                {
                    std::optional<wgpu::BindGroupLayoutEntry> entry =
                        ToBindGroupLayoutEntry(slot, data.SlotWritable[slotIndex] != 0u, traits);

                    if (entry.has_value())
                    {
                        entries.push_back(*entry);
                    }
                }
            }
        }
    }

    RealizeError CreatePipeline(GraphImpl& graph, const CompiledPipeline& compiled, PipelineRuntime& pipeline)
    {
        const CompiledData& data = graph.Compiled.Data();
        const BoundPass& bound = data.BoundPasses[compiled.LayoutPass];
        const int32_t groups = GroupCountOf(data, compiled);
        if (!std::cmp_less_equal(groups, k_MaxGroupLayouts))
        {
            return RealizeError::BindGroupMaxExceeded;
        }

        pipeline.NumGroupLayouts = static_cast<size_t>(groups);
        const bool uniformConstants = compiled.ConstantsDelivery == lodestone::EntryPointConstantsDelivery::UniformBuffer;

        for (int32_t group = 0; group < groups; ++group)
        {
            std::vector<wgpu::BindGroupLayoutEntry> entries;
            bool constantsPlaced = false;

            ExtractLayoutEntries(compiled, data, bound, group, entries, constantsPlaced);

            // the constants block always has an entry, even when Bind named no slot for it
            if (uniformConstants && !constantsPlaced && std::cmp_equal(compiled.ConstantsGroup, group))
            {
                wgpu::BindGroupLayoutEntry entry{};
                entry.binding = compiled.ConstantsBinding;
                entry.visibility = wgpu::ShaderStage::Compute;
                entry.buffer.type = wgpu::BufferBindingType::Uniform;
                entry.buffer.hasDynamicOffset = false;
                entries.push_back(entry);
            }

            wgpu::BindGroupLayoutDescriptor layoutInfo{};
            layoutInfo.entryCount = entries.size();
            layoutInfo.entries = entries.data();
            wgpu::BindGroupLayout groupLayout = graph.Device.CreateBindGroupLayout(&layoutInfo);

            if (!groupLayout)
            {
                return RealizeError::BindGroupLayoutFailed;
            }

            pipeline.GroupLayouts[group] = std::move(groupLayout);
        }

        wgpu::PipelineLayoutDescriptor pipelineLayoutInfo{};
        pipelineLayoutInfo.bindGroupLayoutCount = pipeline.NumGroupLayouts;
        pipelineLayoutInfo.bindGroupLayouts = pipeline.GroupLayouts.data();
        pipeline.Layout = graph.Device.CreatePipelineLayout(&pipelineLayoutInfo);

        if (!pipeline.Layout)
        {
            return RealizeError::PipelineLayoutFailed;
        }

        const auto isCompute = [](const CompiledStage& stage)
        {
            return stage.Stage == lodestone::ShaderStageKind::Compute;
        };

        const auto stage = std::ranges::find_if(compiled.Stages, isCompute);

        if (stage == compiled.Stages.end() || stage->ShaderCode.empty())
        {
            return RealizeError::ShaderCreateFailed;
        }

        wgpu::ShaderSourceWGSL source{};
        source.code = WgslSource(*stage);
        wgpu::ShaderModuleDescriptor moduleInfo{};
        moduleInfo.nextInChain = &source;
        pipeline.Module = graph.Device.CreateShaderModule(&moduleInfo);

        if (!pipeline.Module)
        {
            return RealizeError::ShaderCreateFailed;
        }

        wgpu::ComputePipelineDescriptor pipelineInfo{};
        pipelineInfo.layout = pipeline.Layout;
        pipelineInfo.compute.module = pipeline.Module;
        pipelineInfo.compute.entryPoint = std::string_view{ stage->EntryPoint };
        pipeline.Pipeline = graph.Device.CreateComputePipeline(&pipelineInfo);

        return pipeline.Pipeline ? RealizeError::Invalid : RealizeError::PipelineCreateFailed;
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
            }
            else
            {
                const BufferDeclaration& declaration = data.Frozen.Buffers[frozen.DeclarationIndex];

                BufferCreateInfo info{};
                info.Domain = ResourceDomain::GPUOnly;
                info.Usage = UsageOf(data, row, declaration.Cpu);
                info.Size = static_cast<size_t>(data.Sized.Resources[row].ByteSize);
                info.Name = name;
                graph.ExtraCopies.push_back(graph.Resources.CreateBuffer(info, {}));
            }
        }
    }

    uint64_t AlignUp(uint64_t value, uint64_t alignment)
    {
        return (value + alignment - 1u) / alignment * alignment;
    }

}

}
