#include "ShaderProgram.hpp"
#include "RhiFlags.hpp"
#include "RhiHandle.hpp"
#include "RhiResult.hpp"
#include "RhiAssert.hpp"
#include "RhiDefines.hpp"
#include "RhiTypes.hpp"
#include "ShaderTypes.hpp"
#include "core/VulkanConversions.hpp"
#include "vulkan/vulkan_core.h"
#include <algorithm>
#include <cassert>
#include <cstdint>
#include <exception>
#include <iostream>
#include <iterator>
#include <memory>
#include <ranges>
#include <span>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>
#include <unordered_map>

#ifdef RHI_SYSTEM_USE_VULKAN
    #include <vulkan/vulkan.h>
#elif defined(RHI_SYSTEM_USE_DX12)
    #include <d3d12.h>
    #include <dxgi1_6.h>
#endif

namespace rhi
{
// very little shared code between impls per API, so doing top-level macro blocks
#ifdef RHI_SYSTEM_USE_VULKAN
    static PFN_vkCreateShadersEXT pfn_vkCreateShadersEXT = nullptr;
    static PFN_vkDestroyShaderEXT pfn_vkDestroyShaderEXT = nullptr;
    static PFN_vkCmdBindShadersEXT pfn_vkCmdBindShadersEXT = nullptr;

    static const std::vector<VkShaderStageFlagBits> k_VulkanGraphicsStages
    {
        VK_SHADER_STAGE_VERTEX_BIT,
        VK_SHADER_STAGE_TESSELLATION_CONTROL_BIT,
        VK_SHADER_STAGE_TESSELLATION_EVALUATION_BIT,
        VK_SHADER_STAGE_GEOMETRY_BIT,
        VK_SHADER_STAGE_FRAGMENT_BIT
    };

    // a mesh program also binds null to the vertex pipeline stages, so this list holds both kinds
    static const std::vector<VkShaderStageFlagBits> k_VulkanMeshStages
    {
        VK_SHADER_STAGE_VERTEX_BIT,
        VK_SHADER_STAGE_TESSELLATION_CONTROL_BIT,
        VK_SHADER_STAGE_TESSELLATION_EVALUATION_BIT,
        VK_SHADER_STAGE_GEOMETRY_BIT,
        VK_SHADER_STAGE_FRAGMENT_BIT,
        VK_SHADER_STAGE_TASK_BIT_EXT,
        VK_SHADER_STAGE_MESH_BIT_EXT
    };

    static const std::vector<VkShaderStageFlagBits> k_VulkanMeshOnlyStages
    {
        VK_SHADER_STAGE_TASK_BIT_EXT,
        VK_SHADER_STAGE_MESH_BIT_EXT
    };

    struct ShaderProgramImpl
    {

        // In order to later discriminate how to handle unbound shader stages during binding, we need to be cognizant
        // of the broader "type" of program represented here
        enum class ProgramType : uint8_t
        {
            Invalid = 0,
            Graphics,
            Compute,
            Mesh
        };

        ShaderProgramImpl(const DeviceHandle& device) :
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
            for (VkShaderEXT shader : Shaders)
            {
                if (shader != VK_NULL_HANDLE)
                {
                    pfn_vkDestroyShaderEXT(ParentDevice.As<VkDevice>(), shader, nullptr);
                }
            }
        }

        std::vector<VkShaderEXT> Shaders;
        std::vector<VkShaderStageFlagBits> UsedStages;
        std::vector<VkShaderStageFlagBits> UnusedStages;
        std::vector<std::string> EntryPointNames;

        DeviceHandle ParentDevice;
        ProgramType Type{ ProgramType::Invalid };

        static VkPushConstantRange ConvertPushConstantRange(const PushConstantRange& range)
        {
            return VkPushConstantRange{ vk::ToVulkanStages(range.StageFlags), range.Offset, range.Size };
        }

