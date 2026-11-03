#pragma once
#ifndef RHI_SYSTEM_EXECUTABLE_STATISTICS_HPP
#define RHI_SYSTEM_EXECUTABLE_STATISTICS_HPP
#include "RhiFlags.hpp"
#include <cstdint>
#include <expected>
#include <string>
#include <string_view>
#include <variant>
#include <vector>

namespace rhi
{
    class Device;
    struct ShaderBinaryOptions;

    /** @brief Failures of capturing the driver's statistics for a compute shader. */
    enum class ExecutableStatisticsError : uint8_t
    {
        Invalid = 0,
        Success,
        ExtensionNotEnabled,
        NotComputeStage,
        ShaderModuleCreationFailed,
        PipelineLayoutCreationFailed,
        PipelineCreationFailed,
        PropertiesQueryFailed,
        StatisticsQueryFailed,
    };

    [[nodiscard]] std::string_view ToString(ExecutableStatisticsError error) noexcept;

    template<typename T>
    using ExecutableStatisticsResult = std::expected<T, ExecutableStatisticsError>;

    /** @brief One value that the driver reports for a compiled executable. The variant index stores the format
     *  that the driver reported. */
    struct ExecutableStatistic
    {
        std::string Name;
        std::string Description;
        std::variant<bool, int64_t, uint64_t, double> Value;
    };

    /** @brief One executable that the driver compiled from a program. A compute program usually has one. */
    struct ShaderExecutable
    {
        std::string Name;
        std::string Description;
        ShaderStageFlags Stages{ ShaderStageFlags::None };
        int32_t SubgroupSize{ 0 };
        std::vector<ExecutableStatistic> Statistics;
    };

    /** @brief Compiles `compute_options` as a temporary compute pipeline and returns the driver's statistics for
     *  it. The pipeline has the same compile inputs as the shader object, but it is a separate compile. */
    [[nodiscard]] ExecutableStatisticsResult<std::vector<ShaderExecutable>> CaptureComputeStatistics(
        const Device& device,
        const ShaderBinaryOptions& compute_options);

} // namespace rhi

#endif // !RHI_SYSTEM_EXECUTABLE_STATISTICS_HPP
