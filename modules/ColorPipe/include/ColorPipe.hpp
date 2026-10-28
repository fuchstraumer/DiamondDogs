#pragma once
#ifndef DIAMOND_DOGS_COLOR_PIPE_COLOR_PIPE_HPP
#define DIAMOND_DOGS_COLOR_PIPE_COLOR_PIPE_HPP
#include "ColorPipeError.hpp"
#include "DisplayTarget.hpp"
#include "ImageDataFormats.hpp"
#include "PlatformTypes.hpp"
#include "RhiFlags.hpp"
#include "RhiHandle.hpp"
#include <cstdint>
#include <memory>
#include <string_view>

class ResourceContext;
class Swapchain;

namespace rhi
{
    struct CommandBuffer;
    class Device;
    class GpuTimerBlock;
    class ShaderLibrary;
}

namespace color
{
    /** @brief A render target of the pipe. `Layout` is the layout to declare when rendering to the image, and to leave
     *  it in. */
    struct ColorTargetView
    {
        rhi::ImageHandle Image{};
        rhi::ImageViewHandle View{};
        ImageFormat Format{};
        rhi::ImageLayout Layout{ rhi::ImageLayout::Undefined };
        uint32_t Width{ 0u };
        uint32_t Height{ 0u };
    };

    struct BloomSettings
    {
        bool Enabled{ false };
        float Intensity{ 0.04f };
        float Scatter{ 0.7f };
    };

    struct ColorPipeCreateInfo
    {
        rhi::Device* Device{ nullptr };
        ResourceContext* Resources{ nullptr };
        /** @brief The bundle that contains the ColorComposite module */
        const rhi::ShaderLibrary* Library{ nullptr };
        /** @brief Null for a headless pipe. Then Width and Height size the targets, and no HDR metadata is set. */
        Swapchain* TargetSwapchain{ nullptr };
        uint32_t Width{ 0u };
        uint32_t Height{ 0u };
        DisplayColorCapabilities Capabilities{};
        /** @brief The pair from ChooseSurface. In a headless pipe, the format and encoding of the output image. */
        SurfaceChoice Surface{};
        DisplayOverrides Overrides{};
        TonemapperKind Tonemapper{ TonemapperKind::Invalid };
    };

    struct ColorPipeImpl;

    class ColorPipe
    {
    public:
        /** @brief Creates the programs, the targets, the exposure buffer and the descriptor sets. Submits one command
         *  buffer to the general queue, and waits for it. */
        [[nodiscard]] static ColorPipeResult<std::unique_ptr<ColorPipe>> Create(const ColorPipeCreateInfo& create_info);

        explicit ColorPipe(std::unique_ptr<ColorPipeImpl>&& _impl) noexcept;
        /** @brief Destroys the targets and the descriptor pool. The device must be idle, and the ResourceContext alive. */
        ~ColorPipe();
        ColorPipe(const ColorPipe&) = delete;
        ColorPipe& operator=(const ColorPipe&) = delete;

        /** @brief Returns the DisplayTarget that resolved from the capabilities, the surface and the overrides. */
        [[nodiscard]] const DisplayTarget& Target() const noexcept;
        [[nodiscard]] TonemapperKind Tonemapper() const noexcept;

        /** @brief Returns the RGBA16F target for the scene-linear AP1 scene. */
        [[nodiscard]] ColorTargetView SceneTarget() const noexcept;
        /** @brief Returns the RGBA8 UNORM target for Dear ImGui. Clear it to transparent. */
        [[nodiscard]] ColorTargetView UiTarget() const noexcept;

        /** @brief Sets the exposure in stops for the next RecordComposite. */
        void SetExposureEv(float exposure_ev) noexcept;

        void SetBloomSettings(const BloomSettings& settings) noexcept;
        [[nodiscard]] const BloomSettings& GetBloomSettings() const noexcept;

        /** @brief Selects the tonemapper of the next RecordComposite. Fails for a kind that has no program yet. */
        [[nodiscard]] ColorPipeError SetTonemapper(TonemapperKind kind) noexcept;

        /** @brief Resolves the DisplayTarget again, and sets the HDR metadata of the swapchain. */
        void SetOverrides(const DisplayOverrides& overrides);

        /** @brief Records the composite pass. `output_view` is in the ColorAttachment layout, has the size of the pipe,
         *  and has the format of Surface. `timers` can be null. */
        void RecordComposite(rhi::CommandBuffer& command_buffer, rhi::ImageViewHandle output_view, rhi::GpuTimerBlock* timers = nullptr);

        /** @brief Rebuilds the targets at the extent of the swapchain, and sets its HDR metadata. The device must be idle. */
        [[nodiscard]] ColorPipeError OnSwapchainRecreated();

        /** @brief Rebuilds the targets at a new size. The device must be idle. */
        [[nodiscard]] ColorPipeError Resize(uint32_t width, uint32_t height);

    private:
        std::unique_ptr<ColorPipeImpl> impl;
    };
}

#endif // !DIAMOND_DOGS_COLOR_PIPE_COLOR_PIPE_HPP
