#include "ShaderObjectState.hpp"
#include "vulkan/vulkan_core.h"
#include <stdexcept>

namespace
{
    // the core 1.3 dynamic state (topology, cull mode, depth test...) needs no pointers. These are the EXT ones
    PFN_vkCmdSetVertexInputEXT pfn_vkCmdSetVertexInputEXT = nullptr;
    PFN_vkCmdSetDepthClampEnableEXT pfn_vkCmdSetDepthClampEnableEXT = nullptr;
    PFN_vkCmdSetPolygonModeEXT pfn_vkCmdSetPolygonModeEXT = nullptr;
    PFN_vkCmdSetRasterizationSamplesEXT pfn_vkCmdSetRasterizationSamplesEXT = nullptr;
    PFN_vkCmdSetSampleMaskEXT pfn_vkCmdSetSampleMaskEXT = nullptr;
    PFN_vkCmdSetAlphaToCoverageEnableEXT pfn_vkCmdSetAlphaToCoverageEnableEXT = nullptr;
    PFN_vkCmdSetAlphaToOneEnableEXT pfn_vkCmdSetAlphaToOneEnableEXT = nullptr;
    PFN_vkCmdSetColorBlendEnableEXT pfn_vkCmdSetColorBlendEnableEXT = nullptr;
    PFN_vkCmdSetColorBlendEquationEXT pfn_vkCmdSetColorBlendEquationEXT = nullptr;
    PFN_vkCmdSetColorWriteMaskEXT pfn_vkCmdSetColorWriteMaskEXT = nullptr;
    PFN_vkCmdSetLogicOpEnableEXT pfn_vkCmdSetLogicOpEnableEXT = nullptr;

    template<typename Function>
    void Load(VkDevice device, Function& function, const char* name)
    {
        function = reinterpret_cast<Function>(vkGetDeviceProcAddr(device, name));
        if (function == nullptr)
        {
            throw std::runtime_error(name);
        }
    }
}

void LoadShaderObjectStateFunctions(VkDevice device)
{
    Load(device, pfn_vkCmdSetVertexInputEXT, "vkCmdSetVertexInputEXT");
    Load(device, pfn_vkCmdSetDepthClampEnableEXT, "vkCmdSetDepthClampEnableEXT");
    Load(device, pfn_vkCmdSetPolygonModeEXT, "vkCmdSetPolygonModeEXT");
    Load(device, pfn_vkCmdSetRasterizationSamplesEXT, "vkCmdSetRasterizationSamplesEXT");
    Load(device, pfn_vkCmdSetSampleMaskEXT, "vkCmdSetSampleMaskEXT");
    Load(device, pfn_vkCmdSetAlphaToCoverageEnableEXT, "vkCmdSetAlphaToCoverageEnableEXT");
    Load(device, pfn_vkCmdSetAlphaToOneEnableEXT, "vkCmdSetAlphaToOneEnableEXT");
    Load(device, pfn_vkCmdSetColorBlendEnableEXT, "vkCmdSetColorBlendEnableEXT");
    Load(device, pfn_vkCmdSetColorBlendEquationEXT, "vkCmdSetColorBlendEquationEXT");
    Load(device, pfn_vkCmdSetColorWriteMaskEXT, "vkCmdSetColorWriteMaskEXT");
    Load(device, pfn_vkCmdSetLogicOpEnableEXT, "vkCmdSetLogicOpEnableEXT");
}

void SetShaderObjectDrawState(VkCommandBuffer command_buffer, const ShaderObjectDrawState& state)
{
    vkCmdSetPrimitiveTopology(command_buffer, VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST);
    vkCmdSetPrimitiveRestartEnable(command_buffer, VK_FALSE);
    // every vertex comes from SV_VertexID
    pfn_vkCmdSetVertexInputEXT(command_buffer, 0, nullptr, 0, nullptr);

    pfn_vkCmdSetDepthClampEnableEXT(command_buffer, VK_FALSE);
    vkCmdSetRasterizerDiscardEnable(command_buffer, VK_FALSE);
    pfn_vkCmdSetPolygonModeEXT(command_buffer, VK_POLYGON_MODE_FILL);
    vkCmdSetCullMode(command_buffer, state.CullMode);
    vkCmdSetFrontFace(command_buffer, VK_FRONT_FACE_COUNTER_CLOCKWISE);
    vkCmdSetDepthBiasEnable(command_buffer, VK_FALSE);

    pfn_vkCmdSetRasterizationSamplesEXT(command_buffer, state.Samples);
    // one 32-bit word covers up to 32 samples
    constexpr VkSampleMask k_SampleMask = 0xFFFFFFFFu;
    pfn_vkCmdSetSampleMaskEXT(command_buffer, state.Samples, &k_SampleMask);
    pfn_vkCmdSetAlphaToCoverageEnableEXT(command_buffer, VK_FALSE);
    pfn_vkCmdSetAlphaToOneEnableEXT(command_buffer, VK_FALSE);

    const VkBool32 blendEnable = state.AlphaBlend ? VK_TRUE : VK_FALSE;
    pfn_vkCmdSetColorBlendEnableEXT(command_buffer, 0, 1, &blendEnable);
    constexpr VkColorBlendEquationEXT k_AlphaBlend
    {
        VK_BLEND_FACTOR_SRC_ALPHA, VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA, VK_BLEND_OP_ADD,
        VK_BLEND_FACTOR_ONE, VK_BLEND_FACTOR_ZERO, VK_BLEND_OP_ADD
    };
    pfn_vkCmdSetColorBlendEquationEXT(command_buffer, 0, 1, &k_AlphaBlend);
    constexpr VkColorComponentFlags k_WriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT | VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;
    pfn_vkCmdSetColorWriteMaskEXT(command_buffer, 0, 1, &k_WriteMask);
    pfn_vkCmdSetLogicOpEnableEXT(command_buffer, VK_FALSE);

    vkCmdSetDepthTestEnable(command_buffer, state.DepthTest ? VK_TRUE : VK_FALSE);
    vkCmdSetDepthWriteEnable(command_buffer, state.DepthWrite ? VK_TRUE : VK_FALSE);
    vkCmdSetDepthCompareOp(command_buffer, VK_COMPARE_OP_LESS);
    vkCmdSetDepthBoundsTestEnable(command_buffer, VK_FALSE);
    vkCmdSetStencilTestEnable(command_buffer, VK_FALSE);
}
