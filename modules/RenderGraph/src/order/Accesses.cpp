#include "PassStatements.hpp"
#include "Records.hpp"
#include "order/OrderContext.hpp"
#include "freeze/FrozenGraph.hpp"
#include "order/OrderedGraph.hpp"
#include <cassert>
#include <cstdint>
#include <ranges>
#include <vector>

// Who touches what: one PassAccess per resource touch, then the same accesses collated by slot.
namespace loom::order
{

    void AppendBindingAccesses(const std::vector<ResourceAccess>& inputs,
                                std::vector<PassAccess>& results,
                                const FrozenPass& pass)
    {
        for (auto&& [bindIdx, binding] : std::views::enumerate(pass.Binds))
        {
            const ResourceSlot slot = SlotOfResource(binding.Resource);
            const ResourceAccess& access = inputs[bindIdx];
            PassAccess result
            {
                .Slot = slot,
                .Access = access,
                .Previous = binding.Previous,
                .Where = binding.Where
            };
            results.emplace_back(result);
        }
    }

    void AppendRecordAccesses(OrderContext& context,
                                std::vector<PassAccess>& results,
                                const FrozenPass& pass)
    {
        for (auto&& [recordIdx, record] : std::views::enumerate(pass.Uses))
        {
            ResourceSlot slot{};
            if (record.Target != TargetKind::FrameTarget)
            {
                slot = SlotOfResource(record.Resource);
            }
            else
            {
                slot = SlotOfFrameTarget(context, record.FrameTarget);
            }

            results.emplace_back(PassAccess{ .Slot = slot,
                                                .Access = record.Access,
                                                .Previous = false,
                                                .Where = record.Where });
        }
    }

    // A ThreadsOver dispatch names its image only for the extent, so the image is not an access.
    void AppendDispatchAccesses(std::vector<PassAccess>& results, const FrozenPass& pass)
    {
        if (pass.Dispatch.Form != DispatchForm::Indirect)
        {
            return;
        }

        results.push_back(PassAccess{ .Slot = SlotOfResource(pass.Dispatch.Resource),
                                        .Access = ResourceAccess::Read,
                                        .Previous = false,
                                        .Where = pass.Dispatch.Where });
    }

    // Keep reads the old contents, and so does any blend but Opaque. Clear and DontCare never read them.
    ResourceAccess ColorTargetAccess(const FrozenColorTarget& color)
    {
        const bool readsOld = color.Load.Kind == ColorLoad::Kind::Keep || color.Blend != BlendMode::Opaque;
        return readsOld ? ResourceAccess::ReadWrite : ResourceAccess::Write;
    }

    // Keep with writes off is a depth test only. A Clear writes the target, even with depth writes off.
    ResourceAccess DepthTargetAccess(const FrozenDepthTarget& depth)
    {
        const bool keeps = depth.Load.Kind == DepthLoad::Kind::Keep;
        const bool writes = depth.Write == DepthWrite::Enabled;

        if (keeps)
        {
            return writes ? ResourceAccess::ReadWrite : ResourceAccess::Read;
        }

        return ResourceAccess::Write;
    }

    void AppendDrawAccesses(const OrderContext& context, std::vector<PassAccess>& results, const FrozenPass& pass)
    {
        if (pass.Draw.Form == DrawForm::DrawIndirect)
        {
            results.push_back(PassAccess{ .Slot = SlotOfResource(pass.Draw.Arguments),
                                            .Access = ResourceAccess::Read,
                                            .Previous = false,
                                            .Where = pass.Draw.Where });
        }

        for (const FrozenVertexBuffer& vertex : pass.VertexBuffers)
        {
            results.push_back(PassAccess{ .Slot = SlotOfResource(vertex.Buffer),
                                            .Access = ResourceAccess::Read,
                                            .Previous = false,
                                            .Where = vertex.Where });
        }

        if (pass.Index.Buffer.IsValid())
        {
            results.push_back(PassAccess{ .Slot = SlotOfResource(pass.Index.Buffer),
                                            .Access = ResourceAccess::Read,
                                            .Previous = false,
                                            .Where = pass.Index.Where });
        }

        for (const FrozenColorTarget& color : pass.Colors)
        {
            const ResourceSlot slot = color.Kind == TargetKind::FrameTarget
                                            ? SlotOfFrameTarget(context, color.FrameTarget)
                                            : SlotOfResource(color.Image);

            results.push_back(PassAccess{ .Slot = slot,
                                            .Access = ColorTargetAccess(color),
                                            .Previous = false,
                                            .Where = color.Where });
        }

        if (pass.Depth.Image.IsValid())
        {
            results.push_back(PassAccess{ .Slot = SlotOfResource(pass.Depth.Image),
                                            .Access = DepthTargetAccess(pass.Depth),
                                            .Previous = false,
                                            .Where = pass.Depth.Where });
        }
    }

    // One PassAccess per resource touch, from every statement that can name a resource. The fields of the other
    // pass kinds keep their invalid values, so each Append adds nothing for a pass of another kind.
    void GatherAccesses(OrderContext& context)
    {
        for (int32_t i = 0; i < context.PassCount; ++i)
        {
            // the bind access information coming *in* to this pass/stage
            const std::vector<ResourceAccess>& inputs = context.Binds.Accesses[i];
            // Where we will write results
            std::vector<PassAccess>& results = context.PassAccesses[i];
            const FrozenPass& pass = context.Frozen.Passes[i];
            assert(inputs.size() == pass.Binds.size());

            AppendBindingAccesses(inputs, results, pass);
            AppendRecordAccesses(context, results, pass);
            AppendDispatchAccesses(results, pass);
            AppendDrawAccesses(context, results, pass);
        }
    }

    CollatedAccesses CollateAccessesBySlot(OrderContext& context)
    {
        using AccessRef = CollatedAccesses::AccessRef;
        CollatedAccesses collated;
        collated.Readers.resize(context.SlotCount);
        collated.Writers.resize(context.SlotCount);

        for (int32_t passIdx = 0; passIdx < context.PassCount; ++passIdx)
        {
            for (auto&& [accessIdx, access] : std::views::enumerate(context.PassAccesses[passIdx]))
            {
                const int32_t slotIdx = access.Slot.Value;
                std::vector<AccessRef>& readers = collated.Readers[slotIdx];
                std::vector<AccessRef>& writers = collated.Writers[slotIdx];

                if (access.Previous)
                {
                    continue;
                }

                auto pushReader = [&readers, passIdx, accessIdx] ()
                {
                    if (!readers.empty() && (readers.back().PassIdx == passIdx))
                    {
                        return;
                    }

                    // have to cast bc enumerate gives us ptrdiff_t
                    AccessRef readData
                    {
                        .PassIdx = passIdx,
                        .AccessIdx = static_cast<int32_t>(accessIdx)
                    };

                    readers.push_back(readData);
                };

                auto pushWriter = [&writers, passIdx, accessIdx] ()
                {
                    if (!writers.empty() && (writers.back().PassIdx == passIdx))
                    {
                        return;
                    }

                    AccessRef writeData
                    {
                        .PassIdx = passIdx,
                        .AccessIdx = static_cast<int32_t>(accessIdx)
                    };

                    writers.push_back(writeData);
                };

                switch (access.Access)
                {
                case ResourceAccess::Read:
                    pushReader();
                    break;
                case ResourceAccess::Write:
                    pushWriter();
                    break;
                case ResourceAccess::ReadWrite:
                    pushReader();
                    pushWriter();
                    break;
                case ResourceAccess::Invalid:
                    assert(false && "Invalid resource access");
                    break;
                }
            }
        }

        return collated;
    }

}


