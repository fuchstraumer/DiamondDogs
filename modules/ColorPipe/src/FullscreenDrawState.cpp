#include "FullscreenDrawState.hpp"
#include <vulkan/vulkan_core.h>

namespace color
{
    namespace
    {
        template<typename Function>
        [[nodiscard]] bool LoadFunction(VkDevice device, Function& function, const char* name) noexcept;
    }

    bool FullscreenDrawState::Load(VkDevice device) noexcept
    {
        return LoadFunction(device, setVertexInput, "vkCmdSetVertexInputEXT") &&
            LoadFunction(device, setDepthClampEnable, "vkCmdSetDepthClampEnableEXT") &&
            LoadFunction(device, setPolygonMode, "vkCmdSetPolygonModeEXT") &&
            LoadFunction(device, setRasterizationSamples, "vkCmdSetRasterizationSamplesEXT") &&
            LoadFunction(device, setSampleMask, "vkCmdSetSampleMaskEXT") &&
            LoadFunction(device, setAlphaToCoverageEnable, "vkCmdSetAlphaToCoverageEnableEXT") &&
            LoadFunction(device, setAlphaToOneEnable, "vkCmdSetAlphaToOneEnableEXT") &&
            LoadFunction(device, setColorBlendEnable, "vkCmdSetColorBlendEnableEXT") &&
            LoadFunction(device, setColorBlendEquation, "vkCmdSetColorBlendEquationEXT") &&
            LoadFunction(device, setColorWriteMask, "vkCmdSetColorWriteMaskEXT") &&
            LoadFunction(device, setLogicOpEnable, "vkCmdSetLogicOpEnableEXT");
    }

    void FullscreenDrawState::Record(VkCommandBuffer command_buffer) const noexcept
    {
        vkCmdSetPrimitiveTopology(command_buffer, VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST);
        vkCmdSetPrimitiveRestartEnable(command_buffer, VK_FALSE);
        // every vertex comes from SV_VertexID
        setVertexInput(command_buffer, 0, nullptr, 0, nullptr);

        setDepthClampEnable(command_buffer, VK_FALSE);
        vkCmdSetRasterizerDiscardEnable(command_buffer, VK_FALSE);
        setPolygonMode(command_buffer, VK_POLYGON_MODE_FILL);
        vkCmdSetCullMode(command_buffer, VK_CULL_MODE_NONE);
        vkCmdSetFrontFace(command_buffer, VK_FRONT_FACE_COUNTER_CLOCKWISE);
        vkCmdSetDepthBiasEnable(command_buffer, VK_FALSE);

        setRasterizationSamples(command_buffer, VK_SAMPLE_COUNT_1_BIT);
        constexpr VkSampleMask k_SampleMask = 0xFFFFFFFFu;
        setSampleMask(command_buffer, VK_SAMPLE_COUNT_1_BIT, &k_SampleMask);
        setAlphaToCoverageEnable(command_buffer, VK_FALSE);
        setAlphaToOneEnable(command_buffer, VK_FALSE);

        // the equation is set because the state is dynamic
        constexpr VkBool32 k_BlendEnable = VK_FALSE;
        setColorBlendEnable(command_buffer, 0, 1, &k_BlendEnable);
        constexpr VkColorBlendEquationEXT k_Replace
        {
            VK_BLEND_FACTOR_ONE, VK_BLEND_FACTOR_ZERO, VK_BLEND_OP_ADD,
            VK_BLEND_FACTOR_ONE, VK_BLEND_FACTOR_ZERO, VK_BLEND_OP_ADD
        };
        setColorBlendEquation(command_buffer, 0, 1, &k_Replace);
        constexpr VkColorComponentFlags k_WriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT | VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;
        setColorWriteMask(command_buffer, 0, 1, &k_WriteMask);
        setLogicOpEnable(command_buffer, VK_FALSE);

        vkCmdSetDepthTestEnable(command_buffer, VK_FALSE);
        vkCmdSetDepthWriteEnable(command_buffer, VK_FALSE);
        vkCmdSetDepthCompareOp(command_buffer, VK_COMPARE_OP_ALWAYS);
        vkCmdSetDepthBoundsTestEnable(command_buffer, VK_FALSE);
        vkCmdSetStencilTestEnable(command_buffer, VK_FALSE);
    }

    namespace
    {
        template<typename Function>
        bool LoadFunction(VkDevice device, Function& function, const char* name) noexcept
        {
            function = reinterpret_cast<Function>(vkGetDeviceProcAddr(device, name));

            return function != nullptr;
        }
    }
}
