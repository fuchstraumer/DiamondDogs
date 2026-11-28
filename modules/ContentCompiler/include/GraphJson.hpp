#pragma once
#ifndef DIAMOND_DOGS_CC_GRAPH_JSON_HPP
#define DIAMOND_DOGS_CC_GRAPH_JSON_HPP
#include "ContentDiagnostic.hpp"
#include "Ingest.hpp"
#include <cstdint>
#include <expected>
#include <string>
#include <string_view>
#include <vector>

namespace cc
{
    class KindRegistry;

    inline constexpr std::string_view k_GraphFileSchema{ "cc.AuthoredGraph" };
    inline constexpr int64_t k_GraphFileVersion{ 1 };

    // An authored graph read from a file. NodeNames is in authored node order, so the node indices in a diagnostic
    // from Ingest give the names.
    struct GraphFile
    {
        AuthoredGraph Graph;
        std::vector<std::string> NodeNames;
    };

    // Reads a graph file and decodes the params of each node as the params type of its kind. It collects every
    // diagnostic, and a file with an error gives no graph. It checks the file only: Ingest checks the graph. The
    // module README, section "Graph files", shows the format.
    [[nodiscard]] std::expected<GraphFile, std::vector<Diagnostic>> ReadGraphJson(std::string_view json_text,
                                                                                const KindRegistry& registry);
}

#endif // !DIAMOND_DOGS_CC_GRAPH_JSON_HPP
