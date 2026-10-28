#pragma once
#ifndef DIAMOND_DOGS_SEABED_MATERIAL_HPP
#define DIAMOND_DOGS_SEABED_MATERIAL_HPP

#include "ResourceTypes.hpp"
#include <cstdint>
#include <filesystem>
#include <optional>
#include <string_view>

class ResourceContext;

namespace rhi
{
    class Device;
}

namespace ocean
{
    /** @brief The packed seabed material textures (packed with texture_pack.py --mode ord) on the GPU, with a linear sampler.
     *  Albedo is RGBA8 (linear RGB albedo in RGB, 1.0 in A).
     *  Ord is RGBA8 (R=AmbientOcclusion, G=Roughness, B=Displacement, A=1.0).
     *  Normal is RG8 (tangent-space normal XY in [0, 1]).
     *  All textures include full mip chains. */
    struct SeabedMaterial
    {
        GraphicsResource AlbedoImage{};
        GraphicsResource AlbedoView{};
        GraphicsResource OrdImage{};
        GraphicsResource OrdView{};
        GraphicsResource NormalImage{};
        GraphicsResource NormalView{};
        GraphicsResource Sampler{};
        uint32_t Size{ 0u };

        void Destroy(ResourceContext& resources) noexcept;
    };

    /** @brief Loads <folder>/<name>_Albedo.ktx, <name>_Ord.ktx, <name>_Normal.ktx, creates the GPU textures and sampler.
     *  Returns std::nullopt when any file is missing, invalid, or GPU resource creation fails. */
    [[nodiscard]] std::optional<SeabedMaterial> LoadSeabedMaterial(
        const std::filesystem::path& folder,
        std::string_view name,
        ResourceContext& resources,
        rhi::Device& device);
}

#endif //!DIAMOND_DOGS_SEABED_MATERIAL_HPP
