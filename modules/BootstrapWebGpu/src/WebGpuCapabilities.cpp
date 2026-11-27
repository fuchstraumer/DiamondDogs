#include "WebGpuCapabilities.hpp"
#include "DeviceCapability.hpp"
#include <webgpu/webgpu_cpp.h>
#include <algorithm>
#include <optional>
#include <span>
#include <string_view>

namespace
{
    struct NamedFeature
    {
        std::string_view Name;
        wgpu::FeatureName Feature;
    };

    // the WebGPU specification's names. MultiDrawIndirect is a Chromium experiment, behind the unsafe-WebGPU flag
    constexpr NamedFeature k_FeatureNames[]
    {
        { "depth-clip-control", wgpu::FeatureName::DepthClipControl },
        { "depth32float-stencil8", wgpu::FeatureName::Depth32FloatStencil8 },
        { "texture-compression-bc", wgpu::FeatureName::TextureCompressionBC },
        { "texture-compression-bc-sliced-3d", wgpu::FeatureName::TextureCompressionBCSliced3D },
        { "texture-compression-etc2", wgpu::FeatureName::TextureCompressionETC2 },
        { "texture-compression-astc", wgpu::FeatureName::TextureCompressionASTC },
        { "texture-compression-astc-sliced-3d", wgpu::FeatureName::TextureCompressionASTCSliced3D },
        { "timestamp-query", wgpu::FeatureName::TimestampQuery },
        { "indirect-first-instance", wgpu::FeatureName::IndirectFirstInstance },
        { "shader-f16", wgpu::FeatureName::ShaderF16 },
        { "rg11b10ufloat-renderable", wgpu::FeatureName::RG11B10UfloatRenderable },
        { "bgra8unorm-storage", wgpu::FeatureName::BGRA8UnormStorage },
        { "float32-filterable", wgpu::FeatureName::Float32Filterable },
        { "float32-blendable", wgpu::FeatureName::Float32Blendable },
        { "clip-distances", wgpu::FeatureName::ClipDistances },
        { "dual-source-blending", wgpu::FeatureName::DualSourceBlending },
        { "subgroups", wgpu::FeatureName::Subgroups },
        { "texture-formats-tier1", wgpu::FeatureName::TextureFormatsTier1 },
        { "texture-formats-tier2", wgpu::FeatureName::TextureFormatsTier2 },
        { "primitive-index", wgpu::FeatureName::PrimitiveIndex },
        { "texture-component-swizzle", wgpu::FeatureName::TextureComponentSwizzle },
        { "chromium-experimental-multi-draw-indirect", wgpu::FeatureName::MultiDrawIndirect },
    };

    // WGSL `enable` names, which is how a WGSL bundle lists its capabilities
    constexpr NamedFeature k_WgslEnables[]
    {
        { "f16", wgpu::FeatureName::ShaderF16 },
        { "subgroups", wgpu::FeatureName::Subgroups },
        { "clip_distances", wgpu::FeatureName::ClipDistances },
        { "dual_source_blending", wgpu::FeatureName::DualSourceBlending },
        { "primitive_index", wgpu::FeatureName::PrimitiveIndex },
    };

    constexpr wgpu::FeatureName k_DrawIndirectCount[]
    {
        wgpu::FeatureName::MultiDrawIndirect,
    };

    [[nodiscard]] std::optional<wgpu::FeatureName> FindFeature(std::span<const NamedFeature> table, std::string_view name) noexcept;
}

namespace bootstrap
{
    std::optional<wgpu::FeatureName> WebGpuFeatureNamed(std::string_view name) noexcept
    {
        return FindFeature(k_FeatureNames, name);
    }

    std::optional<wgpu::FeatureName> WebGpuFeatureOfWgslEnable(std::string_view enable_name) noexcept
    {
        return FindFeature(k_WgslEnables, enable_name);
    }

    std::span<const wgpu::FeatureName> WebGpuFeaturesOf(DeviceCapability capability) noexcept
    {
        switch (capability)
        {
        case DeviceCapability::DrawIndirectCount:
            return k_DrawIndirectCount;
        default:
            return {};
        }
    }

    std::string_view NameOf(wgpu::FeatureName feature) noexcept
    {
        const auto found = std::ranges::find(k_FeatureNames, feature, &NamedFeature::Feature);

        if (found == std::ranges::end(k_FeatureNames))
        {
            return "unknown";
        }

        return found->Name;
    }
}

namespace
{
    std::optional<wgpu::FeatureName> FindFeature(std::span<const NamedFeature> table, std::string_view name) noexcept
    {
        const auto found = std::ranges::find(table, name, &NamedFeature::Name);

        if (found == table.end())
        {
            return std::nullopt;
        }

        return found->Feature;
    }
}
