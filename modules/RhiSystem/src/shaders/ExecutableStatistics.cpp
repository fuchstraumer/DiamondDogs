#include "ExecutableStatistics.hpp"
#include "Device.hpp"
#include "RhiFlags.hpp"
#include "RhiHandle.hpp"
#include "ShaderTypes.hpp"
#include "core/VulkanConversions.hpp"
#include <cstdint>
#include <expected>
#include <ranges>
#include <string>
#include <string_view>
#include <variant>
#include <vector>
#include <vulkan/vulkan_core.h>

namespace
{
    struct PipelineExecutableFunctions
    {
        PFN_vkGetPipelineExecutablePropertiesKHR GetProperties{ nullptr };
        PFN_vkGetPipelineExecutableStatisticsKHR GetStatistics{ nullptr };
    };

    // destroys whatever the probe created, on every return path
    struct ProbeObjects
    {
        VkDevice Device{ VK_NULL_HANDLE };
        VkShaderModule Module{ VK_NULL_HANDLE };
        VkPipelineLayout Layout{ VK_NULL_HANDLE };
        VkPipeline Pipeline{ VK_NULL_HANDLE };

        ProbeObjects(VkDevice _device) noexcept;
        ~ProbeObjects();
        ProbeObjects(const ProbeObjects&) = delete;
        ProbeObjects& operator=(const ProbeObjects&) = delete;
    };

    PipelineExecutableFunctions LoadPipelineExecutableFunctions(VkDevice device) noexcept;
    [[nodiscard]] rhi::ExecutableStatisticsError CreateProbePipeline(const rhi::ShaderBinaryOptions& compute_options,
                                                                     ProbeObjects& probe);
    [[nodiscard]] rhi::ExecutableStatisticsResult<rhi::ShaderExecutable> ReadExecutable(
        const PipelineExecutableFunctions& functions,
        VkDevice device,
        VkPipeline pipeline,
        const VkPipelineExecutablePropertiesKHR& properties,
        uint32_t executable_index);
    rhi::ExecutableStatistic ToExecutableStatistic(const VkPipelineExecutableStatisticKHR& statistic);
    rhi::ShaderStageFlags FromVulkanStages(VkShaderStageFlags vulkan_stages);
}

namespace rhi
{

    std::string_view ToString(ExecutableStatisticsError error) noexcept
    {
        switch (error)
        {
            case ExecutableStatisticsError::Invalid: return "Invalid";
            case ExecutableStatisticsError::Success: return "Success";
            case ExecutableStatisticsError::ExtensionNotEnabled: return "ExtensionNotEnabled";
            case ExecutableStatisticsError::NotComputeStage: return "NotComputeStage";
            case ExecutableStatisticsError::ShaderModuleCreationFailed: return "ShaderModuleCreationFailed";
            case ExecutableStatisticsError::PipelineLayoutCreationFailed: return "PipelineLayoutCreationFailed";
            case ExecutableStatisticsError::PipelineCreationFailed: return "PipelineCreationFailed";
            case ExecutableStatisticsError::PropertiesQueryFailed: return "PropertiesQueryFailed";
            case ExecutableStatisticsError::StatisticsQueryFailed: return "StatisticsQueryFailed";
        }

        return "Unknown";
    }

    ExecutableStatisticsResult<std::vector<ShaderExecutable>> CaptureComputeStatistics(
        const Device& device,
        const ShaderBinaryOptions& compute_options)
    {
        if (!device.HasExtension(VK_KHR_PIPELINE_EXECUTABLE_PROPERTIES_EXTENSION_NAME))
        {
            return std::unexpected(ExecutableStatisticsError::ExtensionNotEnabled);
        }

        if (compute_options.Stage != ShaderStageFlags::Compute)
        {
            return std::unexpected(ExecutableStatisticsError::NotComputeStage);
        }

        const VkDevice vkDevice = device.Handle().As<VkDevice>();
        const PipelineExecutableFunctions functions = LoadPipelineExecutableFunctions(vkDevice);
        ProbeObjects probe{ vkDevice };

        const ExecutableStatisticsError createError = CreateProbePipeline(compute_options, probe);

        if (createError != ExecutableStatisticsError::Success)
        {
            return std::unexpected(createError);
        }

        const VkPipelineInfoKHR pipelineInfo
        {
            VK_STRUCTURE_TYPE_PIPELINE_INFO_KHR,
            nullptr,
            probe.Pipeline
        };

        uint32_t executableCount{ 0u };

        if (functions.GetProperties(vkDevice, &pipelineInfo, &executableCount, nullptr) != VK_SUCCESS)
        {
            return std::unexpected(ExecutableStatisticsError::PropertiesQueryFailed);
        }

        std::vector<VkPipelineExecutablePropertiesKHR> executableProperties(
            executableCount,
            VkPipelineExecutablePropertiesKHR{ VK_STRUCTURE_TYPE_PIPELINE_EXECUTABLE_PROPERTIES_KHR });

        if (functions.GetProperties(vkDevice, &pipelineInfo, &executableCount, executableProperties.data()) != VK_SUCCESS)
        {
            return std::unexpected(ExecutableStatisticsError::PropertiesQueryFailed);
        }

        std::vector<ShaderExecutable> executables;
        executables.reserve(executableCount);

        for (uint32_t executableIndex = 0u; executableIndex < executableCount; ++executableIndex)
        {
            ExecutableStatisticsResult<ShaderExecutable> executable = ReadExecutable(
                functions,
                vkDevice,
                probe.Pipeline,
                executableProperties[executableIndex],
                executableIndex);

            if (!executable)
            {
                return std::unexpected(executable.error());
            }

            executables.emplace_back(std::move(*executable));
        }

        return executables;
    }

} // namespace rhi

