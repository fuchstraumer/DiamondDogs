#include "RhiContextWebGpu.hpp"
#include "ShaderManifestDevice.hpp"
#include "RhiContext.hpp"
#include <algorithm>
#include <array>
#include <cstdint>
#include <memory>
#include <span>
#include <string_view>
#include <utility>
#include <webgpu/webgpu_cpp.h>

namespace rhi
{

namespace
{

struct FeatureEntry
{
    std::string_view Name;
    wgpu::FeatureName Feature;
};

const lodestone::manifest::DeviceLimits& EmptyLimits() noexcept;
uint32_t ConvertLimit(uint32_t limit_value) noexcept;
lodestone::manifest::DeviceLimits ReadDeviceLimits(const wgpu::Device& device);
std::span<const FeatureEntry> FeatureTable() noexcept;

} // namespace

struct RhiContextImpl
{
    RhiBackend Backend{ RhiBackend::WebGpu };
    WebGpuRhiContextView View;
    lodestone::manifest::DeviceLimits Limits;
};
RhiContext::RhiContext() noexcept = default;

RhiContext::RhiContext(std::unique_ptr<RhiContextImpl> _impl) noexcept :
  impl{ std::move(_impl) }
{}

RhiContext::~RhiContext() = default;

RhiContext::RhiContext(RhiContext&& other) noexcept = default;

RhiContext& RhiContext::operator=(RhiContext&& other) noexcept = default;

bool RhiContext::IsValid() const noexcept
{
    return impl != nullptr;
}

RhiBackend RhiContext::Backend() const noexcept
{
    if (impl == nullptr)
    {
        return RhiBackend::Invalid;
    }

    return impl->Backend;
}

const lodestone::manifest::DeviceLimits& RhiContext::Limits() const noexcept
{
    if (impl == nullptr)
    {
        return EmptyLimits();
    }

    return impl->Limits;
}

const RhiContextImpl* RhiContext::Impl() const noexcept
{
    return impl.get();
}
bool RhiContext::IsFeatureEnabled(std::string_view capability_name) const noexcept
{
    if (impl == nullptr)
    {
        return false;
    }

    std::span<const FeatureEntry> table = FeatureTable();
    const auto entry = std::ranges::lower_bound(table, capability_name, {}, &FeatureEntry::Name);

    if (entry == table.end())
    {
        return false;
    }

    if (entry->Name != capability_name)
    {
        return false;
    }

    return impl->View.Device.HasFeature(entry->Feature);
}

RhiContext MakeWebGpuRhiContext(const WebGpuRhiContextView& view)
{
    std::unique_ptr<RhiContextImpl> impl = std::make_unique<RhiContextImpl>();
    impl->View = view;
    impl->Limits = ReadDeviceLimits(view.Device);
    return RhiContext{ std::move(impl) };
}

WebGpuRhiContextView UnwrapWebGpuRhiContext(const RhiContext& context) noexcept
{
    const RhiContextImpl* impl = context.Impl();

    if (impl == nullptr)
    {
        return WebGpuRhiContextView{};
    }

    if (impl->Backend != RhiBackend::WebGpu)
    {
        return WebGpuRhiContextView{};
    }

    return impl->View;
}

namespace
{

const lodestone::manifest::DeviceLimits& EmptyLimits() noexcept
{
    static const lodestone::manifest::DeviceLimits emptyLimits{};
    return emptyLimits;
}

uint32_t ConvertLimit(uint32_t limit_value) noexcept
{
    if (limit_value == wgpu::kLimitU32Undefined)
    {
        return 0u;
    }

    return limit_value;
}

lodestone::manifest::DeviceLimits ReadDeviceLimits(const wgpu::Device& device)
{
    lodestone::manifest::DeviceLimits result;

    if (!device)
    {
        return result;
    }

    wgpu::Limits limits{};
    device.GetLimits(&limits);
    result.MaxWorkgroupMemoryBytes = ConvertLimit(limits.maxComputeWorkgroupStorageSize);
    result.MaxInvocationsPerWorkgroup = ConvertLimit(limits.maxComputeInvocationsPerWorkgroup);
    result.MaxWorkgroupSizeX = ConvertLimit(limits.maxComputeWorkgroupSizeX);
    result.MaxWorkgroupSizeY = ConvertLimit(limits.maxComputeWorkgroupSizeY);
    result.MaxWorkgroupSizeZ = ConvertLimit(limits.maxComputeWorkgroupSizeZ);
    result.MaxPushConstantBytes = ConvertLimit(limits.maxImmediateSize);
    return result;
}

std::span<const FeatureEntry> FeatureTable() noexcept
{
    static constexpr std::array<FeatureEntry, 29> k_FeatureTable
    {
        FeatureEntry{ "BGRA8UnormStorage", wgpu::FeatureName::BGRA8UnormStorage },
        FeatureEntry{ "ClipDistances", wgpu::FeatureName::ClipDistances },
        FeatureEntry{ "CoreFeaturesAndLimits", wgpu::FeatureName::CoreFeaturesAndLimits },
        FeatureEntry{ "Depth32FloatStencil8", wgpu::FeatureName::Depth32FloatStencil8 },
        FeatureEntry{ "DepthClipControl", wgpu::FeatureName::DepthClipControl },
        FeatureEntry{ "DualSourceBlending", wgpu::FeatureName::DualSourceBlending },
        FeatureEntry{ "Float32Blendable", wgpu::FeatureName::Float32Blendable },
        FeatureEntry{ "Float32Filterable", wgpu::FeatureName::Float32Filterable },
        FeatureEntry{ "IndirectFirstInstance", wgpu::FeatureName::IndirectFirstInstance },
        FeatureEntry{ "MultiDrawIndirect", wgpu::FeatureName::MultiDrawIndirect },
        FeatureEntry{ "PrimitiveIndex", wgpu::FeatureName::PrimitiveIndex },
        FeatureEntry{ "RG11B10UfloatRenderable", wgpu::FeatureName::RG11B10UfloatRenderable },
        FeatureEntry{ "ShaderF16", wgpu::FeatureName::ShaderF16 },
        FeatureEntry{ "SubgroupSizeControl", wgpu::FeatureName::SubgroupSizeControl },
        FeatureEntry{ "Subgroups", wgpu::FeatureName::Subgroups },
        FeatureEntry{ "TextureComponentSwizzle", wgpu::FeatureName::TextureComponentSwizzle },
        FeatureEntry{ "TextureCompressionASTC", wgpu::FeatureName::TextureCompressionASTC },
        FeatureEntry{ "TextureCompressionASTCSliced3D", wgpu::FeatureName::TextureCompressionASTCSliced3D },
        FeatureEntry{ "TextureCompressionBC", wgpu::FeatureName::TextureCompressionBC },
        FeatureEntry{ "TextureCompressionBCSliced3D", wgpu::FeatureName::TextureCompressionBCSliced3D },
        FeatureEntry{ "TextureCompressionETC2", wgpu::FeatureName::TextureCompressionETC2 },
        FeatureEntry{ "TextureFormatsTier1", wgpu::FeatureName::TextureFormatsTier1 },
        FeatureEntry{ "TextureFormatsTier2", wgpu::FeatureName::TextureFormatsTier2 },
        FeatureEntry{ "TimestampQuery", wgpu::FeatureName::TimestampQuery },
        FeatureEntry{ "Unorm16TextureFormats", wgpu::FeatureName::Unorm16TextureFormats },
        // the following are the feature names as they're reflected from shaders that use them, which ldoestone
        // exports to us. we may need to create a better split to map shader-reflected feature names to the
        // canonical WebGPU feature names in a future revision.
        FeatureEntry{ "clip_distances", wgpu::FeatureName::ClipDistances },
        FeatureEntry{ "dual_source_blending", wgpu::FeatureName::DualSourceBlending },
        FeatureEntry{ "f16", wgpu::FeatureName::ShaderF16 },
        FeatureEntry{ "subgroups", wgpu::FeatureName::Subgroups },
    };

    static_assert(std::ranges::is_sorted(k_FeatureTable, {}, &FeatureEntry::Name),
                  "FeatureTable must stay sorted by Name");

    return k_FeatureTable;
}

} // namespace

} // namespace rhi
