#include "order/OrderedGraph.hpp"
#include "order/OrderContext.hpp"
#include "LoomDiagnostic.hpp"
#include "freeze/FrozenGraph.hpp"
#include <cstdint>
#include <optional>
#include <ranges>
#include <utility>
#include <vector>

// The Order stage's steps, in the order they run. Each step lives in the file named for the question it answers.
namespace loom
{

    std::optional<OrderedGraph> Order(const FrozenGraph& frozen,
                                      const BindAccessTable& binds,
                                      DiagnosticSink& sink)
    {
        using namespace order;
        const int32_t nodeCount = static_cast<int32_t>(frozen.Passes.size()) +
                                  static_cast<int32_t>(frozen.Anchors.size());
        const int32_t slotCount = static_cast<int32_t>(frozen.Resources.size()) +
                                  static_cast<int32_t>(frozen.FrameTargets.size());
        OrderContext context
        {
            .Frozen=frozen,
            .Binds=binds,
            .Sink=sink,
            .PassCount=static_cast<int32_t>(frozen.Passes.size()),
            .NodeCount=nodeCount,
            .SlotCount=slotCount,
        };
        context.PassAccesses.resize(context.PassCount, {});
        for (auto& vec : context.PassAccesses)
        {
            // should be overkill in most cases, but still should be of aid
            // (plus, it's a vector of vectors... repeated reallocs are easy to make happen)
            vec.reserve(16u);
        }

        context.Edges.reserve(context.PassCount * 4);

        AddAnchorEdges(context);
        AddFrameBoundEdges(context);
        AddSequenceEdges(context);
        GatherAccesses(context);
        // data indexed by pass -> data indexed by slot
        CollatedAccesses collated = CollateAccessesBySlot(context);
        AddDataFlowEdges(context, collated);
        BuildSuccessors(context);
        auto sortNodesResult = SortNodes(context);
        if (!sortNodesResult.has_value())
        {
            return std::nullopt;
        }
        else
        {
            const auto& orderedNodes = *sortNodesResult;
            const Reachability reachability = ComputeReachability(context, orderedNodes);

            const int32_t errorsBefore = context.Sink.ErrorCount();
            const std::vector<uint8_t> hazardSlots = CheckHazards(context, collated, reachability);
            const std::vector<int32_t> positions = PositionsOf(context, orderedNodes);
            CheckReadsBeforeWrites(context, collated, positions, hazardSlots);

            // if we found hazards, then we can't return a valid ordered graph
            if (context.Sink.ErrorCount() != errorsBefore)
            {
                return std::nullopt;
            }

            // The warnings run only on a graph with no errors: the author fixes those first, and LM504 needs every
            // pair with a writer to be ordered, which a clean LM502 proves
            const Liveness liveness = CheckDeadWrites(context, collated, positions);
            CheckOrphans(context, liveness);

            // copy each pass node (pass.Value < context.PassCount) into result passes
            OrderedGraph result;
            auto filterFn = [&](NodeIndex node) { return node.Value < context.PassCount; };
            auto toPassIdx = [&](NodeIndex node) { return PassIndex{ node.Value }; };
            // important: filter *then* transform, otherwise you pay a perf penalty
            result.Passes = orderedNodes |
                            std::views::filter(filterFn) |
                            std::views::transform(toPassIdx) |
                            std::ranges::to<std::vector>();
            result.Edges = std::move(context.Edges);
            return result;
        }
    }

}
