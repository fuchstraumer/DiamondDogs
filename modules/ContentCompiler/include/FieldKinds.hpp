#pragma once
#ifndef DIAMOND_DOGS_CC_FIELD_KINDS_HPP
#define DIAMOND_DOGS_CC_FIELD_KINDS_HPP
#include "ContentDiagnostic.hpp"
#include "ContentTypes.hpp"
#include "ExecutorTypes.hpp"
#include "async/Task.hpp"
#include "reflect/Describe.hpp"
#include <array>
#include <cstdint>
#include <expected>
#include <string>
#include <string_view>
#include <tuple>
#include <vector>

// CPU kinds on a 2D grid of float samples. They prove the chain from a graph file to a result with no GPU, and
// they are the CPU reference for the GPU field kinds that come later. The module README, section "Field kinds",
// gives the byte layout.
namespace cc
{
    class KindRegistry;
}

namespace cc::field
{
    // A field artifact holds Width and Height as uint32_t, then Width * Height float samples, row by row.
    struct FieldSamples
    {
        uint32_t Width{ 0 };
        uint32_t Height{ 0 };
        std::vector<float> Samples;
    };

    // The largest field that field.Const makes: 4096 x 4096 samples, 64 MiB.
    inline constexpr uint64_t k_MaxSampleCount{ 4096ull * 4096ull };

    // The name of the one output of each field kind.
    inline constexpr std::string_view k_FieldOutput{ "Field" };

    // Samples must hold Width * Height values.
    [[nodiscard]] Artifact MakeFieldArtifact(const FieldSamples& field);
    // Returns a message when the artifact does not hold a field.
    [[nodiscard]] std::expected<FieldSamples, std::string> ReadFieldArtifact(const Artifact& artifact);

    struct ConstParams
    {
        // 0 is not a size: a node that leaves out Width or Height fails when it runs
        uint32_t Width{ 0 };
        uint32_t Height{ 0 };
        float Value{ 0.0f };
    };

    struct ScaleBiasParams
    {
        float Scale{ 1.0f };
        float Bias{ 0.0f };
    };

    struct AddParams
    {
    };

    // no inputs; every sample is Value
    struct ConstKind
    {
        static constexpr std::string_view Name{ "field.Const" };
        static constexpr uint32_t Version{ 1 };
        using Params = ConstParams;
        static constexpr std::array<Slot, 0> Slots{};
        static constexpr std::array Outputs
        {
            Output{ k_FieldOutput, ArtifactKind::CpuBytes },
        };

        static foundation::Task<KindResult> Run(const Params& params, Inputs inputs, Context& context);
    };

    // each sample is Source * Scale + Bias
    struct ScaleBiasKind
    {
        static constexpr std::string_view Name{ "field.ScaleBias" };
        static constexpr uint32_t Version{ 1 };
        using Params = ScaleBiasParams;
        static constexpr std::array Slots
        {
            Slot{ "Source", SlotUse::Required, ArtifactKind::CpuBytes },
        };
        static constexpr std::array Outputs
        {
            Output{ k_FieldOutput, ArtifactKind::CpuBytes },
        };

        static foundation::Task<KindResult> Run(const Params& params, Inputs inputs, Context& context);
    };

    // each sample is Left + Right; the two fields must have the same size
    struct AddKind
    {
        static constexpr std::string_view Name{ "field.Add" };
        static constexpr uint32_t Version{ 1 };
        using Params = AddParams;
        static constexpr std::array Slots
        {
            Slot{ "Left", SlotUse::Required, ArtifactKind::CpuBytes },
            Slot{ "Right", SlotUse::Required, ArtifactKind::CpuBytes },
        };
        static constexpr std::array Outputs
        {
            Output{ k_FieldOutput, ArtifactKind::CpuBytes },
        };

        static foundation::Task<KindResult> Run(const Params& params, Inputs inputs, Context& context);
    };

    // Registers the three kinds. Returns a diagnostic for each kind that did not register, so empty is success.
    [[nodiscard]] std::vector<Diagnostic> RegisterFieldKinds(KindRegistry& registry);
}

namespace foundation::reflect
{
    template<>
    struct Describe<cc::field::ConstParams>
    {
        using T = cc::field::ConstParams;

        static constexpr std::string_view Name = "field.ConstParams";
        static constexpr std::tuple Fields
        {
            Field<T, uint32_t>{ "Width", &T::Width },
            Field<T, uint32_t>{ "Height", &T::Height },
            Field<T, float>{ "Value", &T::Value },
        };
    };

    template<>
    struct Describe<cc::field::ScaleBiasParams>
    {
        using T = cc::field::ScaleBiasParams;

        static constexpr std::string_view Name = "field.ScaleBiasParams";
        static constexpr std::tuple Fields
        {
            Field<T, float>{ "Scale", &T::Scale },
            Field<T, float>{ "Bias", &T::Bias },
        };
    };

    template<>
    struct Describe<cc::field::AddParams>
    {
        static constexpr std::string_view Name = "field.AddParams";
        static constexpr std::tuple Fields{};
    };
}

#endif // !DIAMOND_DOGS_CC_FIELD_KINDS_HPP