        Result CreateShaderObjects(std::span<ShaderBinaryOptions> stages_to_create)
        {
            if (stages_to_create.front().Stage == ShaderStageFlags::Compute)
            {
                // fixed: this used VK_SHADER_CODE_TYPE_BINARY_EXT (the code is SPIR-V, not a cached driver binary),
                // set no layouts or push constants (so any shader with a binding failed to create), and required
                // full subgroups (which fails for any workgroup X size that isn't a multiple of the subgroup size)
                constexpr static VkShaderCreateInfoEXT k_ComputeCreateInfo
                {
                    VK_STRUCTURE_TYPE_SHADER_CREATE_INFO_EXT,
                    nullptr,
                    0, // flags
                    VK_SHADER_STAGE_COMPUTE_BIT, // stage
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

                Type = ProgramType::Compute;
                const ShaderBinaryOptions& computeOptions = stages_to_create.front();
                const std::vector<VkPushConstantRange> pushConstantRanges = computeOptions.PushConstants |
                    std::views::transform(ConvertPushConstantRange) |
                    std::ranges::to<std::vector>();
                const std::vector<VkDescriptorSetLayout> setLayouts = computeOptions.DescriptorLayouts |
                    std::views::transform(&DescriptorSetLayoutHandle::As<VkDescriptorSetLayout>) |
                    std::ranges::to<std::vector>();

                VkShaderCreateInfoEXT createInfo = k_ComputeCreateInfo;
                createInfo.codeSize = computeOptions.Bytecode.size() * sizeof(uint32_t);
                createInfo.pCode = computeOptions.Bytecode.data();
                createInfo.pName = computeOptions.EntryPointName.c_str();
                createInfo.setLayoutCount = static_cast<uint32_t>(setLayouts.size());
                createInfo.pSetLayouts = setLayouts.data();
                createInfo.pushConstantRangeCount = static_cast<uint32_t>(pushConstantRanges.size());
                createInfo.pPushConstantRanges = pushConstantRanges.data();
                VkShaderEXT createdShader = VK_NULL_HANDLE;

                Result result = pfn_vkCreateShadersEXT(ParentDevice.As<VkDevice>(),
                                                        1,
                                                        &createInfo,
                                                        nullptr,
                                                        &createdShader);
                RhiAssert(result);

                Shaders.emplace_back(createdShader);
                UsedStages.emplace_back(createInfo.stage);
                EntryPointNames.emplace_back(createInfo.pName);
                return result;
            }
            else
            {
                constexpr static VkShaderCreateInfoEXT k_GraphicsCreateInfo
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

                std::vector<VkShaderCreateInfoEXT> createInfos(stages_to_create.size(), k_GraphicsCreateInfo);
                std::vector<VkShaderEXT> createdShaders(stages_to_create.size(), VK_NULL_HANDLE);

                for (auto&& [stageOptions, createInfo] : std::views::zip(stages_to_create, createInfos))
                {
                    createInfo.stage = static_cast<VkShaderStageFlagBits>(vk::ToVulkanStages(stageOptions.Stage));
                    createInfo.codeSize = stageOptions.Bytecode.size() * sizeof(uint32_t);
                    createInfo.pCode = stageOptions.Bytecode.data();
                    EntryPointNames.emplace_back(stageOptions.EntryPointName);
                    createInfo.pName = stageOptions.EntryPointName.c_str();

                    auto& [pushConstantRanges, setLayouts] = stageMetaInfo[stageOptions.Stage];

                    if (!stageOptions.PushConstants.empty())
                    {
                        pushConstantRanges = stageOptions.PushConstants |
                            std::views::transform(ConvertPushConstantRange) |
                            std::ranges::to<PushConstantsVec>();
                        createInfo.pushConstantRangeCount = static_cast<uint32_t>(pushConstantRanges.size());
                        createInfo.pPushConstantRanges = pushConstantRanges.data();
                    }

                    if (!stageOptions.DescriptorLayouts.empty())
                    {
                        setLayouts = stageOptions.DescriptorLayouts |
                            std::views::transform(&DescriptorSetLayoutHandle::As<VkDescriptorSetLayout>) |
                            std::ranges::to<DescriptorSetLayoutsVec>();
                        createInfo.setLayoutCount = static_cast<uint32_t>(setLayouts.size());
                        createInfo.pSetLayouts = setLayouts.data();
                    }
                }

                // every stage but the last links to the stage after it, and zip stops at the shorter range
                for (auto&& [createInfo, nextStageOptions] : std::views::zip(createInfos, stages_to_create | std::views::drop(1)))
                {
                    createInfo.nextStage = vk::ToVulkanStages(nextStageOptions.Stage);
                }

                assert(pfn_vkCreateShadersEXT);
                Result result = pfn_vkCreateShadersEXT(ParentDevice.As<VkDevice>(),
                                                        static_cast<uint32_t>(createInfos.size()),
                                                        createInfos.data(),
                                                        nullptr,
                                                        createdShaders.data());
                RhiAssert(result);

                
                Shaders.append_range(createdShaders);
                UsedStages.append_range(createInfos | std::views::transform(&VkShaderCreateInfoEXT::stage));

                // now the sort of annoying meta-information layer: we need to figure out what kind of program this is based on the used set of stages,
                // and then fill the unused stages based on that
                // TODO: Identify if we can't pare down the device features to disable unused stages like geoemetry and tesselation, bc they're rarely used
                const auto usesAnyStageOf = [this](const std::vector<VkShaderStageFlagBits>& stages)
                {
                    return std::ranges::find_first_of(stages, UsedStages) != stages.end();
                };

                // set_difference needs both ranges sorted, and the caller may list stages in any order
                std::vector<VkShaderStageFlagBits> sortedUsedStages = UsedStages;
                std::ranges::sort(sortedUsedStages);

                // mesh first: a mesh program has a fragment stage too, so the graphics test would match it
                if (usesAnyStageOf(k_VulkanMeshOnlyStages))
                {
                    Type = ProgramType::Mesh;
                    std::ranges::set_difference(k_VulkanMeshStages, sortedUsedStages, std::back_inserter(UnusedStages));
                }
                else if (usesAnyStageOf(k_VulkanGraphicsStages))
                {
                    Type = ProgramType::Graphics;
                    std::ranges::set_difference(k_VulkanGraphicsStages, sortedUsedStages, std::back_inserter(UnusedStages));
                }
                else
                {
                    // if it's not one of the well-known "types", then we'll just consider it a custom combination and not worry about unused stages
                    Type = ProgramType::Invalid;
                }

                return result;
            }
        }

        [[nodiscard]] Result BindShaders(const CommandBufferHandle& cmd_buffer) const
        {
            // If device was created with the tesselation and geometry shader features on, in order to use shader objects we have to 
            // bind null shaders for the unused stages after binding the actual stages we want to use

            // So first, we'll find all the stages that are actually used and bind them - accumulating the used stage bits into a bitmask,
            // which we then invert to find the unused stages, and bind null shaders for those stages (also collect handles into a local vector)
            // Shaders and UsedStages are appended together, so they already line up one to one
            pfn_vkCmdBindShadersEXT(cmd_buffer.As<VkCommandBuffer>(),
                                static_cast<uint32_t>(UsedStages.size()),
                                UsedStages.data(),
                                Shaders.data());

            // now bind null shaders for unused stages
            if (!UnusedStages.empty())
            {
                pfn_vkCmdBindShadersEXT(cmd_buffer.As<VkCommandBuffer>(),
                                    static_cast<uint32_t>(UnusedStages.size()),
                                    UnusedStages.data(),
                                    nullptr);
            }

            return Result::Success();
        }

        [[nodiscard]] ShaderStageFlags GetStages() const
        {
            ShaderStageFlags flags = ShaderStageFlags::None;
            for (VkShaderStageFlagBits stage : UsedStages)
            {
                switch (stage)
                {
                    case VK_SHADER_STAGE_VERTEX_BIT:
                        flags |= ShaderStageFlags::Vertex;
                        break;
                    case VK_SHADER_STAGE_TESSELLATION_CONTROL_BIT:
                        flags |= ShaderStageFlags::TesselationControl;
                        break;
                    case VK_SHADER_STAGE_TESSELLATION_EVALUATION_BIT:
                        flags |= ShaderStageFlags::TesselationEvaluation;
                        break;
                    case VK_SHADER_STAGE_GEOMETRY_BIT:
                        flags |= ShaderStageFlags::Geometry;
                        break;
                    case VK_SHADER_STAGE_FRAGMENT_BIT:
                        flags |= ShaderStageFlags::Fragment;
                        break;
                    case VK_SHADER_STAGE_COMPUTE_BIT:
                        flags |= ShaderStageFlags::Compute;
                        break;
                    case VK_SHADER_STAGE_RAYGEN_BIT_KHR:
                        flags |= ShaderStageFlags::RayGeneration;
                        break;
                    case VK_SHADER_STAGE_ANY_HIT_BIT_KHR:
                        flags |= ShaderStageFlags::AnyHit;
                        break;
                    case VK_SHADER_STAGE_CLOSEST_HIT_BIT_KHR:
                        flags |= ShaderStageFlags::ClosestHit;
                        break;
                    case VK_SHADER_STAGE_MISS_BIT_KHR:
                        flags |= ShaderStageFlags::Miss;
                        break;
                    case VK_SHADER_STAGE_INTERSECTION_BIT_KHR:
                        flags |= ShaderStageFlags::Intersection;
                        break;
                    case VK_SHADER_STAGE_CALLABLE_BIT_KHR:
                        flags |= ShaderStageFlags::Callable;
                        break;
                    case VK_SHADER_STAGE_TASK_BIT_EXT:
                        flags |= ShaderStageFlags::Task;
                        break;
                    case VK_SHADER_STAGE_MESH_BIT_EXT:
                        flags |= ShaderStageFlags::Mesh;
                        break;
                    default:
                        std::unreachable();
                }
            }
            return flags;
        }


    };

#endif // RHI_SYSTEM_USE_VULKAN

#if defined(RHI_SYSTEM_USE_DX12)
    // DX12 implementation would go here
    struct ShaderProgramImpl
    {
        ShaderProgramImpl(const DeviceHandle& device) :
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
    ShaderProgram::ShaderProgram(std::unique_ptr<ShaderProgramImpl>&& impl) noexcept :
        impl{ std::move(impl) }
    {
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

