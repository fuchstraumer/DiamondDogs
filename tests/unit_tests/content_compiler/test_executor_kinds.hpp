#pragma once
#ifndef DIAMOND_DOGS_CC_TEST_EXECUTOR_KINDS_HPP
#define DIAMOND_DOGS_CC_TEST_EXECUTOR_KINDS_HPP

#include "ContentTypes.hpp"
#include "ExecutorTypes.hpp"
#include "KindRegistry.hpp"
#include "async/Task.hpp"
#include "reflect/Describe.hpp"

#include <array>
#include <cstdint>
#include <string_view>
#include <tuple>
#include <vector>

// The kinds that the executor tests run. Each kind counts its runs and appends the number that identifies its
// recipe to a log, so a test can see how often and in which order the executor ran them.
namespace cctest
{
    struct NumberParams
    {
        float Value{ 0.0f };
    };

    struct DeltaParams
    {
        float Delta{ 0.0f };
    };

    struct TagParams
    {
        int32_t Tag{ 0 };
    };

    struct RunCounts
    {
        int32_t Number{ 0 };
        int32_t Sum{ 0 };
        int32_t Offset{ 0 };
        int32_t Pair{ 0 };
        int32_t Fail{ 0 };
        int32_t Forget{ 0 };
        int32_t Slow{ 0 };
    };

    // no inputs, one CpuBytes output: its float param
    struct NumberKind
    {
        static constexpr std::string_view Name{ "exec.Number" };
        static constexpr uint32_t Version{ 1 };
        using Params = NumberParams;
        static constexpr std::array<cc::Slot, 0> Slots{};
        static constexpr std::array Outputs
        {
            cc::Output{ "Value", cc::ArtifactKind::CpuBytes },
        };

        static foundation::Task<cc::KindResult> Run(const Params& params, cc::Inputs inputs, cc::Context& context);
    };

    // two required inputs, one output: the sum
    struct SumKind
    {
        static constexpr std::string_view Name{ "exec.Sum" };
        static constexpr uint32_t Version{ 1 };
        using Params = TagParams;
        static constexpr std::array Slots
        {
            cc::Slot{ "Left", cc::SlotUse::Required, cc::ArtifactKind::CpuBytes },
            cc::Slot{ "Right", cc::SlotUse::Required, cc::ArtifactKind::CpuBytes },
        };
        static constexpr std::array Outputs
        {
            cc::Output{ "Value", cc::ArtifactKind::CpuBytes },
        };

        static foundation::Task<cc::KindResult> Run(const Params& params, cc::Inputs inputs, cc::Context& context);
    };

    // one required input, one output: the input plus the param
    struct OffsetKind
    {
        static constexpr std::string_view Name{ "exec.Offset" };
        static constexpr uint32_t Version{ 1 };
        using Params = DeltaParams;
        static constexpr std::array Slots
        {
            cc::Slot{ "Source", cc::SlotUse::Required, cc::ArtifactKind::CpuBytes },
        };
        static constexpr std::array Outputs
        {
            cc::Output{ "Value", cc::ArtifactKind::CpuBytes },
        };

        static foundation::Task<cc::KindResult> Run(const Params& params, cc::Inputs inputs, cc::Context& context);
    };

    // no inputs, two outputs: A is the tag, B is the tag plus one half
    struct PairKind
    {
        static constexpr std::string_view Name{ "exec.Pair" };
        static constexpr uint32_t Version{ 1 };
        using Params = TagParams;
        static constexpr std::array<cc::Slot, 0> Slots{};
        static constexpr std::array Outputs
        {
            cc::Output{ "A", cc::ArtifactKind::CpuBytes },
            cc::Output{ "B", cc::ArtifactKind::CpuBytes },
        };

        static foundation::Task<cc::KindResult> Run(const Params& params, cc::Inputs inputs, cc::Context& context);
    };

    // reports one diagnostic, and returns Failed
    struct FailKind
    {
        static constexpr std::string_view Name{ "exec.Fail" };
        static constexpr uint32_t Version{ 1 };
        using Params = TagParams;
        static constexpr std::array<cc::Slot, 0> Slots{};
        static constexpr std::array Outputs
        {
            cc::Output{ "Value", cc::ArtifactKind::CpuBytes },
        };

        static foundation::Task<cc::KindResult> Run(const Params& params, cc::Inputs inputs, cc::Context& context);
    };

    // returns Succeeded and sets no output
    struct ForgetKind
    {
        static constexpr std::string_view Name{ "exec.Forget" };
        static constexpr uint32_t Version{ 1 };
        using Params = TagParams;
        static constexpr std::array<cc::Slot, 0> Slots{};
        static constexpr std::array Outputs
        {
            cc::Output{ "Value", cc::ArtifactKind::CpuBytes },
        };

        static foundation::Task<cc::KindResult> Run(const Params& params, cc::Inputs inputs, cc::Context& context);
    };

    // OffsetKind that yields once before it writes, so it stays Running over one Tick
    struct SlowKind
    {
        static constexpr std::string_view Name{ "exec.Slow" };
        static constexpr uint32_t Version{ 1 };
        using Params = DeltaParams;
        static constexpr std::array Slots
        {
            cc::Slot{ "Source", cc::SlotUse::Required, cc::ArtifactKind::CpuBytes },
        };
        static constexpr std::array Outputs
        {
            cc::Output{ "Value", cc::ArtifactKind::CpuBytes },
        };

        static foundation::Task<cc::KindResult> Run(const Params& params, cc::Inputs inputs, cc::Context& context);
    };

    // Returns the run counters. The tests run on one thread.
    RunCounts& Counts();

    // Returns the identifying number of each run, in the order the runs started: the value of a Number, the delta
    // of an Offset or a Slow, the tag of a Sum, a Pair, a Fail or a Forget.
    std::vector<float>& RunLog();

    // Sets every counter to 0 and empties the log. Call it at the start of each test.
    void ResetRuns();

    // Returns a registry with the seven kinds above, registered in that order and frozen.
    cc::KindRegistry MakeFrozenExecutorRegistry();

    cc::Artifact MakeFloatArtifact(float value);
    float FloatOf(const cc::Artifact& artifact);
}

namespace foundation::reflect
{
    template<>
    struct Describe<cctest::NumberParams>
    {
        using T = cctest::NumberParams;

        static constexpr std::string_view Name = "NumberParams";
        static constexpr std::tuple Fields
        {
            Field<T, float>{ "Value", &T::Value },
        };
    };

    template<>
    struct Describe<cctest::DeltaParams>
    {
        using T = cctest::DeltaParams;

        static constexpr std::string_view Name = "DeltaParams";
        static constexpr std::tuple Fields
        {
            Field<T, float>{ "Delta", &T::Delta },
        };
    };

    template<>
    struct Describe<cctest::TagParams>
    {
        using T = cctest::TagParams;

        static constexpr std::string_view Name = "TagParams";
        static constexpr std::tuple Fields
        {
            Field<T, int32_t>{ "Tag", &T::Tag },
        };
    };
}

#endif // !DIAMOND_DOGS_CC_TEST_EXECUTOR_KINDS_HPP
