#include "WebGpuMapping.hpp"
#include "ImageDataFormats.hpp"
#include "LoomDeviceLimits.hpp"
#include "LoomGraph.hpp"
#include "PassStatements.hpp"
#include "ResourceFlags.hpp"
#include "RhiContext.hpp"
#include "RhiContextWebGpu.hpp"
#include "ShaderLibraryTypes.hpp"
#include "ShaderManifestDevice.hpp"
#include "bind/BoundGraph.hpp"
#include <webgpu/webgpu_cpp.h>
#include <algorithm>
#include <cstdint>
#include <iterator>
#include <optional>

namespace loom
{

namespace
{

    struct FormatRow
    {
        ImageComponentFormats Component;
        ImageDataType Data;
        wgpu::TextureFormat Format;
        uint32_t BlockBytes;
    };

    constexpr FormatRow k_FormatRows[]
    {
        { ImageComponentFormats::R8, ImageDataType::UNorm, wgpu::TextureFormat::R8Unorm, 1u },
        { ImageComponentFormats::R8, ImageDataType::SNorm, wgpu::TextureFormat::R8Snorm, 1u },
        { ImageComponentFormats::R8, ImageDataType::UInt, wgpu::TextureFormat::R8Uint, 1u },
        { ImageComponentFormats::R8, ImageDataType::SInt, wgpu::TextureFormat::R8Sint, 1u },
        { ImageComponentFormats::R16, ImageDataType::UNorm, wgpu::TextureFormat::R16Unorm, 2u },
        { ImageComponentFormats::R16, ImageDataType::SNorm, wgpu::TextureFormat::R16Snorm, 2u },
        { ImageComponentFormats::R16, ImageDataType::UInt, wgpu::TextureFormat::R16Uint, 2u },
        { ImageComponentFormats::R16, ImageDataType::SInt, wgpu::TextureFormat::R16Sint, 2u },
        { ImageComponentFormats::R16, ImageDataType::Float, wgpu::TextureFormat::R16Float, 2u },
        { ImageComponentFormats::R32, ImageDataType::UInt, wgpu::TextureFormat::R32Uint, 4u },
        { ImageComponentFormats::R32, ImageDataType::SInt, wgpu::TextureFormat::R32Sint, 4u },
        { ImageComponentFormats::R32, ImageDataType::Float, wgpu::TextureFormat::R32Float, 4u },
        { ImageComponentFormats::RG8, ImageDataType::UNorm, wgpu::TextureFormat::RG8Unorm, 2u },
        { ImageComponentFormats::RG8, ImageDataType::SNorm, wgpu::TextureFormat::RG8Snorm, 2u },
        { ImageComponentFormats::RG8, ImageDataType::UInt, wgpu::TextureFormat::RG8Uint, 2u },
        { ImageComponentFormats::RG8, ImageDataType::SInt, wgpu::TextureFormat::RG8Sint, 2u },
        { ImageComponentFormats::RG16, ImageDataType::UNorm, wgpu::TextureFormat::RG16Unorm, 4u },
        { ImageComponentFormats::RG16, ImageDataType::SNorm, wgpu::TextureFormat::RG16Snorm, 4u },
        { ImageComponentFormats::RG16, ImageDataType::UInt, wgpu::TextureFormat::RG16Uint, 4u },
        { ImageComponentFormats::RG16, ImageDataType::SInt, wgpu::TextureFormat::RG16Sint, 4u },
        { ImageComponentFormats::RG16, ImageDataType::Float, wgpu::TextureFormat::RG16Float, 4u },
        { ImageComponentFormats::RG32, ImageDataType::UInt, wgpu::TextureFormat::RG32Uint, 8u },
        { ImageComponentFormats::RG32, ImageDataType::SInt, wgpu::TextureFormat::RG32Sint, 8u },
        { ImageComponentFormats::RG32, ImageDataType::Float, wgpu::TextureFormat::RG32Float, 8u },
        { ImageComponentFormats::RGBA8, ImageDataType::UNorm, wgpu::TextureFormat::RGBA8Unorm, 4u },
        { ImageComponentFormats::RGBA8, ImageDataType::sRGB, wgpu::TextureFormat::RGBA8UnormSrgb, 4u },
        { ImageComponentFormats::RGBA8, ImageDataType::SNorm, wgpu::TextureFormat::RGBA8Snorm, 4u },
        { ImageComponentFormats::RGBA8, ImageDataType::UInt, wgpu::TextureFormat::RGBA8Uint, 4u },
        { ImageComponentFormats::RGBA8, ImageDataType::SInt, wgpu::TextureFormat::RGBA8Sint, 4u },
        { ImageComponentFormats::BGRA8, ImageDataType::UNorm, wgpu::TextureFormat::BGRA8Unorm, 4u },
        { ImageComponentFormats::BGRA8, ImageDataType::sRGB, wgpu::TextureFormat::BGRA8UnormSrgb, 4u },
        { ImageComponentFormats::RGBA16, ImageDataType::UNorm, wgpu::TextureFormat::RGBA16Unorm, 8u },
        { ImageComponentFormats::RGBA16, ImageDataType::SNorm, wgpu::TextureFormat::RGBA16Snorm, 8u },
        { ImageComponentFormats::RGBA16, ImageDataType::UInt, wgpu::TextureFormat::RGBA16Uint, 8u },
        { ImageComponentFormats::RGBA16, ImageDataType::SInt, wgpu::TextureFormat::RGBA16Sint, 8u },
        { ImageComponentFormats::RGBA16, ImageDataType::Float, wgpu::TextureFormat::RGBA16Float, 8u },
        { ImageComponentFormats::RGBA32, ImageDataType::UInt, wgpu::TextureFormat::RGBA32Uint, 16u },
        { ImageComponentFormats::RGBA32, ImageDataType::SInt, wgpu::TextureFormat::RGBA32Sint, 16u },
        { ImageComponentFormats::RGBA32, ImageDataType::Float, wgpu::TextureFormat::RGBA32Float, 16u },
        { ImageComponentFormats::A2B10G10R10, ImageDataType::UNorm, wgpu::TextureFormat::RGB10A2Unorm, 4u },
        { ImageComponentFormats::A2B10G10R10, ImageDataType::UInt, wgpu::TextureFormat::RGB10A2Uint, 4u },
        { ImageComponentFormats::D16, ImageDataType::UNorm, wgpu::TextureFormat::Depth16Unorm, 2u },
        { ImageComponentFormats::D24, ImageDataType::UNorm, wgpu::TextureFormat::Depth24Plus, 4u },
        { ImageComponentFormats::D32, ImageDataType::Float, wgpu::TextureFormat::Depth32Float, 4u },
        { ImageComponentFormats::D24S8, ImageDataType::UNorm, wgpu::TextureFormat::Depth24PlusStencil8, 4u },
        { ImageComponentFormats::D32S8, ImageDataType::Float, wgpu::TextureFormat::Depth32FloatStencil8, 8u },
    };

