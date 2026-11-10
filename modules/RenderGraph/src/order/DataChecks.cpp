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
#include <source_location>
#include <tuple>
#include <utility>
#include <vector>

// Is the data correct: unordered accesses (LM502) and reads before any write (LM503). Errors: they fail Order().

namespace loom::order
{

    void RecordHazard(const OrderContext& context,
                        std::vector<Hazard>& hazards,
                        int32_t slot,
                        const CollatedAccesses::AccessRef& lhs,
                        const CollatedAccesses::AccessRef& rhs)
    {
        const bool inOrder = lhs.PassIdx < rhs.PassIdx;
        const CollatedAccesses::AccessRef& low = inOrder ? lhs : rhs;
        const CollatedAccesses::AccessRef& high = inOrder ? rhs : lhs;
        const std::source_location& lowWhere = context.PassAccesses[low.PassIdx][low.AccessIdx].Where;
        const std::source_location& highWhere = context.PassAccesses[high.PassIdx][high.AccessIdx].Where;
        hazards.push_back({ .LowPass=low.PassIdx, .HighPass=high.PassIdx, .Slot=slot, .LowWhere=lowWhere, .HighWhere=highWhere });
    }

    void ReportHazards(const OrderContext& context, const std::vector<Hazard>& hazards)
    {
        for (size_t i = 0; i < hazards.size(); ++i)
        {
            const Hazard& curr = hazards[i];
            const bool samePair = (i > 0) &&
                                    (curr.LowPass == hazards[i - 1].LowPass) &&
                                    (curr.HighPass == hazards[i - 1].HighPass);

            if (samePair && (hazards[i - 1].Slot == curr.Slot))
            {
                continue;
            }

            Diagnostic diagnostic
            {
                .Code = DiagnosticCode::LM502,
                .Severity = samePair ? DiagnosticSeverity::Note : DiagnosticSeverity::Error,
                .Where = curr.LowWhere,
                .OtherWhere = curr.HighWhere,
                .Pass = context.Frozen.Passes[curr.LowPass].Key,
                .Resource = ResourceOfSlot(context, ResourceSlot{ curr.Slot })
            };

            DiagnosticValue diagValue
            {
                .FieldName = "OtherPass",
                .Kind = DiagnosticValueKind::Text,
                .Text = context.Frozen.Passes[curr.HighPass].Key.Name
            };

            diagnostic.Actual = diagValue;

            context.Sink.Report(diagnostic);
        }
    }

    std::vector<uint8_t> CheckHazards(OrderContext& context, const CollatedAccesses& collated, const Reachability& reachability)
    {
        std::vector<Hazard> hazards;

        for (int32_t slot = 0; slot < context.SlotCount; ++slot)
        {
            const auto& writers = collated.Writers[slot];
            const auto& readers = collated.Readers[slot];

            // no writers: there's no way to have hazards if this slot is only read
            // one writer: `AddDataFlowEdges` would have already added the necessary
            // edges to prevent hazards, and this check runs after the sort. that means
            // we already know for a fact that the writer is ordered correctly
            if (writers.size() <= 1)
            {
                continue;
            }

            // write-write: ever pair of writer passes
            for (size_t first = 0; first < writers.size(); ++first)
            {
                const NodeIndex firstNode = NodeOfPass(PassIndex{ writers[first].PassIdx });
                for (size_t second = first + 1; second < writers.size(); ++second)
                {
                    const NodeIndex secondNode{ writers[second].PassIdx };
                    if (!Ordered(reachability, firstNode, secondNode))
                    {
                        RecordHazard(context, hazards, slot, writers[first], writers[second]);
                    }
                }
            }

            // write-read: every writer against every reader
            for (const auto writer : writers)
            {
                const NodeIndex writerNode{ writer.PassIdx };

                for (const auto reader : readers)
                {
                    const NodeIndex readerNode{ reader.PassIdx };
                    // If we are investigating a read-write resource,
                    // we need to check for self-reference
                    if (writerNode == readerNode)
                    {
                        continue;
                    }

                    if (!Ordered(reachability, writerNode, readerNode))
                    {
                        RecordHazard(context, hazards, slot, writer, reader);
                    }
                }
            }
        }

        const auto hazardKey = [](const Hazard& hazard)
        {
            return std::tuple{ hazard.LowPass, hazard.HighPass, hazard.Slot };
        };
        // using hazardKey extracts the three numeric components of the hazard that we can 
        // use to sort (and `{}` becomes default sort comparator, std::less<> by default)
        std::ranges::stable_sort(hazards, {}, hazardKey);
        ReportHazards(context, hazards);
        
        std::vector<uint8_t> hazardSlots(context.SlotCount, 0);
        for (const auto& hazard : hazards)
        {
            hazardSlots[hazard.Slot] = 1;
        }

        return hazardSlots;
    }

