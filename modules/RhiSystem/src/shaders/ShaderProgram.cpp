#include "ShaderProgram.hpp"
#include "Device.hpp"
#include "RhiResult.hpp"
#include "RhiAssert.hpp"
#include "RhiDefines.hpp"
#include "ShaderBlob.hpp"
#include "ShaderCompiler.hpp"
#include "ShaderCompilerReply.hpp"
#include <slang.h>
#include <slang-com-ptr.h>
#include <slang-com-helper.h>
#include <stdexcept>
#include <fstream>
#include <sstream>
#include <iostream>
#include <format>
#include <vector>

#ifdef RHI_SYSTEM_USE_VULKAN
    #include <vulkan/vulkan.h>
#elif defined(RHI_SYSTEM_USE_DX12)
    #include <d3d12.h>
    #include <dxgi1_6.h>
#endif

namespace rhi
{
#ifdef RHI_SYSTEM_USE_VULKAN


    static PFN_vkCreateShadersEXT pfn_vkCreateShadersEXT = nullptr;
    static PFN_vkDestroyShaderEXT pfn_vkDestroyShaderEXT = nullptr;
    static PFN_vkCmdBindShadersEXT pfn_vkCmdBindShadersEXT = nullptr;

    static const std::vector<VkShaderStageFlagBits> VulkanGraphicsStages
    {
        VK_SHADER_STAGE_VERTEX_BIT,
        VK_SHADER_STAGE_TESSELLATION_CONTROL_BIT,
        VK_SHADER_STAGE_TESSELLATION_EVALUATION_BIT,
        VK_SHADER_STAGE_GEOMETRY_BIT,
        VK_SHADER_STAGE_FRAGMENT_BIT
    };

    static const std::vector<VkShaderStageFlagBits> VulkanMeshStages
    {
        VK_SHADER_STAGE_VERTEX_BIT,
        VK_SHADER_STAGE_TESSELLATION_CONTROL_BIT,
        VK_SHADER_STAGE_TESSELLATION_EVALUATION_BIT,
        VK_SHADER_STAGE_GEOMETRY_BIT,
        VK_SHADER_STAGE_FRAGMENT_BIT,
        VK_SHADER_STAGE_TASK_BIT_EXT,
        VK_SHADER_STAGE_MESH_BIT_EXT
    };

    struct ShaderProgramImpl
    {

        // In order to later discriminate how to handle unbound shader stages during binding, we need to be cognizant
        // of the broader "type" of program represented here
        enum class ProgramType
        {
            Invalid = 0,
            Graphics,
            Compute,
            Mesh
        };

        ShaderProgramImpl(DeviceHandle device) :
            ParentDevice{ device }
        {
            if ((pfn_vkCreateShadersEXT == nullptr) || (pfn_vkDestroyShaderEXT == nullptr) || (pfn_vkCmdBindShadersEXT == nullptr))
            {
                pfn_vkCreateShadersEXT = reinterpret_cast<PFN_vkCreateShadersEXT>(vkGetDeviceProcAddr(device.As<VkDevice>(), "vkCreateShadersEXT"));
                pfn_vkDestroyShaderEXT = reinterpret_cast<PFN_vkDestroyShaderEXT>(vkGetDeviceProcAddr(device.As<VkDevice>(), "vkDestroyShaderEXT"));
                pfn_vkCmdBindShadersEXT = reinterpret_cast<PFN_vkCmdBindShadersEXT>(vkGetDeviceProcAddr(device.As<VkDevice>(), "vkCmdBindShadersEXT"));
                if ((pfn_vkCreateShadersEXT == nullptr) || (pfn_vkDestroyShaderEXT == nullptr) || (pfn_vkCmdBindShadersEXT == nullptr))
                {
                    throw std::runtime_error("Failed to load Vulkan shader object extension functions");
                }
            }
        }

        ~ShaderProgramImpl()
        {
            for (VkShaderEXT shader : shaders)
            {
                if (shader != VK_NULL_HANDLE)
                {
                    pfn_vkDestroyShaderEXT(ParentDevice.As<VkDevice>(), shader, nullptr);
                }
            }
        }

        std::vector<VkShaderEXT> shaders;
        std::vector<VkShaderStageFlagBits> usedStages;
        std::vector<VkShaderStageFlagBits> unusedStages;

        DeviceHandle ParentDevice;
        ProgramType Type{ ProgramType::Invalid };

