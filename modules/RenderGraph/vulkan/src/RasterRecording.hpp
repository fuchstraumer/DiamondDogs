#pragma once
#ifndef DIAMOND_DOGS_LOOM_VULKAN_RASTER_RECORDING_HPP
#define DIAMOND_DOGS_LOOM_VULKAN_RASTER_RECORDING_HPP
#include <vulkan/vulkan_core.h>
#include <cstdint>

// RasterRecording.cpp: the raw Vulkan that rhi::CommandBuffer has no call for yet (step 7, her yes): the dynamic state
// that shader objects need, the indirect draws, and the draw itself. Each gap is listed in the Loom agent's handbook
// for the RhiSystem agent, so it can move behind RhiSystem later.
namespace loom
{
    struct PipelineDescription;
    struct FrozenPass;

    // The VK_EXT_extended_dynamic_state3 and VK_EXT_vertex_input_dynamic_state entry points, which the loader does not
    // export. ColorPipe's FullscreenDrawState loads the same set.
    struct RasterFunctions
    {
        PFN_vkCmdSetVertexInputEXT SetVertexInput{ nullptr };
        PFN_vkCmdSetDepthClampEnableEXT SetDepthClampEnable{ nullptr };
        PFN_vkCmdSetPolygonModeEXT SetPolygonMode{ nullptr };
        PFN_vkCmdSetRasterizationSamplesEXT SetRasterizationSamples{ nullptr };
        PFN_vkCmdSetSampleMaskEXT SetSampleMask{ nullptr };
        PFN_vkCmdSetAlphaToCoverageEnableEXT SetAlphaToCoverageEnable{ nullptr };
        PFN_vkCmdSetAlphaToOneEnableEXT SetAlphaToOneEnable{ nullptr };
        PFN_vkCmdSetColorBlendEnableEXT SetColorBlendEnable{ nullptr };
        PFN_vkCmdSetColorBlendEquationEXT SetColorBlendEquation{ nullptr };
        PFN_vkCmdSetColorWriteMaskEXT SetColorWriteMask{ nullptr };
        PFN_vkCmdSetLogicOpEnableEXT SetLogicOpEnable{ nullptr };
    };

    // False when the device lacks one of them
    [[nodiscard]] bool LoadRasterFunctions(VkDevice device, RasterFunctions& functions) noexcept;

    // Every piece of graphics state that a shader-object draw needs, from the pass's static description (step 7 Q2):
    // topology, polygon mode, cull, front face, depth test, write and compare, blend per color slot. Vertices are
    // pulled, so the vertex input is empty. color_slots is the highest color slot + 1.
    void RecordDrawState(const RasterFunctions& functions, VkCommandBuffer command_buffer,
                         const PipelineDescription& description, uint32_t color_slots) noexcept;

    // The pass's one draw command. The buffers are the rows' objects: arguments and count for the indirect forms, and
    // the index buffer when the pass has one (it is bound here).
    void RecordDrawCommand(VkCommandBuffer command_buffer, const FrozenPass& pass, VkBuffer arguments, VkBuffer count,
                           VkBuffer indices) noexcept;

}

#endif // !DIAMOND_DOGS_LOOM_VULKAN_RASTER_RECORDING_HPP
