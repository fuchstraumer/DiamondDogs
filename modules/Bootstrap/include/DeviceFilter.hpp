#pragma once
#ifndef DIAMOND_DOGS_BOOTSTRAP_DEVICE_FILTER_HPP
#define DIAMOND_DOGS_BOOTSTRAP_DEVICE_FILTER_HPP
#include "DeviceCapability.hpp"
#include "ShaderManifestDevice.hpp"
#include <span>

// No graphics API here: every device stage returns this type, and Loom's core reads it
namespace bootstrap
{
    /** What the device was created with, in the shape that a variant choice (ShaderLibrary, Loom's Compile) takes.
     *  It answers from the enabled set, never from the physical device. The views point into the device stage, so
     *  the filter must not outlive the stage's Stop. */
    struct DeviceFilter
    {
        // answers for the shader capability names of the stage's target: SPIR-V names, or WGSL enable names
        lodestone::manifest::CapabilitySupportFn Supports{ nullptr };
        void* User{ nullptr };
        lodestone::manifest::DeviceLimits Limits;
        // the neutral capabilities that the device was created with, sorted. A capability is here only when every
        // API need that the stage maps it to was enabled
        std::span<const DeviceCapability> Capabilities;
    };

    /** @brief Returns true when the device was created with `capability`. */
    [[nodiscard]] inline bool HasCapability(const DeviceFilter& filter, DeviceCapability capability) noexcept
    {
        return HasCapability(filter.Capabilities, capability);
    }
}

#endif // !DIAMOND_DOGS_BOOTSTRAP_DEVICE_FILTER_HPP
