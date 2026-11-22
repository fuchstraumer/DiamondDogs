#pragma once
#ifndef DIAMOND_DOGS_LOOM_WEBGPU_MAPPING_HPP
#define DIAMOND_DOGS_LOOM_WEBGPU_MAPPING_HPP
#include "ImageDataFormats.hpp"
#include "ResourceFlags.hpp"
#include "ShaderLibraryTypes.hpp"
#include "bind/BoundGraph.hpp"
#include <webgpu/webgpu_cpp.h>
#include <cstdint>
#include <optional>

// WebGpuMapping.cpp: Lodestone's neutral values and foundation's image formats in WebGPU terms.
namespace loom
{

    // The facts of one slot that BindingSlot does not hold: Realize reads them from the slot's resource row. A slot of
    // another kind ignores the fields it does not use.
    struct SlotTraits
    {
        wgpu::TextureFormat StorageFormat{ wgpu::TextureFormat::Undefined };
        wgpu::TextureViewDimension ViewDimension{ wgpu::TextureViewDimension::e2D };
        wgpu::TextureSampleType SampleType{ wgpu::TextureSampleType::Float };
        wgpu::SamplerBindingType SamplerType{ wgpu::SamplerBindingType::Filtering };
    };

    // No value for a format that WebGPU has no texture format for.
    [[nodiscard]] std::optional<wgpu::TextureFormat> ToWgpuTextureFormat(const ImageFormat& format) noexcept;
    // The bytes of one texel (or one block of a compressed format). 0 for an unknown format.
    [[nodiscard]] uint32_t TexelBytesOf(const ImageFormat& format) noexcept;
    // How a texture is sampled: Uint, Sint, Depth, UnfilterableFloat for 32-bit float, else Float.
    [[nodiscard]] wgpu::TextureSampleType ToWgpuSampleType(const ImageFormat& format) noexcept;
    // 1D, 2D and 3D, with a 2D array. No value for an array of 1D or 3D textures, which WebGPU does not have.
    [[nodiscard]] std::optional<wgpu::TextureViewDimension> ToWgpuViewDimension(lodestone::TextureDimension dimension,
                                                                                bool arrayed) noexcept;
    [[nodiscard]] wgpu::ShaderStage ToWgpuShaderStages(lodestone::ShaderStageKind stages) noexcept;
    // The layout entry of a bound slot. `writable` is the reflected access (CompiledData::SlotWritable). No value for a
    // kind with no WebGPU binding type.
    [[nodiscard]] std::optional<wgpu::BindGroupLayoutEntry> ToBindGroupLayoutEntry(const BindingSlot& slot,
                                                                                   bool writable,
                                                                                   const SlotTraits& traits) noexcept;

}

#endif // !DIAMOND_DOGS_LOOM_WEBGPU_MAPPING_HPP
