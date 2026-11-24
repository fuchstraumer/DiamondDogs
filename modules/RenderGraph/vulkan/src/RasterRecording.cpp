#include "RasterRecording.hpp"
#include "PassStatements.hpp"
#include "Records.hpp"
#include "freeze/FrozenGraph.hpp"
#include "layout/LaidOutGraph.hpp"
#include <vulkan/vulkan_core.h>
#include <array>
#include <cstdint>

namespace loom
{

namespace
{

    // Draws from Loom use at most this many color slots; maxColorAttachments is checked at Layout (LM601)
    constexpr uint32_t k_MaxColorSlots = 8u;

    // vkCmdDrawIndirect reads four uint32 values per draw, vkCmdDrawIndexedIndirect five
    constexpr uint32_t k_DrawStride = 16u;
    constexpr uint32_t k_DrawIndexedStride = 20u;

    template<typename Function>
    bool LoadFunction(VkDevice device, Function& function, const char* name) noexcept
    {
        function = reinterpret_cast<Function>(vkGetDeviceProcAddr(device, name));
        return function != nullptr;
    }

    VkCompareOp ToVkCompareOp(CompareOp compare) noexcept
    {
        switch (compare)
        {
        case CompareOp::Never:
            return VK_COMPARE_OP_NEVER;
        case CompareOp::Less:
            return VK_COMPARE_OP_LESS;
        case CompareOp::Equal:
            return VK_COMPARE_OP_EQUAL;
        case CompareOp::LessOrEqual:
            return VK_COMPARE_OP_LESS_OR_EQUAL;
        case CompareOp::Greater:
            return VK_COMPARE_OP_GREATER;
        case CompareOp::NotEqual:
            return VK_COMPARE_OP_NOT_EQUAL;
        case CompareOp::GreaterOrEqual:
            return VK_COMPARE_OP_GREATER_OR_EQUAL;
        case CompareOp::Always:
            [[fallthrough]];
        case CompareOp::Invalid:
            break;
        }

        return VK_COMPARE_OP_ALWAYS;
    }

    VkCullModeFlags ToVkCullMode(CullMode cull) noexcept
    {
        switch (cull)
        {
        case CullMode::Front:
            return VK_CULL_MODE_FRONT_BIT;
        case CullMode::Back:
            return VK_CULL_MODE_BACK_BIT;
        case CullMode::FrontAndBack:
            return VK_CULL_MODE_FRONT_AND_BACK;
        case CullMode::None:
            [[fallthrough]];
        case CullMode::Invalid:
            break;
        }

        return VK_CULL_MODE_NONE;
    }

    VkPrimitiveTopology ToVkTopology(PrimitiveTopology topology) noexcept
    {
        switch (topology)
        {
        case PrimitiveTopology::TriangleStrip:
            return VK_PRIMITIVE_TOPOLOGY_TRIANGLE_STRIP;
        case PrimitiveTopology::LineList:
            return VK_PRIMITIVE_TOPOLOGY_LINE_LIST;
        case PrimitiveTopology::PointList:
            return VK_PRIMITIVE_TOPOLOGY_POINT_LIST;
        case PrimitiveTopology::TriangleList:
            [[fallthrough]];
        case PrimitiveTopology::Invalid:
            break;
        }

        return VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
    }

    // The four blend presets (PassStatements.hpp), as the color and alpha equations
    VkColorBlendEquationEXT ToVkBlendEquation(BlendMode blend) noexcept
    {
        switch (blend)
        {
        case BlendMode::AlphaBlend:
            return VkColorBlendEquationEXT
            {
                VK_BLEND_FACTOR_SRC_ALPHA, VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA, VK_BLEND_OP_ADD,
                VK_BLEND_FACTOR_ONE, VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA, VK_BLEND_OP_ADD
            };
        case BlendMode::Additive:
            return VkColorBlendEquationEXT
            {
                VK_BLEND_FACTOR_ONE, VK_BLEND_FACTOR_ONE, VK_BLEND_OP_ADD,
                VK_BLEND_FACTOR_ONE, VK_BLEND_FACTOR_ONE, VK_BLEND_OP_ADD
            };
        case BlendMode::Premultiplied:
            return VkColorBlendEquationEXT
            {
                VK_BLEND_FACTOR_ONE, VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA, VK_BLEND_OP_ADD,
                VK_BLEND_FACTOR_ONE, VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA, VK_BLEND_OP_ADD
            };
        case BlendMode::Opaque:
            [[fallthrough]];
        case BlendMode::Invalid:
            break;
        }

        return VkColorBlendEquationEXT
        {
            VK_BLEND_FACTOR_ONE, VK_BLEND_FACTOR_ZERO, VK_BLEND_OP_ADD,
            VK_BLEND_FACTOR_ONE, VK_BLEND_FACTOR_ZERO, VK_BLEND_OP_ADD
        };
    }

}

