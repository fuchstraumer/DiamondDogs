#pragma once
#ifndef DIAMOND_DOGS_CC_INGEST_HPP
#define DIAMOND_DOGS_CC_INGEST_HPP
#include "ContentDiagnostic.hpp"
#include "ContentTypes.hpp"
#include "KindRegistry.hpp"
#include "ParamsBox.hpp"
#include "RecipeTable.hpp"
#include <cstdint>
#include <expected>
#include <string>
#include <vector>

namespace cc
{
    struct AuthoredNode
    {
        std::string KindName;
        ParamsBox Params;
    };

    struct AuthoredEdge
    {
        // producer, authored index
        uint32_t FromNode{ 0 };
        // consumer
        uint32_t ToNode{ 0 };
        std::string ToSlot;
    };

    struct AuthoredGraph
    {
        std::vector<AuthoredNode> Nodes;
        std::vector<AuthoredEdge> Edges;
    };

    struct IngestResult
    {
        // in authored node order
        std::vector<RecipeId> NodeRecipes;
    };

    // Validates the whole graph and collects every diagnostic. A graph with an error adds nothing to the
    // table. The registry must be frozen.
    std::expected<IngestResult, std::vector<Diagnostic>> Ingest(const AuthoredGraph& graph, Quality quality,
                                                                const KindRegistry& registry,
                                                                RecipeTable& recipes);
}

#endif // !DIAMOND_DOGS_CC_INGEST_HPP
