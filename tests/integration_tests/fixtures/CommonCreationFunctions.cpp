#include "CommonCreationFunctions.hpp"
#include "LogicalDevice.hpp"
#include "PhysicalDevice.hpp"
#include "Swapchain.hpp"
#include "vkAssert.hpp"
#include <vector>

#if defined(_MSC_VER) && !defined(strdup)
#define strdup _strdup
#endif

struct PipelineExecutableFunctions
{
    PFN_vkGetPipelineExecutablePropertiesKHR vkGetPipelineExecutablePropertiesKHR{ nullptr };
    PFN_vkGetPipelineExecutableStatisticsKHR vkGetPipelineExecutableStatisticsKHR{ nullptr };
    PFN_vkGetPipelineExecutableInternalRepresentationsKHR vkGetPipelineExecutableInternalRepresentationsKHR{ nullptr };
    bool initialized{ false };
};

static PipelineExecutableFunctions& GetPipelineExecutableFunctions(const VkDevice device)
{
    static PipelineExecutableFunctions functions;
    if (!functions.initialized)
    {
        functions.vkGetPipelineExecutablePropertiesKHR =
            reinterpret_cast<PFN_vkGetPipelineExecutablePropertiesKHR>(vkGetDeviceProcAddr(device, "vkGetPipelineExecutablePropertiesKHR"));
        functions.vkGetPipelineExecutableStatisticsKHR =
            reinterpret_cast<PFN_vkGetPipelineExecutableStatisticsKHR>(vkGetDeviceProcAddr(device, "vkGetPipelineExecutableStatisticsKHR"));
        functions.vkGetPipelineExecutableInternalRepresentationsKHR =
            reinterpret_cast<PFN_vkGetPipelineExecutableInternalRepresentationsKHR>(vkGetDeviceProcAddr(device, "vkGetPipelineExecutableInternalRepresentationsKHR"));
        functions.initialized = true;
    }
    return functions;
}

