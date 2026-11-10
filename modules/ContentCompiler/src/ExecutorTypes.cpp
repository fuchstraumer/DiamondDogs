#include "ExecutorTypes.hpp"
#include "ArtifactTable.hpp"
#include "ContentDiagnostic.hpp"
#include <cassert>
#include <cstdint>
#include <span>
#include <utility>

namespace cc
{
    Inputs::Inputs(const ArtifactTable& _table, std::span<const ArtifactRef> _refs) noexcept
        : table{ &_table }, refs{ _refs }
    {
    }

    int32_t Inputs::Count() const noexcept
    {
        return static_cast<int32_t>(refs.size());
    }

    bool Inputs::Has(int32_t _slot) const noexcept
    {
        assert(_slot >= 0 && _slot < Count() && "the slot is not a slot of this kind");

        return refs[_slot].IsValid();
    }

    const Artifact& Inputs::Get(int32_t _slot) const
    {
        assert(Has(_slot) && "Get needs a slot with an artifact; check Has for an optional slot");

        return table->Get(refs[_slot]);
    }

    Context::Context(Quality _level, int32_t output_count) : level{ _level }, outputs(output_count)
    {
    }

    Quality Context::Level() const noexcept
    {
        return level;
    }

    void Context::SetOutput(int32_t _output, Artifact _artifact)
    {
        assert(_output >= 0 && _output < static_cast<int32_t>(outputs.size()) && "the output is not an output of this kind");
        assert(outputs[_output].Kind == ArtifactKind::Invalid && "each output is set once");
        assert(_artifact.Kind != ArtifactKind::Invalid && "an artifact needs a kind");

        outputs[_output] = std::move(_artifact);
    }

    void Context::Report(Diagnostic _diagnostic)
    {
        diagnostics.push_back(std::move(_diagnostic));
    }

    std::span<Artifact> Context::Outputs() noexcept
    {
        return outputs;
    }

    std::span<const Diagnostic> Context::Diagnostics() const noexcept
    {
        return diagnostics;
    }
}
