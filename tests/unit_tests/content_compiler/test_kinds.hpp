#pragma once
#ifndef DIAMOND_DOGS_CC_TEST_KINDS_HPP
#define DIAMOND_DOGS_CC_TEST_KINDS_HPP

#include "ContentDiagnostic.hpp"
#include "ContentTypes.hpp"
#include "ExecutorTypes.hpp"
#include "Ingest.hpp"
#include "KindRegistry.hpp"
#include "ParamsBox.hpp"
#include "async/Task.hpp"
#include "reflect/Describe.hpp"

#include <array>
#include <cstdint>
#include <string_view>
#include <tuple>
#include <vector>

namespace cctest
{
    enum class Shape : uint8_t
    {
        Invalid = 0,
        Flat,
        Ridged,
    };

    struct Inner
    {
        float Amplitude{ 1.0f };
        std::vector<float> Weights;
    };

    struct ConstParams
    {
        float Value{ 0.0f };
        uint32_t Seed{ 0 };
        Shape Mode{ Shape::Flat };
        Inner Nested;
    };

    struct ScaleParams
    {
        float Factor{ 1.0f };
    };

    struct BlendParams
    {
        float Mix{ 0.5f };
        bool Clamp{ false };
    };

    struct ImageParams
    {
        uint32_t Width{ 0 };
    };

    struct UnrelatedParams
    {
        int32_t Number{ 0 };
    };

    // no inputs, a CPU artifact
    struct ConstKind
    {
        static constexpr std::string_view Name{ "test.Const" };
        static constexpr uint32_t Version{ 1 };
        using Params = ConstParams;
        static constexpr std::array<cc::Slot, 0> Slots{};
        static constexpr std::array Outputs
        {
            cc::Output{ "Value", cc::ArtifactKind::CpuBytes },
        };

        static foundation::Task<cc::Result> Run(const Params& params, cc::Inputs inputs, cc::Context& context);
    };

    // one required input
    struct ScaleKind
    {
        static constexpr std::string_view Name{ "test.Scale" };
        static constexpr uint32_t Version{ 3 };
        using Params = ScaleParams;
        static constexpr std::array Slots
        {
            cc::Slot{ "Source", cc::SlotUse::Required, cc::ArtifactKind::CpuBytes },
        };
        static constexpr std::array Outputs
        {
            cc::Output{ "Value", cc::ArtifactKind::CpuBytes },
        };

        static foundation::Task<cc::Result> Run(const Params& params, cc::Inputs inputs, cc::Context& context);
    };

    // two required inputs and one optional input
    struct BlendKind
    {
        static constexpr std::string_view Name{ "test.Blend" };
        static constexpr uint32_t Version{ 2 };
        using Params = BlendParams;
        static constexpr std::array Slots
        {
            cc::Slot{ "A", cc::SlotUse::Required, cc::ArtifactKind::CpuBytes },
            cc::Slot{ "B", cc::SlotUse::Required, cc::ArtifactKind::CpuBytes },
            cc::Slot{ "Mask", cc::SlotUse::Optional, cc::ArtifactKind::CpuBytes },
        };
        static constexpr std::array Outputs
        {
            cc::Output{ "Value", cc::ArtifactKind::CpuBytes },
        };

        static foundation::Task<cc::Result> Run(const Params& params, cc::Inputs inputs, cc::Context& context);
    };

    // a GPU artifact with no inputs
    struct ImageKind
    {
        static constexpr std::string_view Name{ "test.Image" };
        static constexpr uint32_t Version{ 1 };
        using Params = ImageParams;
        static constexpr std::array<cc::Slot, 0> Slots{};
        static constexpr std::array Outputs
        {
            cc::Output{ "Image", cc::ArtifactKind::GpuImage },
        };

        static foundation::Task<cc::Result> Run(const Params& params, cc::Inputs inputs, cc::Context& context);
    };

    // takes a GPU image, so a CPU producer in front of it is a slot kind mismatch
    struct SharpenKind
    {
        static constexpr std::string_view Name{ "test.Sharpen" };
        static constexpr uint32_t Version{ 1 };
        using Params = ScaleParams;
        static constexpr std::array Slots
        {
            cc::Slot{ "Source", cc::SlotUse::Required, cc::ArtifactKind::GpuImage },
        };
        static constexpr std::array Outputs
        {
            cc::Output{ "Image", cc::ArtifactKind::GpuImage },
        };

        static foundation::Task<cc::Result> Run(const Params& params, cc::Inputs inputs, cc::Context& context);
    };

