#pragma once
#ifndef DIAMOND_DOGS_RHI_CONTEXT_WEBGPU_HPP
#define DIAMOND_DOGS_RHI_CONTEXT_WEBGPU_HPP
#include "RhiContext.hpp"
#include <webgpu/webgpu_cpp.h>
#include <cstdint>

namespace rhi
{

struct WebGpuRhiContextView
{
    wgpu::Instance Instance;
    wgpu::Adapter Adapter;
    wgpu::Device Device;
    wgpu::Queue Queue;
    bool ValidationEnabled{ false };
};

[[nodiscard]] RhiContext MakeWebGpuRhiContext(const WebGpuRhiContextView& view);
[[nodiscard]] WebGpuRhiContextView UnwrapWebGpuRhiContext(const RhiContext& context) noexcept;

} // namespace rhi

#endif // !DIAMOND_DOGS_RHI_CONTEXT_WEBGPU_HPP
