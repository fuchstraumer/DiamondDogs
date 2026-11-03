#pragma once
#ifndef DIAMOND_DOGS_GPU_PYRAMID_HPP
#define DIAMOND_DOGS_GPU_PYRAMID_HPP
#include "RhiFlags.hpp"
#include <vulkan/vulkan_core.h>
#include <cstdint>
#include <expected>
#include <memory>
#include <span>
#include <string>
#include <string_view>

namespace rhi
{
    struct CommandBuffer;
    class Device;
    class ShaderLibrary;
}

// Fills the mip chain of 2D array textures on the GPU, with NvproPyramid (assets/shaders/compute/Pyramid): a Slang
// transcription of NVIDIA's nvpro_pyramid, from the vk_compute_mipmaps sample (Apache 2.0). The scheduling below is
// a port of its nvpro_pyramid_dispatch.hpp. The algorithm is NVIDIA's.
//
// A consumer cooks NvproPyramid.slang into its own bundle (GPU_PYRAMID_SHADER_MODULE in CMake), creates one
// MipGenerator per reduction from that bundle's ShaderLibrary, and one MipTarget per texture.
namespace pyramid
{
    /** @brief The module that NvproPyramid.slang declares */
    constexpr std::string_view k_ModuleName{ "NvproPyramid" };

    /** @brief The PYRAMID_REDUCTION axis: how each block of a level becomes one texel of the next */
    enum class Reduction : uint8_t
    {
        Average,
        Min,
        Max,
    };

    struct MipGeneratorCreateInfo
    {
        Reduction Reduce{ Reduction::Average };
        /** @brief The PYRAMID_SHARED_F16 axis: cache the samples that later levels read as fp16 in groupshared
         *  memory. Halves that memory, at one more fp16 rounding per cached level */
        bool SharedF16{ false };
    };

    /** @brief One texture to fill. It is in the General layout whenever the generator reads or writes it */
    struct MipTargetCreateInfo
    {
        VkImage Image{ VK_NULL_HANDLE };
        /** @brief Must support storage and sampled use, and linear filtering for Reduction::Average */
        VkFormat Format{ VK_FORMAT_UNDEFINED };
        uint32_t Width{ 0u };
        uint32_t Height{ 0u };
        /** @brief Every level the image has, from 2 up to the full chain (at most 16) */
        uint32_t MipLevels{ 0u };
        uint32_t ArrayLayers{ 0u };
    };

    struct MipTargetImpl;

    /** @brief The views and descriptor sets of one texture, and its dispatch schedule. Made by a MipGenerator, and
     *  only recorded with that generator */
    class MipTarget
    {
    public:
        explicit MipTarget(std::unique_ptr<MipTargetImpl>&& _impl) noexcept;
        ~MipTarget();
        MipTarget(const MipTarget&) = delete;
        MipTarget& operator=(const MipTarget&) = delete;

        /** @brief The number of dispatches that fill the chain, so the number of steps that Record separates with
         *  a barrier */
        [[nodiscard]] uint32_t DispatchCount() const noexcept;
        /** @brief How many of those dispatches use the fast pipeline */
        [[nodiscard]] uint32_t FastDispatchCount() const noexcept;

    private:
        friend class MipGenerator;
        std::unique_ptr<MipTargetImpl> impl;
    };

    struct MipGeneratorImpl;

    class MipGenerator
    {
    public:
        /** @brief Selects the variant of `create_info` from the NvproPyramid module of `library`, and creates its
         *  programs and the sampler. The fast pipeline is used only when the device can run compute shaders with
         *  full subgroups of exactly 32 lanes, with shuffles: the original was not tested with other sizes.
         *  Needs shaderStorageImageWriteWithoutFormat (see the todo on PyramidLevels in NvproPyramid.slang) */
        [[nodiscard]] static std::expected<std::unique_ptr<MipGenerator>, std::string> Create(
            rhi::Device& device,
            const rhi::ShaderLibrary& library,
            const MipGeneratorCreateInfo& create_info);

        explicit MipGenerator(std::unique_ptr<MipGeneratorImpl>&& _impl) noexcept;
        ~MipGenerator();
        MipGenerator(const MipGenerator&) = delete;
        MipGenerator& operator=(const MipGenerator&) = delete;

        /** @brief Creates the views and descriptor sets for one texture, and plans its dispatches */
        [[nodiscard]] std::expected<std::unique_ptr<MipTarget>, std::string> CreateTarget(const MipTargetCreateInfo& create_info) const;

        /** @brief Fills mips 1 and up of every target from its mip 0. The dispatches of all targets run together,
         *  step by step, with one barrier between steps and none before or after: the caller makes mip 0 visible
         *  to k_ReadStages and k_ReadAccess first, and waits on k_WriteStages and k_WriteAccess after */
        void Record(rhi::CommandBuffer& command_buffer, std::span<const MipTarget* const> targets) const;

        /** @brief Every level is read as a sampled image and written as a storage image, by compute shaders */
        static constexpr rhi::PipelineStage k_ReadStages = rhi::PipelineStage::ComputeShader;
        static constexpr rhi::Access k_ReadAccess = rhi::Access::ShaderSampledRead;
        static constexpr rhi::PipelineStage k_WriteStages = rhi::PipelineStage::ComputeShader;
        static constexpr rhi::Access k_WriteAccess = rhi::Access::ShaderStorageWrite;

        [[nodiscard]] bool HasFastPipeline() const noexcept;

    private:
        std::unique_ptr<MipGeneratorImpl> impl;
    };
}

#endif //!DIAMOND_DOGS_GPU_PYRAMID_HPP
