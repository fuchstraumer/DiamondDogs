#pragma once
#ifndef DIAMOND_DOGS_LOOM_FREEZE_CONTEXT_HPP
#define DIAMOND_DOGS_LOOM_FREEZE_CONTEXT_HPP
#include "FrozenGraph.hpp"
#include "Declarations.hpp"
#include "LoomDiagnostic.hpp"
#include "LoomError.hpp"
#include "LoomKeys.hpp"
#include "Records.hpp"
#include <cstdint>
#include <source_location>
#include <string_view>
#include <unordered_map>

// What every Freeze step shares: the key classes, the context, and the flattened pass list. The report helpers are
// in Reports.hpp, and each step's functions in the header named for its source file.
namespace loom::freeze
{

    enum class KeyClass : uint8_t
    {
        Invalid = 0,
        Buffer = 1, // the three resource classes all index FrozenGraph::Resources. They are separate so
        Image = 2,  // that one name as a buffer and as an image is LM107, not LM102.
        Sampler = 3,
        FrameTarget = 4,
        Anchor = 5,
        Pass = 6,
        Sequence = 7
    };

    constexpr ResourceKind KeyClassToKind(KeyClass key_class) noexcept
    {
        switch (key_class)
        {
        case KeyClass::Buffer:
            return ResourceKind::Buffer;
        case KeyClass::Image:
            return ResourceKind::Image;
        case KeyClass::Sampler:
            return ResourceKind::Sampler;
        default:
            return ResourceKind::Invalid;
        }
    }

    constexpr KeyClass ClassOf(ResourceKind kind) noexcept
    {
        switch (kind)
        {
        case ResourceKind::Buffer:
            return KeyClass::Buffer;
        case ResourceKind::Image:
            return KeyClass::Image;
        case ResourceKind::Sampler:
            return KeyClass::Sampler;
        case ResourceKind::Invalid:
        default:
            return KeyClass::Invalid;
        }
    }

    struct KeyEntry
    {
        KeyClass Class{ KeyClass::Invalid };
        int32_t Index{ -1 };
        std::source_location Where;
    };

    struct FreezeContext
    {
        const GraphRecord& Record;
        DiagnosticSink& Sink;
        FrozenGraph Graph;
        std::unordered_map<uint64_t, KeyEntry> Keys;
    };

    // Where one pass came from in the record. Freeze flattens top-level and nested passes into one list.
    struct PassSource
    {
        const PassRecord* Pass{ nullptr };
        // The sequence's index in GraphRecord::Sequences. It equals its index in FrozenGraph::Sequences
        // unless PublishSequences skipped a duplicate, and that reported an error, so the graph is discarded
        // anyway.
        SequenceIndex Sequence;
        int32_t PositionInSequence{ -1 };
    };

}

#endif // !DIAMOND_DOGS_LOOM_FREEZE_CONTEXT_HPP
