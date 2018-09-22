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
    float subsurfaceColor[4]{ 1.0f, 1.0f, 1.0f, 0.0f };
    float sheenColor[4]{ 1.0f, 1.0f, 1.0f, 0.0f };
    float normal[4]{ 0.0f, 0.0f, 1.0f, 0.0f };
};

#endif //!ASSET_PIPELINE_MATERIAL_PARAMETERS_HPP
