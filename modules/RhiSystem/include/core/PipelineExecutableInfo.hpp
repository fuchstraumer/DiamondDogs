#pragma once
#ifndef RENDERING_CONTEXT_PIPELINE_EXECUTABLE_INFO_HPP
#define RENDERING_CONTEXT_PIPELINE_EXECUTABLE_INFO_HPP
#include <cstdint>
#include "vulkan/vulkan_core.h"

struct PipelineExecutableInfo;

struct ShaderExecutableInfo
{
    ~ShaderExecutableInfo();
    ShaderExecutableInfo(ShaderExecutableInfo&& other) noexcept;
    ShaderExecutableInfo& operator=(ShaderExecutableInfo&& other) noexcept;
    ShaderExecutableInfo(const ShaderExecutableInfo&) = delete;
    ShaderExecutableInfo& operator=(const ShaderExecutableInfo&) = delete;

    struct Statistic
    {
        const char* name{ nullptr };
        uint32_t type;
        union
        {
            VkBool32 b32;
            int64_t i64;
            uint64_t ui64;
            double d64;
        } value;
    };

    const uint32_t& StatisticsCount() const noexcept;
    const Statistic& RetrieveStat(const uint32_t idx) const noexcept;
    const char* Name() const noexcept;
    const char* Description() const noexcept;

private:
    char name[256];
    char description[256];
    char** statisticNames{ nullptr };
    uint32_t count{ 0u };
    Statistic* statistics{ nullptr };
    friend void RetrievePipelineExecutableInfo(const VkDevice deviceHandle, PipelineExecutableInfo& pipelineExecutableInfo);
    ShaderExecutableInfo(const uint32_t num_statistics);
};

struct PipelineExecutableInfo
{
    PipelineExecutableInfo(const uint32_t stage_count, const VkPipeline pipeline);
    ~PipelineExecutableInfo();
    PipelineExecutableInfo(PipelineExecutableInfo&& other) noexcept;
    PipelineExecutableInfo& operator=(PipelineExecutableInfo&& other) noexcept;
    PipelineExecutableInfo(const PipelineExecutableInfo&) = delete;
    PipelineExecutableInfo& operator=(const PipelineExecutableInfo&) = delete;

    uint32_t ExecutableCount() const noexcept;
    const ShaderExecutableInfo& ExecutableInfo(const uint32_t idx) const noexcept;

private:
    VkPipeline pipeline;
    uint32_t stageCount;
    ShaderExecutableInfo** execInfos{ nullptr };
    // This function actually creates the objects
    friend void RetrievePipelineExecutableInfo(const VkDevice deviceHandle, PipelineExecutableInfo& pipelineExecutableInfo);
};

void RetrievePipelineExecutableInfo(const VkDevice deviceHandle, PipelineExecutableInfo& pipelineExecutableInfo);

#endif //!RENDERING_CONTEXT_PIPELINE_EXECUTABLE_INFO_HPP