DepthStencil CreateDepthStencil(const vpr::Device * device, const vpr::PhysicalDevice* physical_device, const vpr::Swapchain * swapchain) {
    DepthStencil depth_stencil;
    depth_stencil.Format = device->FindDepthFormat();

    const VkImageCreateInfo image_info{
        VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO,
        nullptr,
        0,
        VK_IMAGE_TYPE_2D,
        depth_stencil.Format,
        VkExtent3D{ swapchain->Extent().width, swapchain->Extent().height, 1 },
        1,
        1,
        VK_SAMPLE_COUNT_1_BIT,
        device->GetFormatTiling(depth_stencil.Format, VK_FORMAT_FEATURE_DEPTH_STENCIL_ATTACHMENT_BIT),
        VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT,
        VK_SHARING_MODE_EXCLUSIVE,
        0,
        nullptr,
        VK_IMAGE_LAYOUT_UNDEFINED
    };

    VkResult result = VK_SUCCESS;
    result = vkCreateImage(device->vkHandle(), &image_info, nullptr, &depth_stencil.Image);
    VkAssert(result);

    VkMemoryAllocateInfo alloc_info{ VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO, nullptr };
    VkMemoryRequirements memreqs{};
    vkGetImageMemoryRequirements(device->vkHandle(), depth_stencil.Image, &memreqs);
    alloc_info.allocationSize = memreqs.size;
    alloc_info.memoryTypeIndex = device->GetMemoryTypeIdx(memreqs.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
    result = vkAllocateMemory(device->vkHandle(), &alloc_info, nullptr, &depth_stencil.Memory);
    VkAssert(result);
    result = vkBindImageMemory(device->vkHandle(), depth_stencil.Image, depth_stencil.Memory, 0);
    VkAssert(result);

    const VkImageViewCreateInfo view_info{
        VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO,
        nullptr,
        0,
        depth_stencil.Image,
        VK_IMAGE_VIEW_TYPE_2D,
        depth_stencil.Format,
        {},
        VkImageSubresourceRange{ VK_IMAGE_ASPECT_DEPTH_BIT, 0, 1, 0, 1 }
    };

    result = vkCreateImageView(device->vkHandle(), &view_info, nullptr, &depth_stencil.View);
    VkAssert(result);

    return depth_stencil;
}

VkRenderPass CreateBasicRenderpass(const vpr::Device* device, const vpr::Swapchain* swapchain, VkFormat depth_format) {

    VkRenderPass renderpass = VK_NULL_HANDLE;

    const std::array<const VkAttachmentDescription, 2> attachmentDescriptions{
        VkAttachmentDescription{
        0, swapchain->ColorFormat(), VK_SAMPLE_COUNT_1_BIT,
        VK_ATTACHMENT_LOAD_OP_CLEAR, VK_ATTACHMENT_STORE_OP_STORE,
        VK_ATTACHMENT_LOAD_OP_DONT_CARE, VK_ATTACHMENT_STORE_OP_DONT_CARE,
        VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_PRESENT_SRC_KHR
    },
        VkAttachmentDescription{
        0, depth_format, VK_SAMPLE_COUNT_1_BIT,
        VK_ATTACHMENT_LOAD_OP_CLEAR, VK_ATTACHMENT_STORE_OP_STORE,
        VK_ATTACHMENT_LOAD_OP_DONT_CARE, VK_ATTACHMENT_STORE_OP_DONT_CARE,
        VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL
    }
    };

    const std::array<VkAttachmentReference, 2> attachmentReferences{
        VkAttachmentReference{ 0, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL },
        VkAttachmentReference{ 1, VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL }
    };

    const VkSubpassDescription subpassDescription{
        0,
        VK_PIPELINE_BIND_POINT_GRAPHICS,
        0,
        nullptr,
        1,
        &attachmentReferences[0],
        nullptr,
        &attachmentReferences[1],
        0,
        nullptr
    };

    const std::array<VkSubpassDependency, 2> subpassDependencies{
        VkSubpassDependency{
        VK_SUBPASS_EXTERNAL,
        0,
        VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT,
        VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
        VK_ACCESS_MEMORY_READ_BIT,
        VK_ACCESS_COLOR_ATTACHMENT_READ_BIT | VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT,
        VK_DEPENDENCY_BY_REGION_BIT
    },
        VkSubpassDependency{
        0,
        VK_SUBPASS_EXTERNAL,
        VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
        VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT,
        VK_ACCESS_COLOR_ATTACHMENT_READ_BIT | VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT,
        VK_ACCESS_MEMORY_READ_BIT,
        VK_DEPENDENCY_BY_REGION_BIT
    }
    };

    const VkRenderPassCreateInfo create_info{
        VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO,
        nullptr,
        0,
        static_cast<uint32_t>(attachmentDescriptions.size()),
        attachmentDescriptions.data(),
        1,
        &subpassDescription,
        static_cast<uint32_t>(subpassDependencies.size()),
        subpassDependencies.data()
    };

    VkResult result = vkCreateRenderPass(device->vkHandle(), &create_info, nullptr, &renderpass);
    VkAssert(result);

    return renderpass;
}

VkPipeline CreateBasicPipeline(const BasicPipelineCreateInfo& createInfo)
{
    VkPipeline pipeline = VK_NULL_HANDLE;

    const VkPipelineInputAssemblyStateCreateInfo assembly_info{
        VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO,
        nullptr,
        0,
        createInfo.topology,
        VK_FALSE
    };

    constexpr static VkPipelineViewportStateCreateInfo viewport_info{
        VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO,
        nullptr,
        0,
        1,
        nullptr,
        1,
        nullptr
    };

    const VkPipelineRasterizationStateCreateInfo rasterization_info{
        VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO,
        nullptr,
        0,
        VK_FALSE,
        VK_FALSE,
        VK_POLYGON_MODE_FILL,
        createInfo.cullMode,
        VK_FRONT_FACE_COUNTER_CLOCKWISE,
        VK_FALSE,
        0.0f,
        0.0f,
        0.0f,
        1.0f
    };

    constexpr static VkPipelineMultisampleStateCreateInfo multisample_info{
        VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO,
        nullptr,
        0,
        VK_SAMPLE_COUNT_1_BIT,
        VK_FALSE,
        0.0f,
        nullptr,
        VK_FALSE,
        VK_FALSE
    };

    const VkPipelineDepthStencilStateCreateInfo depth_info {
        VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO,
        nullptr,
        0,
        VK_TRUE,
        VK_TRUE,
        createInfo.depthCompareOp,
        VK_FALSE,
        VK_FALSE,
        VK_STENCIL_OP_ZERO,
        VK_STENCIL_OP_ZERO,
    };

    constexpr static VkPipelineColorBlendAttachmentState colorBlendAttachment{
        VK_TRUE,
        VK_BLEND_FACTOR_SRC_ALPHA,
        VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA,
        VK_BLEND_OP_ADD,
        VK_BLEND_FACTOR_SRC_ALPHA,
        VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA,
        VK_BLEND_OP_ADD,
        VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT | VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT
    };

    constexpr static VkPipelineColorBlendStateCreateInfo color_blend_info{
        VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO,
        nullptr,
        0,
        VK_FALSE,
        VK_LOGIC_OP_COPY,
        1,
        &colorBlendAttachment,
    { 1.0f, 1.0f, 1.0f, 1.0f }
    };

    constexpr static VkDynamicState dynamic_states[2]{
        VK_DYNAMIC_STATE_SCISSOR, VK_DYNAMIC_STATE_VIEWPORT
    };

    constexpr static VkPipelineDynamicStateCreateInfo dynamic_state_info{
        VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO,
        nullptr,
        0,
        2,
        dynamic_states
    };

    VkPipelineCreateFlags createFlags = createInfo.pipelineFlags;

    if (createInfo.derivedPipeline != VK_NULL_HANDLE)
    {
        createFlags |= VK_PIPELINE_CREATE_DERIVATIVE_BIT;
    }
    else
    {
        createFlags |= VK_PIPELINE_CREATE_ALLOW_DERIVATIVES_BIT;
    }

    VkGraphicsPipelineCreateInfo pipeline_create_info = {
        VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO,
        nullptr,
        VK_PIPELINE_CREATE_ALLOW_DERIVATIVES_BIT,
        createInfo.numStages,
        createInfo.stages,
        createInfo.vertexState,
        &assembly_info,
        nullptr,
        &viewport_info,
        &rasterization_info,
        &multisample_info,
        &depth_info,
        &color_blend_info,
        &dynamic_state_info,
        createInfo.pipelineLayout,
        createInfo.renderPass,
        0,
        createInfo.derivedPipeline,
        -1
    };

    VkResult result = vkCreateGraphicsPipelines(createInfo.device->vkHandle(), createInfo.pipelineCache, 1, &pipeline_create_info, nullptr, &pipeline);
    VkAssert(result);

    return pipeline;
}

DepthStencil::DepthStencil(const vpr::Device* device, const vpr::PhysicalDevice* p_device, const vpr::Swapchain* swap) : Parent(device->vkHandle()) {
    *this = CreateDepthStencil(device, p_device, swap);
    Parent = device->vkHandle();
}

DepthStencil::~DepthStencil() {
    if (Parent == VK_NULL_HANDLE) {
        return;
    }

    if (Memory != VK_NULL_HANDLE) {
        vkFreeMemory(Parent, Memory, nullptr);
    }

    if (View != VK_NULL_HANDLE) {
        vkDestroyImageView(Parent, View, nullptr);
    }

    if (Image != VK_NULL_HANDLE) {
        vkDestroyImage(Parent, Image, nullptr);
    }
}

