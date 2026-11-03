#pragma once
#ifndef DIAMOND_DOGS_FOAM_MATERIAL_HPP
#define DIAMOND_DOGS_FOAM_MATERIAL_HPP
#include "ResourceTypes.hpp"
#include <array>
#include <expected>
#include <filesystem>
#include <memory>
#include <string>
#include <string_view>

class ResourceContext;

namespace ocean
{
    /** @brief The packed foam textures (foam_texture_pack.py) on the GPU, with the albedo tint of the material.
     *  The surface texture is RGBA8: R the dissolve mask, G height, B roughness, A albedo luminance. The normal
     *  texture is RG8, the tangent-space normal x and y. Both have their full mip chain. */
    class FoamMaterial
    {
    public:
        /** @brief Reads <folder>/<name>_Surface.ktx, <name>_Normal.ktx and <name>_Material.json, and uploads both
         *  textures. Returns a message with the file path when a file is missing or does not match the output of
         *  the packer. The resource context must outlive the material */
        [[nodiscard]] static std::expected<std::unique_ptr<FoamMaterial>, std::string> Load(
            ResourceContext& resources,
            const std::filesystem::path& folder,
            std::string_view name);

        FoamMaterial(ResourceContext& _resources, const GraphicsResource& _surface, const GraphicsResource& _normal, const std::array<float, 3>& _tint) noexcept;
        ~FoamMaterial();
        FoamMaterial(const FoamMaterial&) = delete;
        FoamMaterial& operator=(const FoamMaterial&) = delete;

        [[nodiscard]] GraphicsResource Surface() const noexcept;
        [[nodiscard]] GraphicsResource Normal() const noexcept;
        /** @brief The linear rgb tint that multiplies the albedo luminance */
        [[nodiscard]] std::array<float, 3> Tint() const noexcept;

    private:
        ResourceContext* resources;
        GraphicsResource surface;
        GraphicsResource normal;
        std::array<float, 3> tint;
    };
}

#endif //!DIAMOND_DOGS_FOAM_MATERIAL_HPP