        static VkShaderStageFlags ConvertStageToVulkan(ShaderStageFlags stage)
        {
            switch (stage)
            {
                case ShaderStageFlags::Vertex: return VK_SHADER_STAGE_VERTEX_BIT;
                case ShaderStageFlags::TesselationControl: return VK_SHADER_STAGE_TESSELLATION_CONTROL_BIT;
                case ShaderStageFlags::TesselationEvaluation: return VK_SHADER_STAGE_TESSELLATION_EVALUATION_BIT;
                case ShaderStageFlags::Geometry: return VK_SHADER_STAGE_GEOMETRY_BIT;
                case ShaderStageFlags::Fragment: return VK_SHADER_STAGE_FRAGMENT_BIT;
                case ShaderStageFlags::Compute: return VK_SHADER_STAGE_COMPUTE_BIT;
                case ShaderStageFlags::RayGeneration: return VK_SHADER_STAGE_RAYGEN_BIT_KHR;
                case ShaderStageFlags::AnyHit: return VK_SHADER_STAGE_ANY_HIT_BIT_KHR;
                case ShaderStageFlags::ClosestHit: return VK_SHADER_STAGE_CLOSEST_HIT_BIT_KHR;
                case ShaderStageFlags::Miss: return VK_SHADER_STAGE_MISS_BIT_KHR;
                case ShaderStageFlags::Intersection: return VK_SHADER_STAGE_INTERSECTION_BIT_KHR;
                case ShaderStageFlags::Callable: return VK_SHADER_STAGE_CALLABLE_BIT_KHR;
                case ShaderStageFlags::Task: return VK_SHADER_STAGE_TASK_BIT_EXT;
                case ShaderStageFlags::Mesh: return VK_SHADER_STAGE_MESH_BIT_EXT;
                default:
                    throw std::invalid_argument("Unsupported shader stage for Vulkan conversion");
            }
        }