    const FormatRow* FindFormatRow(const ImageFormat& format) noexcept;
    bool IsDepthComponent(ImageComponentFormats component) noexcept;
    bool Is32BitFloat(const ImageFormat& format) noexcept;
    std::optional<wgpu::StorageTextureAccess> StorageAccessOf(const BindingSlot& slot, bool writable) noexcept;

}

    std::optional<wgpu::TextureFormat> ToWgpuTextureFormat(const ImageFormat& format) noexcept
    {
        const FormatRow* row = FindFormatRow(format);

        if (row == nullptr)
        {
            return std::nullopt;
        }

        return row->Format;
    }

    uint32_t TexelBytesOf(const ImageFormat& format) noexcept
    {
        const FormatRow* row = FindFormatRow(format);
        return row == nullptr ? 0u : row->BlockBytes;
    }

    wgpu::TextureSampleType ToWgpuSampleType(const ImageFormat& format) noexcept
    {
        if (IsDepthComponent(format.ComponentFormat))
        {
            return wgpu::TextureSampleType::Depth;
        }

        if (format.DataType == ImageDataType::UInt)
        {
            return wgpu::TextureSampleType::Uint;
        }

        if (format.DataType == ImageDataType::SInt)
        {
            return wgpu::TextureSampleType::Sint;
        }

        // a 32-bit float texture is filterable only with a device feature, so the layout claims no filtering
        return Is32BitFloat(format) ? wgpu::TextureSampleType::UnfilterableFloat : wgpu::TextureSampleType::Float;
    }