    bool LoadRasterFunctions(VkDevice device, RasterFunctions& functions) noexcept
    {
        return LoadFunction(device, functions.SetVertexInput, "vkCmdSetVertexInputEXT") &&
               LoadFunction(device, functions.SetDepthClampEnable, "vkCmdSetDepthClampEnableEXT") &&
               LoadFunction(device, functions.SetPolygonMode, "vkCmdSetPolygonModeEXT") &&
               LoadFunction(device, functions.SetRasterizationSamples, "vkCmdSetRasterizationSamplesEXT") &&
               LoadFunction(device, functions.SetSampleMask, "vkCmdSetSampleMaskEXT") &&
               LoadFunction(device, functions.SetAlphaToCoverageEnable, "vkCmdSetAlphaToCoverageEnableEXT") &&
               LoadFunction(device, functions.SetAlphaToOneEnable, "vkCmdSetAlphaToOneEnableEXT") &&
               LoadFunction(device, functions.SetColorBlendEnable, "vkCmdSetColorBlendEnableEXT") &&
               LoadFunction(device, functions.SetColorBlendEquation, "vkCmdSetColorBlendEquationEXT") &&
               LoadFunction(device, functions.SetColorWriteMask, "vkCmdSetColorWriteMaskEXT") &&
               LoadFunction(device, functions.SetLogicOpEnable, "vkCmdSetLogicOpEnableEXT");
    }

    void RecordDrawState(const RasterFunctions& functions, VkCommandBuffer command_buffer,
                         const PipelineDescription& description, uint32_t color_slots) noexcept
    {
        const RasterState& raster = description.Raster;

        // vertices are pulled from buffers (step 7 Q8)
        vkCmdSetPrimitiveTopology(command_buffer, ToVkTopology(raster.Topology));
        vkCmdSetPrimitiveRestartEnable(command_buffer, VK_FALSE);
        functions.SetVertexInput(command_buffer, 0u, nullptr, 0u, nullptr);

        functions.SetDepthClampEnable(command_buffer, VK_FALSE);
        vkCmdSetRasterizerDiscardEnable(command_buffer, VK_FALSE);
        functions.SetPolygonMode(command_buffer, raster.Polygon == PolygonMode::Line ? VK_POLYGON_MODE_LINE : VK_POLYGON_MODE_FILL);
        vkCmdSetCullMode(command_buffer, ToVkCullMode(raster.Cull));
        vkCmdSetFrontFace(command_buffer, raster.Front == FrontFace::Clockwise ? VK_FRONT_FACE_CLOCKWISE : VK_FRONT_FACE_COUNTER_CLOCKWISE);
        vkCmdSetDepthBiasEnable(command_buffer, VK_FALSE);

        // no MSAA in step 7 (Q1)
        functions.SetRasterizationSamples(command_buffer, VK_SAMPLE_COUNT_1_BIT);
        constexpr VkSampleMask k_SampleMask = 0xFFFFFFFFu;
        functions.SetSampleMask(command_buffer, VK_SAMPLE_COUNT_1_BIT, &k_SampleMask);
        functions.SetAlphaToCoverageEnable(command_buffer, VK_FALSE);
        functions.SetAlphaToOneEnable(command_buffer, VK_FALSE);
        functions.SetLogicOpEnable(command_buffer, VK_FALSE);

        // every slot up to the highest gets state; a slot with no target writes nowhere
        std::array<VkBool32, k_MaxColorSlots> enables{};
        std::array<VkColorBlendEquationEXT, k_MaxColorSlots> equations{};
        std::array<VkColorComponentFlags, k_MaxColorSlots> masks{};
        const uint32_t slots = color_slots < k_MaxColorSlots ? color_slots : k_MaxColorSlots;

        for (uint32_t slot = 0u; slot < slots; ++slot)
        {
            equations[slot] = ToVkBlendEquation(BlendMode::Opaque);
            masks[slot] = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT | VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;
        }

        for (const LaidOutColor& color : description.Colors)
        {
            const uint32_t slot = static_cast<uint32_t>(color.Slot);

            if (slot >= slots)
            {
                continue;
            }

            enables[slot] = color.Blend == BlendMode::Opaque ? VK_FALSE : VK_TRUE;
            equations[slot] = ToVkBlendEquation(color.Blend);
        }

        if (slots > 0u)
        {
            functions.SetColorBlendEnable(command_buffer, 0u, slots, enables.data());
            functions.SetColorBlendEquation(command_buffer, 0u, slots, equations.data());
            functions.SetColorWriteMask(command_buffer, 0u, slots, masks.data());
        }

        // Compare Always with writes off turns the depth test off (PassBuilders.hpp)
        const bool hasDepth = description.DepthCompare != CompareOp::Invalid;
        const bool writes = hasDepth && description.DepthWrites == DepthWrite::Enabled;
        const bool tests = hasDepth && (writes || description.DepthCompare != CompareOp::Always);
        vkCmdSetDepthTestEnable(command_buffer, tests ? VK_TRUE : VK_FALSE);
        vkCmdSetDepthWriteEnable(command_buffer, writes ? VK_TRUE : VK_FALSE);
        vkCmdSetDepthCompareOp(command_buffer, ToVkCompareOp(description.DepthCompare));
        vkCmdSetDepthBoundsTestEnable(command_buffer, VK_FALSE);
        vkCmdSetStencilTestEnable(command_buffer, VK_FALSE);
    }

