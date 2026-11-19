#include "BenchReport.hpp"
#include "content/JsonWriter.hpp"
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <fstream>
#include <print>
#include <utility>
#include <vector>

namespace planet
{
    namespace
    {
        [[nodiscard]] double Round3(double value) noexcept;
        void WriteTimingSummary(foundation::content::JsonWriter& writer,
                                const TimingSummary& summary) noexcept;
    }

    TimingSummary SummarizeSamples(std::span<const double> samples)
    {
        if (samples.empty())
        {
            return TimingSummary{};
        }

        std::vector<double> sorted(samples.begin(), samples.end());
        std::ranges::sort(sorted);
        const size_t last = sorted.size() - 1u;
        return TimingSummary
        {
            sorted.front(),
            sorted[last / 2u],
            sorted[static_cast<size_t>(0.95 * static_cast<double>(last))],
        };
    }

    void PrintTimingTable(std::span<const ScopeTimingReport> scopes, const TimingSummary& submit_summary)
    {
        std::println("{:<26} {:>10} {:>10} {:>10}", "scope", "min", "median", "p95");

        for (const ScopeTimingReport& scope : scopes)
        {
            const std::string label = std::string(2u * scope.Depth, ' ') + scope.Name;
            std::println("{:<26} {:>10.1f} {:>10.1f} {:>10.1f}",
                         label, scope.Summary.Min, scope.Summary.Median, scope.Summary.P95);
        }

        std::println("CPU, submit to fence, ms: min {:.3f}, median {:.3f}, p95 {:.3f}",
                     submit_summary.Min, submit_summary.Median, submit_summary.P95);
    }

    std::optional<std::string> FormatBenchJsonLine(const BenchMetadata& metadata,
                                                   std::span<const ScopeTimingReport> scopes,
                                                   const TimingSummary& submit_summary)
    {
        foundation::content::JsonWriter writer(foundation::content::JsonStyle::Compact);
        writer.BeginObject();
        writer.KeyString("device", metadata.DeviceName);
        writer.KeyString("build", metadata.BuildType);
        writer.KeyInt("points", metadata.PointCount);
        writer.KeyInt("plates", metadata.PlateCount);
        writer.KeyString("flags", metadata.FlagStorage);
        writer.KeyUInt("steps", metadata.Steps);
        writer.KeyString("detail", metadata.Detail == StepTimerDetail::Dispatches ? "dispatches" : "passes");
        writer.Key("scopes");
        writer.BeginObject();

        for (const ScopeTimingReport& scope : scopes)
        {
            writer.Key(scope.Name);
            WriteTimingSummary(writer, scope.Summary);
        }

        writer.EndObject();
        writer.Key("submit_ms");
        WriteTimingSummary(writer, submit_summary);
        writer.EndObject();
        foundation::content::JsonResult<std::string> finished = writer.Finish();

        if (!finished.has_value())
        {
            std::println(stderr,
                         "PlanetGenBench: failed to format JSON: {}",
                         foundation::content::ToString(finished.error()));
            return std::nullopt;
        }

        return std::move(*finished);
    }

    bool AppendJsonLine(const std::filesystem::path& file_path, std::string_view json_line)
    {
        std::ofstream file(file_path, std::ios::app);

        if (!file)
        {
            std::println(stderr, "PlanetGenBench: cannot open {}", file_path.string());
            return false;
        }

        std::string output;
        output.reserve(json_line.size() + 1u);
        output.append(json_line);
        output.push_back('\n');
        file.write(output.data(), static_cast<std::streamsize>(output.size()));

        if (!file)
        {
            std::println(stderr, "PlanetGenBench: cannot write {}", file_path.string());
            return false;
        }

        std::println("appended the results to {}", file_path.string());
        return true;
    }

    namespace
    {
        double Round3(double value) noexcept
        {
            return std::round(value * 1000.0) / 1000.0;
        }

        void WriteTimingSummary(foundation::content::JsonWriter& writer,
                                const TimingSummary& summary) noexcept
        {
            writer.BeginObject();
            writer.KeyDouble("min", Round3(summary.Min));
            writer.KeyDouble("median", Round3(summary.Median));
            writer.KeyDouble("p95", Round3(summary.P95));
            writer.EndObject();
        }
    }
}
