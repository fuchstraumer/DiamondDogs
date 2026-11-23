#include "compile/Uses.hpp"
#include "Declarations.hpp"
#include "bind/BoundGraph.hpp"
#include "Records.hpp"
#include "compile/CompiledData.hpp"
#include "FramePlan.hpp"
#include "PassStatements.hpp"
#include "ShaderLibraryTypes.hpp"
#include "freeze/FrozenGraph.hpp"
#include <algorithm>
#include "RhiFlags.hpp"
#include <cstdint>
#include <utility>
#include <vector>

// Each pass's use of each resource row, for readiness, Q3g and the barriers. Bound slots give the shader uses; an
// indirect dispatch or draw adds the read of its arguments. Draw targets and Record uses come with step 7.
namespace loom::compile
{

namespace
{

    rhi::PipelineStage ToPipelineStages(lodestone::ShaderStageKind stages)
    {
        constexpr std::pair<lodestone::ShaderStageKind, rhi::PipelineStage> k_Table[]
        {
            { lodestone::ShaderStageKind::Vertex, rhi::PipelineStage::VertexShader },
            { lodestone::ShaderStageKind::Fragment, rhi::PipelineStage::FragmentShader },
            { lodestone::ShaderStageKind::Compute, rhi::PipelineStage::ComputeShader },
            { lodestone::ShaderStageKind::Task, rhi::PipelineStage::TaskShader },
            { lodestone::ShaderStageKind::Mesh, rhi::PipelineStage::MeshShader },
        };

        const uint32_t bits = static_cast<uint32_t>(stages);
        rhi::PipelineStage result = rhi::PipelineStage::None;

        for (const auto& [stage, pipelineStage] : k_Table)
        {
            if ((bits & static_cast<uint32_t>(stage)) != 0u)
            {
                result |= pipelineStage;
            }
        }

        return result;
    }

    bool Reads(ResourceAccess access)
    {
        return access == ResourceAccess::Read || access == ResourceAccess::ReadWrite;
    }

    bool Writes(ResourceAccess access)
    {
        return access == ResourceAccess::Write || access == ResourceAccess::ReadWrite;
    }

    ResourceAccess Combine(ResourceAccess lhs, ResourceAccess rhs)
    {
        if (lhs == ResourceAccess::Invalid)
        {
            return rhs;
        }

        const bool reads = Reads(lhs) || Reads(rhs);
        const bool writes = Writes(lhs) || Writes(rhs);

        if (reads && writes)
        {
            return ResourceAccess::ReadWrite;
        }

        return writes ? ResourceAccess::Write : ResourceAccess::Read;
    }

    // The accesses of a bound slot, by binding kind. Storage and sampled reads use the narrow synchronization2 bits,
    // so a barrier to one does not count as visibility to the other. A sampler has none: it is a use for readiness only.
    rhi::Access AccessesOf(const BindingSlot& slot)
    {
        switch (slot.Kind)
        {
        case lodestone::BindingKind::UniformBuffer:
            [[fallthrough]];
        case lodestone::BindingKind::ParameterBlock:
            [[fallthrough]];
        case lodestone::BindingKind::InlineUniform:
            return rhi::Access::UniformRead;
        case lodestone::BindingKind::TexelBuffer:
        {
            // a texel buffer that is only read binds as a uniform texel buffer, which is a sampled read
            if (!Writes(slot.Access))
            {
                return rhi::Access::ShaderSampledRead;
            }

            [[fallthrough]];
        }
        case lodestone::BindingKind::StorageBuffer:
            [[fallthrough]];
        case lodestone::BindingKind::StorageTexture:
        {
            rhi::Access accesses = rhi::Access::None;

            if (Reads(slot.Access))
            {
                accesses |= rhi::Access::ShaderStorageRead;
            }

            if (Writes(slot.Access))
            {
                accesses |= rhi::Access::ShaderStorageWrite;
            }

            return accesses;
        }
        case lodestone::BindingKind::Texture:
            [[fallthrough]];
        case lodestone::BindingKind::CombinedTextureSampler:
            return rhi::Access::ShaderSampledRead;
        case lodestone::BindingKind::InputRenderTarget:
            return rhi::Access::InputAttachmentRead;
        case lodestone::BindingKind::Sampler:
            [[fallthrough]];
        default:
            break;
        }

        return rhi::Access::None;
    }

