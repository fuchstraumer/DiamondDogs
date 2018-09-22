#pragma once
#ifndef DIAMOND_DOGS_GRAPHICS_PIPELINE_STATE_HPP
#define DIAMOND_DOGS_GRAPHICS_PIPELINE_STATE_HPP
#include <vulkan/vulkan.h>
#include <set>
#include <cstdint>
#include <bitset>

namespace st {
    class Shader;
}

enum class MaterialModel : uint8_t {
    None = 0, // Unlit model. Does not require a material.
    Lit = 1, // Base lit model. Requires a material.
};

enum class BlendingMode : uint8_t {
    Opaque = 0, // ignore alpha channel of material
    Transparent = 1, // regular alpha blending. default. uses source_over rule.
    Fade = 2,
    Add = 3,
    Masked = 4
};

enum class VertexDomain : uint8_t {
    Object = 0,
    World = 1,
    View = 2,
    Device = 3
};

enum class TransparencyMode : uint8_t {
    Default = 0, // No modifications rendering transparent object
    TwoPassSingleSided = 1, // Rendered first to depth, then again to color. Renders only half object (part that passes depth)
    TwoPassDoubleSided = 2, // Rendered twice in color buffer, first with backfaces then front. Creates more accurate transparency.
};

/*
    struct encompassing on/off shading options
*/
struct ShadingToggles {
    constexpr ShadingToggles() noexcept;
    ~ShadingToggles() = default;
    constexpr bool ColorWrite() const noexcept;
    constexpr bool DepthWrite() const noexcept;
    constexpr bool DepthTest() const noexcept;
    constexpr bool DoubleSided() const noexcept;
    constexpr size_t Hash() const noexcept;
    void SetColorWrite(const bool& val) noexcept;
    void SetDepthWrite(const bool& val) noexcept;
    void SetDepthTest(const bool& val) noexcept;
    void SetDoubleSided(const bool& val) noexcept;
private:
    std::bitset<4> data;
};

struct VulkanPipelineState {

};


#endif //!DIAMOND_DOGS_GRAPHICS_PIPELINE_STATE_HPP