    std::optional<wgpu::TextureViewDimension> ToWgpuViewDimension(lodestone::TextureDimension dimension,
                                                                  bool arrayed) noexcept
    {
        switch (dimension)
        {
        case lodestone::TextureDimension::Texture1D:
            return arrayed ? std::nullopt : std::optional<wgpu::TextureViewDimension>{ wgpu::TextureViewDimension::e1D };
        case lodestone::TextureDimension::Texture3D:
            return arrayed ? std::nullopt : std::optional<wgpu::TextureViewDimension>{ wgpu::TextureViewDimension::e3D };
        case lodestone::TextureDimension::Texture2D:
            return arrayed ? wgpu::TextureViewDimension::e2DArray : wgpu::TextureViewDimension::e2D;
        default:
            return std::nullopt;
        }
    }

    wgpu::ShaderStage ToWgpuShaderStages(lodestone::ShaderStageKind stages) noexcept
    {
        const auto has = [stages](lodestone::ShaderStageKind stage)
        {
            return (static_cast<uint32_t>(stages) & static_cast<uint32_t>(stage)) != 0u;
        };

        wgpu::ShaderStage result = wgpu::ShaderStage::None;

        if (has(lodestone::ShaderStageKind::Vertex))
        {
            result |= wgpu::ShaderStage::Vertex;
        }

        if (has(lodestone::ShaderStageKind::Fragment))
        {
            result |= wgpu::ShaderStage::Fragment;
        }

        if (has(lodestone::ShaderStageKind::Compute))
        {
            result |= wgpu::ShaderStage::Compute;
        }

        return result;
    }

    std::optional<wgpu::BindGroupLayoutEntry> ToBindGroupLayoutEntry(const BindingSlot& slot, bool writable,
                                                                     const SlotTraits& traits) noexcept
    {
        wgpu::BindGroupLayoutEntry entry{};
        entry.binding = slot.Binding;
        entry.visibility = ToWgpuShaderStages(slot.Stages);

        switch (slot.Kind)
        {
        case lodestone::BindingKind::UniformBuffer:
            entry.buffer.type = wgpu::BufferBindingType::Uniform;
            entry.buffer.hasDynamicOffset = false;
            return entry;
        case lodestone::BindingKind::StorageBuffer:
            // a read-only binding must be a read-only layout: the shader declares var<storage, read>
            entry.buffer.type = writable ? wgpu::BufferBindingType::Storage : wgpu::BufferBindingType::ReadOnlyStorage;
            entry.buffer.hasDynamicOffset = false;
            return entry;
        case lodestone::BindingKind::Texture:
            entry.texture.sampleType = traits.SampleType;
            entry.texture.viewDimension = traits.ViewDimension;
            entry.texture.multisampled = false;
            return entry;
        case lodestone::BindingKind::StorageTexture:
        {
            const std::optional<wgpu::StorageTextureAccess> access = StorageAccessOf(slot, writable);

            if (!access.has_value())
            {
                return std::nullopt;
            }

            entry.storageTexture.access = *access;
            entry.storageTexture.format = traits.StorageFormat;
            entry.storageTexture.viewDimension = traits.ViewDimension;
            return entry;
        }
        case lodestone::BindingKind::Sampler:
            entry.sampler.type = traits.SamplerType;
            return entry;
        default:
            return std::nullopt;
        }
    }

    ShaderDeviceLimits ShaderLimitsOf(const rhi::RhiContext& context)
    {
        const lodestone::manifest::DeviceLimits& known = context.Limits();

        ShaderDeviceLimits result
        {
            .MaxWorkgroupMemoryBytes = known.MaxWorkgroupMemoryBytes,
            .MaxInvocationsPerWorkgroup = known.MaxInvocationsPerWorkgroup,
            .MaxWorkgroupSizeX = known.MaxWorkgroupSizeX,
            .MaxWorkgroupSizeY = known.MaxWorkgroupSizeY,
            .MaxWorkgroupSizeZ = known.MaxWorkgroupSizeZ,
            .MaxPushConstantBytes = known.MaxPushConstantBytes,
        };

        if (!context.IsValid() || context.Backend() != rhi::RhiBackend::WebGpu)
        {
            return result;
        }

        // the device's own limits are the truth: the context's table is a default for an adapter
        wgpu::Limits limits{};

        if (rhi::UnwrapWebGpuRhiContext(context).Device.GetLimits(&limits) != wgpu::Status::Success)
        {
            return result;
        }

        result.MaxWorkgroupMemoryBytes = limits.maxComputeWorkgroupStorageSize;
        result.MaxInvocationsPerWorkgroup = limits.maxComputeInvocationsPerWorkgroup;
        result.MaxWorkgroupSizeX = limits.maxComputeWorkgroupSizeX;
        result.MaxWorkgroupSizeY = limits.maxComputeWorkgroupSizeY;
        result.MaxWorkgroupSizeZ = limits.maxComputeWorkgroupSizeZ;
        return result;
    }

