#pragma once
#ifndef DIAMOND_DOGS_CC_INGEST_VALIDATION_HPP
#define DIAMOND_DOGS_CC_INGEST_VALIDATION_HPP

#include "ContentDiagnostic.hpp"
#include "ContentTypes.hpp"
#include "Ingest.hpp"
#include "KindRegistry.hpp"
#include "RecipeTable.hpp"

#include <cstddef>
#include <cstdint>
#include <span>
#include <vector>

namespace cc::detail
{
    inline constexpr int32_t k_NoNode = -1;

    // What phase 2 of Ingest reads. It is complete only when ValidateGraph returned no diagnostic.
    struct ValidatedGraph
    {
        // the kind of each node, in authored order
        std::vector<KindId> NodeKinds;
        // For each node and each slot of its kind, the authored index of the producer, or k_NoNode.
        std::vector<std::vector<int32_t>> SlotProducers;
        // Kahn's algorithm, ties broken by the lowest authored index
        std::vector<int32_t> TopologicalOrder;
    };

    // A node whose key equals the key of an earlier node while the canonical bytes differ.
    struct BatchCollision
    {
        size_t Node{ 0 };
        size_t Earlier{ 0 };
    };

    // Compares the recipes of one graph with each other. Equal key and equal bytes are one recipe and are fine.
    std::vector<BatchCollision> FindBatchCollisions(std::span<const PreparedRecipe> prepared);

    // Phase 1 of Ingest: finds every refusal in the graph and changes nothing else.
    std::vector<Diagnostic> ValidateGraph(const AuthoredGraph& graph, const KindRegistry& registry,
                                          ValidatedGraph& validated);
}

#endif // !DIAMOND_DOGS_CC_INGEST_VALIDATION_HPP