    void ReportReadBeforeWrite(const OrderContext& context,
                                const ResourceSlot slot,
                                const CollatedAccesses::AccessRef& first_reader,
                                const CollatedAccesses::AccessRef& first_writer)
    {
        const auto& passAccesses = context.PassAccesses[first_reader.PassIdx];
        const auto& where = passAccesses[first_reader.AccessIdx].Where;
        Diagnostic diag
        {
            .Code = DiagnosticCode::LM503,
            .Severity = DiagnosticSeverity::Error,
            .Where = where,
            .Pass = context.Frozen.Passes[first_reader.PassIdx].Key,
            .Resource = ResourceOfSlot(context, slot)
        };

        if (first_writer.PassIdx != -1)
        {
            const auto& writerAccesses = context.PassAccesses[first_writer.PassIdx];
            const auto& otherWhere = writerAccesses[first_writer.AccessIdx].Where;
            diag.OtherWhere = otherWhere;
            DiagnosticValue diagValue
            {
                .FieldName = "OtherPass",
                .Kind = DiagnosticValueKind::Text,
                .Text = context.Frozen.Passes[first_writer.PassIdx].Key.Name
            };
            diag.Actual = diagValue;
        }

        context.Sink.Report(diag);
    }

    void CheckReadsBeforeWrites(OrderContext& context,
                                const CollatedAccesses& collated,
                                const std::vector<int32_t>& positions,
                                const std::vector<uint8_t>& hazard_slots)
    {
        using AccessRef = CollatedAccesses::AccessRef;
        for (int32_t i = 0; i < context.SlotCount; ++i)
        {
            const auto& readers = collated.Readers[i];
            const bool hasSource = HasSource(context, ResourceSlot{i});
            const bool hasHazard = hazard_slots[i] != 0;
            // if nothing reads this, if it has data sources outside the graph, or if we already
            // marked that it has a hazard - skip it
            if (readers.empty() || hasSource || hasHazard)
            {
                continue;
            }

            const auto positionOf = [&positions](const AccessRef& ref)
            {
                const NodeIndex node = NodeOfPass(PassIndex{ref.PassIdx});
                return positions[node.Value];
            };

            const AccessRef firstReader = std::ranges::min(readers, {}, positionOf);

            const auto& writers = collated.Writers[i];

            // We have no writers, but we also already know we don't have any input data
            // sources for this slot - that's a problem!
            if (writers.empty())
            {
                static constexpr AccessRef k_InvalidWriter{ -1, -1 };
                ReportReadBeforeWrite(context, ResourceSlot{i}, firstReader, k_InvalidWriter);
                continue;
            }

            const AccessRef firstWriter = std::ranges::min(writers, {}, positionOf);

            if (positionOf(firstReader) <= positionOf(firstWriter))
            {
                ReportReadBeforeWrite(context, ResourceSlot{i}, firstReader, firstWriter);
            }

        }
    }

    bool HasSource(const OrderContext& context, ResourceSlot slot)
    {
        const int32_t resourceCount = static_cast<int32_t>(context.Frozen.Resources.size());
        if (slot.Value >= resourceCount)
        {
            // Slot is a frame target, meaning it does not have an external source
            return false;
        }

        const FrozenResource& resource = context.Frozen.Resources[slot.Value];
        if (resource.Origin == ResourceOrigin::Imported)
        {
            // Imported resources have an external source
            return true;
        }

        auto bufferHasSrc = [&](const int32_t decl_index) -> bool
        {
            const BufferDeclaration& bufferDecl = context.Frozen.Buffers[decl_index];
            return bufferDecl.Cpu == CpuAccess::Upload;
        };

        auto imageHasSrc = [&](const int32_t decl_index) -> bool
        {
            const FrozenImage& image = context.Frozen.Images[decl_index];
            return image.Declaration.Cpu == CpuAccess::Upload;
        };

        const int32_t declIndex = resource.DeclarationIndex;
        switch (resource.Kind)
        {
        case ResourceKind::Buffer:
            return bufferHasSrc(declIndex);
        case ResourceKind::Image:
            return imageHasSrc(declIndex);
        case ResourceKind::Sampler:
            // samplers have a source by virtue of not having real "contents" that create hazards
            return true;
        case ResourceKind::Invalid:
            std::unreachable(); // resources have all had their kind set by now
        }

        // Otherwise, the resource does not have an external source
        // (initial data fields don't yet exist)
        return false;
    }

}


