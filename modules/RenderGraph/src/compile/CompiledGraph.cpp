#include "CompiledGraph.hpp"
#include "LoomKeys.hpp"
#include "compile/CompiledData.hpp"
#include <cassert>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <span>
#include <string_view>
#include <type_traits>
#include <utility>

namespace loom
{

namespace
{

    template<typename T>
    const T* StartLifetimeAsArray(const std::byte* data, size_t count) noexcept;

}

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

    int32_t CompiledGraph::RowCount() const noexcept
    {
        return data->RowCount;
    }

    int32_t CompiledGraph::PipelineCount() const noexcept
    {
        return static_cast<int32_t>(data->Pipelines.size());
    }

    int32_t CompiledGraph::PassIndexOf(PassKey pass) const noexcept
    {
        for (int32_t index = 0; index < PassCount(); ++index)
        {
            if (data->PassHashes[index] == pass.Hash)
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
            if (data->ResourceHashes[index] == resource.Hash)
            {
                return index;
            }
        }

        return -1;
    }

namespace
{

    template<typename T>
    const T* StartLifetimeAsArray(const std::byte* data, size_t count) noexcept
    {
        static_assert(std::is_trivially_copyable_v<T> && !std::is_pointer_v<T>,
                      "StartLifetimeAsArray requires trivially copyable non-pointer types");

#ifdef __cpp_lib_start_lifetime_as
        return std::start_lifetime_as_array<T>(data, count);
#else
        static_cast<void>(count);
        return reinterpret_cast<const T*>(data);
#endif
    }

}

    std::span<const uint32_t> SpirvWords(const CompiledStage& stage) noexcept
    {
        if (stage.ShaderCode.empty())
        {
            return {};
        }

        assert(stage.ShaderCode.size() % sizeof(uint32_t) == 0);
        const size_t wordCount = stage.ShaderCode.size() / sizeof(uint32_t);
        return std::span<const uint32_t>{ StartLifetimeAsArray<uint32_t>(stage.ShaderCode.data(), wordCount), wordCount };
    }

    std::string_view WgslSource(const CompiledStage& stage) noexcept
    {
        if (stage.ShaderCode.empty())
        {
            return {};
        }

        return std::string_view{ StartLifetimeAsArray<char>(stage.ShaderCode.data(), stage.ShaderCode.size()), stage.ShaderCode.size() };
    }

}
