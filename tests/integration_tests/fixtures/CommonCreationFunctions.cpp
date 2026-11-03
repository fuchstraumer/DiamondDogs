#include "CommonCreationFunctions.hpp"
#include "Device.hpp"
#include "Swapchain.hpp"
#include "RhiResult.hpp"
#include "RhiAssert.hpp"
#include "vulkan/vulkan_core.h"
#include <cstdint>
#include <stdexcept>
#include <utility>
#include <vector>

// used to have this in VPR device, but now that we have an API abstracted RHI device it doesn't belong there anymore
// handling depth format choice and query is TBD for the rest of our cod3ebase, but for now this is fine for our test fixtures.
static VkFormat FindSupportedFormat(const rhi::Device* device,
                                    const std::vector<VkFormat>& candidates,
                                    VkImageTiling tiling,
                                    VkFormatFeatureFlags features)
{
    for (VkFormat format : candidates)
    {
        VkFormatProperties props;
        vkGetPhysicalDeviceFormatProperties(device->GetPhysicalDevice().As<VkPhysicalDevice>(), format, &props);

        if (tiling == VK_IMAGE_TILING_LINEAR && (props.linearTilingFeatures & features) == features)
        {
            return format;
        }
        else if (tiling == VK_IMAGE_TILING_OPTIMAL && (props.optimalTilingFeatures & features) == features)
        {
            return format;
        }
    }

    throw std::runtime_error("Failed to find supported format!");
}

static VkImageTiling GetFormatTiling(const rhi::Device* device, VkFormat format, VkFormatFeatureFlags features)
{
    VkFormatProperties props;
    vkGetPhysicalDeviceFormatProperties(device->GetPhysicalDevice().As<VkPhysicalDevice>(), format, &props);

    if ((props.linearTilingFeatures & features) == features)
    {
        return VK_IMAGE_TILING_LINEAR;
    }
    else if ((props.optimalTilingFeatures & features) == features)
    {
        return VK_IMAGE_TILING_OPTIMAL;
    }
    else
    {
        throw std::runtime_error("Requested format does not support required features with either tiling!");
    }
}

const static std::vector<VkFormat> k_DepthFormats =
{
    VK_FORMAT_D32_SFLOAT,
    VK_FORMAT_D32_SFLOAT_S8_UINT,
    VK_FORMAT_D24_UNORM_S8_UINT
};

