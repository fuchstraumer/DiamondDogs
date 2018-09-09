#include "FeatureRenderer.hpp"
#include "RenderGraph.hpp"


static const std::unordered_map<std::string, VkPrimitiveTopology> topology_from_str_map = {
    { "PointList", VK_PRIMITIVE_TOPOLOGY_POINT_LIST },
    { "LineList", VK_PRIMITIVE_TOPOLOGY_LINE_LIST },
    { "LineStrip", VK_PRIMITIVE_TOPOLOGY_LINE_STRIP },
    { "TriangleList", VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST },
    { "TriangleStrip", VK_PRIMITIVE_TOPOLOGY_TRIANGLE_STRIP },
    { "TriangleFan", VK_PRIMITIVE_TOPOLOGY_TRIANGLE_FAN },
    { "LineListWithAdjacency", VK_PRIMITIVE_TOPOLOGY_LINE_LIST_WITH_ADJACENCY },
    { "LineStripWithAdjacency", VK_PRIMITIVE_TOPOLOGY_LINE_STRIP_WITH_ADJACENCY },
    { "TriangleListWithAdjacency", VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST_WITH_ADJACENCY },
    { "TriangleStripWithAdjacency", VK_PRIMITIVE_TOPOLOGY_TRIANGLE_STRIP_WITH_ADJACENCY },
    { "PatchList", VK_PRIMITIVE_TOPOLOGY_PATCH_LIST }
};

static const std::unordered_map<std::string, VkCompareOp> compare_op_from_str_map = {
    { "Never", VK_COMPARE_OP_NEVER },
    { "Less", VK_COMPARE_OP_LESS },
    { "Equal", VK_COMPARE_OP_EQUAL },
    { "LessOrEqual", VK_COMPARE_OP_LESS_OR_EQUAL },
    { "Greater", VK_COMPARE_OP_GREATER },
    { "NotEqual", VK_COMPARE_OP_NOT_EQUAL },
    { "GreaterOrEqual", VK_COMPARE_OP_GREATER_OR_EQUAL },
    { "Always", VK_COMPARE_OP_ALWAYS }
};

static const std::unordered_map<std::string, VkStencilOp> stencil_op_from_str_map = {
    { "Keep", VK_STENCIL_OP_KEEP },
    { "Zero", VK_STENCIL_OP_ZERO },
    { "Replace", VK_STENCIL_OP_REPLACE },
    { "IncrementAndClamp", VK_STENCIL_OP_INCREMENT_AND_CLAMP },
    { "DecrementAndClamp", VK_STENCIL_OP_DECREMENT_AND_CLAMP },
    { "Invert", VK_STENCIL_OP_INVERT },
    { "IncrementAndWrap", VK_STENCIL_OP_INCREMENT_AND_WRAP },
    { "DecrementAndWrap", VK_STENCIL_OP_DECREMENT_AND_WRAP }
};

static const std::unordered_map<std::string, VkLogicOp> logic_op_from_str_map = {
    { "Clear", VK_LOGIC_OP_CLEAR },
    { "And", VK_LOGIC_OP_AND },
    { "AndReverse", VK_LOGIC_OP_AND_REVERSE },
    { "Copy", VK_LOGIC_OP_COPY },
    { "AndInverted", VK_LOGIC_OP_AND_INVERTED },
    { "NoOp", VK_LOGIC_OP_NO_OP },
    { "Xor", VK_LOGIC_OP_XOR },
    { "Or", VK_LOGIC_OP_OR },
    { "Nor", VK_LOGIC_OP_NOR },
    { "Equivalent", VK_LOGIC_OP_EQUIVALENT },
    { "Invert", VK_LOGIC_OP_INVERT },
    { "OrReverse", VK_LOGIC_OP_OR_REVERSE },
    { "CopyInverted", VK_LOGIC_OP_COPY_INVERTED },
    { "OrInverted", VK_LOGIC_OP_OR_INVERTED },
    { "Nand", VK_LOGIC_OP_NAND },
    { "Set", VK_LOGIC_OP_SET }
};

static const std::unordered_map<std::string, VkBlendFactor> blend_factor_from_str_map = {
    { "Zero", VK_BLEND_FACTOR_ZERO },
    { "One", VK_BLEND_FACTOR_ONE }, 
    { "SrcColor", VK_BLEND_FACTOR_SRC_COLOR },
    { "OneMinusSrcColor", VK_BLEND_FACTOR_ONE_MINUS_SRC_COLOR },
    { "DstColor", VK_BLEND_FACTOR_DST_COLOR },
    { "OneMinusDstColor", VK_BLEND_FACTOR_ONE_MINUS_DST_COLOR },
    { "SrcAlpha", VK_BLEND_FACTOR_SRC_ALPHA },
    { "OneMinusSrcAlpha", VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA },
    { "DstAlpha", VK_BLEND_FACTOR_DST_ALPHA },
    { "OneMinusDstAlpha", VK_BLEND_FACTOR_ONE_MINUS_DST_ALPHA },
    { "ConstantColor", VK_BLEND_FACTOR_CONSTANT_COLOR },
    { "OneMinusConstantColor", VK_BLEND_FACTOR_ONE_MINUS_CONSTANT_COLOR },
    { "ConstantAlpha", VK_BLEND_FACTOR_CONSTANT_ALPHA },
    { "OneMinusConstantAlpha", VK_BLEND_FACTOR_ONE_MINUS_CONSTANT_ALPHA },
    { "SrcAlphaSaturate", VK_BLEND_FACTOR_SRC_ALPHA_SATURATE },
    { "Src1Color", VK_BLEND_FACTOR_SRC1_COLOR },
    { "OneMinusSrc1Color", VK_BLEND_FACTOR_ONE_MINUS_SRC1_COLOR },
    { "Src1Alpha", VK_BLEND_FACTOR_SRC1_ALPHA },
    { "OneMinusSrc1Alpha", VK_BLEND_FACTOR_ONE_MINUS_SRC1_ALPHA }
};

static const std::unordered_map<std::string, VkBlendOp> blend_op_from_str_map = {
    { "Add", VK_BLEND_OP_ADD },
    { "Subtract", VK_BLEND_OP_SUBTRACT },
    { "ReverseSubtract", VK_BLEND_OP_REVERSE_SUBTRACT },
    { "Min", VK_BLEND_OP_MIN },
    { "Max", VK_BLEND_OP_MAX }
};

static const std::unordered_map<std::string, VkDynamicState> dynamic_state_from_str_map = {
    { "Viewport", VK_DYNAMIC_STATE_VIEWPORT },
    { "Scissor", VK_DYNAMIC_STATE_SCISSOR },
    { "LineWidth", VK_DYNAMIC_STATE_LINE_WIDTH },
    { "DepthBias", VK_DYNAMIC_STATE_DEPTH_BIAS },
    { "BlendConstants", VK_DYNAMIC_STATE_BLEND_CONSTANTS },
    { "DepthBounds", VK_DYNAMIC_STATE_DEPTH_BOUNDS },
    { "StencilCompareMask", VK_DYNAMIC_STATE_STENCIL_COMPARE_MASK },
    { "StencilWriteMask", VK_DYNAMIC_STATE_STENCIL_WRITE_MASK },
    { "StencilReference", VK_DYNAMIC_STATE_STENCIL_REFERENCE }
};

