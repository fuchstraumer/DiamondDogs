#include "CompiledGraph.hpp"
#include "compile/CompiledData.hpp"
#include <cstdint>
#include <memory>
#include <utility>

namespace loom
{

    CompiledGraph::CompiledGraph(std::unique_ptr<CompiledData> _data) noexcept : data{ std::move(_data) }
    {
    }

    CompiledGraph::~CompiledGraph() = default;
    CompiledGraph::CompiledGraph(CompiledGraph&&) noexcept = default;
    CompiledGraph& CompiledGraph::operator=(CompiledGraph&&) noexcept = default;

    const CompiledData& CompiledGraph::Data() const noexcept
    {
        return *data;
    }

    int32_t CompiledGraph::PassCount() const noexcept
    {
        return static_cast<int32_t>(data->Frozen.Passes.size());
    }

    int32_t CompiledGraph::ResourceCount() const noexcept
    {
        return static_cast<int32_t>(data->Frozen.Resources.size());
    }

    int32_t CompiledGraph::PassIndexOf(PassKey pass) const noexcept
    {
        for (int32_t index = 0; index < PassCount(); ++index)
        {
            if (data->Frozen.Passes[index].Key.Hash == pass.Hash)
            {
                return index;
            }
        }

        return -1;
    }

    int32_t CompiledGraph::ResourceIndexOf(KeyRef resource) const noexcept
    {
        for (int32_t index = 0; index < ResourceCount(); ++index)
        {
            if (data->Frozen.Resources[index].Key.Hash == resource.Hash)
            {
                return index;
            }
        }

        return -1;
    }

}
