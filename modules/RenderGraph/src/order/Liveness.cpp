#include "Records.hpp"
#include "order/OrderContext.hpp"
#include "Declarations.hpp"
#include "LoomDiagnostic.hpp"
#include "LoomError.hpp"
#include "freeze/FrozenGraph.hpp"
#include "order/OrderedGraph.hpp"
#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <utility>
#include <vector>

// Is the work used: dead writes (LM504) and orphan passes (LM505). Warnings: they never fail Order().

namespace loom::order
{

    bool HasSink(const OrderContext& context, ResourceSlot slot)
    {
        const int32_t resourceCount = static_cast<int32_t>(context.Frozen.Resources.size());

        if (slot.Value >= resourceCount)
        {
            // A frame target is a sink only when the graph declares it as an output: the present reads it
            const int32_t target = slot.Value - resourceCount;
            const auto isThisTarget = [target](const FrozenOutput& output) { return output.Target.Value == target; };
            return std::ranges::any_of(context.Frozen.Outputs, isThisTarget);
        }

        const FrozenResource& resource = context.Frozen.Resources[slot.Value];

        // Before the Kind switch: an import's DeclarationIndex points into Imports, not Images
        if (resource.Origin == ResourceOrigin::Imported)
        {
            return true;
        }

        const int32_t declIndex = resource.DeclarationIndex;

        switch (resource.Kind)
        {
        case ResourceKind::Buffer:
        {
            // History: next frame's BindPrevious reads it. Readback: the CPU reads it after the frame
            const BufferDeclaration& buffer = context.Frozen.Buffers[declIndex];
            return buffer.HistoryDepth > 0 || buffer.Cpu == CpuAccess::Readback;
        }
        case ResourceKind::Image:
        {
            const ImageDeclaration& image = context.Frozen.Images[declIndex].Declaration;
            return image.HistoryDepth > 0 || image.Cpu == CpuAccess::Readback;
        }
        case ResourceKind::Sampler:
            // No pass writes a sampler, so LM504 never asks
            return false;
        case ResourceKind::Invalid:
            std::unreachable();
        }

        return false;
    }

    void ReportDeadWrite(const OrderContext& context,
                            ResourceSlot slot,
                            const CollatedAccesses::AccessRef& writer,
                            const CollatedAccesses::AccessRef& next_writer)
    {
        Diagnostic diagnostic
        {
            .Code = DiagnosticCode::LM504,
            .Severity = DiagnosticSeverity::Warning,
            .Where = context.PassAccesses[writer.PassIdx][writer.AccessIdx].Where,
            .Pass = context.Frozen.Passes[writer.PassIdx].Key,
            .Resource = ResourceOfSlot(context, slot)
        };

        // The last writer has no next writer: its data reaches the end of the frame, and no sink reads it there
        if (next_writer.PassIdx != -1)
        {
            diagnostic.OtherWhere = context.PassAccesses[next_writer.PassIdx][next_writer.AccessIdx].Where;
            diagnostic.Actual = DiagnosticValue
            {
                .FieldName = "OverwrittenBy",
                .Kind = DiagnosticValueKind::Text,
                .Text = context.Frozen.Passes[next_writer.PassIdx].Key.Name
            };
        }

        context.Sink.Report(diagnostic);
    }

    Liveness CheckDeadWrites(OrderContext& context,
                                const CollatedAccesses& collated,
                                const std::vector<int32_t>& positions)
    {
        using AccessRef = CollatedAccesses::AccessRef;

        Liveness liveness
        {
            .Feeders = std::vector<std::vector<int32_t>>(context.PassCount),
            .Roots = std::vector<uint8_t>(context.PassCount, 0)
        };

        const auto positionOf = [&positions](const AccessRef& ref)
        {
            const NodeIndex node = NodeOfPass(PassIndex{ ref.PassIdx });
            return positions[node.Value];
        };

        // Reused across slots, so the loop allocates only when a slot has more accesses than any before it
        std::vector<AccessRef> writers;
        std::vector<AccessRef> readers;

        for (int32_t slot = 0; slot < context.SlotCount; ++slot)
        {
            if (collated.Writers[slot].empty())
            {
                continue;
            }

            // Every pair with a writer is ordered (LM502 is clean when this runs), so positions are the true order
            writers.assign(collated.Writers[slot].begin(), collated.Writers[slot].end());
            readers.assign(collated.Readers[slot].begin(), collated.Readers[slot].end());
            std::ranges::sort(writers, {}, positionOf);
            std::ranges::sort(readers, {}, positionOf);

            const bool hasSink = HasSink(context, ResourceSlot{ slot });
            const size_t writerCount = writers.size();
            size_t nextReader = 0;

            // Writer i's data lives strictly after writer i and up to writer i + 1. Each reader falls in
            // exactly one window, so one cursor walks the readers once for the whole slot.
            for (size_t i = 0; i < writerCount; ++i)
            {
                const AccessRef& writer = writers[i];
                const int32_t start = positionOf(writer);
                const bool isLast = (i + 1 == writerCount);
                const int32_t end = isLast ? std::numeric_limits<int32_t>::max() : positionOf(writers[i + 1]);

                // A reader at the writer's own position is a ReadWrite writer: it read the previous write
                while (nextReader < readers.size() && positionOf(readers[nextReader]) <= start)
                {
                    ++nextReader;
                }

                bool isLive = false;

                // Inclusive end: a ReadWrite next writer sits in the readers at `end`, and it reads this write
                while (nextReader < readers.size() && positionOf(readers[nextReader]) <= end)
                {
                    liveness.Feeders[readers[nextReader].PassIdx].push_back(writer.PassIdx);
                    isLive = true;
                    ++nextReader;
                }

                if (isLast && hasSink)
                {
                    liveness.Roots[writer.PassIdx] = 1;
                    isLive = true;
                }

                if (!isLive)
                {
                    static constexpr AccessRef k_NoWriter{ -1, -1 };
                    const AccessRef& nextWriter = isLast ? k_NoWriter : writers[i + 1];
                    ReportDeadWrite(context, ResourceSlot{ slot }, writer, nextWriter);
                }
            }
        }

        return liveness;
    }

    void CheckOrphans(OrderContext& context, const Liveness& liveness)
    {
        // Mark: every pass that a root reaches, walking from each reader back to the writers that feed it
        std::vector<uint8_t> marked = liveness.Roots;
        std::vector<int32_t> worklist;

        for (int32_t pass = 0; pass < context.PassCount; ++pass)
        {
            if (marked[pass] != 0)
            {
                worklist.push_back(pass);
            }
        }

        while (!worklist.empty())
        {
            const int32_t reader = worklist.back();
            worklist.pop_back();

            for (const int32_t writer : liveness.Feeders[reader])
            {
                if (marked[writer] == 0)
                {
                    marked[writer] = 1;
                    worklist.push_back(writer);
                }
            }
        }

        // Sweep: an unmarked pass does work that no output ever sees
        for (int32_t pass = 0; pass < context.PassCount; ++pass)
        {
            if (marked[pass] != 0)
            {
                continue;
            }

            const FrozenPass& frozenPass = context.Frozen.Passes[pass];
            const Diagnostic diagnostic
            {
                .Code = DiagnosticCode::LM505,
                .Severity = DiagnosticSeverity::Warning,
                .Where = frozenPass.Where,
                .Pass = frozenPass.Key
            };

            context.Sink.Report(diagnostic);
        }
    }

}
