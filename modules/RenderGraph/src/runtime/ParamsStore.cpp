#include "ParamsStore.hpp"
#include "CompiledGraph.hpp"
#include "FramePlan.hpp"
#include "LoomDiagnostic.hpp"
#include "LoomError.hpp"
#include "LoomKeys.hpp"
#include "compile/CompiledData.hpp"
#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <source_location>
#include <span>
#include <string_view>

namespace loom
{

namespace
{

    // Each slot starts at a multiple of 16, so any params type sits aligned in the store
    constexpr uint32_t k_SlotAlignment = 16u;

    DiagnosticValue IntegerValue(std::string_view field_name, int64_t value)
    {
        return DiagnosticValue
        {
            .FieldName = field_name,
            .Kind = DiagnosticValueKind::Integer,
            .Integer = value,
        };
    }

}

    void ParamsStore::Prepare(const CompiledGraph& compiled)
    {
        const CompiledData& data = compiled.Data();
        slots.clear();
        slots.reserve(data.Params.size());
        uint32_t offset = 0u;

        for (const CompiledParams& params : data.Params)
        {
            slots.push_back(Slot{ .Key = params.Key, .ByteSize = params.ByteSize, .Alignment = params.Alignment, .Offset = offset });
            offset += (params.ByteSize + k_SlotAlignment - 1u) / k_SlotAlignment * k_SlotAlignment;
        }

        storage.assign(offset, std::byte{ 0 });
    }

    int32_t ParamsStore::Set(KeyRef key, std::span<const std::byte> bytes, uint32_t alignment, std::source_location where,
                             DiagnosticSink& sink)
    {
        const auto sameKey = [&key](const Slot& slot)
        {
            return slot.Key.Hash == key.Hash;
        };

        const auto slot = std::ranges::find_if(slots, sameKey);

        if (slot == slots.end())
        {
            return k_ParamsUnchanged;
        }

        const bool fits = bytes.size() == slot->ByteSize && alignment == slot->Alignment;

        if (!fits)
        {
            Diagnostic diag
            {
                .Code = DiagnosticCode::LM704,
                .Severity = DefaultSeverityOf(DiagnosticCode::LM704),
                .Where = where,
                .Resource = key,
                .Expected = IntegerValue("ByteSize", slot->ByteSize),
                .Actual = IntegerValue("ByteSize", static_cast<int64_t>(bytes.size())),
            };

            sink.Report(diag);
            return k_ParamsUnchanged;
        }

        // an app that sets the same values every frame must not rerun its WhenInputsChange passes
        std::byte* const stored = storage.data() + slot->Offset;
        const bool same = slot->Set && std::memcmp(stored, bytes.data(), bytes.size()) == 0;

        if (same)
        {
            return k_ParamsUnchanged;
        }

        std::memcpy(stored, bytes.data(), bytes.size());
        slot->Set = true;
        return static_cast<int32_t>(slot - slots.begin());
    }

    bool ParamsStore::IsSet(int32_t params) const noexcept
    {
        return slots[params].Set;
    }

    std::span<const std::byte> ParamsStore::Bytes(int32_t params) const noexcept
    {
        const Slot& slot = slots[params];
        return std::span<const std::byte>{ storage.data() + slot.Offset, slot.ByteSize };
    }

    bool ParamsStore::CheckRuns(const CompiledGraph& compiled, const FramePlan& plan, DiagnosticSink& sink) const
    {
        const CompiledData& data = compiled.Data();
        bool allSet = true;

        for (const int32_t pass : plan.Runs)
        {
            const int32_t params = data.Passes[pass].Params;

            if (params < 0 || slots[params].Set)
            {
                continue;
            }

            const FrozenPass& frozenPass = data.Frozen.Passes[pass];

            Diagnostic diag
            {
                .Code = DiagnosticCode::LM703,
                .Severity = DefaultSeverityOf(DiagnosticCode::LM703),
                .Where = frozenPass.Params.front().Where,
                .Pass = frozenPass.Key,
                .Resource = slots[params].Key,
            };

            sink.Report(diag);
            allSet = false;
        }

        return allSet;
    }

}
