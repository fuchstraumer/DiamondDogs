#pragma once
#ifndef DIAMOND_DOGS_LOOM_COMPILED_GRAPH_HPP
#define DIAMOND_DOGS_LOOM_COMPILED_GRAPH_HPP
#include "LoomKeys.hpp"
#include <cstdint>
#include <memory>

namespace loom
{
    struct CompiledData;

    // The output of the six Compile stages (Compile.hpp): plain values with no tie to the bundle or to a device (G4).
    // A backend's Realize consumes it. The tables are in src/compile/CompiledData.hpp, for the backends and the tests.
    class CompiledGraph
    {
    public:
        explicit CompiledGraph(std::unique_ptr<CompiledData> _data) noexcept;
        ~CompiledGraph();
        CompiledGraph(const CompiledGraph&) = delete;
        CompiledGraph& operator=(const CompiledGraph&) = delete;
        CompiledGraph(CompiledGraph&&) noexcept;
        CompiledGraph& operator=(CompiledGraph&&) noexcept;

        [[nodiscard]] const CompiledData& Data() const noexcept;
        [[nodiscard]] int32_t PassCount() const noexcept;
        [[nodiscard]] int32_t ResourceCount() const noexcept;
        // The resource rows and then one row per frame target: the size of every per-row span the runtime takes
        [[nodiscard]] int32_t RowCount() const noexcept;
        // the pipelines after Layout's merge: passes with equal descriptions share one
        [[nodiscard]] int32_t PipelineCount() const noexcept;
        // -1 when the graph has no such pass or resource
        [[nodiscard]] int32_t PassIndexOf(PassKey pass) const noexcept;
        [[nodiscard]] int32_t ResourceIndexOf(KeyRef resource) const noexcept;

        // The path Compile chose for a path list. The app supplies that path's imports, and only those: a key that
        // only another path declares has no row. An empty KeyRef when the graph has no such list.
        [[nodiscard]] KeyRef ChosenPath(PathListKey list) const noexcept;
        [[nodiscard]] bool IsChosen(PathListKey list, PathKey path) const noexcept;

    private:
        std::unique_ptr<CompiledData> data;
    };

}

#endif // !DIAMOND_DOGS_LOOM_COMPILED_GRAPH_HPP