    Result ShaderProgram::BindShaders(const CommandBufferHandle& cmd_buffer) const
    {
        // Binding logic has to go through the impl, as it contains API-specific code and functionality
        return impl->BindShaders(cmd_buffer);
    }

    bool ShaderProgram::IsSingleStage() const noexcept
    {
        return impl->UsedStages.size() == 1;
    }

    ShaderStageFlags ShaderProgram::GetStages() const noexcept
    {
        // dispatch to impl since it'll change per backend
        return impl->GetStages();
    }

    std::span<std::string> ShaderProgram::GetEntryPointNames() const noexcept
    {
        if (impl)
        {
            return std::span<std::string>{impl->EntryPointNames.data(), impl->EntryPointNames.size()};
        }

        return std::span<std::string>{};
    }

    const std::vector<SpecializationConstantReflection>& ShaderProgram::GetSpecializationConstants() const noexcept
    {
        static const std::vector<SpecializationConstantReflection> empty;
        return empty;
    }

    Result ShaderProgram::CreateFromBinary(
        const DeviceHandle& device,
        std::span<ShaderBinaryOptions> binary_options,
        std::unique_ptr<ShaderProgram>& out_program)
    {
        try
        {
            auto impl = std::make_unique<ShaderProgramImpl>(device);
            Result result = impl->CreateShaderObjects(binary_options);
            if (result.IsFailure())
            {
                return result;
            }
            else
            {
                out_program = std::make_unique<ShaderProgram>(std::move(impl));
                return Result::Success();
            }
        }
        catch (const std::exception& e)
        {
            std::cerr << "Error creating ShaderProgram from binary options: " << e.what() << std::endl;
            return Result::Failure();
        }
    }

}