    // no inputs, three outputs: two GPU images and CPU bytes
    struct MultiKind
    {
        static constexpr std::string_view Name{ "test.Multi" };
        static constexpr uint32_t Version{ 1 };
        using Params = ImageParams;
        static constexpr std::array<cc::Slot, 0> Slots{};
        static constexpr std::array Outputs
        {
            cc::Output{ "Height", cc::ArtifactKind::GpuImage },
            cc::Output{ "Normal", cc::ArtifactKind::GpuImage },
            cc::Output{ "Stats", cc::ArtifactKind::CpuBytes },
        };

        static foundation::Task<cc::Result> Run(const Params& params, cc::Inputs inputs, cc::Context& context);
    };

    // MultiKind with its outputs in the opposite order: the name and the version are the same
    struct MultiReorderedKind
    {
        static constexpr std::string_view Name{ "test.Multi" };
        static constexpr uint32_t Version{ 1 };
        using Params = ImageParams;
        static constexpr std::array<cc::Slot, 0> Slots{};
        static constexpr std::array Outputs
        {
            cc::Output{ "Stats", cc::ArtifactKind::CpuBytes },
            cc::Output{ "Normal", cc::ArtifactKind::GpuImage },
            cc::Output{ "Height", cc::ArtifactKind::GpuImage },
        };

        static foundation::Task<cc::Result> Run(const Params& params, cc::Inputs inputs, cc::Context& context);
    };

    // Returns a registry with the six kinds above (not the reordered one), registered in that order and frozen.
    cc::KindRegistry MakeFrozenRegistry();

    // Returns a registry with MultiReorderedKind and SharpenKind, in that order, frozen.
    cc::KindRegistry MakeFrozenRegistryWithReorderedMulti();

    // Returns the authored index of the new node.
    uint32_t AddNode(cc::AuthoredGraph& graph, std::string_view kind_name, cc::ParamsBox params);
    void Connect(cc::AuthoredGraph& graph, uint32_t from_node, std::string_view from_output, uint32_t to_node,
                 std::string_view slot);

    std::vector<cc::DiagnosticCode> CodesOf(const std::vector<cc::Diagnostic>& diagnostics);
}

namespace foundation::reflect
{
    template<>
    struct Describe<cctest::Shape>
    {
        static constexpr std::string_view Name = "Shape";
        static constexpr std::array<EnumCase<cctest::Shape>, 3> Cases
        {
            EnumCase<cctest::Shape>{ "Invalid", cctest::Shape::Invalid },
            EnumCase<cctest::Shape>{ "Flat", cctest::Shape::Flat },
            EnumCase<cctest::Shape>{ "Ridged", cctest::Shape::Ridged },
        };
    };

    template<>
    struct Describe<cctest::Inner>
    {
        using T = cctest::Inner;

        static constexpr std::string_view Name = "Inner";
        static constexpr std::tuple Fields
        {
            Field<T, float>{ "Amplitude", &T::Amplitude },
            Field<T, std::vector<float>>{ "Weights", &T::Weights },
        };
    };

    template<>
    struct Describe<cctest::ConstParams>
    {
        using T = cctest::ConstParams;

        static constexpr std::string_view Name = "ConstParams";
        static constexpr std::tuple Fields
        {
            Field<T, float>{ "Value", &T::Value },
            Field<T, uint32_t>{ "Seed", &T::Seed },
            Field<T, cctest::Shape>{ "Mode", &T::Mode },
            Field<T, cctest::Inner>{ "Nested", &T::Nested },
        };
    };

    template<>
    struct Describe<cctest::ScaleParams>
    {
        using T = cctest::ScaleParams;

        static constexpr std::string_view Name = "ScaleParams";
        static constexpr std::tuple Fields
        {
            Field<T, float>{ "Factor", &T::Factor },
        };
    };

    template<>
    struct Describe<cctest::BlendParams>
    {
        using T = cctest::BlendParams;

        static constexpr std::string_view Name = "BlendParams";
        static constexpr std::tuple Fields
        {
            Field<T, float>{ "Mix", &T::Mix },
            Field<T, bool>{ "Clamp", &T::Clamp },
        };
    };

    template<>
    struct Describe<cctest::ImageParams>
    {
        using T = cctest::ImageParams;

        static constexpr std::string_view Name = "ImageParams";
        static constexpr std::tuple Fields
        {
            Field<T, uint32_t>{ "Width", &T::Width },
        };
    };

    template<>
    struct Describe<cctest::UnrelatedParams>
    {
        using T = cctest::UnrelatedParams;

        static constexpr std::string_view Name = "UnrelatedParams";
        static constexpr std::tuple Fields
        {
            Field<T, int32_t>{ "Number", &T::Number },
        };
    };
}

#endif // !DIAMOND_DOGS_CC_TEST_KINDS_HPP
