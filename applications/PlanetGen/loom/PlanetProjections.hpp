#pragma once
#include <array>
#include <cstdint>
#include <string_view>

namespace planet_loom
{
    enum class RenderProjection : uint8_t
    {
        Invalid = 0,
        Equirectangular,
        EqualEarth,
        Mollweide,
        Orthographic,
        PolarNorth,
        PolarSouth,
    };

    // The one table of map projections: the shader index (RenderMap.slang RenderMapConstants.Projection), the
    // command-line and tool name, a short alias, and the UI label. Keep it in shader-index order: the UI combo index
    // is the shader index. The regression script mirrors the names.
    struct ProjectionInfo
    {
        RenderProjection Projection;
        uint32_t ShaderIndex;
        std::string_view Name;
        std::string_view Alias;
        const char* Label;
    };

    inline constexpr std::array<ProjectionInfo, 6> k_Projections
    {
        ProjectionInfo{ RenderProjection::Equirectangular, 0u, "equirectangular", "equirect", "0: Equirectangular" },
        ProjectionInfo{ RenderProjection::EqualEarth, 1u, "equal_earth", "equal-earth", "1: Equal Earth" },
        ProjectionInfo{ RenderProjection::Mollweide, 2u, "mollweide", "mollweide", "2: Mollweide" },
        ProjectionInfo{ RenderProjection::Orthographic, 3u, "orthographic", "globe", "3: Orthographic (Globe)" },
        ProjectionInfo{ RenderProjection::PolarNorth, 4u, "polar_north", "north", "4: Polar North (Equal-Area)" },
        ProjectionInfo{ RenderProjection::PolarSouth, 5u, "polar_south", "south", "5: Polar South (Equal-Area)" },
    };

    [[nodiscard]] consteval bool ProjectionsInShaderOrder()
    {
        for (size_t index = 0u; index < k_Projections.size(); ++index)
        {
            if (k_Projections[index].ShaderIndex != index)
            {
                return false;
            }
        }

        return true;
    }

    static_assert(ProjectionsInShaderOrder(), "k_Projections must be in shader-index order");

    /** Returns the shader index of a projection; Invalid maps to equirectangular (0). */
    [[nodiscard]] constexpr uint32_t ProjectionToShaderIndex(RenderProjection projection) noexcept
    {
        for (const ProjectionInfo& info : k_Projections)
        {
            if (info.Projection == projection)
            {
                return info.ShaderIndex;
            }
        }

        return k_Projections[0].ShaderIndex;
    }

    /** Maps a shader index to RenderProjection, or RenderProjection::Invalid. */
    [[nodiscard]] constexpr RenderProjection ShaderIndexToProjection(int32_t index) noexcept
    {
        if (index < 0 || static_cast<size_t>(index) >= k_Projections.size())
        {
            return RenderProjection::Invalid;
        }

        return k_Projections[static_cast<size_t>(index)].Projection;
    }

    /** Maps a projection name or alias to RenderProjection, or RenderProjection::Invalid. */
    [[nodiscard]] constexpr RenderProjection ProjectionFromName(std::string_view text) noexcept
    {
        for (const ProjectionInfo& info : k_Projections)
        {
            if (text == info.Name || text == info.Alias)
            {
                return info.Projection;
            }
        }

        return RenderProjection::Invalid;
    }
}