    // The layout that a bound slot needs. Storage images are General; sampled reads are ShaderReadOnly.
    rhi::ImageLayout LayoutOf(const BindingSlot& slot)
    {
        switch (slot.Kind)
        {
        case lodestone::BindingKind::StorageTexture:
            return rhi::ImageLayout::General;
        case lodestone::BindingKind::Texture:
            [[fallthrough]];
        case lodestone::BindingKind::CombinedTextureSampler:
            [[fallthrough]];
        case lodestone::BindingKind::InputRenderTarget:
            return rhi::ImageLayout::ShaderReadOnly;
        default:
            break;
        }

        return rhi::ImageLayout::Undefined;
    }

    // Two layouts for one image at one time cannot both hold: General serves every shader access.
    rhi::ImageLayout CombineLayouts(rhi::ImageLayout lhs, rhi::ImageLayout rhs)
    {
        if (lhs == rhi::ImageLayout::Undefined || lhs == rhs)
        {
            return rhs;
        }

        if (rhs == rhi::ImageLayout::Undefined)
        {
            return lhs;
        }

        return rhi::ImageLayout::General;
    }

    void AddUse(std::vector<PassUse>& uses, int32_t first_use, const PassUse& use)
    {
        // a BindPrevious read and a write of one row touch two copies: two uses
        const auto sameRow = [&use](const PassUse& existing)
        {
            return existing.Resource == use.Resource && existing.Previous == use.Previous;
        };

        const auto existing = std::find_if(uses.begin() + first_use, uses.end(), sameRow);

        if (existing == uses.end())
        {
            uses.push_back(use);
            return;
        }

        existing->Access = Combine(existing->Access, use.Access);
        existing->Layout = CombineLayouts(existing->Layout, use.Layout);
        existing->Scope.Stages |= use.Scope.Stages;
        existing->Scope.Accesses |= use.Scope.Accesses;
    }

    void AddArgumentsUse(std::vector<PassUse>& uses, int32_t first_use, ResourceIndex arguments)
    {
        if (!arguments.IsValid())
        {
            return;
        }

        const PassUse use
        {
            .Resource = arguments.Value,
            .Access = ResourceAccess::Read,
            .Scope = SyncScope{ .Stages = rhi::PipelineStage::DrawIndirect, .Accesses = rhi::Access::IndirectCommandRead },
        };

        AddUse(uses, first_use, use);
    }

    int32_t HistoryDepthOf(const FrozenGraph& frozen, int32_t row)
    {
        const FrozenResource& resource = frozen.Resources[row];

        if (resource.Origin != ResourceOrigin::Declared)
        {
            return 0;
        }

        switch (resource.Kind)
        {
        case ResourceKind::Buffer:
            return frozen.Buffers[resource.DeclarationIndex].HistoryDepth;
        case ResourceKind::Image:
            return frozen.Images[resource.DeclarationIndex].Declaration.HistoryDepth;
        default:
            break;
        }

        return 0;
    }

