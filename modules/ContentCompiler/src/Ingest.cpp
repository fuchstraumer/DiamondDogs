#include "Ingest.hpp"
#include "ContentDiagnostic.hpp"
#include "ContentTypes.hpp"
#include "IngestValidation.hpp"
#include "KindRegistry.hpp"
#include "RecipeTable.hpp"
#include <cassert>
#include <cstddef>
#include <cstdint>
#include <expected>
#include <optional>
#include <span>
#include <utility>
#include <vector>

namespace
{
    std::vector<cc::PreparedRecipe> PrepareNodes(const cc::AuthoredGraph& graph, cc::Quality quality,
                                                 const cc::KindRegistry& registry,
                                                 const cc::detail::ValidatedGraph& validated);
    std::vector<cc::Diagnostic> FindCollisions(std::span<const cc::PreparedRecipe> prepared,
                                               const cc::RecipeTable& recipes);
    std::vector<cc::RecipeInputRef> GatherInputs(int32_t node, std::span<const cc::Slot> slots,
                                                 const cc::detail::ValidatedGraph& validated,
                                                 std::span<const cc::RecipeId> nodeRecipes);
}

namespace cc
{
    std::expected<IngestResult, std::vector<Diagnostic>> Ingest(const AuthoredGraph& graph, Quality quality,
                                                                const KindRegistry& registry,
                                                                RecipeTable& recipes)
    {
        assert(registry.IsFrozen() && "Freeze the registry before ingestion");

        detail::ValidatedGraph validated;
        std::vector<Diagnostic> diagnostics = detail::ValidateGraph(graph, registry, validated);

        if (!diagnostics.empty())
        {
            return std::unexpected(std::move(diagnostics));
        }

        // Keys depend on the keys of the inputs and not on ids, so every key exists before the table changes.
        std::vector<PreparedRecipe> prepared = PrepareNodes(graph, quality, registry, validated);
        std::vector<Diagnostic> collisions = FindCollisions(prepared, recipes);

        if (!collisions.empty())
        {
            return std::unexpected(std::move(collisions));
        }

        IngestResult result;
        result.NodeRecipes.assign(graph.Nodes.size(), RecipeId{});

        // topological order, so that the id of each input exists before its consumer is added
        for (const int32_t node : validated.TopologicalOrder)
        {
            const KindId kindId = validated.NodeKinds[node];
            const std::span<const Slot> slots = registry.Ops(kindId).Slots;

            Recipe recipe
            {
                .Key = prepared[node].Key,
                .Kind = kindId,
                .Level = quality,
                .Params = graph.Nodes[node].Params,
                .Inputs = GatherInputs(node, slots, validated, result.NodeRecipes),
            };

            // FindCollisions checked every key, so this cannot fail
            const std::expected<RecipeId, Diagnostic> interned =
                recipes.InternComputed(std::move(recipe), std::move(prepared[node].Bytes));
            assert(interned.has_value());

            result.NodeRecipes[node] = *interned;
        }

        return result;
    }
}

namespace
{
    std::vector<cc::PreparedRecipe> PrepareNodes(const cc::AuthoredGraph& graph, cc::Quality quality,
                                                 const cc::KindRegistry& registry,
                                                 const cc::detail::ValidatedGraph& validated)
    {
        std::vector<cc::PreparedRecipe> prepared(graph.Nodes.size());

        for (const int32_t node : validated.TopologicalOrder)
        {
            const cc::KindId kindId = validated.NodeKinds[node];
            const std::span<const cc::Slot> slots = registry.Ops(kindId).Slots;
            const std::vector<int32_t>& producers = validated.SlotProducers[node];
            const std::vector<int32_t>& outputIndices = validated.SlotOutputs[node];

            std::vector<foundation::content::RecipeInput> keyInputs;

            // slot order, and an optional slot with no edge is left out
            for (size_t slotIndex = 0; slotIndex < slots.size(); ++slotIndex)
            {
                const int32_t producer = producers[slotIndex];

                if (producer == cc::detail::k_NoNode)
                {
                    continue;
                }

                const cc::KindId producerKindId = validated.NodeKinds[producer];
                const std::span<const cc::Output> producerOutputs = registry.Ops(producerKindId).Outputs;
                const cc::Output& output = producerOutputs[outputIndices[slotIndex]];

                keyInputs.push_back(
                    foundation::content::RecipeInput{ slots[slotIndex].Name, prepared[producer].Key, output.Name });
            }

            prepared[node] = cc::RecipeTable::Prepare(registry, kindId, quality, graph.Nodes[node].Params, keyInputs);
        }

        return prepared;
    }

    std::vector<cc::Diagnostic> FindCollisions(std::span<const cc::PreparedRecipe> prepared,
                                               const cc::RecipeTable& recipes)
    {
        std::vector<cc::Diagnostic> diagnostics;

        for (size_t node = 0; node < prepared.size(); ++node)
        {
            const cc::PreparedRecipe& recipe = prepared[node];
            const std::expected<std::optional<cc::RecipeId>, cc::Diagnostic> existing =
                recipes.Lookup(recipe.Key, recipe.Bytes);

            if (existing.has_value())
            {
                continue;
            }

            cc::Diagnostic collision = existing.error();
            collision.Nodes = { static_cast<uint32_t>(node) };
            diagnostics.push_back(std::move(collision));
        }

        for (const cc::detail::BatchCollision& collision : cc::detail::FindBatchCollisions(prepared))
        {
            const std::string message = std::format("nodes {} and {} have one key and different canonical bytes",
                                                    collision.Earlier, collision.Node);

            diagnostics.push_back(cc::Diagnostic
            {
                .Level = cc::Severity::Error,
                .Code = cc::DiagnosticCode::KeyCollision,
                .Nodes = { static_cast<uint32_t>(collision.Node) },
                .Message = message,
            });
        }

        return diagnostics;
    }

    std::vector<cc::RecipeInputRef> GatherInputs(int32_t node, std::span<const cc::Slot> slots,
                                                 const cc::detail::ValidatedGraph& validated,
                                                 std::span<const cc::RecipeId> nodeRecipes)
    {
        const std::vector<int32_t>& producers = validated.SlotProducers[node];
        const std::vector<int32_t>& outputIndices = validated.SlotOutputs[node];
        std::vector<cc::RecipeInputRef> inputs;

        // slot order, and an optional slot with no edge is left out
        for (size_t slotIndex = 0; slotIndex < slots.size(); ++slotIndex)
        {
            const int32_t producer = producers[slotIndex];

            if (producer == cc::detail::k_NoNode)
            {
                continue;
            }

            inputs.push_back(
                cc::RecipeInputRef{ slots[slotIndex].Name, nodeRecipes[producer], outputIndices[slotIndex] });
        }

        return inputs;
    }
}
