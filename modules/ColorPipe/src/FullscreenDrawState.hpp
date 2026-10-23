#pragma once
#ifndef DIAMOND_DOGS_COLOR_PIPE_FULLSCREEN_DRAW_STATE_HPP
#define DIAMOND_DOGS_COLOR_PIPE_FULLSCREEN_DRAW_STATE_HPP
#include <vulkan/vulkan_core.h>

namespace color
{
    /** @brief Records all dynamic graphics state of a fullscreen triangle: no vertex buffers, no depth, no blending,
     *  one sample. The pointers are entry points of VK_EXT_extended_dynamic_state3, which the loader does not export. */
    class FullscreenDrawState
    {
    public:
        /** @brief Loads the entry points. Returns false when the device lacks one. */
        [[nodiscard]] bool Load(VkDevice device) noexcept;
        void Record(VkCommandBuffer command_buffer) const noexcept;

    private:
        PFN_vkCmdSetVertexInputEXT setVertexInput{ nullptr };
        PFN_vkCmdSetDepthClampEnableEXT setDepthClampEnable{ nullptr };
        PFN_vkCmdSetPolygonModeEXT setPolygonMode{ nullptr };
        PFN_vkCmdSetRasterizationSamplesEXT setRasterizationSamples{ nullptr };
        PFN_vkCmdSetSampleMaskEXT setSampleMask{ nullptr };
        PFN_vkCmdSetAlphaToCoverageEnableEXT setAlphaToCoverageEnable{ nullptr };
        PFN_vkCmdSetAlphaToOneEnableEXT setAlphaToOneEnable{ nullptr };
        PFN_vkCmdSetColorBlendEnableEXT setColorBlendEnable{ nullptr };
        PFN_vkCmdSetColorBlendEquationEXT setColorBlendEquation{ nullptr };
        PFN_vkCmdSetColorWriteMaskEXT setColorWriteMask{ nullptr };
        PFN_vkCmdSetLogicOpEnableEXT setLogicOpEnable{ nullptr };
    };
}

#endif // !DIAMOND_DOGS_COLOR_PIPE_FULLSCREEN_DRAW_STATE_HPP