    // One barrier batch goes before each level, so every use of an image in one level must see one layout. Uses that
    // disagree all take General.
    void ResolveLevelLayouts(CompiledData& data)
    {
        const int32_t passCount = static_cast<int32_t>(data.Passes.size());

        for (int32_t first = 0; first < passCount; ++first)
        {
            const CompiledPass& firstPass = data.Passes[first];

            for (int32_t firstUse = firstPass.FirstUse; firstUse < firstPass.FirstUse + firstPass.UseCount; ++firstUse)
            {
                PassUse& use = data.Uses[firstUse];

                if (use.Layout == rhi::ImageLayout::Undefined)
                {
                    continue;
                }

                for (int32_t second = first + 1; second < passCount; ++second)
                {
                    if (data.Ordered.Levels[second] != data.Ordered.Levels[first])
                    {
                        continue;
                    }

                    const CompiledPass& secondPass = data.Passes[second];

                    for (int32_t secondUse = secondPass.FirstUse; secondUse < secondPass.FirstUse + secondPass.UseCount; ++secondUse)
                    {
                        PassUse& other = data.Uses[secondUse];
                        const bool sameCopy = other.Resource == use.Resource && other.Previous == use.Previous;
                        const bool conflict = sameCopy && other.Layout != use.Layout;

                        if (conflict)
                        {
                            use.Layout = rhi::ImageLayout::General;
                            other.Layout = rhi::ImageLayout::General;
                        }
                    }
                }
            }
        }
    }

    // A descriptor names the layout of its pass's use of the row.
    void RecordSlotLayouts(CompiledData& data)
    {
        data.SlotLayouts.assign(data.Slots.size(), rhi::ImageLayout::Undefined);

        for (int32_t passIndex = 0; passIndex < static_cast<int32_t>(data.Passes.size()); ++passIndex)
        {
            const BoundPass& bound = data.BoundPasses[passIndex];
            const CompiledPass& pass = data.Passes[passIndex];

            for (int32_t slotIndex = bound.FirstSlot; slotIndex < bound.FirstSlot + bound.SlotCount; ++slotIndex)
            {
                const BindingSlot& slot = data.Slots[slotIndex];

                if (LayoutOf(slot) == rhi::ImageLayout::Undefined)
                {
                    continue;
                }

                for (int32_t useIndex = pass.FirstUse; useIndex < pass.FirstUse + pass.UseCount; ++useIndex)
                {
                    const PassUse& use = data.Uses[useIndex];

                    if (use.Resource == slot.Resource.Value && use.Previous == slot.Previous)
                    {
                        data.SlotLayouts[slotIndex] = use.Layout;
                    }
                }
            }
        }
    }

}

    void GatherUses(CompiledData& data)
    {
        const int32_t passCount = static_cast<int32_t>(data.Frozen.Passes.size());
        data.Passes.resize(passCount);
        data.PassHashes.resize(passCount);

        data.ResourceHashes.resize(data.Frozen.Resources.size());
        for (int32_t idx = 0; std::cmp_less(idx, data.ResourceHashes.size()); ++idx)
        {
            data.ResourceHashes[idx] = data.Frozen.Resources[idx].Key.Hash;
        }

        for (int32_t passIndex = 0; passIndex < passCount; ++passIndex)
        {
            const FrozenPass& frozenPass = data.Frozen.Passes[passIndex];
            const BoundPass& boundPass = data.BoundPasses[passIndex];
            data.PassHashes[passIndex] = frozenPass.Key.Hash;
            CompiledPass& pass = data.Passes[passIndex];
            pass.FirstUse = static_cast<int32_t>(data.Uses.size());

            for (int32_t slotIndex = boundPass.FirstSlot; slotIndex < boundPass.FirstSlot + boundPass.SlotCount; ++slotIndex)
            {
                const BindingSlot& slot = data.Slots[slotIndex];

                if (!slot.Resource.IsValid())
                {
                    continue;
                }

                PassUse use
                {
                    .Resource = slot.Resource.Value,
                    .Access = slot.Access,
                    .Scope = SyncScope{ .Stages = ToPipelineStages(slot.Stages), .Accesses = AccessesOf(slot) },
                    .Layout = LayoutOf(slot),
                    .Previous = slot.Previous,
                };

                // last frame's copy is only read: a write through it would change the history
                if (use.Previous)
                {
                    use.Access = ResourceAccess::Read;
                    use.Scope.Accesses = use.Scope.Accesses & ~(rhi::Access::ShaderStorageWrite | rhi::Access::ShaderWrite);
                }

                AddUse(data.Uses, pass.FirstUse, use);
            }

            if (frozenPass.Kind == PassKind::Dispatch && frozenPass.Dispatch.Form == DispatchForm::Indirect)
            {
                AddArgumentsUse(data.Uses, pass.FirstUse, frozenPass.Dispatch.Resource);
            }

            if (frozenPass.Kind == PassKind::Draw && frozenPass.Draw.Form == DrawForm::DrawIndirect)
            {
                AddArgumentsUse(data.Uses, pass.FirstUse, frozenPass.Draw.Arguments);
            }

            pass.UseCount = static_cast<int32_t>(data.Uses.size()) - pass.FirstUse;

            for (int32_t useIndex = pass.FirstUse; useIndex < pass.FirstUse + pass.UseCount; ++useIndex)
            {
                PassUse& use = data.Uses[useIndex];
                use.Initializes = use.Access == ResourceAccess::Write && !use.Previous;
            }

            const auto previous = [](const FrozenBind& bind)
            {
                return bind.Previous;
            };

            pass.ReadsHistory = std::ranges::any_of(frozenPass.Binds, previous);
            pass.Policy = frozenPass.Policy;

            if (pass.Policy == RunPolicy::WhenInputsChange)
            {
                data.HasWhenInputsChangesPasses = true;
            }
        }

        ResolveLevelLayouts(data);
        RecordSlotLayouts(data);
    }

