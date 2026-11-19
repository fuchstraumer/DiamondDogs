#pragma once
#ifndef DIAMOND_DOGS_PLANET_BENCH_REPORT_HPP
#define DIAMOND_DOGS_PLANET_BENCH_REPORT_HPP
#include "PlanetStep.hpp"
#include <cstdint>
#include <filesystem>
#include <optional>
#include <span>
#include <string>
#include <string_view>

namespace planet
{
    struct TimingSummary
    {
        double Min{ 0.0 };
        double Median{ 0.0 };
        double P95{ 0.0 };
    };

    struct ScopeTimingReport
    {
        std::string Name;
        uint32_t Depth{ 0u };
        TimingSummary Summary;
    };

    struct BenchMetadata
    {
        std::string_view DeviceName;
        std::string_view BuildType;
        int32_t PointCount{ 0 };
        int32_t PlateCount{ 0 };
        std::string_view FlagStorage;
        uint32_t Steps{ 0u };
        StepTimerDetail Detail{ StepTimerDetail::Invalid };
    };

    [[nodiscard]] TimingSummary SummarizeSamples(std::span<const double> samples);

    void PrintTimingTable(std::span<const ScopeTimingReport> scopes, const TimingSummary& submit_summary);

    [[nodiscard]] std::optional<std::string> FormatBenchJsonLine(const BenchMetadata& metadata,
                                                                 std::span<const ScopeTimingReport> scopes,
                                                                 const TimingSummary& submit_summary);

    [[nodiscard]] bool AppendJsonLine(const std::filesystem::path& file_path, std::string_view json_line);
}

#endif // !DIAMOND_DOGS_PLANET_BENCH_REPORT_HPP
