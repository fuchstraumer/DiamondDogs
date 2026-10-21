#pragma once
#ifndef DIAMOND_DOGS_OCEAN_SHADER_OBJECT_STATE_HPP
#define DIAMOND_DOGS_OCEAN_SHADER_OBJECT_STATE_HPP
#include <vulkan/vulkan_core.h>

// With shader objects every piece of graphics state is dynamic, and a draw needs all of it set. This is the same set
// TriangleTest records (validation clean), for drawing with no vertex buffers. It lives in the demo until the RHI has
// a wrapper for it (audit.md section 8, candidate 4).
struct ShaderObjectDrawState
{
    bool DepthTest{ true };
    bool DepthWrite{ true };
    bool AlphaBlend{ false };
    VkCullModeFlags CullMode{ VK_CULL_MODE_NONE };
    /** @brief Must match the sample count of the attachments being rendered to */
    VkSampleCountFlagBits Samples{ VK_SAMPLE_COUNT_1_BIT };
};

/** @brief Loads the EXT entry points. Call once, after the device exists */
void LoadShaderObjectStateFunctions(VkDevice device);
void SetShaderObjectDrawState(VkCommandBuffer command_buffer, const ShaderObjectDrawState& state);

#endif //!DIAMOND_DOGS_OCEAN_SHADER_OBJECT_STATE_HPP