    void GatherParams(CompiledData& data)
    {
        for (int32_t passIndex = 0; passIndex < static_cast<int32_t>(data.Frozen.Passes.size()); ++passIndex)
        {
            const FrozenPass& frozenPass = data.Frozen.Passes[passIndex];

            if (data.BoundPasses[passIndex].Params.ParamsIndex < 0)
            {
                continue;
            }

            // Freeze allows one Params statement per pass (LM113)
            const ParamsRecord& record = frozenPass.Params.front();

            const auto sameKey = [&record](const CompiledParams& params)
            {
                return params.Key.Hash == record.Params.Hash;
            };

            const auto existing = std::ranges::find_if(data.Params, sameKey);

            if (existing != data.Params.end())
            {
                data.Passes[passIndex].Params = static_cast<int32_t>(existing - data.Params.begin());
                continue;
            }

            data.Passes[passIndex].Params = static_cast<int32_t>(data.Params.size());
            data.Params.push_back(CompiledParams{ .Key = record.Params, .ByteSize = record.ByteSize, .Alignment = record.Alignment });
        }
    }

    void GatherCopies(CompiledData& data)
    {
        const int32_t rowCount = static_cast<int32_t>(data.Frozen.Resources.size());
        data.CopyCounts.assign(rowCount, 1);
        data.ExtraCopyBase.assign(rowCount, -1);
        data.CopyRows.resize(rowCount);

        for (int32_t row = 0; row < rowCount; ++row)
        {
            data.CopyRows[row] = row;
        }

        for (int32_t row = 0; row < rowCount; ++row)
        {
            const int32_t depth = HistoryDepthOf(data.Frozen, row);

            if (depth <= 0)
            {
                continue;
            }

            data.CopyCounts[row] = depth + 1;
            data.ExtraCopyBase[row] = static_cast<int32_t>(data.CopyRows.size());

            for (int32_t extra = 0; extra < depth; ++extra)
            {
                data.CopyRows.push_back(row);
            }
        }
    }

    void ComputeSpans(CompiledData& data)
    {
        data.Spans.assign(data.Frozen.Resources.size(), ResourceSpan{});

        for (int32_t passIndex = 0; passIndex < static_cast<int32_t>(data.Passes.size()); ++passIndex)
        {
            const CompiledPass& pass = data.Passes[passIndex];
            const int32_t level = data.Ordered.Levels[passIndex];

            for (int32_t useIndex = pass.FirstUse; useIndex < pass.FirstUse + pass.UseCount; ++useIndex)
            {
                ResourceSpan& span = data.Spans[data.Uses[useIndex].Resource];
                span.FirstLevel = (span.FirstLevel < 0) ? level : std::min(span.FirstLevel, level);
                span.LastLevel = std::max(span.LastLevel, level);
            }
        }
    }

}