        Result CreateShaderObjects(std::span<ShaderBinaryOptions> stagesToCreate)
        {
            if (stagesToCreate.front().Stage == ShaderStageFlags::Compute)
            {
                constexpr static VkShaderCreateInfoEXT computeCreateInfo
                {
                    VK_STRUCTURE_TYPE_SHADER_CREATE_INFO_EXT,
                    nullptr,
                    VK_SHADER_CREATE_REQUIRE_FULL_SUBGROUPS_BIT_EXT,
                    VK_SHADER_STAGE_COMPUTE_BIT, // stage
                    0, // nextStage
                    VK_SHADER_CODE_TYPE_BINARY_EXT, // codeType
                    0, // codeSize
                    nullptr, // pCode
                    "main", // pName
                    0, // setLayoutCount
                    nullptr, // pSetLayouts
                    0, // pushConstantRangeCount
                    nullptr, // pPushConstantRanges
                    nullptr // pSpecializationInfo
                };

                Type = ProgramType::Compute;
                VkShaderCreateInfoEXT createInfo = computeCreateInfo;
                createInfo.codeSize = stagesToCreate.front().Bytecode.size() * sizeof(uint32_t);
                createInfo.pCode = stagesToCreate.front().Bytecode.data();
                createInfo.pName = stagesToCreate.front().EntryPointName.c_str();
                VkShaderEXT createdShader = VK_NULL_HANDLE;

                Result result = pfn_vkCreateShadersEXT(ParentDevice.As<VkDevice>(),
                                                        1,
                                                        &createInfo,
                                                        nullptr,
                                                        &createdShader);
                RhiAssert(result);

                shaders.emplace_back(createdShader);
                usedStages.emplace_back(createInfo.stage);
                return result;
            }
            else
            {
                constexpr static VkShaderCreateInfoEXT graphicsCreateInfo
                {
                    VK_STRUCTURE_TYPE_SHADER_CREATE_INFO_EXT,
                    nullptr,
                    VK_SHADER_CREATE_LINK_STAGE_BIT_EXT,
                    VkShaderStageFlagBits(0), // stage
                    0, // nextStage
                    VK_SHADER_CODE_TYPE_SPIRV_EXT, // codeType
                    0, // codeSize
                    nullptr, // pCode
                    "main", // pName
                    0, // setLayoutCount
                    nullptr, // pSetLayouts
                    0, // pushConstantRangeCount
                    nullptr, // pPushConstantRanges
                    nullptr // pSpecializationInfo
                };

                // need to convert from input format of push constant and descriptor layout info that's RHI-level and agnostic, to Vulkan format
                // and then ensure they persist in storage until shade creation is done. yay
                using PushConstantsVec = std::vector<VkPushConstantRange>;
                using DescriptorSetLayoutsVec = std::vector<VkDescriptorSetLayout>;
                std::unordered_map<ShaderStageFlags, std::pair<PushConstantsVec, DescriptorSetLayoutsVec>> stageMetaInfo;

                std::vector<VkShaderCreateInfoEXT> createInfos(stagesToCreate.size(), graphicsCreateInfo);
                std::vector<VkShaderEXT> createdShaders(stagesToCreate.size(), VK_NULL_HANDLE);

                for (size_t i = 0; i < stagesToCreate.size(); ++i)
                {
                    const auto& stageOptions = stagesToCreate[i];
                    createInfos[i].stage = static_cast<VkShaderStageFlagBits>(ConvertStageToVulkan(stageOptions.Stage));
                    if (i < stagesToCreate.size() - 1)
                    {
                        createInfos[i].nextStage = ConvertStageToVulkan(stagesToCreate[i + 1].Stage);
                    }
                    createInfos[i].codeSize = stageOptions.Bytecode.size() * sizeof(uint32_t);
                    createInfos[i].pCode = stageOptions.Bytecode.data();
                    createInfos[i].pName = stageOptions.EntryPointName.c_str();

                    if (stageOptions.PushConstants.size() > 0)
                    {
                        stageMetaInfo[stageOptions.Stage].first = PushConstantsVec(stageOptions.PushConstants.size(), VkPushConstantRange{});
                        std::transform(stageOptions.PushConstants.begin(),
                                       stageOptions.PushConstants.end(),
                                       stageMetaInfo[stageOptions.Stage].first.begin(),
                                       [](const PushConstantRange& r)
                                       {
                                           return VkPushConstantRange{ ConvertStageToVulkan(r.StageFlags), r.Offset, r.Size };
                                       });
                        createInfos[i].pushConstantRangeCount = static_cast<uint32_t>(stageOptions.PushConstants.size());
                        createInfos[i].pPushConstantRanges = stageMetaInfo[stageOptions.Stage].first.data();
                    }

                    if (stageOptions.DescriptorLayouts.size() > 0)
                    {
                        stageMetaInfo[stageOptions.Stage].second = DescriptorSetLayoutsVec(stageOptions.DescriptorLayouts.size(), VK_NULL_HANDLE);
                        std::transform(stageOptions.DescriptorLayouts.begin(),
                                       stageOptions.DescriptorLayouts.end(),
                                       stageMetaInfo[stageOptions.Stage].second.begin(),
                                       [](const rhi::DescriptorSetLayoutHandle& h)
                                       {
                                           return h.As<VkDescriptorSetLayout>();
                                       });
                        createInfos[i].setLayoutCount = static_cast<uint32_t>(stageOptions.DescriptorLayouts.size());
                        createInfos[i].pSetLayouts = stageMetaInfo[stageOptions.Stage].second.data();
                    }
                }

                assert(pfn_vkCreateShadersEXT);
                Result result = pfn_vkCreateShadersEXT(ParentDevice.As<VkDevice>(),
                                                        static_cast<uint32_t>(createInfos.size()),
                                                        createInfos.data(),
                                                        nullptr,
                                                        createdShaders.data());
                RhiAssert(result);

                
                for (size_t i = 0; i < stagesToCreate.size(); ++i)
                {
                    shaders.emplace_back(createdShaders[i]);
                    usedStages.emplace_back(createInfos[i].stage);
                }

                // now the sort of annoying meta-information layer: we need to figure out what kind of program this is based on the used set of stages,
                // and then fill the unused stages based on that
                // TODO: Identify if we can't pare down the device features to disable unused stages like geoemetry and tesselation, bc they're rarely used
                if (std::any_of(VulkanGraphicsStages.begin(), VulkanGraphicsStages.end(), [&](VkShaderStageFlagBits stage) { return std::find(usedStages.begin(), usedStages.end(), stage) != usedStages.end(); }))
                {
                    Type = ProgramType::Graphics;
                    std::set_difference(VulkanGraphicsStages.begin(), VulkanGraphicsStages.end(),
                                        usedStages.begin(), usedStages.end(), std::back_inserter(unusedStages));
                }
                else if (std::any_of(VulkanMeshStages.begin(), VulkanMeshStages.end(), [&](VkShaderStageFlagBits stage) { return std::find(usedStages.begin(), usedStages.end(), stage) != usedStages.end(); }))
                {
                    Type = ProgramType::Mesh;
                    std::set_difference(VulkanMeshStages.begin(), VulkanMeshStages.end(),
                                        usedStages.begin(), usedStages.end(), std::back_inserter(unusedStages));
                }
                else
                {
                    // if it's not one of the well-known "types", then we'll just consider it a custom combination and not worry about unused stages
                    Type = ProgramType::Invalid;
                }

                return result;
            }
        }

