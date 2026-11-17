#pragma once
#ifndef DIAMOND_DOGS_PLANET_GPU_ERRORS_HPP
#define DIAMOND_DOGS_PLANET_GPU_ERRORS_HPP
#include <cstdint>
#include <expected>
#include <string_view>

namespace planet
{
    enum class PlanetGpuError : uint8_t
    {
        Invalid = 0,
        Success,
        ModuleNotFound,
        VariantNotFound,
        // the variant that the caller asked for needs a capability that the device was created without
        NoVariantFitsDevice,
        EntryPointNotFound,
        ProgramCreationFailed,
        UniformNotFound,
        DescriptorPoolFailed,
        DescriptorWriteFailed,
        BufferCreationFailed,
        BufferNotFound,
        SubmitFailed,
        ReadbackFailed,
    };

    template<typename T>
    using PlanetGpuResult = std::expected<T, PlanetGpuError>;

    [[nodiscard]] std::string_view ToString(PlanetGpuError error) noexcept;
}

#endif // !DIAMOND_DOGS_PLANET_GPU_ERRORS_HPP
