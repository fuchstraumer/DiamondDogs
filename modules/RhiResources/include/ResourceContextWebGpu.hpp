#pragma once
#ifndef DIAMOND_DOGS_RESOURCE_CONTEXT_WEBGPU_HPP
#define DIAMOND_DOGS_RESOURCE_CONTEXT_WEBGPU_HPP
#include "ResourceTypes.hpp"

#include <webgpu/webgpu_cpp.h>

namespace rhi
{

/** @brief Returns the buffer that the resource refers to, or an empty buffer when the resource is not a buffer. */
[[nodiscard]] wgpu::Buffer UnwrapWebGpuBuffer(const GraphicsResource& resource) noexcept;
/** @brief Returns the texture that the resource refers to, or an empty texture when the resource is not an image. */
[[nodiscard]] wgpu::Texture UnwrapWebGpuTexture(const GraphicsResource& resource) noexcept;
/** @brief Returns the texture view of the resource, or an empty view when the resource has none. */
[[nodiscard]] wgpu::TextureView UnwrapWebGpuTextureView(const GraphicsResource& resource) noexcept;
/** @brief Returns the sampler of the resource, or an empty sampler when the resource has none. */
[[nodiscard]] wgpu::Sampler UnwrapWebGpuSampler(const GraphicsResource& resource) noexcept;

} // namespace rhi

#endif // !DIAMOND_DOGS_RESOURCE_CONTEXT_WEBGPU_HPP