DepthStencil CreateDepthStencil(const rhi::Device* device, const Swapchain* swapchain)
{
    DepthStencil depthStencil;
    depthStencil.Format = FindSupportedFormat(device, k_DepthFormats, VK_IMAGE_TILING_OPTIMAL, VK_FORMAT_FEATURE_DEPTH_STENCIL_ATTACHMENT_BIT);

    const VkImageCreateInfo imageInfo
    {
        VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO,
        nullptr,
        0,
        VK_IMAGE_TYPE_2D,
        depthStencil.Format,
        VkExtent3D{ static_cast<uint32_t>(swapchain->GetExtent().x), static_cast<uint32_t>(swapchain->GetExtent().y), 1 },
        1,
        1,
        VK_SAMPLE_COUNT_1_BIT,
        GetFormatTiling(device, depthStencil.Format, VK_FORMAT_FEATURE_DEPTH_STENCIL_ATTACHMENT_BIT),
        VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT,
        VK_SHARING_MODE_EXCLUSIVE,
        0,
        nullptr,
        VK_IMAGE_LAYOUT_UNDEFINED
    };

    VkDevice vkDevice = device->Handle().As<VkDevice>();
    VkResult result = VK_SUCCESS;
    result = vkCreateImage(vkDevice, &imageInfo, nullptr, &depthStencil.Image);
    RhiAssert(result);

    VkMemoryAllocateInfo allocInfo{ VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO, nullptr };
    VkMemoryRequirements memreqs{};
    vkGetImageMemoryRequirements(vkDevice, depthStencil.Image, &memreqs);
    allocInfo.allocationSize = memreqs.size;
    allocInfo.memoryTypeIndex = device->GetMemoryTypeIndex(memreqs.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
    result = vkAllocateMemory(vkDevice, &allocInfo, nullptr, &depthStencil.Memory);
    RhiAssert(result);
    result = vkBindImageMemory(vkDevice, depthStencil.Image, depthStencil.Memory, 0);
    RhiAssert(result);

    const VkImageViewCreateInfo viewInfo
    {
        VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO,
        nullptr,
        0,
        depthStencil.Image,
        VK_IMAGE_VIEW_TYPE_2D,
        depthStencil.Format,
        {},
        VkImageSubresourceRange{ VK_IMAGE_ASPECT_DEPTH_BIT, 0, 1, 0, 1 }
    };

    result = vkCreateImageView(vkDevice, &viewInfo, nullptr, &depthStencil.View);
    RhiAssert(result);

    return depthStencil;
}

VkPipeline CreateBasicPipeline(const BasicPipelineCreateInfo& create_info)
{
    VkPipeline pipeline = VK_NULL_HANDLE;

    const VkPipelineInputAssemblyStateCreateInfo assemblyInfo
    {
        VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO,
        nullptr,
        0,
        create_info.topology,
        VK_FALSE
    };

    constexpr static VkPipelineViewportStateCreateInfo k_ViewportInfo
    {
        VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO,
        nullptr,
        0,
        1,
        nullptr,
        1,
        nullptr
    };

    const VkPipelineRasterizationStateCreateInfo rasterizationInfo
    {
        VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO,
        nullptr,
        0,
        VK_FALSE,
        VK_FALSE,
        VK_POLYGON_MODE_FILL,
        create_info.cullMode,
        VK_FRONT_FACE_COUNTER_CLOCKWISE,
        VK_FALSE,
        0.0f,
        0.0f,
        0.0f,
        1.0f
    };

    constexpr static VkPipelineMultisampleStateCreateInfo k_MultisampleInfo
    {
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

    const VkPipelineDepthStencilStateCreateInfo depthInfo
    {
        .sType=VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO,
        .pNext=nullptr,
        .flags=0,
        .depthTestEnable=VK_TRUE,
        .depthWriteEnable=VK_TRUE,
        .depthCompareOp=create_info.depthCompareOp,
        .depthBoundsTestEnable=VK_FALSE,
        .stencilTestEnable=VK_FALSE,
        .front={ .failOp=VK_STENCIL_OP_ZERO, .passOp=VK_STENCIL_OP_ZERO},
        .back={ .failOp=VK_STENCIL_OP_ZERO, .passOp=VK_STENCIL_OP_ZERO},
    };

    constexpr static VkPipelineColorBlendAttachmentState k_ColorBlendAttachmentInfo
    {
        VK_TRUE,
        VK_BLEND_FACTOR_SRC_ALPHA,
        VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA,
        VK_BLEND_OP_ADD,
        VK_BLEND_FACTOR_SRC_ALPHA,
        VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA,
        VK_BLEND_OP_ADD,
        VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT | VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT
    };

    constexpr static VkPipelineColorBlendStateCreateInfo k_ColorBlendInfo
    {
        VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO,
        nullptr,
        0,
        VK_FALSE,
        VK_LOGIC_OP_COPY,
        1,
        &k_ColorBlendAttachmentInfo,
        { 1.0f, 1.0f, 1.0f, 1.0f }
    };

    constexpr static VkDynamicState k_DynamicStates[2]
    {
        VK_DYNAMIC_STATE_SCISSOR,
        VK_DYNAMIC_STATE_VIEWPORT
    };

    constexpr static VkPipelineDynamicStateCreateInfo k_DynamicStateInfo
    {
        VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO,
        nullptr,
        0,
        2,
        k_DynamicStates
    };

    VkPipelineCreateFlags createFlags = create_info.pipelineFlags;

    if (create_info.derivedPipeline != VK_NULL_HANDLE)
    {
        createFlags |= VK_PIPELINE_CREATE_DERIVATIVE_BIT;
    }
    else
    {
        createFlags |= VK_PIPELINE_CREATE_ALLOW_DERIVATIVES_BIT;
    }

    VkGraphicsPipelineCreateInfo pipelineCreateInfo =
    {
        VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO,
        create_info.renderingCreateInfo,
        createFlags,
        create_info.numStages,
        create_info.stages,
        create_info.vertexState,
        &assemblyInfo,
        nullptr,
        &k_ViewportInfo,
        &rasterizationInfo,
        &k_MultisampleInfo,
        &depthInfo,
        &k_ColorBlendInfo,
        &k_DynamicStateInfo,
        create_info.pipelineLayout,
        VK_NULL_HANDLE,
        0,
        create_info.derivedPipeline,
        -1
    };

    VkDevice vkDevice = create_info.device->Handle().As<VkDevice>();
    VkResult result = vkCreateGraphicsPipelines(vkDevice, create_info.pipelineCache, 1, &pipelineCreateInfo, nullptr, &pipeline);
    RhiAssert(result);

    return pipeline;
}

DepthStencil::DepthStencil(const rhi::Device* device, const Swapchain* swap) :
    Format{ VK_FORMAT_UNDEFINED },
    Parent{ device->Handle().As<VkDevice>() }
{
    *this = CreateDepthStencil(device, swap);
}

DepthStencil::DepthStencil() : Format{ VK_FORMAT_UNDEFINED }
{

}

DepthStencil::DepthStencil(DepthStencil&& other) noexcept :
    Image{ other.Image },
    Memory{ other.Memory },
    View{ other.View },
    Format{ other.Format },
    Parent{ other.Parent }
{
    other.Image = VK_NULL_HANDLE;
    other.Memory = VK_NULL_HANDLE;
    other.View = VK_NULL_HANDLE;
    other.Parent = VK_NULL_HANDLE;
}

DepthStencil& DepthStencil::operator=(DepthStencil&& other) noexcept
{
    if (this != &other)
    {
        Image = other.Image;
        other.Image = VK_NULL_HANDLE;
        Memory = other.Memory;
        other.Memory = VK_NULL_HANDLE;
        View = other.View;
        other.View = VK_NULL_HANDLE;
        Format = other.Format;
        Parent = other.Parent;
        other.Parent = VK_NULL_HANDLE;
    }
    return *this;
}

DepthStencil::~DepthStencil()
{
    if (Parent == VK_NULL_HANDLE)
    {
        return;
    }

    if (Memory != VK_NULL_HANDLE)
    {
        vkFreeMemory(Parent, Memory, nullptr);
    }

    if (View != VK_NULL_HANDLE)
    {
        vkDestroyImageView(Parent, View, nullptr);
    }

    if (Image != VK_NULL_HANDLE)
    {
        vkDestroyImage(Parent, Image, nullptr);
    }
}
