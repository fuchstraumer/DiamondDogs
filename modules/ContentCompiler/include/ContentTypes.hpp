#pragma once
#ifndef DIAMOND_DOGS_CC_CONTENT_TYPES_HPP
#define DIAMOND_DOGS_CC_CONTENT_TYPES_HPP

#include "reflect/Describe.hpp"

#include <array>
#include <compare>
#include <cstddef>
#include <cstdint>
#include <string_view>

namespace cc
{
    enum class Quality : uint8_t
    {
        Invalid = 0,
        Preview,
        Final,
    };

    enum class ArtifactKind : uint8_t
    {
        Invalid = 0,
        CpuBytes,
        GpuImage,
        GpuBuffer,
    };

    enum class SlotUse : uint8_t
    {
        Invalid = 0,
        Required,
        Optional,
    };

    struct KindId
    {
        uint32_t Value{ 0 };

        friend constexpr bool operator==(const KindId&, const KindId&) noexcept = default;
        friend constexpr auto operator<=>(const KindId&, const KindId&) noexcept = default;
    };

    struct RecipeId
    {
        uint32_t Value{ 0 };

        friend constexpr bool operator==(const RecipeId&, const RecipeId&) noexcept = default;
        friend constexpr auto operator<=>(const RecipeId&, const RecipeId&) noexcept = default;
    };

    struct RecipeIdHasher
    {
        size_t operator()(const RecipeId& id) const noexcept;
    };

    struct Slot
    {
        std::string_view Name;
        SlotUse Use{ SlotUse::Invalid };
        ArtifactKind Accepts{ ArtifactKind::Invalid };
    };
}

namespace foundation::reflect
{
    template<>
    struct Describe<cc::Quality>
    {
        static constexpr std::string_view Name = "cc.Quality";
        static constexpr std::array<EnumCase<cc::Quality>, 3> Cases
        {
            EnumCase<cc::Quality>{ "Invalid", cc::Quality::Invalid },
            EnumCase<cc::Quality>{ "Preview", cc::Quality::Preview },
            EnumCase<cc::Quality>{ "Final", cc::Quality::Final },
        };
    };
}

#endif // !DIAMOND_DOGS_CC_CONTENT_TYPES_HPP
