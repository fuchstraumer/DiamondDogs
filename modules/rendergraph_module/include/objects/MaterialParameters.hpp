#pragma once
#ifndef ASSET_PIPELINE_MATERIAL_PARAMETERS_HPP
#define ASSET_PIPELINE_MATERIAL_PARAMETERS_HPP

enum class shading_model : unsigned int {
    Invalid = 0,
    Lit,
    Subsurface,
    Cloth,
    Unlit,
};

struct MaterialParameters {
    float baseColor[4]{ 1.0f, 1.0f, 1.0f, 1.0f };
    float emissive[4]{ 0.0f, 0.0f, 0.0f, 0.0f };
    float roughness{ 1.0f };
    float metallic{ 0.0f };
    float reflectance{ 0.50f };
    float ambientOcclusion{ 0.0f };
    float clearCoat{ 1.0f };
    float clearCoatRoughness{ 0.0f };
    float anisotropy{ 0.0f };
    float anisotropyDirection[3]{ 1.0f, 0.0f, 0.0f };
    float thickness{ 0.50f };
    float subsurfacePower{ 12.234f };
    float subsurfaceColor[3]{ 1.0f, 1.0f, 1.0f };
    float sheenColor[3]{ 1.0f, 1.0f, 1.0f };
    float normal[3]{ 0.0f, 0.0f, 1.0f };
    shading_model model{ shading_model::Lit };
};

#endif //!ASSET_PIPELINE_MATERIAL_PARAMETERS_HPP
