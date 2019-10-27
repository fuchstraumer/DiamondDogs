#include "PipelineExecutableInfo.hpp"
#include <cstring>
#include <utility>
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

ShaderExecutableInfo::ShaderExecutableInfo(const uint32_t num_stages) : count(num_stages)
{
    statisticNames = new char* [count];
    statistics = new Statistic[count];
}

ShaderExecutableInfo::~ShaderExecutableInfo()
{
    if (statisticNames != nullptr)
    {
        for (uint32_t i = 0; i < count; ++i)
        {
            free(statisticNames[i]);
        }
        delete[] statisticNames;
    }

    if (statistics != nullptr)
    {
        delete[] statistics;
    }
}

ShaderExecutableInfo::ShaderExecutableInfo(ShaderExecutableInfo&& other) noexcept : count(std::move(other.count)), statisticNames(std::move(other.statisticNames)), statistics(std::move(other.statistics))
{
    // Explicitly nulling makes sure that other won't have delete[] called on it's members
    other.statisticNames = nullptr;
    other.statistics = nullptr;
}

ShaderExecutableInfo& ShaderExecutableInfo::operator=(ShaderExecutableInfo&& other) noexcept
{
    count = std::move(other.count);
    statisticNames = std::move(other.statisticNames);
    other.statisticNames = nullptr;
    statistics = std::move(other.statistics);
    other.statistics = nullptr;
    return *this;
}

const uint32_t& ShaderExecutableInfo::StatisticsCount() const noexcept
{
    return count;
}

const ShaderExecutableInfo::Statistic& ShaderExecutableInfo::RetrieveStat(const uint32_t idx) const noexcept
{
    return statistics[idx];
}

const char* ShaderExecutableInfo::Name() const noexcept
{
    return name;
}

const char* ShaderExecutableInfo::Description() const noexcept
{
    return description;
}

PipelineExecutableInfo::PipelineExecutableInfo(const uint32_t stage_count, const VkPipeline _pipeline) : stageCount(stage_count), execInfos{ nullptr }, pipeline(_pipeline)
{
    execInfos = new ShaderExecutableInfo * [stageCount];
}

PipelineExecutableInfo::~PipelineExecutableInfo()
{
    if (execInfos != nullptr)
    {
        for (uint32_t i = 0; i < stageCount; ++i)
        {
            delete execInfos[i];
            execInfos[i] = nullptr;
        }
        delete[] execInfos;
    }
}

PipelineExecutableInfo::PipelineExecutableInfo(PipelineExecutableInfo&& other) noexcept : stageCount(std::move(other.stageCount)), execInfos(std::move(other.execInfos))
{
    other.execInfos = nullptr;
}

PipelineExecutableInfo& PipelineExecutableInfo::operator=(PipelineExecutableInfo&& other) noexcept
{
    stageCount = std::move(other.stageCount);
    execInfos = std::move(other.execInfos);
    other.execInfos = nullptr;
    return *this;
}

uint32_t PipelineExecutableInfo::ExecutableCount() const noexcept
{
    return stageCount;
}

const ShaderExecutableInfo& PipelineExecutableInfo::ExecutableInfo(const uint32_t idx) const noexcept
{
    return *execInfos[idx];
}

void RetrievePipelineExecutableInfo(const VkDevice deviceHandle, PipelineExecutableInfo& pipelineExecutableInfo)
{
    auto execFns = GetPipelineExecutableFunctions(deviceHandle);

    VkPipelineInfoKHR pipelineInfo
    {
        VK_STRUCTURE_TYPE_PIPELINE_INFO_KHR,
        nullptr,
        pipelineExecutableInfo.pipeline
    };

    uint32_t propertyCount{ 0u };
    VkResult result = execFns.vkGetPipelineExecutablePropertiesKHR(deviceHandle, &pipelineInfo, &propertyCount, nullptr);

    std::vector<VkPipelineExecutablePropertiesKHR> pipelineProperties(propertyCount, VkPipelineExecutablePropertiesKHR{});
    result = execFns.vkGetPipelineExecutablePropertiesKHR(deviceHandle, &pipelineInfo, &propertyCount, pipelineProperties.data());

    std::vector<VkPipelineExecutableInfoKHR> pipelineExecInfos(propertyCount);
    for (uint32_t i = 0; i < propertyCount; ++i)
    {
        const VkPipelineExecutableInfoKHR vkExecutableInfo
        {
            VK_STRUCTURE_TYPE_PIPELINE_EXECUTABLE_INFO_KHR,
            nullptr,
            pipelineExecutableInfo.pipeline,
            i
        };

        uint32_t statisticsCount{ 0u };
        result = execFns.vkGetPipelineExecutableStatisticsKHR(deviceHandle, &vkExecutableInfo, &statisticsCount, nullptr);

        pipelineExecutableInfo.execInfos[i] = new ShaderExecutableInfo(statisticsCount);

        ShaderExecutableInfo& execInfo = *pipelineExecutableInfo.execInfos[i];

        const size_t nameLen = std::strlen(pipelineProperties[i].name);
#ifdef _MSC_VER
        strncpy_s(execInfo.name, VK_MAX_DESCRIPTION_SIZE, pipelineProperties[i].name, nameLen);
#else

        strncpy(execInfo.name, pipelineProperties[i].name, nameLen);
#endif
        const size_t descriptionLen = std::strlen(pipelineProperties[i].description);
#ifdef _MSC_VER
        strncpy_s(execInfo.description, VK_MAX_DESCRIPTION_SIZE, pipelineProperties[i].description, descriptionLen);
#else
        strncpy(execInfo.description, pipelineProperties[i].description, descriptionLen);
#endif

        if (result == VK_SUCCESS)
        {

            std::vector<VkPipelineExecutableStatisticKHR> pipelineStatistics(statisticsCount);
            result = execFns.vkGetPipelineExecutableStatisticsKHR(deviceHandle, &vkExecutableInfo, &statisticsCount, pipelineStatistics.data());

            for (uint32_t j = 0; j < statisticsCount; ++j)
            {
                execInfo.statisticNames[j] = strdup(pipelineStatistics[j].name);
                execInfo.statistics[j].type = pipelineStatistics[j].format;
                // this is the dumbest thing I've done in a while
                reinterpret_cast<VkPipelineExecutableStatisticValueKHR&>(execInfo.statistics[j].value) = pipelineStatistics[j].value;
                // string name is just pointer to memory stored in execInfo
                execInfo.statistics[j].name = execInfo.statisticNames[j];
            }
        }
    }
}