        Result BindShaders(CommandBufferHandle cmdBuffer) const
        {
            // If device was created with the tesselation and geometry shader features on, in order to use shader objects we have to 
            // bind null shaders for the unused stages after binding the actual stages we want to use

            // So first, we'll find all the stages that are actually used and bind them - accumulating the used stage bits into a bitmask,
            // which we then invert to find the unused stages, and bind null shaders for those stages (also collect handles into a local vector)
            std::vector<VkShaderEXT> shadersToBind;
            std::vector<VkShaderStageFlagBits> stagesToBind;
            for (size_t i = 0; i < usedStages.size(); ++i)
            {
                shadersToBind.emplace_back(shaders[i]);
                stagesToBind.emplace_back(usedStages[i]);
            }

            pfn_vkCmdBindShadersEXT(cmdBuffer.As<VkCommandBuffer>(),
                                static_cast<uint32_t>(shadersToBind.size()),
                                stagesToBind.data(),
                                shadersToBind.data());

            // now bind null shaders for unused stages
            if (!unusedStages.empty())
            {
                pfn_vkCmdBindShadersEXT(cmdBuffer.As<VkCommandBuffer>(),
                                    static_cast<uint32_t>(unusedStages.size()),
                                    unusedStages.data(),
                                    nullptr);
            }

            return Result::Success();
        }

    }; 
#endif // RHI_SYSTEM_USE_VULKAN

#if defined(RHI_SYSTEM_USE_DX12)
    // DX12 implementation would go here
    struct ShaderProgramImpl
    {
        ShaderProgramImpl(DeviceHandle device) :
            ParentDevice{ device }
        {
            // DX12 shader object implementation
            throw std::runtime_error("DX12 ShaderObject implementation not yet available");
        }

        bool CreateShaderObjects(std::span<ShaderBinaryOptions> stagesToCreate)
        {
            // DX12 shader object creation logic would go here
            static_assert(false, "DX12 ShaderObject creation not yet implemented");
        }

        DeviceHandle ParentDevice;
        // DX12 shader object members would go here
    };
#endif

    // ShaderObject implementation
    ShaderProgram::ShaderProgram(DeviceHandle device, ShaderBlob& parent_blob, std::span<ShaderStage> stagesInProgram) :
        impl{ std::make_unique<ShaderProgramImpl>(device) }
    {
    }

    ShaderProgram::ShaderProgram(DeviceHandle device, std::span<ShaderBinaryOptions> binaryOptions) :
        impl{ std::make_unique<ShaderProgramImpl>(device) }
    {
        Result isValid = impl->CreateShaderObjects(binaryOptions);
        if (isValid.IsFailure())
        {
            throw std::runtime_error("Failed to create shader objects from binary options");
        }
    }

    ShaderProgram::ShaderProgram(ShaderProgram&& other) noexcept :
        impl{ std::move(other.impl) }
    {
    }

    ShaderProgram& ShaderProgram::operator=(ShaderProgram&& other) noexcept
    {
        if (this != &other)
        {
            impl = std::move(other.impl);
        }
        return *this;
    }

    ShaderProgram::~ShaderProgram()
    {
    }

    void ShaderProgram::Destroy() noexcept
    {
        impl.reset();
    }

    Result ShaderProgram::BindShaders(CommandBufferHandle cmdBuffer) const
    {
        // Binding logic has to go through the impl, as it contains API-specific code and functionality
        return impl->BindShaders(cmdBuffer);
    }

    const std::vector<SpecializationConstantReflection>& ShaderProgram::GetSpecializationConstants() const noexcept
    {
        static const std::vector<SpecializationConstantReflection> empty;
        return empty;
    }

}
