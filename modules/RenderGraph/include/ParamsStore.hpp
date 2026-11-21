#pragma once
#ifndef DIAMOND_DOGS_LOOM_PARAMS_STORE_HPP
#define DIAMOND_DOGS_LOOM_PARAMS_STORE_HPP
#include "LoomKeys.hpp"
#include <cstddef>
#include <cstdint>
#include <source_location>
#include <span>
#include <vector>

// The values of the graph's params keys. Graph::Set copies a value in; Tick reads the bytes of each running
// pass. One slot per key, sized at Prepare, so Set and Tick allocate nothing. Recall that "Params" is just
// our name for the entrypoint resources attached to a single entry point, since Slang handles those uniquely
namespace loom
{
    class CompiledGraph;
    class DiagnosticSink;
    struct FramePlan;

    class ParamsStore
    {
    public:
        void Prepare(const CompiledGraph& compiled);

        // LM704 when the size or the alignment differs from the type that the key's Params statement used
        void Set(KeyRef key, std::span<const std::byte> bytes, uint32_t alignment, std::source_location where,
                 DiagnosticSink& sink);

        // `params` is an index into CompiledData::Params
        [[nodiscard]] bool IsSet(int32_t params) const noexcept;
        [[nodiscard]] std::span<const std::byte> Bytes(int32_t params) const noexcept;

        // LM703 for each pass in plan.Runs whose params key was never set. Returns false when it reported one.
        [[nodiscard]] bool CheckRuns(const CompiledGraph& compiled, const FramePlan& plan, DiagnosticSink& sink) const;

    private:
        struct Slot
        {
            KeyRef Key;
            uint32_t ByteSize{ 0u };
            uint32_t Alignment{ 0u };
            uint32_t Offset{ 0u };
            bool Set{ false };
        };

        std::vector<Slot> slots;
        std::vector<std::byte> storage;
    };

}

#endif // !DIAMOND_DOGS_LOOM_PARAMS_STORE_HPP