    void RecordDrawCommand(VkCommandBuffer command_buffer, const FrozenPass& pass, VkBuffer arguments, VkBuffer count,
                           VkBuffer indices) noexcept
    {
        const FrozenDraw& draw = pass.Draw;
        const bool indexed = indices != VK_NULL_HANDLE;

        if (indexed)
        {
            const VkIndexType type = pass.Index.Type == IndexType::UInt16 ? VK_INDEX_TYPE_UINT16 : VK_INDEX_TYPE_UINT32;
            vkCmdBindIndexBuffer(command_buffer, indices, 0u, type);
        }

        const VkDeviceSize offset = static_cast<VkDeviceSize>(draw.ArgumentOffset);
        const uint32_t stride = indexed ? k_DrawIndexedStride : k_DrawStride;

        switch (draw.Form)
        {
        case DrawForm::Draw:
            vkCmdDraw(command_buffer, static_cast<uint32_t>(draw.VertexOrIndexCount), static_cast<uint32_t>(draw.InstanceCount),
                      static_cast<uint32_t>(draw.FirstVertexOrIndex), static_cast<uint32_t>(draw.FirstInstance));
            break;
        case DrawForm::DrawIndexed:
            vkCmdDrawIndexed(command_buffer, static_cast<uint32_t>(draw.VertexOrIndexCount), static_cast<uint32_t>(draw.InstanceCount),
                             static_cast<uint32_t>(draw.FirstVertexOrIndex), draw.VertexOffset,
                             static_cast<uint32_t>(draw.FirstInstance));
            break;
        case DrawForm::DrawIndirect:
        {
            if (indexed)
            {
                vkCmdDrawIndexedIndirect(command_buffer, arguments, offset, 1u, stride);
                break;
            }

            vkCmdDrawIndirect(command_buffer, arguments, offset, 1u, stride);
            break;
        }
        case DrawForm::DrawIndirectCount:
        {
            const VkDeviceSize countOffset = static_cast<VkDeviceSize>(draw.CountOffset);
            const uint32_t maxDraws = static_cast<uint32_t>(draw.MaxDraws);

            if (indexed)
            {
                vkCmdDrawIndexedIndirectCount(command_buffer, arguments, offset, count, countOffset, maxDraws, stride);
                break;
            }

            vkCmdDrawIndirectCount(command_buffer, arguments, offset, count, countOffset, maxDraws, stride);
            break;
        }
        case DrawForm::Invalid:
            break;
        }
    }

}
