#pragma once
#ifndef DIAMOND_DOGS_COLOR_PIPE_COLOR_PIPE_ERROR_HPP
#define DIAMOND_DOGS_COLOR_PIPE_COLOR_PIPE_ERROR_HPP
#include <cstdint>
#include <expected>
#include <string_view>

namespace color
{
    /** @brief Why a ColorPipe call failed */
    enum class ColorPipeError : uint8_t
    {
        Invalid = 0,
        Success,
        /** @brief The surface choice has no encoding */
        SurfaceUnusable,
        /** @brief The shader library has no ColorComposite module */
        CompositeModuleMissing,
        /** @brief The module has no variant for an encoding or a tonemapper */
        CompositeVariantMissing,
        ProgramCreationFailed,
        /** @brief The reflected push constant block does not have the size of DisplayParameters */
        ShaderLayoutMismatch,
        /** @brief Creating an image or the exposure buffer failed, or the device lacks a needed feature or format */
        ResourceCreationFailed,
        DescriptorSetupFailed,
        /** @brief The tonemapper has no program yet */
        TonemapperUnavailable,
        /** @brief OnSwapchainRecreated needs a swapchain, and the create info had none */
        SwapchainMissing,
    };

    template<typename T>
    using ColorPipeResult = std::expected<T, ColorPipeError>;

    [[nodiscard]] std::string_view ToString(ColorPipeError error) noexcept;
}

#endif // !DIAMOND_DOGS_COLOR_PIPE_COLOR_PIPE_ERROR_HPP
