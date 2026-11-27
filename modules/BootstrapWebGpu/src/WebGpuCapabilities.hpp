#pragma once
#ifndef DIAMOND_DOGS_BOOTSTRAP_WEBGPU_CAPABILITIES_HPP
#define DIAMOND_DOGS_BOOTSTRAP_WEBGPU_CAPABILITIES_HPP
#include "DeviceCapability.hpp"
#include <webgpu/webgpu_cpp.h>
#include <optional>
#include <span>
#include <string_view>

namespace bootstrap
{
    /** @brief The WebGPU feature of a DeviceFeature need, which names it in the JavaScript spelling, for example
     *  "timestamp-query". Empty when no feature has that name. */
    [[nodiscard]] std::optional<wgpu::FeatureName> WebGpuFeatureNamed(std::string_view name) noexcept;
    /** @brief The WebGPU feature that a WGSL `enable` directive needs, for example "f16" and ShaderF16. Empty when no
     *  feature enables that name. */
    [[nodiscard]] std::optional<wgpu::FeatureName> WebGpuFeatureOfWgslEnable(std::string_view enable_name) noexcept;
    /** @brief The WebGPU features of a neutral capability. The device has the capability only when it has every one.
     *  Empty when WebGPU cannot provide it. */
    [[nodiscard]] std::span<const wgpu::FeatureName> WebGpuFeaturesOf(DeviceCapability capability) noexcept;
    /** @brief The JavaScript spelling of a feature, for failure text. "unknown" for a feature that no table names. */
    [[nodiscard]] std::string_view NameOf(wgpu::FeatureName feature) noexcept;
}

#endif // !DIAMOND_DOGS_BOOTSTRAP_WEBGPU_CAPABILITIES_HPP