    LayoutLimits LayoutLimitsOf(const rhi::RhiContext& context)
    {
        LayoutLimits result{};

        if (!context.IsValid() || context.Backend() != rhi::RhiBackend::WebGpu)
        {
            return result;
        }

        wgpu::Limits limits{};

        if (rhi::UnwrapWebGpuRhiContext(context).Device.GetLimits(&limits) != wgpu::Status::Success)
        {
            return result;
        }

        const auto clamp32 = [](uint64_t value)
        {
            return static_cast<uint32_t>(std::min<uint64_t>(value, UINT32_MAX));
        };

        result.MaxComputeWorkGroupCountX = limits.maxComputeWorkgroupsPerDimension;
        result.MaxComputeWorkGroupCountY = limits.maxComputeWorkgroupsPerDimension;
        result.MaxComputeWorkGroupCountZ = limits.maxComputeWorkgroupsPerDimension;
        result.MaxColorAttachments = limits.maxColorAttachments;
        result.MaxBoundDescriptorSets = limits.maxBindGroups;
        result.MaxPerStageDescriptorSamplers = limits.maxSamplersPerShaderStage;
        result.MaxPerStageDescriptorUniformBuffers = limits.maxUniformBuffersPerShaderStage;
        result.MaxPerStageDescriptorStorageBuffers = limits.maxStorageBuffersPerShaderStage;
        result.MaxPerStageDescriptorSampledImages = limits.maxSampledTexturesPerShaderStage;
        result.MaxPerStageDescriptorStorageImages = limits.maxStorageTexturesPerShaderStage;
        // WebGPU has no limit on the sum, so the sum of the five per-stage limits is the most a stage can bind
        result.MaxPerStageResources = clamp32(static_cast<uint64_t>(limits.maxSamplersPerShaderStage) +
                                              limits.maxUniformBuffersPerShaderStage +
                                              limits.maxStorageBuffersPerShaderStage +
                                              limits.maxSampledTexturesPerShaderStage +
                                              limits.maxStorageTexturesPerShaderStage);
        result.MaxUniformBufferRange = clamp32(limits.maxUniformBufferBindingSize);
        result.MaxStorageBufferRange = clamp32(limits.maxStorageBufferBindingSize);
        result.MaxImageDimension1D = limits.maxTextureDimension1D;
        result.MaxImageDimension2D = limits.maxTextureDimension2D;
        result.MaxImageDimension3D = limits.maxTextureDimension3D;
        return result;
    }

namespace
{

    const FormatRow* FindFormatRow(const ImageFormat& format) noexcept
    {
        const auto matches = [&format](const FormatRow& row)
        {
            return row.Component == format.ComponentFormat && row.Data == format.DataType;
        };

        const FormatRow* const end = std::end(k_FormatRows);
        const FormatRow* const found = std::find_if(std::begin(k_FormatRows), end, matches);
        return found == end ? nullptr : found;
    }

    bool IsDepthComponent(ImageComponentFormats component) noexcept
    {
        return component == ImageComponentFormats::D16 ||
               component == ImageComponentFormats::D24 ||
               component == ImageComponentFormats::D32 ||
               component == ImageComponentFormats::D24S8 ||
               component == ImageComponentFormats::D32S8;
    }

    bool Is32BitFloat(const ImageFormat& format) noexcept
    {
        return format.DataType == ImageDataType::Float &&
               (format.ComponentFormat == ImageComponentFormats::R32 ||
                format.ComponentFormat == ImageComponentFormats::RG32 ||
                format.ComponentFormat == ImageComponentFormats::RGBA32);
    }

    std::optional<wgpu::StorageTextureAccess> StorageAccessOf(const BindingSlot& slot, bool writable) noexcept
    {
        if (!writable)
        {
            return wgpu::StorageTextureAccess::ReadOnly;
        }

        // the slot's access is narrowed: Write on a reflected ReadWrite binding still needs the ReadWrite layout, which
        // the slot cannot tell apart from a reflected WriteOnly one
        return slot.Access == ResourceAccess::ReadWrite ? wgpu::StorageTextureAccess::ReadWrite
                                                        : wgpu::StorageTextureAccess::WriteOnly;
    }

}

}
