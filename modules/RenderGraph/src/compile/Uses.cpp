#include "compile/Uses.hpp"
#include "bind/BoundGraph.hpp"
#include "Records.hpp"
#include "compile/CompiledData.hpp"
#include "FramePlan.hpp"
#include "PassStatements.hpp"
#include "ShaderLibraryTypes.hpp"
#include "freeze/FrozenGraph.hpp"
#include <algorithm>
#include <cstdint>
#include <vector>

// Each pass's use of each resource row, for readiness, Q3g and the barriers. Bound slots give the shader uses; an
// indirect dispatch or draw adds the read of its arguments. Draw targets and Record uses come with step 7.
namespace loom::compile
{

namespace
{

    SyncStage ToSyncStages(lodestone::ShaderStageKind stages)
    {
        const uint32_t bits = static_cast<uint32_t>(stages);
        SyncStage result = SyncStage::None;

        if ((bits & static_cast<uint32_t>(lodestone::ShaderStageKind::Vertex)) != 0u)
        {
            result |= SyncStage::Vertex;
        }

        if ((bits & static_cast<uint32_t>(lodestone::ShaderStageKind::Fragment)) != 0u)
        {
            result |= SyncStage::Fragment;
        }

        if ((bits & static_cast<uint32_t>(lodestone::ShaderStageKind::Compute)) != 0u)
        {
            result |= SyncStage::Compute;
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

    // The accesses of a bound slot, by binding kind. A sampler has none: it is a use for readiness only.
    SyncAccess AccessesOf(const BindingSlot& slot)
    {
        switch (slot.Kind)
        {
        case lodestone::BindingKind::UniformBuffer:
            [[fallthrough]];
        case lodestone::BindingKind::ParameterBlock:
            [[fallthrough]];
        case lodestone::BindingKind::InlineUniform:
            return SyncAccess::UniformRead;
        case lodestone::BindingKind::StorageBuffer:
            [[fallthrough]];
        case lodestone::BindingKind::TexelBuffer:
            [[fallthrough]];
        case lodestone::BindingKind::StorageTexture:
        {
            SyncAccess accesses = SyncAccess::None;

            if (Reads(slot.Access))
            {
                accesses |= SyncAccess::ShaderRead;
            }

            if (Writes(slot.Access))
            {
                accesses |= SyncAccess::ShaderWrite;
            }

            return accesses;
        }
        case lodestone::BindingKind::Texture:
            [[fallthrough]];
        case lodestone::BindingKind::CombinedTextureSampler:
            [[fallthrough]];
        case lodestone::BindingKind::InputRenderTarget:
            return SyncAccess::ShaderRead;
        case lodestone::BindingKind::Sampler:
            [[fallthrough]];
        default:
            break;
        }

        return SyncAccess::None;
    }

    void AddUse(std::vector<PassUse>& uses, int32_t first_use, const PassUse& use)
    {
        const auto sameRow = [&use](const PassUse& existing)
        {
            return existing.Resource == use.Resource;
        };

        const auto existing = std::find_if(uses.begin() + first_use, uses.end(), sameRow);

        if (existing == uses.end())
        {
            uses.push_back(use);
            return;
        }

        existing->Access = Combine(existing->Access, use.Access);
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
            .Scope = SyncScope{ .Stages = SyncStage::Indirect, .Accesses = SyncAccess::IndirectRead },
        };

        AddUse(uses, first_use, use);
    }

}

    void GatherUses(CompiledData& data)
    {
        const int32_t passCount = static_cast<int32_t>(data.Frozen.Passes.size());
        data.Passes.resize(passCount);

        for (int32_t passIndex = 0; passIndex < passCount; ++passIndex)
        {
            const FrozenPass& frozenPass = data.Frozen.Passes[passIndex];
            const BoundPass& boundPass = data.BoundPasses[passIndex];
            CompiledPass& pass = data.Passes[passIndex];
            pass.FirstUse = static_cast<int32_t>(data.Uses.size());

            for (int32_t slotIndex = boundPass.FirstSlot; slotIndex < boundPass.FirstSlot + boundPass.SlotCount; ++slotIndex)
            {
                const BindingSlot& slot = data.Slots[slotIndex];

                if (!slot.Resource.IsValid())
                {
                    continue;
                }

                const PassUse use
                {
                    .Resource = slot.Resource.Value,
                    .Access = slot.Access,
                    .Scope = SyncScope{ .Stages = ToSyncStages(slot.Stages), .Accesses = AccessesOf(slot) },
                };

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
                use.Initializes = use.Access == ResourceAccess::Write;
            }

            const auto previous = [](const FrozenBind& bind)
            {
                return bind.Previous;
            };

            pass.ReadsHistory = std::ranges::any_of(frozenPass.Binds, previous);
        }
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