namespace
{

    ProbeObjects::ProbeObjects(VkDevice _device) noexcept :
        Device{ _device }
    {}

    ProbeObjects::~ProbeObjects()
    {
        if (Pipeline != VK_NULL_HANDLE)
        {
            vkDestroyPipeline(Device, Pipeline, nullptr);
        }

        if (Layout != VK_NULL_HANDLE)
        {
            vkDestroyPipelineLayout(Device, Layout, nullptr);
        }

        if (Module != VK_NULL_HANDLE)
        {
            vkDestroyShaderModule(Device, Module, nullptr);
        }
    }

    PipelineExecutableFunctions LoadPipelineExecutableFunctions(VkDevice device) noexcept
    {
        PipelineExecutableFunctions functions{};
        functions.GetProperties = reinterpret_cast<PFN_vkGetPipelineExecutablePropertiesKHR>(
            vkGetDeviceProcAddr(device, "vkGetPipelineExecutablePropertiesKHR"));
        functions.GetStatistics = reinterpret_cast<PFN_vkGetPipelineExecutableStatisticsKHR>(
            vkGetDeviceProcAddr(device, "vkGetPipelineExecutableStatisticsKHR"));
        return functions;
    }

    // same compile inputs as the compute path of ShaderProgramImpl::CreateShaderObjects, which passes no
    // specialization info either
    rhi::ExecutableStatisticsError CreateProbePipeline(const rhi::ShaderBinaryOptions& compute_options, ProbeObjects& probe)
    {
        const VkShaderModuleCreateInfo moduleCreateInfo
        {
            VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO,
            nullptr,
            0,
            compute_options.Bytecode.size_bytes(),
            compute_options.Bytecode.data()
        };

        if (vkCreateShaderModule(probe.Device, &moduleCreateInfo, nullptr, &probe.Module) != VK_SUCCESS)
        {
            return rhi::ExecutableStatisticsError::ShaderModuleCreationFailed;
        }

        const std::vector<VkPushConstantRange> pushConstantRanges = compute_options.PushConstants |
            std::views::transform([](const rhi::PushConstantRange& range)
            {
                return VkPushConstantRange{ rhi::vk::ToVulkanStages(range.StageFlags), range.Offset, range.Size };
            }) |
            std::ranges::to<std::vector>();
        const std::vector<VkDescriptorSetLayout> setLayouts = compute_options.DescriptorLayouts |
            std::views::transform(&rhi::DescriptorSetLayoutHandle::As<VkDescriptorSetLayout>) |
            std::ranges::to<std::vector>();

        const VkPipelineLayoutCreateInfo layoutCreateInfo
        {
            VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO,
            nullptr,
            0,
            static_cast<uint32_t>(setLayouts.size()),
            setLayouts.data(),
            static_cast<uint32_t>(pushConstantRanges.size()),
            pushConstantRanges.data()
        };

        if (vkCreatePipelineLayout(probe.Device, &layoutCreateInfo, nullptr, &probe.Layout) != VK_SUCCESS)
        {
            return rhi::ExecutableStatisticsError::PipelineLayoutCreationFailed;
        }

        const VkPipelineShaderStageRequiredSubgroupSizeCreateInfo requiredSubgroupSize
        {
            VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_REQUIRED_SUBGROUP_SIZE_CREATE_INFO,
            nullptr,
            compute_options.RequiredSubgroupSize
        };

        VkPipelineShaderStageCreateInfo stageCreateInfo
        {
            VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,
            nullptr,
            0,
            VK_SHADER_STAGE_COMPUTE_BIT,
            probe.Module,
            compute_options.EntryPointName.c_str(),
            nullptr
        };

        if (compute_options.RequiredSubgroupSize != 0u)
        {
            stageCreateInfo.pNext = &requiredSubgroupSize;
        }

        if (compute_options.RequireFullSubgroups)
        {
            stageCreateInfo.flags |= VK_PIPELINE_SHADER_STAGE_CREATE_REQUIRE_FULL_SUBGROUPS_BIT;
        }

        const VkComputePipelineCreateInfo pipelineCreateInfo
        {
            VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO,
            nullptr,
            VK_PIPELINE_CREATE_CAPTURE_STATISTICS_BIT_KHR,
            stageCreateInfo,
            probe.Layout,
            VK_NULL_HANDLE,
            -1
        };

        if (vkCreateComputePipelines(probe.Device, VK_NULL_HANDLE, 1, &pipelineCreateInfo, nullptr, &probe.Pipeline) != VK_SUCCESS)
        {
            return rhi::ExecutableStatisticsError::PipelineCreationFailed;
        }

        return rhi::ExecutableStatisticsError::Success;
    }

    rhi::ExecutableStatisticsResult<rhi::ShaderExecutable> ReadExecutable(
        const PipelineExecutableFunctions& functions,
        VkDevice device,
        VkPipeline pipeline,
        const VkPipelineExecutablePropertiesKHR& properties,
        uint32_t executable_index)
    {
        const VkPipelineExecutableInfoKHR executableInfo
        {
            VK_STRUCTURE_TYPE_PIPELINE_EXECUTABLE_INFO_KHR,
            nullptr,
            pipeline,
            executable_index
        };

        uint32_t statisticCount{ 0u };

        if (functions.GetStatistics(device, &executableInfo, &statisticCount, nullptr) != VK_SUCCESS)
        {
            return std::unexpected(rhi::ExecutableStatisticsError::StatisticsQueryFailed);
        }

        std::vector<VkPipelineExecutableStatisticKHR> vulkanStatistics(
            statisticCount,
            VkPipelineExecutableStatisticKHR{ VK_STRUCTURE_TYPE_PIPELINE_EXECUTABLE_STATISTIC_KHR });

        if (functions.GetStatistics(device, &executableInfo, &statisticCount, vulkanStatistics.data()) != VK_SUCCESS)
        {
            return std::unexpected(rhi::ExecutableStatisticsError::StatisticsQueryFailed);
        }

        rhi::ShaderExecutable executable{};
        executable.Name = properties.name;
        executable.Description = properties.description;
        executable.Stages = FromVulkanStages(properties.stages);
        executable.SubgroupSize = static_cast<int32_t>(properties.subgroupSize);
        executable.Statistics = vulkanStatistics |
            std::views::transform(ToExecutableStatistic) |
            std::ranges::to<std::vector>();
        return executable;
    }

    rhi::ExecutableStatistic ToExecutableStatistic(const VkPipelineExecutableStatisticKHR& statistic)
    {
        rhi::ExecutableStatistic result{};
        result.Name = statistic.name;
        result.Description = statistic.description;

        switch (statistic.format)
        {
            case VK_PIPELINE_EXECUTABLE_STATISTIC_FORMAT_BOOL32_KHR: result.Value = (statistic.value.b32 != VK_FALSE); break;
            case VK_PIPELINE_EXECUTABLE_STATISTIC_FORMAT_INT64_KHR: result.Value = statistic.value.i64; break;
            case VK_PIPELINE_EXECUTABLE_STATISTIC_FORMAT_UINT64_KHR: result.Value = statistic.value.u64; break;
            case VK_PIPELINE_EXECUTABLE_STATISTIC_FORMAT_FLOAT64_KHR: result.Value = statistic.value.f64; break;
            default: result.Value = statistic.value.u64; break;
        }

        return result;
    }

    rhi::ShaderStageFlags FromVulkanStages(VkShaderStageFlags vulkan_stages)
    {
        rhi::ShaderStageFlags result = rhi::ShaderStageFlags::None;

        for (int32_t bitIndex = 0; bitIndex < static_cast<int32_t>(rhi::ShaderStageFlags::Count); ++bitIndex)
        {
            const rhi::ShaderStageFlags stage = static_cast<rhi::ShaderStageFlags>(1u << bitIndex);

            if ((rhi::vk::ToVulkanStage(stage) & vulkan_stages) != 0)
            {
                result |= stage;
            }
        }

        return result;
    }

}
